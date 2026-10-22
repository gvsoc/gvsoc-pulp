// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

/*
 * Snitch instruction cache: the set-associative io_v2 cache used by the
 * Snitch / Spatz clusters as their per-core L0 and shared L1 icaches.
 *
 * The configuration only carries the parameters of the RTL snitch_icache;
 * cfg.level selects which of its two levels an instance is:
 *
 *   - L0 (snitch_icache_l0), one per core: fully associative with
 *     L0_LINE_COUNT lines replaced in turn, the line hitting skipped. It has
 *     one refill in flight (pending_refill_q), shared by the demand miss and
 *     the prefetch, and serves its hits during it. Its prefetcher decides a
 *     prefetch on a hit with no refill pending when the target is not in the
 *     cache; the prefetch takes the refill slot and evicts its line at once,
 *     and its request goes out the next cycle. The target is the first
 *     statically-taken branch or JAL of the fetched line from the fetch
 *     position, else the next line. See prefetch_target().
 *   - L1 (snitch_icache_lookup and snitch_icache_handler), shared: LINE_COUNT
 *     sets of WAY_COUNT ways, one input port per L0 (NR_FETCH_PORTS), one
 *     lookup per cycle granted round-robin among them and answered in its
 *     cycle. A miss goes to one of NUM_AXI_OUTSTANDING refill slots (merged
 *     with a miss on the same line) and the lookups go on; they only stall
 *     when every slot is busy. A landed refill answers all the requests
 *     waiting for its line at once.
 *
 * Fetches outside the cacheable window (the PMA) and every fetch while the
 * cache is disabled (the `enable` wire) are forwarded through the refill port
 * without touching the cache.
 */

#include <bit>
#include <vp/vp.hpp>
#include <vp/queue.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/signal.hpp>
#include <vector>
#include <deque>
#include <map>
#include <unordered_map>
#include <pulp/snitch/snitch_icache/snitch_icache_config.hpp>

static int ceil_log2(unsigned int n)
{
    if (n <= 1) return 0;
    return 32 - __builtin_clz(n - 1);
}

typedef struct
{
    uint32_t tag;
    bool dirty;
    uint8_t *data;
    vp::Trace tag_event;
    int64_t timestamp;
} cache_line_t;

class SnitchIcache : public vp::Component
{
public:
    SnitchIcache(vp::ComponentConf &conf);

    void reset(bool active) override;

    SnitchIcacheConfig cfg;

private:
    // Clock events
    static void refill_event_clear_handler(vp::Block *__this, vp::ClockEvent *event);
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    static void delayed_resp_handler(vp::Block *__this, vp::ClockEvent *event);

    // Answer a request which was queued (its master got IO_REQ_GRANTED) and is
    // now served: the latency annotated on it is applied as a delay, since the
    // master does not read it on an asynchronous response.
    void resp_queued(vp::IoReq *req);

    // Wire callbacks
    static void enable_sync(vp::Block *_this, bool active);
    static void flush_sync(vp::Block *_this, bool active);
    static void flush_line_sync(vp::Block *_this, bool active);
    static void flush_line_addr_sync(vp::Block *_this, uint32_t addr);

    // io_v2 callbacks
    static vp::IoReqStatus input_req(vp::Block *__this, vp::IoReq *req);
    static vp::IoReqStatus input_req_muxed(vp::Block *__this, vp::IoReq *req, int port);
    static vp::IoRespAck refill_resp(vp::Block *__this, vp::IoReq *req);
    static void refill_retry(vp::Block *__this, vp::IoRetryChannel);

    vp::IoReqStatus handle_req(vp::IoReq *req);
    // False for a fetch outside the cacheable window, which bypasses the cache
    bool cacheable(uint64_t addr) const
    {
        return this->cfg.cacheable_size == 0 || (addr >= this->cfg.cacheable_base &&
            addr - this->cfg.cacheable_base < this->cfg.cacheable_size);
    }
    void check_state();

    cache_line_t *refill(int line_index, unsigned int addr, unsigned int tag,
                          vp::IoReq *req, unsigned int line_offset, bool *pending);
    // Line the RTL L0 prefetcher aims at from a fetch of addr in line: the
    // target of the first taken branch or JAL from the fetched instruction
    // onwards, else the next line
    unsigned int prefetch_target(cache_line_t *line, unsigned int addr);
    // The prefetch decision of a fetch of addr hitting line, as the RTL L0
    // takes it on every hit with no refill pending
    void decide_prefetch(cache_line_t *line, unsigned int addr);
    void send_prefetch();
    cache_line_t *get_line(vp::IoReq *req, unsigned int *line_index,
                            unsigned int *tag, unsigned int *line_offset);

    void enable(bool e);
    void flush();
    void flush_line_op(unsigned int addr);

    // Derived geometry (computed in the ctor from cfg)
    bool is_l1 = false;
    unsigned int nb_ways = 0;
    unsigned int line_bytes = 0;
    unsigned int line_size_bits = 0;
    unsigned int nb_sets_bits = 0;
    unsigned int nb_sets = 0;

    bool enabled = false;

    vp::Trace trace;
    vp::Trace io_event;

    // io_v2 ports — method pointers are passed at construction.
    vp::IoSlave  input_itf{&SnitchIcache::input_req};
    vp::IoMaster refill_itf{&SnitchIcache::refill_retry, &SnitchIcache::refill_resp};

    // Side-band wire ports
    vp::WireSlave<bool>     enable_itf;
    vp::WireSlave<bool>     flush_itf;
    vp::WireMaster<bool>    flush_ack_itf;
    vp::WireSlave<bool>     flush_line_itf;
    vp::WireSlave<uint32_t> flush_line_addr_itf;

    // One miss-status holding register. The L0 has 1 in flight at once, the
    // L1 NUM_AXI_OUTSTANDING; each owns the request vehicle used downstream,
    // so responses are told apart by their request pointer, and each remembers
    // what has to happen when its line lands.
    // Request of a port of the arbitrated L1, with the cycle from which it can
    // be granted (lookup), or answered (response).
    struct PortReq
    {
        vp::IoReq *req;
        int port;
        int64_t cycle;
    };

    struct RefillSlot
    {
        vp::IoReq req;
        // Arbitrated L1: the requests waiting for this line, answered when it
        // lands, and for a synchronous refill the cycle it lands.
        std::vector<PortReq> waiters;
        int64_t done_cycle = -1;
        // Arbitrated L1: first cycle the slot can take a new miss. The RTL
        // pending table entry is cleared on the edge closing the landing
        // cycle, so a miss can only take it the cycle after.
        int64_t reuse_cycle = -1;
        // Arbitrated L1: the line is refilled into this buffer and only
        // written to a way of its set when it lands, as in the RTL
        std::vector<uint8_t> data;
        unsigned int line_index = 0;
        cache_line_t *line = nullptr;
        uint32_t tag = 0;
        bool busy = false;
        bool is_prefetch = false;
        // Demand only: the CPU request waiting for this line, and where in the
        // line it reads/writes.
        vp::IoReq *cpu_req = nullptr;
        unsigned int line_offset = 0;
    };
    std::vector<RefillSlot> refill_slots;
    unsigned int nb_busy_refills = 0;

    // A free slot, or nullptr when they are all in flight.
    RefillSlot *alloc_refill_slot();
    // The slot owning this downstream request.
    RefillSlot *slot_of(vp::IoReq *req);
    // The slot already refilling this tag, if any: a second miss on a line
    // that is on its way must wait for it instead of fetching it twice.
    RefillSlot *slot_for_tag(uint32_t tag);
    void free_refill_slot(RefillSlot *slot);
    // The line of a set to refill: the replacement way, or the next one not
    // already being refilled by a slot in flight, so that two refills never
    // land in the same line. nullptr when every way of the set is.
    // The line being hit (skip) is not replaced, as the RTL L0 evictor skips
    // the line hitting.
    cache_line_t *refill_victim(unsigned int line_index, cache_line_t *skip=nullptr);
    bool refill_slot_available() const { return this->nb_busy_refills < this->refill_slots.size(); }
    // Prefetch decided, holding its refill slot, whose request goes out from
    // the next cycle (prefetch_req_vld_q of the RTL), and the event sending it
    RefillSlot *prefetch_slot = nullptr;
    vp::ClockEvent prefetch_event{this, &SnitchIcache::prefetch_handler};
    // L0: prefetch decision of a fetch whose miss was answered. The RTL L0
    // writes the line on the landing cycle and the fetch hits it, taking the
    // decision, only the cycle after.
    static void miss_decide_handler(vp::Block *__this, vp::ClockEvent *event);
    vp::ClockEvent miss_decide_event{this, &SnitchIcache::miss_decide_handler};
    cache_line_t *miss_decide_line = nullptr;
    unsigned int miss_decide_addr = 0;
    static void prefetch_handler(vp::Block *__this, vp::ClockEvent *event);

    // Address the core shows on its fetch port while it does not fetch (wfi,
    // barrier), sent as a request of size 0, ~0 once it fetches again. The
    // RTL L0 evaluates its prefetch on it like on a hit, without a request:
    // hit_any does not need in_valid_i.
    uint64_t idle_addr = ~(uint64_t)0;
    // The address is registered in the core (pc_q): seen from the next cycle
    vp::ClockEvent idle_event{this, &SnitchIcache::idle_handler};
    static void idle_handler(vp::Block *__this, vp::ClockEvent *event);

    // FIFO of CPU requests that were acknowledged upstream (GRANTED) but not yet
    // served. Two sources feed it:
    //  - reqs arriving while a refill is already in flight (they re-enter via
    //    fsm_handler once the refill resolves);
    //  - the CPU req whose miss triggered the current refill (placed at the head
    //    so the refill_resp path can pop it and reply to the master).
    vp::Queue refill_pending_reqs;

    // GUI / VCD signals (match cache_v3)
    vp::Signal<bool>     pending_refill;
    vp::Signal<uint64_t> refill_event;
    vp::Signal<uint64_t> req_event;

    vp::ClockEvent *fsm_event = nullptr;
    vp::ClockEvent  refill_event_clear_event;

    // Queued requests answered with a latency, by the cycle of their response
    // (in order for the same cycle)
    std::multimap<int64_t, vp::IoReq *> delayed_resps;
    vp::ClockEvent  delayed_resp_event;

    // L1: one input port per L0, the requests
    // waiting for their lookup per port, the arbiter state, and the answers
    // waiting for their cycle.
    static void lookup_handler(vp::Block *__this, vp::ClockEvent *event);
    static void resp_handler(vp::Block *__this, vp::ClockEvent *event);
    static void refill_done_handler(vp::Block *__this, vp::ClockEvent *event);
    // Looks up the request at the head of a port: false if it must stay there
    // (no refill slot or refill denied), the lookups then stall.
    bool lookup(PortReq &pr);
    void complete_refill(RefillSlot *slot);
    // Way replaced by a landing refill: the eviction LFSR of the RTL handler
    // (snitch_icache_lfsr over the set index bits, a toggle for 2 ways),
    // stepped at each refill, which never looks for an invalid way
    unsigned int rtl_evict_way();
    uint32_t evict_lfsr = 0;
    // Next way replaced in the L0
    unsigned int rr_victim = 0;
    std::vector<vp::IoSlave *> ports;
    std::vector<std::deque<PortReq>> port_reqs;
    // A hit answer: the data is read at the lookup, as the RTL reads its
    // SRAM, and only given to the master with the answer, since the master
    // may still be using the buffer until then
    struct Resp
    {
        PortReq pr;
        std::vector<uint8_t> data;
    };
    std::deque<Resp> resps;
    // Requests forwarded while the cache is disabled, to answer on their port
    std::unordered_map<vp::IoReq *, int> bypass_ports;
    // The port arbiter is the multi_accept_rr_arb of the RTL: an rr_arb_tree
    // with its pointer (rr_q), and the port it keeps granting while the
    // lookup cannot take its request (locked_port)
    int rr_q = 0;
    int locked_port = -1;
    // Arbitrated L1: cycle without lookup. The RTL lookup gives the tag and
    // data memories to the refill write on the landing cycle and takes no
    // request then, which leaves the handler stage, modelled by the lookup,
    // idle the cycle after.
    int64_t lookup_block_cycle = -1;
    // The locked request was looked up and waits in the handler (for a
    // refill slot): the blocked cycle does not hold it back.
    bool locked_in_handler = false;
    int rr_tree_winner(std::vector<bool> &req);
    vp::ClockEvent lookup_event;
    vp::ClockEvent resp_event;
    vp::ClockEvent refill_done_event;

    // Earliest cycle at which another synchronous refill can complete. Used to
    // fold a previous refill's in-flight window into subsequent synchronous
    // hits/misses so the CPU sees the serialised latency.
    int64_t refill_timestamp = -1;

    // Flush-line wire staging (address arrives via a separate wire)
    uint32_t flush_line_addr = 0;

    // Lines storage (nb_sets * nb_ways * cache_line_t).
    cache_line_t *lines = nullptr;


    // Set if a refill was denied by the downstream and must be retried on the
    // next retry() signal. Used only while a queued request is being drained —
    // if we are denied on the inline path we propagate DENIED to the master.
    bool refill_retry_pending = false;
    // Set if we returned IO_REQ_DENIED to the upstream master; on the next retry
    // from our refill port we owe input_itf.retry() to un-stick it.
    bool input_needs_retry = false;
};


SnitchIcache::SnitchIcache(vp::ComponentConf &config)
    : vp::Component(config, this->cfg),
      refill_pending_reqs(this, "refill_queue"),
      pending_refill(*this, "refill", 0),
      refill_event(*this, "refill_addr", 32),
      req_event(*this, "req_addr", 64, vp::SignalCommon::ResetKind::HighZ),
      refill_event_clear_event(this, &SnitchIcache::refill_event_clear_handler),
      delayed_resp_event(this, &SnitchIcache::delayed_resp_handler),
      lookup_event(this, &SnitchIcache::lookup_handler),
      resp_event(this, &SnitchIcache::resp_handler),
      refill_done_event(this, &SnitchIcache::refill_done_handler)
{
    // Derived geometry: the L0 is a single set of L0_LINE_COUNT ways, the L1
    // LINE_COUNT sets of WAY_COUNT ways
    this->is_l1 = this->cfg.level == 1;
    this->nb_ways = this->is_l1 ? this->cfg.way_count : this->cfg.line_count;
    this->nb_sets = this->is_l1 ? this->cfg.line_count : 1;
    this->line_bytes = this->cfg.line_width / 8;
    this->line_size_bits = ceil_log2(this->line_bytes);
    this->nb_sets_bits = ceil_log2(this->nb_sets);

    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->traces.new_trace_event("port", &this->io_event, 32);

    // io_v2 slave/master ports (methods bound in-class above).
    if (this->is_l1)
    {
        int nb_ports = this->cfg.nr_fetch_ports;
        this->port_reqs.resize(nb_ports);
        for (int i = 0; i < nb_ports; i++)
        {
            vp::IoSlave *itf = new vp::IoSlave(i, &SnitchIcache::input_req_muxed);
            this->ports.push_back(itf);
            this->new_slave_port(i == 0 ? "input" : "input_" + std::to_string(i), itf);
        }
    }
    else
    {
        this->new_slave_port("input", &this->input_itf);
    }
    this->new_master_port("refill", &this->refill_itf);

    // Side-band wires
    this->enable_itf.set_sync_meth(&SnitchIcache::enable_sync);
    this->new_slave_port("enable", &this->enable_itf);

    this->flush_itf.set_sync_meth(&SnitchIcache::flush_sync);
    this->new_slave_port("flush", &this->flush_itf);

    this->flush_line_itf.set_sync_meth(&SnitchIcache::flush_line_sync);
    this->new_slave_port("flush_line", &this->flush_line_itf);

    this->flush_line_addr_itf.set_sync_meth(&SnitchIcache::flush_line_addr_sync);
    this->new_slave_port("flush_line_addr", &this->flush_line_addr_itf);

    this->new_master_port("flush_ack", &this->flush_ack_itf);

    this->lines = new cache_line_t[this->nb_sets * this->nb_ways];
    for (unsigned int i = 0; i < this->nb_sets; i++)
    {
        for (unsigned int j = 0; j < this->nb_ways; j++)
        {
            cache_line_t *line = &this->lines[i * this->nb_ways + j];
            line->timestamp = -1;
            line->tag = -1;
            line->data = new uint8_t[this->line_bytes];
            this->traces.new_trace_event(
                "set_" + std::to_string(j) + "/line_" + std::to_string(i),
                &line->tag_event, 32);
        }
    }

    // Miss-status holding registers: the L0 pending_refill_q, shared by the
    // demand miss and the prefetch, the L1 NUM_AXI_OUTSTANDING
    unsigned int nb_refills = this->is_l1 ? this->cfg.num_axi_outstanding : 1;
    this->refill_slots.resize(nb_refills);
    for (RefillSlot &slot : this->refill_slots)
    {
        slot.data.resize(this->line_bytes);
    }

    this->fsm_event = this->event_new(&SnitchIcache::fsm_handler);

    this->trace.msg(vp::Trace::LEVEL_INFO,
        "Instantiating cache (sets: %d, ways: %d, line_size: %d)\n",
        this->nb_sets, this->nb_ways, this->line_bytes);
}


SnitchIcache::RefillSlot *SnitchIcache::alloc_refill_slot()
{
    int64_t cycles = this->clock.get_cycles();
    for (RefillSlot &slot : this->refill_slots)
    {
        if (!slot.busy && slot.reuse_cycle <= cycles)
        {
            slot.busy = true;
            slot.line = nullptr;
            slot.cpu_req = nullptr;
            slot.is_prefetch = false;
            slot.line_offset = 0;
            this->nb_busy_refills++;
            this->pending_refill.set(1);
            return &slot;
        }
    }
    return nullptr;
}


SnitchIcache::RefillSlot *SnitchIcache::slot_of(vp::IoReq *req)
{
    for (RefillSlot &slot : this->refill_slots)
    {
        if (&slot.req == req)
        {
            return &slot;
        }
    }
    return nullptr;
}


SnitchIcache::RefillSlot *SnitchIcache::slot_for_tag(uint32_t tag)
{
    for (RefillSlot &slot : this->refill_slots)
    {
        if (slot.busy && slot.line != nullptr && slot.tag == tag)
        {
            return &slot;
        }
    }
    return nullptr;
}


cache_line_t *SnitchIcache::refill_victim(unsigned int line_index, cache_line_t *skip)
{
    // RTL L0: the lines are replaced in turn (snitch_icache_l0 cnt_q), the
    // line hitting skipped
    unsigned int way = this->rr_victim;
    this->rr_victim = (this->rr_victim + 1) % this->nb_ways;
    if (&this->lines[line_index * this->nb_ways + way] == skip)
    {
        way = this->rr_victim;
        this->rr_victim = (this->rr_victim + 1) % this->nb_ways;
    }
    for (unsigned int i = 0; i < this->nb_ways; i++)
    {
        cache_line_t *line = &this->lines[line_index * this->nb_ways +
            (way + i) % this->nb_ways];
        bool busy = false;
        for (RefillSlot &slot : this->refill_slots)
        {
            busy |= slot.busy && slot.line == line;
        }
        if (!busy)
        {
            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Evicting way %d (tag: 0x%x)\n",
                (int)((way + i) % this->nb_ways), line->tag);
            return line;
        }
    }
    return nullptr;
}


void SnitchIcache::free_refill_slot(RefillSlot *slot)
{
    if (!slot->busy)
    {
        return;
    }
    slot->busy = false;
    slot->line = nullptr;
    slot->cpu_req = nullptr;
    this->nb_busy_refills--;
    if (this->nb_busy_refills == 0)
    {
        this->pending_refill.set(0);
    }
}


void SnitchIcache::reset(bool active)
{
    if (active)
    {
        this->flush();
        this->enabled = true;
        this->refill_event.release();
        this->refill_retry_pending = false;
        this->input_needs_retry = false;
        this->refill_timestamp = -1;
        this->delayed_resps.clear();
        this->delayed_resp_event.cancel();
        this->prefetch_slot = nullptr;
        this->prefetch_event.cancel();
        this->miss_decide_event.cancel();
        this->miss_decide_line = nullptr;
        this->idle_addr = ~(uint64_t)0;
        this->idle_event.cancel();
        for (RefillSlot &slot : this->refill_slots)
        {
            slot.busy = false;
            slot.line = nullptr;
            slot.cpu_req = nullptr;
        }
        this->nb_busy_refills = 0;
        for (RefillSlot &slot : this->refill_slots)
        {
            slot.waiters.clear();
            slot.done_cycle = -1;
            slot.reuse_cycle = -1;
        }
        for (auto &q : this->port_reqs)
        {
            q.clear();
        }
        this->resps.clear();
        this->bypass_ports.clear();
        this->rr_q = 0;
        this->locked_port = -1;
        this->lookup_block_cycle = -1;
        this->locked_in_handler = false;
        this->evict_lfsr = 0;
        this->rr_victim = 0;
    }
}


// ---------------------------------------------------------------------------
// Refill path (master-side response / retry)
// ---------------------------------------------------------------------------

vp::IoRespAck SnitchIcache::refill_resp(vp::Block *__this, vp::IoReq *req)
{
    SnitchIcache *_this = (SnitchIcache *)__this;

    SnitchIcache::RefillSlot *slot = _this->slot_of(req);

    // Bypass path: the cache is disabled and we simply pass upstream requests
    // through. The request we receive here is the CPU's own request (it belongs
    // to no MSHR), so we forward the response to the CPU on our own slave port.
    if (slot == nullptr)
    {
        if (_this->is_l1)
        {
            auto it = _this->bypass_ports.find(req);
            int port = it->second;
            _this->bypass_ports.erase(it);
            _this->ports[port]->resp(req);
            return vp::IO_RESP_ACCEPTED;
        }
        _this->input_itf.resp(req);
        return vp::IO_RESP_ACCEPTED;
    }

    if (_this->is_l1)
    {
        if (req->is_last)
        {
            _this->complete_refill(slot);
        }
        return vp::IO_RESP_ACCEPTED;
    }

    // Asynchronous completion of a prefetch: tag the line and free the MSHR;
    // queued demand requests (if any) drain through check_state. No CPU
    // request is attached to a prefetch.
    if (slot->is_prefetch)
    {
        if (!req->is_last)
        {
            return vp::IO_RESP_ACCEPTED;
        }
        cache_line_t *line = slot->line;
        line->tag = slot->tag;
        _this->free_refill_slot(slot);
        _this->refill_event.release();
        _this->check_state();
        return vp::IO_RESP_ACCEPTED;
    }

    // Cached refill path. A beat-streaming downstream (KIND_BEAT router) may
    // emit one resp() per beat for a single refill request. The cache-line
    // buffer is filled in place via slice pointers, so the data is already
    // complete by the time the final beat fires; we only run the completion
    // logic on the burst's last beat. Sync DONE and async big-packet responses
    // produce a single resp() with is_last=true, so this is a no-op for them.
    if (!req->is_last)
    {
        return vp::IO_RESP_ACCEPTED;
    }

    // Cached-refill path. The CPU request whose miss triggered this refill is
    // held by the MSHR itself.
    vp::IoReq *cpu_req = slot->cpu_req;
    vp_assert(cpu_req != nullptr, &_this->trace,
        "Received refill response with no pending CPU request\n");
    uint8_t *data = cpu_req->get_data();
    uint64_t size = cpu_req->get_size();
    bool is_write = cpu_req->get_is_write();

    _this->trace.msg(vp::Trace::LEVEL_TRACE,
        "Received refill response (cpu_req: %p, is_write: %d, data: %p, size: 0x%lx)\n",
        cpu_req, is_write, data, size);

    unsigned int line_offset = slot->line_offset;
    cache_line_t *line = slot->line;
    uint32_t line_tag = slot->tag;
    _this->free_refill_slot(slot);
    _this->refill_event.release();

    if (data)
    {
        line->tag = line_tag;

        if (!is_write)
        {
            memcpy(data, &line->data[line_offset], size);
        }
        else
        {
            memcpy(&line->data[line_offset], data, size);
        }

        // The fetch which missed hits the line the next cycle, and takes its
        // prefetch decision then
        _this->miss_decide_line = line;
        _this->miss_decide_addr = (unsigned int)cpu_req->get_addr();
        _this->miss_decide_event.enqueue(1);
    }

    _this->input_itf.resp(cpu_req);
    _this->check_state();

    return vp::IO_RESP_ACCEPTED;
}


void SnitchIcache::refill_retry(vp::Block *__this, vp::IoRetryChannel)
{
    SnitchIcache *_this = (SnitchIcache *)__this;

    // The downstream is now ready. Two independent things may be waiting on this:
    //
    //  (a) We previously returned DENIED to the upstream because a new CPU request
    //      missed and the refill target refused it. The master is waiting for a
    //      retry() on the input slave port.
    //
    //  (b) A queued CPU request was being drained, hit a miss, and the refill was
    //      denied inside fsm_handler. The CPU request stayed in the queue and we
    //      now need to nudge fsm_handler to try again.
    //
    // Both flags are one-shot and cleared here.
    _this->refill_retry_pending = false;

    if (_this->prefetch_slot != nullptr)
    {
        _this->send_prefetch();
    }

    if (_this->input_needs_retry)
    {
        _this->input_needs_retry = false;
        if (_this->is_l1)
        {
            for (vp::IoSlave *port : _this->ports)
            {
                port->retry();
            }
        }
        else
        {
            _this->input_itf.retry();
        }
    }

    if (_this->is_l1)
    {
        _this->lookup_event.enqueue(1);
        return;
    }

    _this->check_state();
}


// ---------------------------------------------------------------------------
// Internal queue drainer
// ---------------------------------------------------------------------------

// Kicked by check_state() whenever there is at least one queued CPU request and
// no refill currently in flight. Pulls one request from the queue, runs it
// through handle_req, and replies to the CPU if it resolves synchronously.
void SnitchIcache::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    SnitchIcache *_this = (SnitchIcache *)__this;

    if (_this->refill_slot_available() && !_this->refill_retry_pending
        && !_this->refill_pending_reqs.empty())
    {
        vp::IoReq *req = (vp::IoReq *)_this->refill_pending_reqs.pop();

        _this->trace.msg(vp::Trace::LEVEL_TRACE,
            "Resuming req (req: %p, is_write: %d, offset: 0x%lx, size: 0x%lx)\n",
            req, req->get_is_write(), req->get_addr(), req->get_size());

        vp::IoReqStatus st = _this->handle_req(req);
        if (st == vp::IO_REQ_DONE)
        {
            _this->resp_queued(req);
        }
        else if (st == vp::IO_REQ_DENIED)
        {
            // Refill was refused. Put the req back at the head so we retry it
            // once refill_retry() clears refill_retry_pending.
            _this->refill_pending_reqs.push_front(req);
        }
        // If GRANTED, SnitchIcache::refill() has already pushed cpu_req back at the
        // head of refill_pending_reqs — refill_resp will pop and reply.
    }

    _this->check_state();
}


void SnitchIcache::resp_queued(vp::IoReq *req)
{
    int64_t latency = req->get_full_latency();
    if (latency <= 0)
    {
        this->input_itf.resp(req);
        return;
    }

    // The delay is now carried by the time of the response
    req->latency = 0;
    req->duration = 0;
    int64_t cycle = this->clock.get_cycles() + latency;
    this->delayed_resps.emplace(cycle, req);
    this->delayed_resp_event.enqueue(this->delayed_resps.begin()->first - this->clock.get_cycles());
}


void SnitchIcache::delayed_resp_handler(vp::Block *__this, vp::ClockEvent *event)
{
    SnitchIcache *_this = (SnitchIcache *)__this;
    int64_t now = _this->clock.get_cycles();

    while (!_this->delayed_resps.empty() && _this->delayed_resps.begin()->first <= now)
    {
        vp::IoReq *req = _this->delayed_resps.begin()->second;
        _this->delayed_resps.erase(_this->delayed_resps.begin());
        _this->input_itf.resp(req);
    }

    if (!_this->delayed_resps.empty())
    {
        _this->delayed_resp_event.enqueue(_this->delayed_resps.begin()->first - now);
    }
}


void SnitchIcache::check_state()
{
    // The prefetch decision on the idle address is taken again once the
    // pending refill is over
    if (this->idle_addr != ~(uint64_t)0)
    {
        this->idle_event.enqueue(1);
    }

    // Use has_reqs() (presence), NOT !empty() (readiness): a CPU request queued
    // via push_back this same cycle carries a now+1 timestamp, so empty() reports
    // it as "not yet available". If a refill completes the same cycle the request
    // is queued, an !empty() test would skip scheduling the drain fsm, and nothing
    // re-checks next cycle -> the queued request is lost and the master hangs. The
    // fsm runs at +1, by which point the element is ready, so scheduling on
    // presence is correct.
    if (this->refill_slot_available() && !this->refill_retry_pending
        && this->refill_pending_reqs.has_reqs())
    {
        if (!this->fsm_event->is_enqueued())
        {
            this->event_enqueue(this->fsm_event, 1);
        }
    }
}


// Scan a fetched line for the first statically-taken control transfer, as
// snitch_icache_l0.sv does, and return the line of its target.
//
// The RTL looks at every 32-bit slot of the line from the fetch position
// onwards (it does not account for compressed instructions either) and
// takes the first backward conditional branch -- the sign bit of the
// immediate is its static prediction -- or JAL. JALR is unpredictable and
// ignored. With no such instruction the prefetch falls back to the next
// line.
//
// This is what lets the RTL run a loop whose body just exceeds the cache:
// the line holding the back edge names the loop top, so the top is
// refetched before the branch is even taken.
unsigned int SnitchIcache::prefetch_target(cache_line_t *line, unsigned int addr)
{
    unsigned int line_size = 1U << this->line_size_bits;
    unsigned int line_base = addr & ~(line_size - 1);

    for (unsigned int off = addr - line_base; off + 4 <= line_size; off += 4)
    {
        uint32_t insn;
        memcpy(&insn, &line->data[off], 4);
        uint32_t opcode = insn & 0x7f;
        int32_t imm = 0;

        if (opcode == 0x63)
        {
            // Conditional branch, statically taken when it goes backwards.
            if ((insn & 0x80000000) == 0)
            {
                continue;
            }
            // SB immediate: {imm[12], imm[10:5], imm[4:1], imm[11]}
            imm = (int32_t)((((insn >> 31) & 0x1) << 12) |
                            (((insn >> 7) & 0x1) << 11) |
                            (((insn >> 25) & 0x3f) << 5) |
                            (((insn >> 8) & 0xf) << 1));
            imm = (imm << 19) >> 19;
        }
        else if (opcode == 0x6f)
        {
            // JAL, UJ immediate: {imm[20], imm[10:1], imm[11], imm[19:12]}
            imm = (int32_t)((((insn >> 31) & 0x1) << 20) |
                            (((insn >> 12) & 0xff) << 12) |
                            (((insn >> 20) & 0x1) << 11) |
                            (((insn >> 21) & 0x3ff) << 1));
            imm = (imm << 11) >> 11;
        }
        else
        {
            continue;
        }

        unsigned int target = (unsigned int)((int64_t)(line_base + off) + imm);
        return target & ~(line_size - 1);
    }

    return line_base + line_size;
}


void SnitchIcache::decide_prefetch(cache_line_t *line, unsigned int addr)
{
    // No decision while a refill is pending (demand or prefetch)
    if (!this->refill_slot_available())
    {
        return;
    }

    unsigned int target = this->prefetch_target(line, addr);
    unsigned int tag = target >> this->line_size_bits;
    unsigned int line_index = tag & (this->nb_sets - 1);

    // Already in the cache (hit_prefetch_any): no prefetch
    for (unsigned int i = 0; i < this->nb_ways; i++)
    {
        if (this->lines[line_index * this->nb_ways + i].tag == tag)
        {
            return;
        }
    }

    cache_line_t *victim = this->refill_victim(line_index, line);
    if (victim == nullptr)
    {
        return;
    }

    // The prefetch takes the refill slot and evicts its line now, its
    // request goes out from the next cycle
    RefillSlot *slot = this->alloc_refill_slot();
    slot->is_prefetch = true;
    slot->line = victim;
    slot->tag = tag;
    victim->tag = -1;
    this->prefetch_slot = slot;
    this->prefetch_event.enqueue(1);

    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Prefetch decided (addr: 0x%x)\n", target);
}


void SnitchIcache::miss_decide_handler(vp::Block *__this, vp::ClockEvent *event)
{
    SnitchIcache *_this = (SnitchIcache *)__this;
    cache_line_t *line = _this->miss_decide_line;
    _this->miss_decide_line = nullptr;
    if (line != nullptr && line->tag == (_this->miss_decide_addr >> _this->line_size_bits))
    {
        _this->decide_prefetch(line, _this->miss_decide_addr);
    }
}


void SnitchIcache::idle_handler(vp::Block *__this, vp::ClockEvent *event)
{
    SnitchIcache *_this = (SnitchIcache *)__this;
    uint64_t addr = _this->idle_addr;

    if (addr == ~(uint64_t)0 || !_this->enabled || !_this->cacheable(addr))
    {
        return;
    }

    // Same decision as on a hit; nothing happens on a miss, which needs the
    // request
    unsigned int tag = addr >> _this->line_size_bits;
    unsigned int line_index = tag & (_this->nb_sets - 1);
    for (unsigned int i = 0; i < _this->nb_ways; i++)
    {
        cache_line_t *line = &_this->lines[line_index * _this->nb_ways + i];
        if (line->tag == tag)
        {
            _this->decide_prefetch(line, (unsigned int)addr);
            return;
        }
    }
}


void SnitchIcache::prefetch_handler(vp::Block *__this, vp::ClockEvent *event)
{
    SnitchIcache *_this = (SnitchIcache *)__this;
    _this->send_prefetch();
}


void SnitchIcache::send_prefetch()
{
    RefillSlot *slot = this->prefetch_slot;
    if (slot == nullptr)
    {
        return;
    }

    cache_line_t *line = slot->line;
    uint32_t full_addr = (uint32_t)slot->tag << this->line_size_bits;

    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Prefetching line (addr: 0x%x)\n", full_addr);

    line->tag_event.event((uint8_t *)&full_addr);

    vp::IoReq *r = &slot->req;
    r->prepare();
    r->is_first = true;
    r->is_last = true;
    r->burst_id = -1;
    r->set_addr(full_addr);
    r->set_is_write(false);
    r->set_size(1U << this->line_size_bits);
    r->set_data(line->data);

    vp::IoReqStatus st = this->refill_itf.req(r);

    if (st == vp::IO_REQ_DENIED)
    {
        // It stays until the refill port takes it (refill_retry)
        return;
    }

    this->prefetch_slot = nullptr;

    if (st == vp::IO_REQ_GRANTED)
    {
        return;
    }

    // Synchronous completion: the line lands after the accumulated refill
    // window; demand hits arriving before that pay the remaining time.
    int64_t now = this->clock.get_cycles();
    int64_t latency = 0;
    if (now < this->refill_timestamp)
    {
        latency += this->refill_timestamp - now;
    }
    latency += r->get_full_latency();

    this->refill_timestamp = now + latency;
    line->tag = slot->tag;
    line->timestamp = now + latency;
    this->free_refill_slot(slot);
    this->check_state();
}


// ---------------------------------------------------------------------------
// Core cache logic (mirrors cache_v3, minus debug/atomics)
// ---------------------------------------------------------------------------

cache_line_t *SnitchIcache::refill(int line_index, unsigned int addr, unsigned int tag,
                              vp::IoReq *cpu_req, unsigned int line_offset,
                              bool *pending)
{
    // Secondary miss: this line is already on its way (typically the
    // prefetcher fetched it a moment ago). Wait for that refill rather than
    // asking for the same line twice -- merging secondary misses is what an
    // MSHR is for, and without it a second register just doubles the traffic.
    if (this->slot_for_tag(tag) != nullptr)
    {
        this->refill_pending_reqs.push_back(cpu_req);
        *pending = true;
        return nullptr;
    }

    // All miss-status registers in flight, or every way of the set being
    // refilled: queue the CPU req and back off. The victim is only chosen
    // once the refill can go, as the RTL L0 evicts nothing for a miss while
    // a refill is pending.
    cache_line_t *line = this->refill_slot_available() ? this->refill_victim(line_index) : nullptr;
    RefillSlot *slot = line != nullptr ? this->alloc_refill_slot() : nullptr;
    if (slot == nullptr)
    {
        this->refill_pending_reqs.push_back(cpu_req);
        *pending = true;
        return nullptr;
    }

    uint32_t full_addr = addr & ~((1U << this->line_size_bits) - 1);

    this->trace.msg(vp::Trace::LEVEL_DEBUG,
        "Refilling line (addr: 0x%x, index: %d)\n", full_addr, line_index);

    line->tag_event.event((uint8_t *)&full_addr);

    vp::IoReq *r = &slot->req;
    r->prepare();
    // A refill is a single whole-line burst. Reset the burst flags explicitly:
    // prepare() does not touch them, and a beat-streaming downstream (KIND_BEAT
    // router / IoV2BeatAdapter) leaves is_first=0/is_last=1 on the shared
    // refill_req after the previous response's last beat. Reusing it without a
    // reset would send the next refill as a stray continuation beat.
    r->is_first = true;
    r->is_last = true;
    r->burst_id = -1;
    r->set_addr(full_addr);
    r->set_is_write(false);
    r->set_size(1U << this->line_size_bits);
    r->set_data(line->data);

    this->refill_event_clear_event.cancel();

    vp::IoReqStatus st = this->refill_itf.req(r);

    if (st == vp::IO_REQ_GRANTED)
    {
        // Completed asynchronously: the slot keeps everything refill_resp
        // needs, so several lines can be in flight at once.
        slot->line = line;
        slot->tag = tag;
        slot->cpu_req = cpu_req;
        slot->line_offset = line_offset;
        *pending = true;
        // The line is evicted until it lands, as the RTL invalidates it
        line->tag = -1;
        return nullptr;
    }

    this->free_refill_slot(slot);

    if (st == vp::IO_REQ_DENIED)
    {
        // The refill was refused. Caller decides whether to propagate DENIED
        // upstream (new inline req) or to keep the CPU req queued (drain path).
        this->refill_retry_pending = true;
        *pending = false;
        return nullptr;
    }

    // Synchronous success. Tag the line, account for serialisation with any
    // previously-started synchronous refill, and annotate the CPU request's
    // latency so the master paces itself correctly.
    line->tag = tag;

    int64_t now = this->clock.get_cycles();
    int64_t latency = 0;
    if (now < this->refill_timestamp)
    {
        latency += this->refill_timestamp - now;
    }
    // get_full_latency() so a bandwidth router on the refill path contributes
    // its (max-combined) transfer time, not just the head latency.
    latency += r->get_full_latency();

    this->refill_timestamp = now + latency;
    this->refill_event_clear_event.enqueue(latency);

    cpu_req->inc_latency(latency);

    line->timestamp = now + latency;

    return line;
}


void SnitchIcache::flush_line_op(unsigned int addr)
{
    this->trace.msg(vp::Trace::LEVEL_INFO, "Flushing cache line (addr: 0x%x)\n", addr);
    unsigned int tag = addr >> this->line_size_bits;
    unsigned int line_index = tag & (this->nb_sets - 1);
    for (unsigned int i = 0; i < this->nb_ways; i++)
    {
        cache_line_t *line = &this->lines[line_index * this->nb_ways + i];
        if (line->tag == tag)
            line->tag = -1;
    }
}


void SnitchIcache::flush()
{
    this->trace.msg(vp::Trace::LEVEL_INFO, "Flushing whole cache\n");
    for (unsigned int i = 0; i < this->nb_sets; i++)
    {
        for (unsigned int j = 0; j < this->nb_ways; j++)
        {
            this->lines[i * this->nb_ways + j].tag = -1;
        }
    }

    if (this->flush_ack_itf.is_bound())
    {
        this->flush_ack_itf.sync(true);
    }
}


void SnitchIcache::enable(bool e)
{
    this->enabled = e;
    this->trace.msg(vp::Trace::LEVEL_INFO, "%s cache\n",
        e ? "Enabling" : "Disabling");
}


cache_line_t *SnitchIcache::get_line(vp::IoReq *req, unsigned int *line_index,
                                unsigned int *tag, unsigned int *line_offset)
{
    uint64_t offset = req->get_addr();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    unsigned int line_size = 1U << this->line_size_bits;

    *tag = offset >> this->line_size_bits;
    *line_index = *tag & (this->nb_sets - 1);
    *line_offset = offset & (line_size - 1);

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "SnitchIcache access (is_write: %d, addr: 0x%lx, size: 0x%lx, tag: 0x%x, "
        "index: %d, line_offset: 0x%x)\n",
        is_write, offset, size, *tag, *line_index, *line_offset);

    cache_line_t *line = &this->lines[*line_index * this->nb_ways];
    for (unsigned int i = 0; i < this->nb_ways; i++)
    {
        if (line->tag == *tag)
        {
            this->trace.msg(vp::Trace::LEVEL_TRACE, "SnitchIcache hit (way: %d)\n", i);
            return line;
        }
        line++;
    }
    return nullptr;
}


vp::IoReqStatus SnitchIcache::handle_req(vp::IoReq *req)
{
    unsigned int line_index;
    unsigned int tag;
    unsigned int line_offset;
    uint64_t size = req->get_size();
    uint8_t *data = req->get_data();
    bool is_write = req->get_is_write();

    cache_line_t *hit_line = this->get_line(req, &line_index, &tag, &line_offset);

    if (hit_line == nullptr)
    {
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "SnitchIcache miss\n");
        uint64_t offset = req->get_addr();
        this->refill_event.set(offset);
        bool pending = false;
        hit_line = this->refill(line_index, offset, tag, req, line_offset,
                                &pending);
        if (hit_line == nullptr)
        {
            if (pending)
            {
                return vp::IO_REQ_GRANTED;
            }
            // Refill denied OR true error. The caller (input_req / fsm_handler)
            // decides how to map this to an upstream status.
            return vp::IO_REQ_DENIED;
        }
    }
    else
    {
        // SnitchIcache hit. If the line is still being refilled (synchronous case from an
        // earlier miss in this cycle), defer the timing of this access until the
        // refill would have landed.
        int64_t now = this->clock.get_cycles();
        if (now < hit_line->timestamp)
        {
            req->inc_latency(hit_line->timestamp - now);
        }
        // The RTL prefetcher looks at every line the core fetches
        this->decide_prefetch(hit_line, req->get_addr());
    }

    if (data)
    {
        if (!is_write)
        {
            memcpy(data, &hit_line->data[line_offset], size);
        }
        else
        {
            memcpy(&hit_line->data[line_offset], data, size);
        }
    }

    return vp::IO_REQ_DONE;
}


vp::IoReqStatus SnitchIcache::input_req(vp::Block *__this, vp::IoReq *req)
{
    SnitchIcache *_this = (SnitchIcache *)__this;

    uint64_t offset = req->get_addr();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    if (size == 0)
    {
        // The address the core shows while it waits, which asks for nothing
        _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Core idle (addr: 0x%lx)\n", offset);
        _this->idle_addr = offset;
        _this->idle_event.enqueue(1);
        return vp::IO_REQ_DONE;
    }

    _this->trace.msg(vp::Trace::LEVEL_TRACE,
        "Received req (req: %p, is_write: %d, addr: 0x%lx, size: 0x%lx)\n",
        req, is_write, offset, size);

    _this->req_event.set_and_release(offset);

    // Bypass: forward the CPU request verbatim through the refill port. The
    // response comes back on our refill_resp callback, which recognises a
    // request owned by no refill slot as a bypass forward and replies to the
    // master on our own slave port.
    if (!_this->enabled || !_this->cacheable(offset))
    {
        vp::IoReqStatus st = _this->refill_itf.req(req);
        if (st == vp::IO_REQ_DENIED)
        {
            _this->input_needs_retry = true;
        }
        return st;
    }

    _this->io_event.event((uint8_t *)&offset);

    // The core fetches again
    _this->idle_addr = ~(uint64_t)0;
    _this->idle_event.cancel();

    // Cached path. The hits are served during a refill, as the RTL L0 does
    // during its prefetch: only the requests behind queued ones wait (to keep
    // their order), and a miss finds a refill slot or queues itself in
    // refill(). A queued request is re-entered by fsm_handler.
    if (_this->refill_pending_reqs.has_reqs() || _this->refill_retry_pending)
    {
        _this->refill_pending_reqs.push_back(req);
        _this->check_state();
        return vp::IO_REQ_GRANTED;
    }

    vp::IoReqStatus st = _this->handle_req(req);
    if (st == vp::IO_REQ_DENIED)
    {
        // Refill was refused by the downstream and this request was new (not yet
        // acked to the master). Propagate DENIED and remember that we owe an
        // input.retry() once the refill port wakes up.
        _this->input_needs_retry = true;
    }
    return st;
}


// ---------------------------------------------------------------------------
// Arbitrated L1
// ---------------------------------------------------------------------------

vp::IoReqStatus SnitchIcache::input_req_muxed(vp::Block *__this, vp::IoReq *req, int port)
{
    SnitchIcache *_this = (SnitchIcache *)__this;
    uint64_t offset = req->get_addr();

    _this->trace.msg(vp::Trace::LEVEL_TRACE,
        "Received req (req: %p, port: %d, addr: 0x%lx, size: 0x%lx)\n",
        req, port, offset, req->get_size());

    _this->req_event.set_and_release(offset);

    if (!_this->enabled || !_this->cacheable(offset))
    {
        vp::IoReqStatus st = _this->refill_itf.req(req);
        if (st == vp::IO_REQ_DENIED)
        {
            _this->input_needs_retry = true;
        }
        else if (st == vp::IO_REQ_GRANTED)
        {
            _this->bypass_ports[req] = port;
        }
        return st;
    }

    _this->io_event.event((uint8_t *)&offset);

    // The request goes through the port register of the RTL before it can
    // be granted, the next cycle at the earliest
    _this->port_reqs[port].push_back({req, port, _this->clock.get_cycles() + 1});
    _this->lookup_event.enqueue(1);
    return vp::IO_REQ_GRANTED;
}


void SnitchIcache::lookup_handler(vp::Block *__this, vp::ClockEvent *event)
{
    SnitchIcache *_this = (SnitchIcache *)__this;
    int64_t cycles = _this->clock.get_cycles();
    int nb_ports = _this->ports.size();
    bool pending = false;

    // The ports whose head request can go
    std::vector<bool> req(nb_ports, false);
    bool any = false;
    for (int i = 0; i < nb_ports; i++)
    {
        std::deque<PortReq> &q = _this->port_reqs[i];
        req[i] = !q.empty() && q.front().cycle <= cycles;
        any |= req[i];
    }

    bool in_handler = _this->locked_port >= 0 && _this->locked_in_handler;
    if (any && cycles == _this->lookup_block_cycle && !in_handler)
    {
        // The arbiter output is not taken: its choice is locked as in the RTL
        if (_this->locked_port < 0)
        {
            _this->locked_port = _this->rr_tree_winner(req);
        }
        _this->lookup_event.enqueue(1);
        return;
    }

    if (any)
    {
        int port = _this->locked_port >= 0 ? _this->locked_port : _this->rr_tree_winner(req);
        std::deque<PortReq> &q = _this->port_reqs[port];
        if (_this->lookup(q.front()))
        {
            q.pop_front();
            _this->locked_port = -1;
            _this->locked_in_handler = false;
            // The pointer goes to the first requesting port after it, or the
            // lowest one
            for (int k = 1; k <= nb_ports; k++)
            {
                int j = (_this->rr_q + k) % nb_ports;
                if (j == port || req[j])
                {
                    _this->rr_q = j;
                    break;
                }
            }
        }
        else
        {
            _this->locked_port = port;
            _this->locked_in_handler = true;
        }
    }

    for (auto &q : _this->port_reqs)
    {
        pending |= !q.empty();
    }
    if (pending)
    {
        _this->lookup_event.enqueue(1);
    }
}


// rr_arb_tree pick: down the tree from the root, the right half when the
// left one has no request, or both have and the pointer bit of the level is
// set
int SnitchIcache::rr_tree_winner(std::vector<bool> &req)
{
    int n = (int)req.size();
    int nb_levels = 0;
    while ((1 << nb_levels) < n) nb_levels++;
    auto any_in = [&](int lo, int hi) {
        for (int j = lo; j < hi && j < n; j++) if (req[j]) return true;
        return false;
    };
    int lo = 0;
    for (int bit = nb_levels - 1; bit >= 0; bit--)
    {
        int half = 1 << bit;
        bool left = any_in(lo, lo + half);
        bool right = any_in(lo + half, lo + 2 * half);
        if (!left || (right && ((this->rr_q >> bit) & 1))) lo += half;
    }
    return lo;
}


bool SnitchIcache::lookup(PortReq &pr)
{
    vp::IoReq *req = pr.req;
    unsigned int line_index, tag, line_offset;
    int64_t cycles = this->clock.get_cycles();

    // A previous refill was denied: the lookups wait for the retry
    if (this->refill_retry_pending)
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Lookup stalled, refill denied (port: %d)\n",
            pr.port);
        return false;
    }

    cache_line_t *line = this->get_line(req, &line_index, &tag, &line_offset);
    if (line != nullptr)
    {
        // Answered in the cycle of the lookup
        Resp resp{{req, pr.port, cycles}, {}};
        if (!req->get_is_write())
        {
            resp.data.assign(&line->data[line_offset], &line->data[line_offset] + req->get_size());
        }
        this->resps.push_back(std::move(resp));
        this->resp_event.enqueue(0);
        return true;
    }

    this->trace.msg(vp::Trace::LEVEL_DEBUG, "SnitchIcache miss (port: %d)\n", pr.port);
    this->refill_event.set(req->get_addr());

    // A miss on a line already on its way waits for it
    RefillSlot *slot = this->slot_for_tag(tag);
    if (slot != nullptr)
    {
        slot->waiters.push_back(pr);
        return true;
    }

    // Every refill slot busy: the lookup stalls
    slot = this->alloc_refill_slot();
    if (slot == nullptr)
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Lookup stalled, no refill slot (port: %d)\n",
            pr.port);
        return false;
    }

    uint32_t full_addr = req->get_addr() & ~((1U << this->line_size_bits) - 1);

    this->trace.msg(vp::Trace::LEVEL_DEBUG,
        "Refilling line (addr: 0x%x, index: %d)\n", full_addr, line_index);

    // The way is only chosen when the line lands: slot->line just marks the
    // slot as refilling a line, for slot_for_tag
    slot->line = &this->lines[line_index * this->nb_ways];
    slot->line_index = line_index;
    slot->tag = tag;
    slot->waiters.clear();
    slot->waiters.push_back(pr);
    slot->done_cycle = -1;

    vp::IoReq *r = &slot->req;
    r->prepare();
    r->is_first = true;
    r->is_last = true;
    r->burst_id = -1;
    r->set_addr(full_addr);
    r->set_is_write(false);
    r->set_size(1U << this->line_size_bits);
    r->set_data(slot->data.data());

    vp::IoReqStatus st = this->refill_itf.req(r);
    if (st == vp::IO_REQ_DENIED)
    {
        slot->waiters.clear();
        slot->line = nullptr;
        this->free_refill_slot(slot);
        this->refill_retry_pending = true;
        return false;
    }

    if (st == vp::IO_REQ_DONE)
    {
        // Synchronous refill: it lands after its latency
        int64_t latency = r->get_full_latency();
        slot->done_cycle = cycles + latency;
        this->refill_done_event.enqueue(latency);
    }

    return true;
}


void SnitchIcache::complete_refill(RefillSlot *slot)
{
    this->lookup_block_cycle = this->clock.get_cycles() + 1;
    unsigned int way = this->rtl_evict_way();
    cache_line_t *line = &this->lines[slot->line_index * this->nb_ways + way];
    memcpy(line->data, slot->data.data(), this->line_bytes);
    line->tag = slot->tag;
    line->timestamp = -1;
    this->refill_event.release();
    uint32_t full_addr = slot->req.get_addr();
    line->tag_event.event((uint8_t *)&full_addr);

    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Refill done (addr: 0x%x, way: %d, waiters: %d)\n",
        (unsigned int)slot->req.get_addr(), way, (int)slot->waiters.size());

    // The line answers every request waiting for it at once
    for (PortReq &w : slot->waiters)
    {
        unsigned int line_offset = w.req->get_addr() & ((1U << this->line_size_bits) - 1);
        if (!w.req->get_is_write())
        {
            memcpy(w.req->get_data(), &line->data[line_offset], w.req->get_size());
        }
        this->ports[w.port]->resp(w.req);
    }

    slot->waiters.clear();
    slot->done_cycle = -1;
    slot->reuse_cycle = this->clock.get_cycles() + 1;
    this->free_refill_slot(slot);

    // Lookups stalled on the refill slots can go again
    this->lookup_event.enqueue(1);
}


unsigned int SnitchIcache::rtl_evict_way()
{
    int n = ceil_log2(this->nb_ways);
    if (n == 0)
    {
        return 0;
    }

    unsigned int way = this->evict_lfsr;
    if (n == 1)
    {
        this->evict_lfsr ^= 1;
    }
    else
    {
        static const uint32_t taps[9] = {0, 0, 0x3, 0x6, 0xc, 0x14, 0x30, 0x60, 0xb8};
        uint32_t mask = (1U << n) - 1;
        uint32_t tap = n <= 8 ? taps[n] : 1U << (n - 1);
        uint32_t q = this->evict_lfsr, d;
        if (q == 0)
        {
            d = mask;
        }
        else
        {
            d = q >> 1;
            if (q & 1) d ^= tap;
            if (d == mask) d = 0;
        }
        this->evict_lfsr = d;
    }
    return way % this->nb_ways;
}


void SnitchIcache::refill_done_handler(vp::Block *__this, vp::ClockEvent *event)
{
    SnitchIcache *_this = (SnitchIcache *)__this;
    int64_t cycles = _this->clock.get_cycles();
    int64_t next = -1;

    for (RefillSlot &slot : _this->refill_slots)
    {
        if (!slot.busy || slot.done_cycle < 0)
        {
            continue;
        }
        if (slot.done_cycle <= cycles)
        {
            _this->complete_refill(&slot);
        }
        else if (next < 0 || slot.done_cycle < next)
        {
            next = slot.done_cycle;
        }
    }

    if (next >= 0)
    {
        _this->refill_done_event.enqueue(next - cycles);
    }
}


void SnitchIcache::resp_handler(vp::Block *__this, vp::ClockEvent *event)
{
    SnitchIcache *_this = (SnitchIcache *)__this;
    int64_t cycles = _this->clock.get_cycles();

    while (!_this->resps.empty() && _this->resps.front().pr.cycle <= cycles)
    {
        Resp resp = std::move(_this->resps.front());
        _this->resps.pop_front();
        if (!resp.data.empty())
        {
            memcpy(resp.pr.req->get_data(), resp.data.data(), resp.data.size());
        }
        _this->ports[resp.pr.port]->resp(resp.pr.req);
    }

    if (!_this->resps.empty())
    {
        _this->resp_event.enqueue(_this->resps.front().pr.cycle - cycles);
    }
}


// ---------------------------------------------------------------------------
// Wire-side plumbing (unchanged)
// ---------------------------------------------------------------------------

void SnitchIcache::enable_sync(vp::Block *__this, bool active)
{
    SnitchIcache *_this = (SnitchIcache *)__this;
    _this->enable(active);
}

void SnitchIcache::flush_sync(vp::Block *__this, bool active)
{
    SnitchIcache *_this = (SnitchIcache *)__this;
    if (active) _this->flush();
}

void SnitchIcache::flush_line_sync(vp::Block *__this, bool active)
{
    SnitchIcache *_this = (SnitchIcache *)__this;
    if (active) _this->flush_line_op(_this->flush_line_addr);
}

void SnitchIcache::flush_line_addr_sync(vp::Block *__this, uint32_t addr)
{
    SnitchIcache *_this = (SnitchIcache *)__this;
    _this->flush_line_addr = addr;
}

void SnitchIcache::refill_event_clear_handler(vp::Block *__this, vp::ClockEvent *event)
{
    SnitchIcache *_this = (SnitchIcache *)__this;
    _this->refill_event.release();
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new SnitchIcache(config);
}

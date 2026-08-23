// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

/*
 * Snitch instruction cache: the set-associative io_v2 cache used by the
 * Snitch / Spatz clusters as their per-core L0 and shared L1 icaches.
 *
 * Forked from the generic cache/cache_v4.cpp, which stays as it is because
 * gap9, el1 and voscap share it and none of them model a snitch_icache. What
 * lives here and not there is the RTL snitch_icache behaviour:
 *
 *   - Miss-status holding registers (cfg.nb_refills): several line refills in
 *     flight at once, each owning its own downstream request vehicle, with a
 *     secondary miss on a line already on its way merged onto the refill that
 *     is fetching it rather than issuing a duplicate. The RTL has two, one for
 *     the demand miss and one for the prefetch, which is also what lets the
 *     armed prefetch go out behind a demand miss instead of after it.
 *   - Branch-target prefetching (cfg.prefetch_branch_target): snitch_icache_l0
 *     scans the line it just fetched for the first statically-taken branch or
 *     JAL and aims the prefetch at its target, falling back to the next line.
 *     See arm_prefetch_from_line() for how faithfully that is reproduced.
 *
 * Everything else is the generic cache and behaves identically:
 *   - pseudo-random LFSR replacement, line-granular
 *   - CPU requests that miss while all refill slots are busy are queued and
 *     replied to once their miss resolves; they are acknowledged as GRANTED
 *   - disable (via the `enable` wire) bypasses the cache: the CPU request is
 *     forwarded verbatim through the refill port (address transformed by
 *     refill_shift / refill_offset first)
 *   - flush, flush-line, flush-ack wires are unchanged
 */

#include <bit>
#include <vp/vp.hpp>
#include <vp/queue.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/signal.hpp>
#include <vector>
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
    // Line brought in by the sequential prefetcher and not yet demanded.
    // The first demand hit on it triggers the prefetch of the next line
    // (tagged prefetching, like the RTL snitch_icache prefetcher).
    bool prefetched;
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

    // Wire callbacks
    static void enable_sync(vp::Block *_this, bool active);
    static void flush_sync(vp::Block *_this, bool active);
    static void flush_line_sync(vp::Block *_this, bool active);
    static void flush_line_addr_sync(vp::Block *_this, uint32_t addr);

    // io_v2 callbacks
    static vp::IoReqStatus input_req(vp::Block *__this, vp::IoReq *req);
    static vp::IoRespAck refill_resp(vp::Block *__this, vp::IoReq *req);
    static void refill_retry(vp::Block *__this, vp::IoRetryChannel);

    vp::IoReqStatus handle_req(vp::IoReq *req);
    void check_state();

    cache_line_t *refill(int line_index, unsigned int addr, unsigned int tag,
                          vp::IoReq *req, unsigned int line_offset, bool *pending);
    void arm_prefetch(unsigned int addr);
    // Arm the prefetcher from the content of a line, the way the RTL
    // snitch_icache L0 prefetcher does: scan the line from the accessed
    // instruction onwards for the first taken branch or JAL and aim at its
    // target; fall back to the next line when there is none.
    void arm_prefetch_from_line(cache_line_t *line, unsigned int addr);
    void try_prefetch();
    cache_line_t *get_line(vp::IoReq *req, unsigned int *line_index,
                            unsigned int *tag, unsigned int *line_offset);

    unsigned int step_lru();
    void enable(bool e);
    void flush();
    void flush_line_op(unsigned int addr);

    // Derived geometry (computed in the ctor from cfg)
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

    // One miss-status holding register. The cache can have cfg.nb_refills of
    // these in flight at once; each owns the request vehicle used downstream,
    // so responses are told apart by their request pointer, and each remembers
    // what has to happen when its line lands.
    struct RefillSlot
    {
        vp::IoReq req;
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
    bool refill_slot_available() const { return this->nb_busy_refills < this->refill_slots.size(); }
    // Sequential prefetcher state (active when cfg.prefetch). A demand miss
    // or the first hit on a prefetched line arms the next line; the prefetch
    // is issued through the refill port when it is otherwise idle, using its
    // own request vehicle so responses can be told apart.
    bool prefetch_wanted = false;
    unsigned int prefetch_addr = 0;

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

    // Earliest cycle at which another synchronous refill can complete. Used to
    // fold a previous refill's in-flight window into subsequent synchronous
    // hits/misses so the CPU sees the serialised latency.
    int64_t refill_timestamp = -1;

    // Pseudo-random LFSR state for replacement policy
    uint8_t lru_out = 0;

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
      refill_event_clear_event(this, &SnitchIcache::refill_event_clear_handler)
{
    // Derived geometry — SnitchIcacheConfig carries bytes, we unpack into log2s.
    this->line_size_bits = ceil_log2(this->cfg.line_size);
    this->nb_sets = this->cfg.size / this->cfg.ways / this->cfg.line_size;
    this->nb_sets_bits = ceil_log2(this->nb_sets);

    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->traces.new_trace_event("port", &this->io_event, 32);

    // io_v2 slave/master ports (methods bound in-class above).
    this->new_slave_port("input", &this->input_itf);
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

    this->lines = new cache_line_t[this->nb_sets * this->cfg.ways];
    for (unsigned int i = 0; i < this->nb_sets; i++)
    {
        for (unsigned int j = 0; j < this->cfg.ways; j++)
        {
            cache_line_t *line = &this->lines[i * this->cfg.ways + j];
            line->timestamp = -1;
            line->tag = -1;
            line->prefetched = false;
            line->data = new uint8_t[this->cfg.line_size];
            this->traces.new_trace_event(
                "set_" + std::to_string(j) + "/line_" + std::to_string(i),
                &line->tag_event, 32);
        }
    }

    // Miss-status holding registers. One is the historical behaviour (a
    // single refill in flight, demand and prefetch sharing it).
    unsigned int nb_refills = this->cfg.nb_refills > 0 ? this->cfg.nb_refills : 1;
    this->refill_slots.resize(nb_refills);

    this->fsm_event = this->event_new(&SnitchIcache::fsm_handler);

    this->trace.msg(vp::Trace::LEVEL_INFO,
        "Instantiating cache (sets: %d, ways: %d, line_size: %d)\n",
        this->nb_sets, this->cfg.ways, this->cfg.line_size);
}


SnitchIcache::RefillSlot *SnitchIcache::alloc_refill_slot()
{
    for (RefillSlot &slot : this->refill_slots)
    {
        if (!slot.busy)
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
        this->enabled = this->cfg.enabled;
        this->refill_event.release();
        this->refill_retry_pending = false;
        this->input_needs_retry = false;
        this->refill_timestamp = -1;
        this->prefetch_wanted = false;
        for (RefillSlot &slot : this->refill_slots)
        {
            slot.busy = false;
            slot.line = nullptr;
            slot.cpu_req = nullptr;
        }
        this->nb_busy_refills = 0;
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
        _this->input_itf.resp(req);
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
        line->prefetched = true;
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

        // The line content is only known now, so this is where the RTL
        // prefetcher gets to look at it: re-aim the prefetch at the first
        // taken branch / JAL it contains. The miss path armed the next
        // line as a placeholder because the data was not there yet.
        _this->arm_prefetch_from_line(line,
            (unsigned int)cpu_req->get_addr());
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

    if (_this->input_needs_retry)
    {
        _this->input_needs_retry = false;
        _this->input_itf.retry();
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
            _this->input_itf.resp(req);
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


void SnitchIcache::check_state()
{
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

    // The refill port is idle and no demand request is waiting: give the
    // armed prefetch a chance.
    if (this->refill_slot_available() && !this->refill_retry_pending
        && !this->refill_pending_reqs.has_reqs())
    {
        this->try_prefetch();
    }
}


// Scan a fetched line for the first statically-taken control transfer, as
// snitch_icache_l0.sv does, and arm the prefetch at its target.
//
// The RTL looks at every 32-bit slot of the line from the fetch position
// onwards (it does not account for compressed instructions either) and
// takes the first backward conditional branch -- the sign bit of the
// immediate is its static prediction -- or JAL. JALR is unpredictable and
// ignored. With no such instruction the prefetch falls back to the next
// line, which is what the plain sequential prefetcher does.
//
// This is what lets the RTL run a loop whose body just exceeds the cache:
// the line holding the back edge names the loop top, so the top is
// refetched before the branch is even taken. A next-line prefetcher stalls
// there, which is the whole of the model's dp-fconv2d_M32 gap.
void SnitchIcache::arm_prefetch_from_line(cache_line_t *line, unsigned int addr)
{
    if (!this->cfg.prefetch)
    {
        return;
    }

    if (!this->cfg.prefetch_branch_target || line == nullptr)
    {
        this->arm_prefetch(addr);
        return;
    }

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

        // Immediates are memory-space deltas; the cache may work in a
        // shifted address space (refill_shift).
        unsigned int target = (unsigned int)((int64_t)(line_base + off) +
            (imm >> this->cfg.refill_shift));

        this->prefetch_wanted = true;
        this->prefetch_addr = target & ~(line_size - 1);
        return;
    }

    this->arm_prefetch(addr);
}


void SnitchIcache::arm_prefetch(unsigned int addr)
{
    if (!this->cfg.prefetch)
    {
        return;
    }
    this->prefetch_wanted = true;
    this->prefetch_addr = (addr & ~((1U << this->line_size_bits) - 1))
        + (1U << this->line_size_bits);
}


void SnitchIcache::try_prefetch()
{
    if (!this->prefetch_wanted || !this->enabled)
    {
        return;
    }

    unsigned int addr = this->prefetch_addr;
    unsigned int tag = addr >> this->line_size_bits;
    unsigned int line_index = tag & (this->nb_sets - 1);

    this->prefetch_wanted = false;

    // Already cached: nothing to do.
    for (unsigned int i = 0; i < this->cfg.ways; i++)
    {
        if (this->lines[line_index * this->cfg.ways + i].tag == tag)
        {
            return;
        }
    }

    // Already on its way: nothing to do.
    if (this->slot_for_tag(tag) != nullptr)
    {
        return;
    }

    unsigned int way = this->step_lru() % this->cfg.ways;
    cache_line_t *line = &this->lines[line_index * this->cfg.ways + way];

    uint32_t full_addr = ((addr & ~((1U << this->line_size_bits) - 1))
                          << this->cfg.refill_shift) + this->cfg.refill_offset;

    this->trace.msg(vp::Trace::LEVEL_DEBUG,
        "Prefetching line (addr: 0x%x, index: %d, way: %d)\n",
        full_addr, line_index, way);

    line->tag_event.event((uint8_t *)&full_addr);

    RefillSlot *slot = this->alloc_refill_slot();
    if (slot == nullptr)
    {
        // No free MSHR: keep the intent armed, check_state retries later.
        this->prefetch_wanted = true;
        return;
    }
    slot->is_prefetch = true;

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

    if (st == vp::IO_REQ_GRANTED)
    {
        slot->line = line;
        slot->tag = tag;
        return;
    }

    this->free_refill_slot(slot);

    if (st == vp::IO_REQ_DENIED)
    {
        // Best effort: drop the prefetch. The downstream will emit a
        // spurious retry() which refill_retry tolerates.
        return;
    }

    // Synchronous completion: the line lands after the accumulated refill
    // window; demand hits arriving before that pay the remaining time
    // (this is exactly how the prefetch hides the refill latency), and a
    // demand miss serialises behind the port occupancy.
    int64_t now = this->clock.get_cycles();
    int64_t latency = 0;
    if (now < this->refill_timestamp)
    {
        latency += this->refill_timestamp - now;
    }
    latency += r->get_full_latency() + this->cfg.refill_latency;

    this->refill_timestamp = now + latency;
    line->tag = tag;
    line->prefetched = true;
    line->timestamp = now + latency;
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

    // All miss-status registers in flight: queue the CPU req and back off.
    RefillSlot *slot = this->alloc_refill_slot();
    if (slot == nullptr)
    {
        this->refill_pending_reqs.push_back(cpu_req);
        *pending = true;
        return nullptr;
    }

    unsigned int refill_way = this->step_lru() % this->cfg.ways;
    cache_line_t *line = &this->lines[line_index * this->cfg.ways + refill_way];

    uint32_t full_addr = ((addr & ~((1U << this->line_size_bits) - 1))
                          << this->cfg.refill_shift) + this->cfg.refill_offset;

    this->trace.msg(vp::Trace::LEVEL_DEBUG,
        "Refilling line (addr: 0x%x, index: %d, way: %d)\n",
        full_addr, line_index, refill_way);

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
        // With more than one MSHR the armed prefetch does not have to wait
        // for this refill to come back: send it out now, behind the demand
        // miss. This is the whole point of the second register -- the RTL
        // has a dedicated prefetch request id alongside the demand one, and
        // without this the refills serialise and each line pays the full
        // memory round trip.
        this->check_state();
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
    latency += r->get_full_latency() + this->cfg.refill_latency;

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
    for (unsigned int i = 0; i < this->cfg.ways; i++)
    {
        cache_line_t *line = &this->lines[line_index * this->cfg.ways + i];
        if (line->tag == tag)
            line->tag = -1;
    }
}


void SnitchIcache::flush()
{
    this->trace.msg(vp::Trace::LEVEL_INFO, "Flushing whole cache\n");
    for (unsigned int i = 0; i < this->nb_sets; i++)
    {
        for (unsigned int j = 0; j < this->cfg.ways; j++)
        {
            this->lines[i * this->cfg.ways + j].tag = -1;
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

    cache_line_t *line = &this->lines[*line_index * this->cfg.ways];
    for (unsigned int i = 0; i < this->cfg.ways; i++)
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
        this->arm_prefetch(offset);
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
        // The RTL prefetcher looks at every line the core fetches, not
        // only at the ones it brought in itself, and latches a new target
        // only once the previous one has been issued (latch_prefetch in
        // snitch_icache_l0.sv). Keeping the stream alive across hits is
        // what matters in a partially resident loop: with tagged
        // prefetching the chain dies on the first hit on a resident line,
        // and the model then pays a full-latency demand miss on the next
        // one.
        hit_line->prefetched = false;
        if (!this->prefetch_wanted)
        {
            this->arm_prefetch_from_line(hit_line, req->get_addr());
        }
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

    _this->trace.msg(vp::Trace::LEVEL_TRACE,
        "Received req (req: %p, is_write: %d, addr: 0x%lx, size: 0x%lx)\n",
        req, is_write, offset, size);

    _this->req_event.set_and_release(offset);

    // Bypass: forward the CPU request verbatim through the refill port after
    // address transformation. The response comes back on our refill_resp
    // callback, which recognises a non-&refill_req req as a bypass forward
    // and replies to the master on our own slave port.
    if (!_this->enabled)
    {
        req->set_addr((offset << _this->cfg.refill_shift) + _this->cfg.refill_offset);
        vp::IoReqStatus st = _this->refill_itf.req(req);
        if (st == vp::IO_REQ_DENIED)
        {
            // Undo the address rewrite so the master can retry cleanly.
            req->set_addr(offset);
            _this->input_needs_retry = true;
        }
        return st;
    }

    _this->io_event.event((uint8_t *)&offset);

    // Cached path. If a refill is pending we must not start another one: queue
    // this request and ack upstream with GRANTED. When the current refill
    // resolves, fsm_handler will re-enter handle_req for this request.
    if (_this->pending_refill.get() || _this->refill_retry_pending)
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
    else if (_this->refill_slot_available() && !_this->refill_retry_pending)
    {
        _this->try_prefetch();
    }
    return st;
}


// ---------------------------------------------------------------------------
// Pseudo-random LRU (8-bit LFSR, matches cache_v3)
// ---------------------------------------------------------------------------

unsigned int SnitchIcache::step_lru()
{
    int feedback = !(((this->lru_out >> 7) & 1)
                  ^ ((this->lru_out >> 3) & 1)
                  ^ ((this->lru_out >> 2) & 1)
                  ^ ((this->lru_out >> 1) & 1));
    this->lru_out = (this->lru_out << 1) | (feedback & 1);
    return (this->lru_out >> 1) & (this->cfg.ways - 1);
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

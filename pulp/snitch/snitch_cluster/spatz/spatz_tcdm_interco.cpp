// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// Spatz TCDM interconnect. Folds the RTL spatz cluster's narrow
// ``spatz_tcdm_interconnect`` (stream_xbar, one master per bank per
// cycle) and the per-superbank ``mem_wide_narrow_mux`` into one model:
// the narrow masters (core TCDM ports + SoC) arbitrate round-robin, and
// one or more wide masters (the iDMA superbank side) preempt the narrow
// ones with unconditional priority, mirroring ``sel_wide_i =
// dma.q_valid`` (a wide access reserves all its banks for the tick and
// the narrow requests to those banks wait). Forked from the generic
// ``interco.log_ico_v2`` crossbar so that model stays wide-agnostic;
// only the wide-input machinery is spatz-specific.
//
// Logarithmic (bank-interleaved) crossbar on the io_v2 protocol with
// round-robin arbitration. M masters -> N banks. The crossbar never
// queues request pointers: an incoming request is DENIED in the normal
// (idle) state and the master is expected to retry it later. The
// crossbar only remembers *which* input wants to talk to *which* bank
// in one bit per (input, bank) pair.
//
// Forward path:
//   1. Master M calls req() for bank B. We decode B from the address,
//      set bit M in banks[B].pending_mask, schedule the FSM (0 delay),
//      and return DENIED.
//   2. The FSM iterates the banks. For each bank with pending bits it
//      picks a single winner via round-robin (find-first-set on a
//      rotated bitmask), clears the bit, and calls retry() on that
//      master. The FSM raises an "in_election" flag for the whole
//      iteration: while it is set, any request landing on input_req is
//      forwarded inline to its bank instead of being denied -- so the
//      master's synchronous retry handler just re-issues and the
//      crossbar serves it within the same tick.
//
//      This relies on the io_v2 synchronous-retry constraint: a master
//      MUST re-issue inside its retry() callback (same cycle), not defer
//      it. The in_election window is open only for the duration of the
//      retry() call here; a master that re-sends a cycle later misses it
//      and live-locks. See vp/itf/io_v2.hpp (IoSlave::retry) and
//      interfaces/io_v2.rst.
//   3. The bank is IoV2Sync and answers DONE inline, so input_req
//      returns DONE to the re-issuing master. By the time retry()
//      returns to the FSM the bank has already been hit this cycle.
//   4. If any bank still has bits after the iteration, the FSM re-arms
//      for the next cycle so each bank serves at most one master per
//      cycle.
//
// Output side (IoV2Sync): the bank must answer inline with
// IO_REQ_DONE and never drives resp()/retry(). Bind only to a sync
// slave such as memory.memory_v3.
//
// Address decoding: the input address is expected to be in
// ``[0, nb_slaves * <bank_size>)`` (the upstream router or remapper
// strips any region base before reaching the crossbar):
//
//     bank_id     = (addr >> interleaving_width) & (nb_slaves - 1)
//     bank_offset = ((addr >> (slave_bits + interleaving_width)) << interleaving_width)
//                   | (addr & ((1 << interleaving_width) - 1))

#include <climits>
#include <memory>
#include <vector>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/debug_mem.hpp>
#include <pulp/snitch/snitch_cluster/spatz/spatz_tcdm_interco/spatz_tcdm_interco_config.hpp>

// Pulse a GUI signal to `v` now (+`delay` sub-cycle offset) and back to high-Z
// one cycle later, so each bank access shows as a one-cycle marker in the GUI.
static inline void gui_pulse(vp::Signal<uint64_t> &s, uint64_t v,
                             int64_t delay, int64_t period)
{
    s.set(v, (int64_t)0, delay);
    s.release(0, delay + period);
}


class SpatzTcdmInterco;

// One per master input port. Just a thin wrapper around its IoSlave --
// the crossbar keeps no per-input state in the new design.
struct InputState
{
    InputState(SpatzTcdmInterco *top, int id);
    SpatzTcdmInterco *top;
    int id;
    vp::IoSlave itf;
};

// One per wide master input port (e.g. the DMA side of the RTL
// mem_wide_narrow_mux). A wide request spans several consecutive banks
// which are all accessed within the same arbiter tick, with priority
// over the narrow masters. Only the intent (address/size) is recorded
// while the request is denied, never the request pointer.
struct WideInputState
{
    WideInputState(SpatzTcdmInterco *top, int id);
    SpatzTcdmInterco *top;
    int id;
    vp::IoSlave itf;
    // True when this input holds a denied request waiting for election.
    bool pending = false;
    // Address/size of the denied request, recorded to compute the bank
    // claim set at election time.
    uint64_t pending_addr = 0;
    uint64_t pending_size = 0;
};

struct BankState
{
    BankState(SpatzTcdmInterco *top, int id);
    SpatzTcdmInterco *top;
    int id;
    vp::IoMaster itf;
    // Bit i set means input i has a pending (denied) request to this bank.
    uint64_t pending_mask = 0;
    // Next round-robin scan start (in [0, nb_masters)).
    int rr_next = 0;
    // GUI trace: the address of the access currently served by this bank.
    vp::Signal<uint64_t> gui_addr;
};


class SpatzTcdmInterco : public vp::Component, public vp::DebugMemIf
{
    friend struct InputState;
    friend struct WideInputState;
    friend struct BankState;

public:
    SpatzTcdmInterco(vp::ComponentConf &conf);
    void reset(bool active) override;

    // Backdoor debug access (vp/debug_mem.hpp). The default
    // debug_mem_regions() is kept: a flat region cannot express the bank
    // interleaving, so the crossbar advertises itself as a terminal and
    // debug_mem_access() redoes the bank math per granule chunk.
    vp::DebugMemIf *debug_mem_if() override { return this; }
    int debug_mem_access(uint64_t addr, uint8_t *data, uint64_t size,
        bool is_write) override;

    SpatzTcdmIntercoConfig cfg;
    vp::Trace trace;

private:
    static vp::IoReqStatus input_req      (vp::Block *__this, vp::IoReq *req, int id);
    static vp::IoReqStatus wide_input_req (vp::Block *__this, vp::IoReq *req, int id);
    static vp::IoRespAck   output_resp    (vp::Block *__this, vp::IoReq *req, int id);
    static void            output_retry   (vp::Block *__this, int id, vp::IoRetryChannel);
    static void            fsm_handler    (vp::Block *__this, vp::ClockEvent *event);

    int       decode_bank   (uint64_t offset) const;
    uint64_t  decode_offset (uint64_t offset) const;
    // GUI: pulse the bank's address trace and the top-level activity strip.
    void      gui_log_bank  (int bank_id, uint64_t addr);
    // Find the first set bit at or after `rr_next` in a `nb`-wide mask,
    // wrapping. Returns the bit index in [0, nb); precondition: mask != 0.
    int       pick_winner   (uint64_t mask, int rr_next, int nb) const;
    // Bank claim set of a wide access, one bit per bank.
    uint64_t  wide_claim_mask(uint64_t addr, uint64_t size) const;
    // Serve a wide request inline: one granule chunk per spanned bank,
    // all within the current tick. Annotates the request with the worst
    // bank latency.
    vp::IoReqStatus forward_wide(vp::IoReq *req, int id);

    int slave_bits = 0;
    std::vector<std::unique_ptr<InputState>> inputs;
    std::vector<std::unique_ptr<WideInputState>> wide_inputs;
    std::vector<std::unique_ptr<BankState>>  banks;
    // True while the FSM is calling retry() on the elected winners.
    // Any incoming request seen during this window is forwarded inline.
    bool in_election = false;
    // Wide input currently being retried by the FSM (-1 outside the wide
    // election). Contrary to the narrow side, only the retried wide input
    // may forward inline: a cascaded fresh wide request would double-serve
    // the banks within the same cycle.
    int wide_retrying = -1;
    // Banks already committed during the current cycle, one bit per bank,
    // and the cycle the mask belongs to. A wide access is accepted on
    // arrival (see wide_input_req) rather than through a deny/retry round
    // trip, so the banks it takes have to be visible to the arbiter that
    // may run later in the same cycle.
    uint64_t banks_busy = 0;
    int64_t  banks_busy_cycle = -1;
    // Cycle in which a wide access was last served. The wide side of the
    // TCDM is fed by a single 512-bit port (`axi_to_mem_interleaved` on the
    // wide crossbar), so it carries ONE access per cycle whichever
    // direction it goes -- a DMA read and a DMA write cannot both progress
    // in the same cycle even when they land on different banks. That is
    // what makes an L1->L1 copy run at half the bandwidth of a one-sided
    // transfer, on the RTL (31 vs 55-56 B/cycle) as in this model.
    int64_t  wide_used_cycle = -1;
    // Scratch request used to slice a wide access into per-bank accesses.
    vp::IoReq wide_chunk_req;

    // Refresh `banks_busy` for the current cycle and return the banks that
    // are still free.
    uint64_t banks_free_now();

    // Per-bank backdoor targets, resolved on first debug access through the
    // bank output ports' final bindings. nullptr where the bank component
    // does not support backdoor accesses.
    std::vector<vp::DebugMemIf *> bank_debug_mem;
    bool bank_debug_mem_resolved = false;

    vp::ClockEvent fsm_event;

    // --- GUI traces (visible in the model-graph / timeline) ---
    vp::Signal<uint64_t> gui_active;
    int64_t gui_last_cycle = -1;
    int     gui_nb_same_cycle = 0;
};


static int ceil_log2_u(unsigned int n)
{
    if (n <= 1) return 0;
    return 32 - __builtin_clz(n - 1);
}


//
// Per-port state ctors
//

InputState::InputState(SpatzTcdmInterco *top, int id)
    : top(top), id(id),
      itf(id, &SpatzTcdmInterco::input_req)
{
}

WideInputState::WideInputState(SpatzTcdmInterco *top, int id)
    : top(top), id(id),
      itf(id, &SpatzTcdmInterco::wide_input_req)
{
}

BankState::BankState(SpatzTcdmInterco *top, int id)
    : top(top), id(id),
      itf(id, &SpatzTcdmInterco::output_retry, &SpatzTcdmInterco::output_resp),
      gui_addr(*(vp::Block *)top, "output_" + std::to_string(id) + "/addr", 64,
               vp::SignalCommon::ResetKind::HighZ)
{
}


//
// SpatzTcdmInterco
//

SpatzTcdmInterco::SpatzTcdmInterco(vp::ComponentConf &config)
    : vp::Component(config, this->cfg),
      fsm_event(this, &SpatzTcdmInterco::fsm_handler),
      gui_active(*this, "active", 1, vp::SignalCommon::ResetKind::HighZ)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    int nb_masters = (int)this->cfg.nb_masters;
    int nb_slaves  = (int)this->cfg.nb_slaves;
    this->slave_bits = ceil_log2_u((unsigned int)nb_slaves);

    vp_assert_always(nb_masters > 0 && nb_masters <= 64, &this->trace,
        "nb_masters must be in (0, 64], got %d\n", nb_masters);

    this->banks.reserve(nb_slaves);
    for (int i = 0; i < nb_slaves; i++)
    {
        std::string name = "output_" + std::to_string(i);
        auto b = std::make_unique<BankState>(this, i);
        this->new_master_port(name, &b->itf);
        this->banks.push_back(std::move(b));
    }

    this->inputs.reserve(nb_masters);
    for (int i = 0; i < nb_masters; i++)
    {
        std::string name = "input_" + std::to_string(i);
        auto in = std::make_unique<InputState>(this, i);
        this->new_slave_port(name, &in->itf);
        this->inputs.push_back(std::move(in));
    }

    int nb_wide_masters = (int)this->cfg.nb_wide_masters;
    this->wide_inputs.reserve(nb_wide_masters);
    for (int i = 0; i < nb_wide_masters; i++)
    {
        std::string name = "wide_input_" + std::to_string(i);
        auto in = std::make_unique<WideInputState>(this, i);
        this->new_slave_port(name, &in->itf);
        this->wide_inputs.push_back(std::move(in));
    }
}

void SpatzTcdmInterco::reset(bool active)
{
    if (active)
    {
        this->in_election = false;
        this->wide_retrying = -1;
        this->banks_busy = 0;
        this->banks_busy_cycle = -1;
        this->wide_used_cycle = -1;
        for (auto &b : this->banks)
        {
            b->pending_mask = 0;
            b->rr_next = 0;
        }
        for (auto &w : this->wide_inputs)
        {
            w->pending = false;
        }
    }
}


void SpatzTcdmInterco::gui_log_bank(int bank_id, uint64_t addr)
{
    int64_t cycles = this->clock.get_cycles();
    if (cycles > this->gui_last_cycle)
    {
        this->gui_nb_same_cycle = 0;
        this->gui_last_cycle = cycles;
    }
    int64_t period = this->clock.get_period();
    int64_t delay = this->gui_nb_same_cycle > 0
        ? period - (period >> this->gui_nb_same_cycle) : 0;
    this->gui_nb_same_cycle++;

    gui_pulse(this->banks[bank_id]->gui_addr, addr, delay, period);
    gui_pulse(this->gui_active, 1, delay, period);
}


int SpatzTcdmInterco::decode_bank(uint64_t offset) const
{
    if (this->slave_bits == 0) return 0;
    int mask = (1 << this->slave_bits) - 1;
    return (int)((offset >> this->cfg.interleaving_width) & (uint64_t)mask);
}

uint64_t SpatzTcdmInterco::decode_offset(uint64_t offset) const
{
    uint64_t iw = (uint64_t)this->cfg.interleaving_width;
    uint64_t iw_mask  = (iw == 0) ? 0 : ((((uint64_t)1) << iw) - 1);
    uint64_t hi_shift = (uint64_t)this->slave_bits + iw;
    return ((offset >> hi_shift) << iw) | (offset & iw_mask);
}

int SpatzTcdmInterco::pick_winner(uint64_t mask, int rr_next, int nb) const
{
    // Valid-bit mask so an over-wide rotation doesn't pull stale bits in.
    uint64_t valid = (nb == 64) ? ~0ULL : ((1ULL << nb) - 1);
    mask &= valid;

    // Rotate the mask right by rr_next so the scan start lands at bit 0,
    // then ctz gives the offset of the first set bit at-or-after rr_next.
    uint64_t rotated;
    if (rr_next == 0)
    {
        rotated = mask;
    }
    else
    {
        rotated = ((mask >> rr_next) | (mask << (nb - rr_next))) & valid;
    }
    int rel = __builtin_ctzll(rotated);
    return (rr_next + rel) % nb;
}


//
// Forward path
//

vp::IoReqStatus SpatzTcdmInterco::input_req(vp::Block *__this, vp::IoReq *req, int id)
{
    SpatzTcdmInterco *_this = (SpatzTcdmInterco *)__this;

    uint64_t addr   = req->get_addr();
    int bank_id    = _this->decode_bank(addr);

    if (_this->in_election)
    {
        // FSM is dispatching retries; any request arriving in this
        // window (typically the re-issue triggered by the retry call
        // we just made) is forwarded inline to the IoV2Sync bank.
        uint64_t bank_offset = _this->decode_offset(addr);

        _this->trace.msg(vp::Trace::LEVEL_DEBUG,
            "Forwarding (input: %d, addr: 0x%llx -> bank %d bank_addr: 0x%llx)\n",
            id,
            (unsigned long long)addr,
            bank_id,
            (unsigned long long)bank_offset);

        _this->gui_log_bank(bank_id, addr);

        req->set_addr(bank_offset);
        vp::IoReqStatus st = _this->banks[bank_id]->itf.req(req);
        vp_assert_always(st == vp::IO_REQ_DONE, &_this->trace,
            "IoV2Sync output returned a non-DONE status (%d)\n", (int)st);
        return vp::IO_REQ_DONE;
    }

    // Idle state: record that this input wants this bank, schedule the
    // arbiter, and tell the master to retry later.
    _this->trace.msg(vp::Trace::LEVEL_DEBUG,
        "Req arrived, denying (input: %d, addr: 0x%llx, size: 0x%llx, write: %d, bank: %d)\n",
        id,
        (unsigned long long)addr,
        (unsigned long long)req->get_size(),
        req->get_is_write() ? 1 : 0,
        bank_id);

    _this->banks[bank_id]->pending_mask |= (1ULL << id);
    _this->fsm_event.enqueue(0);
    return vp::IO_REQ_DENIED;
}

uint64_t SpatzTcdmInterco::wide_claim_mask(uint64_t addr, uint64_t size) const
{
    uint64_t granule = 1ULL << this->cfg.interleaving_width;
    uint64_t mask = 0;
    while (size > 0)
    {
        mask |= 1ULL << this->decode_bank(addr);
        uint64_t chunk = granule - (addr & (granule - 1));
        if (chunk > size) chunk = size;
        addr += chunk;
        size -= chunk;
    }
    return mask;
}

vp::IoReqStatus SpatzTcdmInterco::forward_wide(vp::IoReq *req, int id)
{
    uint64_t granule = 1ULL << this->cfg.interleaving_width;
    uint64_t addr = req->get_addr();
    uint64_t size = req->get_size();
    uint8_t *data = req->get_data();
    // A byte strobe is sliced along with the data, so a partial write keeps
    // its per-byte enables when it is split across banks. Wide accesses come
    // from the DMA, which does not use one today, but sub->prepare() clears
    // the field and dropping it silently would be a trap.
    uint8_t *strb = req->get_strb();
    int64_t max_latency = 0;

    vp_assert_always(req->get_opcode() == vp::IoReqOpcode::READ ||
        req->get_opcode() == vp::IoReqOpcode::WRITE, &this->trace,
        "Atomics are not supported on wide inputs\n");

    this->trace.msg(vp::Trace::LEVEL_DEBUG,
        "Forwarding wide (input: %d, addr: 0x%llx, size: 0x%llx, write: %d)\n",
        id, (unsigned long long)addr, (unsigned long long)size,
        req->get_is_write() ? 1 : 0);

    while (size > 0)
    {
        int bank_id = this->decode_bank(addr);
        uint64_t chunk = granule - (addr & (granule - 1));
        if (chunk > size) chunk = size;

        this->gui_log_bank(bank_id, addr);

        vp::IoReq *sub = &this->wide_chunk_req;
        sub->prepare();
        sub->set_resp_status(vp::IO_RESP_OK);
        sub->set_addr(this->decode_offset(addr));
        sub->set_size(chunk);
        sub->set_data(data);
        sub->set_strb(strb);
        sub->set_opcode(req->get_opcode());

        vp::IoReqStatus st = this->banks[bank_id]->itf.req(sub);
        vp_assert_always(st == vp::IO_REQ_DONE, &this->trace,
            "IoV2Sync output returned a non-DONE status (%d)\n", (int)st);

        if (sub->get_resp_status() != vp::IO_RESP_OK)
        {
            req->set_resp_status(sub->get_resp_status());
        }
        if (sub->get_full_latency() > max_latency)
        {
            max_latency = sub->get_full_latency();
        }

        addr += chunk;
        if (data != nullptr) data += chunk;
        if (strb != nullptr) strb += chunk;
        size -= chunk;
    }

    req->inc_latency(max_latency);
    return vp::IO_REQ_DONE;
}

uint64_t SpatzTcdmInterco::banks_free_now()
{
    int64_t now = this->clock.get_cycles();
    if (this->banks_busy_cycle != now)
    {
        this->banks_busy_cycle = now;
        this->banks_busy = 0;
    }
    return ~this->banks_busy;
}

vp::IoReqStatus SpatzTcdmInterco::wide_input_req(vp::Block *__this, vp::IoReq *req, int id)
{
    SpatzTcdmInterco *_this = (SpatzTcdmInterco *)__this;

    if (_this->wide_retrying == id)
    {
        // The FSM elected this wide input and its banks are reserved for
        // this tick; serve the whole access inline.
        return _this->forward_wide(req, id);
    }

    // A wide master has unconditional priority in the RTL (`sel_wide_i =
    // dma.q_valid` in mem_wide_narrow_mux): the superbank mux switches to
    // the DMA in the cycle it raises its request, and it is the NARROW
    // side that gets its q_ready pulled low. So a wide access must not pay
    // a deny/retry round trip when its banks are free — doing so halved
    // the DMA's write bandwidth into the TCDM (one 64 B beat every two
    // cycles instead of one per cycle, i.e. 32 B/cycle against the 55
    // B/cycle the RTL sustains, which is what
    // tests/calibration/targets/spatz/dma measures on the DRAM->L1 path).
    // Serve it inline and record the banks so the arbiter, which may run
    // later in this same cycle, leaves them to the next tick.
    if (!_this->in_election)
    {
        uint64_t claim = _this->wide_claim_mask(req->get_addr(), req->get_size());
        uint64_t free_banks = _this->banks_free_now();
        if ((claim & ~free_banks) == 0 &&
            _this->wide_used_cycle != _this->clock.get_cycles())
        {
            _this->wide_used_cycle = _this->clock.get_cycles();
            _this->banks_busy |= claim;
            return _this->forward_wide(req, id);
        }
        // Either the wide port has already carried an access this cycle,
        // or the banks were committed to a narrow master before the DMA
        // showed up. Fall through to the deny/retry path: the access is
        // redone next cycle. In the second case the RTL would have
        // preempted the narrow master instead; the model cannot un-serve
        // it, so the cycle is charged to the DMA rather than to the core.
    }

    // Idle state: record the intent (address/size only, never the request
    // pointer), schedule the arbiter, and tell the master to retry later.
    WideInputState *in = _this->wide_inputs[id].get();

    _this->trace.msg(vp::Trace::LEVEL_DEBUG,
        "Wide req arrived, denying (input: %d, addr: 0x%llx, size: 0x%llx, write: %d)\n",
        id,
        (unsigned long long)req->get_addr(),
        (unsigned long long)req->get_size(),
        req->get_is_write() ? 1 : 0);

    in->pending = true;
    in->pending_addr = req->get_addr();
    in->pending_size = req->get_size();
    _this->fsm_event.enqueue(0);
    return vp::IO_REQ_DENIED;
}

void SpatzTcdmInterco::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    SpatzTcdmInterco *_this = (SpatzTcdmInterco *)__this;
    int nb = (int)_this->cfg.nb_masters;
    bool any_remaining = false;

    // Wide masters are served first, with priority over the narrow ones —
    // in the RTL the DMA side of the superbank mux preempts the cores.
    // Each elected wide master reserves all its banks for this tick.
    // Banks taken by a wide access already served inline this cycle (the
    // common case, see wide_input_req) count as reserved too.
    uint64_t banks_taken = ~_this->banks_free_now();
    for (auto &w : _this->wide_inputs)
    {
        if (!w->pending) continue;

        uint64_t claim = _this->wide_claim_mask(w->pending_addr, w->pending_size);
        if ((claim & banks_taken) != 0 ||
            _this->wide_used_cycle == _this->clock.get_cycles())
        {
            // Overlaps a wide master already served this tick; try again
            // next cycle.
            any_remaining = true;
            continue;
        }

        banks_taken |= claim;
        _this->banks_busy |= claim;
        _this->wide_used_cycle = _this->clock.get_cycles();
        w->pending = false;

        _this->trace.msg(vp::Trace::LEVEL_DEBUG,
            "Wide pick (input: %d, claim_mask: 0x%llx)\n",
            w->id, (unsigned long long)claim);

        // Retry runs the master's retry handler synchronously: the master
        // re-issues, wide_input_req forwards inline across the banks.
        _this->wide_retrying = w->id;
        w->itf.retry();
        _this->wide_retrying = -1;
    }

    _this->in_election = true;
    for (auto &bank : _this->banks)
    {
        if (bank->pending_mask == 0) continue;

        // A bank reserved by a wide master this tick serves no narrow
        // master — the pending bits stay for the next cycle.
        if ((banks_taken >> bank->id) & 1)
        {
            any_remaining = true;
            continue;
        }

        int winner = _this->pick_winner(bank->pending_mask, bank->rr_next, nb);
        bank->pending_mask &= ~(1ULL << winner);
        bank->rr_next = (winner + 1) % nb;
        // Committed for this cycle: a wide access arriving later in the
        // same cycle must not take this bank as well.
        _this->banks_busy |= 1ULL << bank->id;

        _this->trace.msg(vp::Trace::LEVEL_DEBUG,
            "Round-robin pick (bank: %d, winner: %d, remaining_mask: 0x%llx)\n",
            bank->id, winner,
            (unsigned long long)bank->pending_mask);

        // Retry runs the master's retry handler synchronously: the
        // master re-issues, input_req (with in_election=true) forwards
        // inline to the bank and returns DONE.
        _this->inputs[winner]->itf.retry();

        if (bank->pending_mask != 0) any_remaining = true;
    }
    _this->in_election = false;

    if (any_remaining)
    {
        // Re-arm next cycle so each bank serves at most one master per
        // cycle.
        _this->fsm_event.enqueue(1);
    }
}


//
// Response path: the IoV2Sync bank must never drive these.
//

vp::IoRespAck SpatzTcdmInterco::output_resp(vp::Block *__this, vp::IoReq * /*req*/, int id)
{
    SpatzTcdmInterco *_this = (SpatzTcdmInterco *)__this;
    _this->trace.fatal("Unexpected async resp() from IoV2Sync bank %d "
                       "(the synchronous sub-protocol forbids it)\n", id);
    return vp::IO_RESP_ACCEPTED;
}

void SpatzTcdmInterco::output_retry(vp::Block *__this, int id, vp::IoRetryChannel)
{
    SpatzTcdmInterco *_this = (SpatzTcdmInterco *)__this;
    _this->trace.fatal("Unexpected retry() from IoV2Sync bank %d "
                       "(the synchronous sub-protocol forbids it)\n", id);
}


//
// Backdoor debug access
//

int SpatzTcdmInterco::debug_mem_access(uint64_t addr, uint8_t *data, uint64_t size,
    bool is_write)
{
    if (!this->bank_debug_mem_resolved)
    {
        this->bank_debug_mem_resolved = true;
        this->bank_debug_mem.assign(this->banks.size(), nullptr);
        for (size_t i = 0; i < this->banks.size(); i++)
        {
            std::vector<vp::SlavePort *> finals =
                this->banks[i]->itf.get_final_ports();
            if (!finals.empty() && finals[0]->get_owner() != nullptr)
            {
                this->bank_debug_mem[i] = finals[0]->get_owner()->debug_mem_if();
            }
        }
    }

    // Walk the access one interleaving granule at a time, each chunk going
    // to its bank at the bank-local offset.
    uint64_t granule = 1ULL << this->cfg.interleaving_width;
    while (size > 0)
    {
        int bank = this->decode_bank(addr);
        uint64_t chunk = granule - (addr & (granule - 1));
        if (chunk > size)
        {
            chunk = size;
        }

        if (this->bank_debug_mem[bank] == nullptr ||
            this->bank_debug_mem[bank]->debug_mem_access(
                this->decode_offset(addr), data, chunk, is_write))
        {
            return -1;
        }

        addr += chunk;
        data += chunk;
        size -= chunk;
    }

    return 0;
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new SpatzTcdmInterco(config);
}

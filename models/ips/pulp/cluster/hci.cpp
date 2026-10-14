// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// GAP9 cluster L1 heterogeneous interconnect (HCI) on the io_v2 protocol.
//
// This is the log_ico_v2 crossbar (M narrow masters -> N banks, per-bank
// round-robin, zero-buffer deny/retry arbitration) extended with the HCI
// wide (HWPE) ports of the real hardware (fe/ips/hci):
//
//   - hci_log_interconnect:     the narrow side, modeled by the inherited
//                               log_ico_v2 election (one master per bank
//                               per cycle). With nb_group0_masters set the
//                               election is two-level like the HW
//                               XBAR_TCDM (logint_RequestBlock2CH): the
//                               masters [0, nb_group0_masters) are the CH0
//                               group (cores), the rest the CH1 group (DMA,
//                               ext); each group elects its own round-robin
//                               winner and a per-bank 2:1 round-robin flag
//                               (logint_MUX2_REQ RR_FLAG, toggled on every
//                               grant of the bank) picks the group when both
//                               have a request. The HW arbitration trees
//                               are binary trees of round-robin 2:1 nodes;
//                               the flat round-robin inside a group is an
//                               approximation of that.
//   - hci_hwpe_interconnect:    one request = one wide transaction. A wide
//                               request covers a contiguous byte range
//                               spanning up to nb_slaves consecutive
//                               granules; word i goes to bank
//                               (addr/granule + i) mod nb_slaves, with the
//                               bank-local address bumped by one granule
//                               past the wrap (falls out of full-address
//                               decoding). The grant is ALL-OR-NOTHING: the
//                               request proceeds only when every bank it
//                               touches is free this cycle.
//   - hci_shallow_interconnect: per-bank 2:1 priority mux at the SRAM
//                               boundary. The narrow side wins by default;
//                               the wide side only gets the banks the
//                               narrow side does not use this cycle. An
//                               8-bit starvation counter
//                               (wide_max_stall, HW low_prio_max_stall)
//                               masks the entire high-priority side for one
//                               cycle once the low side has been stalled
//                               that many consecutive conflict cycles.
//                               wide_prio (HW hwpe_prio) swaps the roles.
//
// Narrow forward path: identical to log_ico_v2 (see its header comment):
// fresh requests are DENIED, the per-cycle election retries one winner per
// bank, the synchronous re-issue is forwarded inline to the IoV2Sync bank.
//
// Wide forward path:
//   1. A fresh wide request is DENIED; the crossbar records which banks it
//      needs (a bit mask computed by chunking [addr, addr+size) at granule
//      boundaries) and arms the election.
//   2. The election first serves the high-priority side, accumulating the
//      set of banks taken this cycle. A pending wide request whose bank
//      mask does not overlap the taken set is retried; the synchronous
//      re-issue is split into one per-bank sub-request per granule chunk,
//      each forwarded inline to its IoV2Sync bank (real data moves here).
//      The wide request completes DONE with latency = max over the chunk
//      latencies.
//   3. On conflict the wide request stays pending and the starvation
//      counter advances; when it reaches wide_max_stall the next election
//      serves only the low-priority side (the whole opposite side is
//      masked for that cycle, as in hci_shallow_interconnect).
//
// Unlike the narrow inputs, a wide request never gets the in-election
// inline free pass: only the one-shot window opened by its own retry()
// forwards it, so the all-or-nothing bank check cannot be bypassed.
//
// Output side: IoV2Sync banks only (inline DONE), as in log_ico_v2.
//
// Per-cycle arbitration metrics (GUI / VCD signals, high-Z when idle), only
// compiled in the event-capable flavours (CONFIG_GVSOC_EVENT_ACTIVE: profile
// / debug); the optimized build carries no bookkeeping for them:
//   - output_<i>/denied:     number of requests that wanted bank i this
//                            cycle and were not served (narrow losers plus
//                            stalled wide requests covering the bank).
//   - input_<i>/access,
//     wide_input_<i>/access: per master port, GRANTED (1) when its access
//                            went through this cycle, DENIED (2) when it
//                            requested and lost the election.
//   - input_<i>/granted, input_<i>/denied (and the wide_input_<i> pair):
//                            the same outcome as two 1-bit pulses, so a
//                            time average gives the granted / denied rate.

#include <climits>
#include <memory>
#include <vector>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/itf/wire.hpp>
#include <vp/debug_mem.hpp>


class Hci;

// Per-cycle master port outcome (input_<i>/access signal values).
enum AccessState
{
    ACCESS_GRANTED  = 1,
    ACCESS_DENIED = 2,
};

// One per narrow master input port. Just a thin wrapper around its IoSlave --
// the crossbar keeps no per-input state for narrow masters.
struct InputState
{
    InputState(Hci *top, int id);
    Hci *top;
    int id;
    vp::IoSlave itf;
    // GRANTED / DENIED for the current cycle, high-Z otherwise, and the
    // same outcome as two pulses.
    vp::Signal<uint8_t> access;
    vp::Signal<bool> granted;
    vp::Signal<bool> denied;
    void report(AccessState state);
};

struct BankState
{
    BankState(Hci *top, int id);
    Hci *top;
    int id;
    vp::IoMaster itf;
    // Bit i set means narrow input i has a pending (denied) request to this bank.
    uint64_t pending_mask = 0;
    // Next round-robin scan start (in [0, nb_masters)); flat election.
    int rr_next = 0;
    // Two-level election: scan start of each group and the group flag
    // (true: the second group has priority when both request).
    int rr_next_group[2] = {0, 0};
    bool group1_prio = false;
    // Requests wanting this bank and not served this cycle, high-Z otherwise.
    vp::Signal<uint8_t> denied;
};

// One per wide (HWPE) input port.
struct WideState
{
    WideState(Hci *top, int id);
    Hci *top;
    int id;
    vp::IoSlave itf;
    // A denied wide request is waiting for election.
    bool pending = false;
    // Banks needed by the pending request (computed at deny time).
    uint64_t bank_mask = 0;
    // One-shot inline-forward window opened by the election retry().
    bool elected = false;
    // Pending when the current election started (a request issued from
    // inside the election is only considered, and reported, next cycle).
    bool requested = false;
    // Scratch request for the per-bank forwards of one wide transaction.
    vp::IoReq sub_req;
    // GRANTED / DENIED for the current cycle, high-Z otherwise, and the
    // same outcome as two pulses.
    vp::Signal<uint8_t> access;
    vp::Signal<bool> granted;
    vp::Signal<bool> denied;
    void report(AccessState state);
};


class Hci : public vp::Component, public vp::DebugMemIf
{
    friend struct InputState;
    friend struct BankState;
    friend struct WideState;

public:
    Hci(vp::ComponentConf &conf);
    void reset(bool active) override;

    // Backdoor debug access (vp/debug_mem.hpp). The default
    // debug_mem_regions() is kept: a flat region cannot express the bank
    // interleaving, so the crossbar advertises itself as a terminal and
    // debug_mem_access() redoes the bank math per granule chunk.
    vp::DebugMemIf *debug_mem_if() override { return this; }
    int debug_mem_access(uint64_t addr, uint8_t *data, uint64_t size,
        bool is_write) override;

    vp::Trace trace;

private:
    static vp::IoReqStatus input_req   (vp::Block *__this, vp::IoReq *req, int id);
    static vp::IoReqStatus wide_input_req(vp::Block *__this, vp::IoReq *req, int id);
    static vp::IoRespAck   output_resp (vp::Block *__this, vp::IoReq *req, int id);
    static void            output_retry(vp::Block *__this, int id, vp::IoRetryChannel);
    static void            fsm_handler (vp::Block *__this, vp::ClockEvent *event);
    static void            ctrl_sync   (vp::Block *__this, uint32_t value);

    int       decode_bank   (uint64_t offset) const;
    uint64_t  decode_offset (uint64_t offset) const;
    // Find the first set bit at or after `rr_next` in a `nb`-wide mask,
    // wrapping. Returns the bit index in [0, nb); precondition: mask != 0.
    int       pick_winner   (uint64_t mask, int rr_next, int nb) const;

    // Set of banks touched by a wide request [addr, addr+size).
    uint64_t        compute_bank_mask(uint64_t addr, uint64_t size);
    // Split a granted wide request into per-bank granule chunks and forward
    // them inline. Returns DONE with the max chunk latency annotated.
    vp::IoReqStatus wide_forward(WideState *w, vp::IoReq *req);

    // Election sub-passes. `taken` accumulates the banks served this cycle.
    void serve_narrow(uint64_t &taken);
    void serve_wide  (uint64_t &taken);
    // Dump the per-cycle denial metrics once the election is over.
    void report_denied();

    int nb_masters = 0;
    // Narrow masters [0, nb_group0_masters) form the first arbitration group
    // (0: flat election).
    int nb_group0_masters = 0;
    uint64_t group0_mask = 0;
    int nb_slaves = 0;
    int interleaving_width = 0;
    int nb_wide_masters = 0;
    // Consecutive conflict cycles after which the low-priority side gets one
    // exclusive cycle. 0 disables the starvation protection (HW reset value).
    int wide_max_stall = 0;
    // False: narrow side (cores/DMA) has priority, wide side is low priority
    // (HW default). True: roles swapped (HW hwpe_prio).
    bool wide_prio = false;
    // Construction-time defaults, restored on reset (runtime ctrl_sync
    // overrides the live values).
    int  cfg_wide_max_stall = 0;
    bool cfg_wide_prio = false;

    int slave_bits = 0;
    // Runtime arbitration control (cluster_ctrl CLUSTER_CFG hci_ctrl field:
    // {arb_policy[10:9] (ignored), hwpe_prio[8], low_prio_max_stall[7:0]}).
    // Overrides the construction-time wide_max_stall / wide_prio when synced.
    vp::WireSlave<uint32_t> ctrl_itf;
    std::vector<std::unique_ptr<InputState>> inputs;
    std::vector<std::unique_ptr<BankState>>  banks;
    std::vector<std::unique_ptr<WideState>>  wides;
    // True while the FSM is calling retry() on the elected winners.
    // Any narrow request landing on input_req in this window is forwarded
    // inline (synchronous re-issue contract).
    bool in_election = false;

    // Per-bank backdoor targets, resolved on first debug access through the
    // bank output ports' final bindings. nullptr where the bank component
    // does not support backdoor accesses.
    std::vector<vp::DebugMemIf *> bank_debug_mem;
    bool bank_debug_mem_resolved = false;
    // Consecutive elections the low-priority side requested and lost.
    int low_stall_ctr = 0;

    vp::ClockEvent fsm_event;
    // Arms the election of the next cycle. The election must run at the end
    // of its cycle, after every master had the chance to issue: enqueued with
    // a delay of 1, it would be placed among the events of the next cycle in
    // enqueue order, possibly before a master issuing in that cycle, whose
    // request would then be served by a second election in the same cycle.
    // This event fires at the start of the next cycle and enqueues the
    // election with a delay of 0, i.e. behind everything already queued for
    // that cycle.
    vp::ClockEvent next_cycle_event;
    static void next_cycle_handler(vp::Block *__this, vp::ClockEvent *event);

#ifdef VP_ASSERT_ACTIVE
    // Time of the last election. All the requests of a cycle must have
    // arrived when its election runs: a request arriving after it would be
    // served by a second election in the same cycle, granting its port a
    // second time. A time, not a cycle count: a request coming from another
    // clock domain can arrive before this domain's cycle count is
    // resynchronized. Only tracked when assertions are compiled in.
    int64_t election_time = -1;
    void check_request_before_election(const char *side, int id);
    // Narrow input being retried by the election (-1 outside a retry), and
    // whether it already re-issued: inside the election, the only request an
    // input may send is the single re-issue of the request being retried.
    int retry_input = -1;
    bool retry_used = false;
#endif
};


static int ceil_log2_u(unsigned int n)
{
    if (n <= 1) return 0;
    return 32 - __builtin_clz(n - 1);
}


//
// Per-port state ctors
//

InputState::InputState(Hci *top, int id)
    : top(top), id(id),
      itf(id, &Hci::input_req),
      access(*top, "input_" + std::to_string(id) + "/access", 8,
             vp::SignalCommon::ResetKind::HighZ),
      granted(*top, "input_" + std::to_string(id) + "/granted", 1,
              vp::SignalCommon::ResetKind::HighZ),
      denied(*top, "input_" + std::to_string(id) + "/denied", 1,
             vp::SignalCommon::ResetKind::HighZ)
{
}

#ifdef CONFIG_GVSOC_EVENT_ACTIVE
void InputState::report(AccessState state)
{
    this->access.set_and_release(state);
    if (state == ACCESS_GRANTED) this->granted.set_and_release(true);
    else                         this->denied.set_and_release(true);
}
#endif

BankState::BankState(Hci *top, int id)
    : top(top), id(id),
      itf(id, &Hci::output_retry, &Hci::output_resp),
      denied(*top, "output_" + std::to_string(id) + "/denied", 8,
             vp::SignalCommon::ResetKind::HighZ)
{
}

WideState::WideState(Hci *top, int id)
    : top(top), id(id),
      itf(id, &Hci::wide_input_req),
      access(*top, "wide_input_" + std::to_string(id) + "/access", 8,
             vp::SignalCommon::ResetKind::HighZ),
      granted(*top, "wide_input_" + std::to_string(id) + "/granted", 1,
              vp::SignalCommon::ResetKind::HighZ),
      denied(*top, "wide_input_" + std::to_string(id) + "/denied", 1,
             vp::SignalCommon::ResetKind::HighZ)
{
}

#ifdef CONFIG_GVSOC_EVENT_ACTIVE
void WideState::report(AccessState state)
{
    this->access.set_and_release(state);
    if (state == ACCESS_GRANTED) this->granted.set_and_release(true);
    else                         this->denied.set_and_release(true);
}
#endif


//
// Hci
//

Hci::Hci(vp::ComponentConf &config)
    : vp::Component(config),
      fsm_event(this, &Hci::fsm_handler),
      next_cycle_event(this, &Hci::next_cycle_handler)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->nb_masters         = get_js_config()->get_child_int("nb_masters");
    this->nb_slaves          = get_js_config()->get_child_int("nb_slaves");
    this->interleaving_width = get_js_config()->get_child_int("interleaving_width");
    this->nb_wide_masters    = get_js_config()->get_child_int("nb_wide_masters");
    this->nb_group0_masters  = get_js_config()->get_child_int("nb_group0_masters");
    this->cfg_wide_max_stall = get_js_config()->get_child_int("wide_max_stall");
    this->cfg_wide_prio      = get_js_config()->get_child_bool("wide_prio");
    this->wide_max_stall     = this->cfg_wide_max_stall;
    this->wide_prio          = this->cfg_wide_prio;

    this->slave_bits = ceil_log2_u((unsigned int)this->nb_slaves);

    vp_assert_always(this->nb_masters > 0 && this->nb_masters <= 64, &this->trace,
        "nb_masters must be in (0, 64], got %d\n", this->nb_masters);
    vp_assert_always(this->nb_group0_masters >= 0 && this->nb_group0_masters <= this->nb_masters,
        &this->trace, "nb_group0_masters must be in [0, nb_masters], got %d\n",
        this->nb_group0_masters);
    this->group0_mask = this->nb_group0_masters >= 64 ? ~0ULL
        : ((1ULL << this->nb_group0_masters) - 1);
    vp_assert_always(this->nb_slaves > 0 && this->nb_slaves <= 64, &this->trace,
        "nb_slaves must be in (0, 64], got %d\n", this->nb_slaves);

    this->banks.reserve(this->nb_slaves);
    for (int i = 0; i < this->nb_slaves; i++)
    {
        std::string name = "output_" + std::to_string(i);
        auto b = std::make_unique<BankState>(this, i);
        this->new_master_port(name, &b->itf);
        this->banks.push_back(std::move(b));
    }

    this->inputs.reserve(this->nb_masters);
    for (int i = 0; i < this->nb_masters; i++)
    {
        std::string name = "input_" + std::to_string(i);
        auto in = std::make_unique<InputState>(this, i);
        this->new_slave_port(name, &in->itf);
        this->inputs.push_back(std::move(in));
    }

    this->wides.reserve(this->nb_wide_masters);
    for (int i = 0; i < this->nb_wide_masters; i++)
    {
        std::string name = "wide_input_" + std::to_string(i);
        auto w = std::make_unique<WideState>(this, i);
        this->new_slave_port(name, &w->itf);
        this->wides.push_back(std::move(w));
    }

    this->ctrl_itf.set_sync_meth(&Hci::ctrl_sync);
    this->new_slave_port("ctrl", &this->ctrl_itf);
}

void Hci::ctrl_sync(vp::Block *__this, uint32_t value)
{
    Hci *_this = (Hci *)__this;

    _this->wide_max_stall = value & 0xff;
    _this->wide_prio      = ((value >> 8) & 1) != 0;
    // arb_policy (bits 10:9, narrow-side arbitration flavour) is not modeled.
    _this->low_stall_ctr  = 0;

    _this->trace.msg(vp::Trace::LEVEL_DEBUG,
        "Updated HCI control (wide_max_stall: %d, wide_prio: %d)\n",
        _this->wide_max_stall, _this->wide_prio ? 1 : 0);
}

void Hci::reset(bool active)
{
    if (active)
    {
        this->in_election = false;
        this->low_stall_ctr = 0;
        this->wide_max_stall = this->cfg_wide_max_stall;
        this->wide_prio = this->cfg_wide_prio;
        for (auto &b : this->banks)
        {
            b->pending_mask = 0;
            b->rr_next = 0;
            b->rr_next_group[0] = 0;
            b->rr_next_group[1] = 0;
            b->group1_prio = false;
        }
        for (auto &w : this->wides)
        {
            w->pending = false;
            w->bank_mask = 0;
            w->elected = false;
            w->requested = false;
        }
    }
}


int Hci::decode_bank(uint64_t offset) const
{
    if (this->slave_bits == 0) return 0;
    int mask = (1 << this->slave_bits) - 1;
    return (int)((offset >> this->interleaving_width) & (uint64_t)mask);
}


uint64_t Hci::decode_offset(uint64_t offset) const
{
    uint64_t iw = (uint64_t)this->interleaving_width;
    uint64_t iw_mask  = (iw == 0) ? 0 : ((((uint64_t)1) << iw) - 1);
    uint64_t hi_shift = (uint64_t)this->slave_bits + iw;
    return ((offset >> hi_shift) << iw) | (offset & iw_mask);
}

int Hci::pick_winner(uint64_t mask, int rr_next, int nb) const
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

uint64_t Hci::compute_bank_mask(uint64_t addr, uint64_t size)
{
    uint64_t granule = ((uint64_t)1) << this->interleaving_width;
    uint64_t mask = 0;
    int nb_chunks = 0;
    while (size)
    {
        uint64_t chunk = granule - (addr & (granule - 1));
        if (chunk > size) chunk = size;
        mask |= (1ULL << this->decode_bank(addr));
        nb_chunks++;
        addr += chunk;
        size -= chunk;
    }
    vp_assert_always(nb_chunks <= this->nb_slaves, &this->trace,
        "Wide request spans more granules (%d) than banks (%d)\n",
        nb_chunks, this->nb_slaves);
    return mask;
}


//
// Narrow forward path (identical to log_ico_v2)
//

vp::IoReqStatus Hci::input_req(vp::Block *__this, vp::IoReq *req, int id)
{
    Hci *_this = (Hci *)__this;

    uint64_t addr   = req->get_addr();
    int bank_id    = _this->decode_bank(addr);

    if (_this->in_election)
    {
#ifdef VP_ASSERT_ACTIVE
        _this->traces.assert(id == _this->retry_input && !_this->retry_used,
            "narrow input %d issued a request during the election, which only "
            "accepts the single re-issue of the request being retried (retried "
            "input: %d, already re-issued: %d)\n",
            id, _this->retry_input, _this->retry_used);
        _this->retry_used = true;
#endif
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

        req->set_addr(bank_offset);
        vp::IoReqStatus st = _this->banks[bank_id]->itf.req(req);
        vp_assert_always(st == vp::IO_REQ_DONE, &_this->trace,
            "IoV2Sync output returned a non-DONE status (%d)\n", (int)st);
#ifdef CONFIG_GVSOC_EVENT_ACTIVE
        _this->inputs[id]->report(ACCESS_GRANTED);
#endif
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

#ifdef VP_ASSERT_ACTIVE
    _this->check_request_before_election("narrow", id);
#endif
    _this->banks[bank_id]->pending_mask |= (1ULL << id);
    _this->fsm_event.enqueue(0);
    return vp::IO_REQ_DENIED;
}


//
// Wide forward path
//

vp::IoReqStatus Hci::wide_input_req(vp::Block *__this, vp::IoReq *req, int id)
{
    Hci *_this = (Hci *)__this;
    WideState *w = _this->wides[id].get();

    if (w->elected)
    {
        // One-shot window opened by the election retry(): forward the
        // re-issued wide request inline, granule chunk by granule chunk.
        w->elected = false;
        return _this->wide_forward(w, req);
    }

#ifdef VP_ASSERT_ACTIVE
    // Inside the election, the only request a wide input may send is the
    // re-issue of its retried request, forwarded above. A new request here
    // (typically the next one, chained from the completion of the retried
    // one) belongs to a later cycle and must be issued then.
    _this->traces.assert(!_this->in_election,
        "wide input %d issued a new request during the election, which only "
        "accepts the re-issue of the request being retried (the next request "
        "must be issued in its own cycle, not chained from a completion)\n",
        id);
    _this->check_request_before_election("wide", id);
#endif

    // Fresh request (or one re-issued outside its window): record the banks
    // it needs and wait for election. Never take the narrow in-election free
    // pass -- the all-or-nothing bank check must not be bypassed.
    w->bank_mask = _this->compute_bank_mask(req->get_addr(), req->get_size());
    w->pending = true;

    _this->trace.msg(vp::Trace::LEVEL_DEBUG,
        "Wide req arrived, denying (wide input: %d, addr: 0x%llx, size: 0x%llx, write: %d, bank_mask: 0x%llx)\n",
        id,
        (unsigned long long)req->get_addr(),
        (unsigned long long)req->get_size(),
        req->get_is_write() ? 1 : 0,
        (unsigned long long)w->bank_mask);

    // A wide request issued from inside the current election (e.g. the
    // master chaining its next access from the completion of the previous
    // one) is considered at the next cycle's election.
    if (_this->in_election)
    {
        _this->next_cycle_event.enqueue(1);
    }
    else
    {
        _this->fsm_event.enqueue(0);
    }
    return vp::IO_REQ_DENIED;
}

vp::IoReqStatus Hci::wide_forward(WideState *w, vp::IoReq *req)
{
    uint64_t addr = req->get_addr();
    uint64_t size = req->get_size();
    uint8_t *data = req->get_data();
    bool is_write = req->get_is_write();
    uint64_t granule = ((uint64_t)1) << this->interleaving_width;
    int64_t max_latency = 0;
    uint64_t offset = 0;

    while (size)
    {
        uint64_t chunk = granule - (addr & (granule - 1));
        if (chunk > size) chunk = size;

        int bank_id = this->decode_bank(addr);
        uint64_t bank_offset = this->decode_offset(addr);

        this->trace.msg(vp::Trace::LEVEL_DEBUG,
            "Forwarding wide chunk (wide input: %d, addr: 0x%llx -> bank %d bank_addr: 0x%llx, size: 0x%llx)\n",
            w->id,
            (unsigned long long)addr,
            bank_id,
            (unsigned long long)bank_offset,
            (unsigned long long)chunk);

        vp::IoReq *sub = &w->sub_req;
        sub->set_addr(bank_offset);
        sub->set_size(chunk);
        sub->set_data(data + offset);
        sub->set_is_write(is_write);
        sub->prepare();

        vp::IoReqStatus st = this->banks[bank_id]->itf.req(sub);
        vp_assert_always(st == vp::IO_REQ_DONE, &this->trace,
            "IoV2Sync output returned a non-DONE status (%d)\n", (int)st);

        if (sub->get_latency() > max_latency) max_latency = sub->get_latency();

        addr += chunk;
        offset += chunk;
        size -= chunk;
    }

    req->set_latency(max_latency);
#ifdef CONFIG_GVSOC_EVENT_ACTIVE
    w->report(ACCESS_GRANTED);
#endif
    return vp::IO_REQ_DONE;
}


//
// Election
//

void Hci::serve_narrow(uint64_t &taken)
{
    int nb = this->nb_masters;

    for (auto &bank : this->banks)
    {
        if (bank->pending_mask == 0) continue;
        // Bank already given to a wide port this cycle (wide_prio or
        // starvation turn).
        if (taken & (1ULL << bank->id)) continue;

        int winner;
        if (this->nb_group0_masters == 0)
        {
            winner = this->pick_winner(bank->pending_mask, bank->rr_next, nb);
            bank->rr_next = (winner + 1) % nb;
        }
        else
        {
            // Two-level election: each group has its own round-robin, the
            // bank's flag arbitrates between the groups when both request
            // and toggles on every grant (logint_MUX2_REQ RR_FLAG).
            uint64_t mask0 = bank->pending_mask & this->group0_mask;
            uint64_t mask1 = bank->pending_mask & ~this->group0_mask;
            int group = (mask1 != 0 && (mask0 == 0 || bank->group1_prio)) ? 1 : 0;
            uint64_t mask = group ? mask1 : mask0;
            winner = this->pick_winner(mask, bank->rr_next_group[group], nb);
            bank->rr_next_group[group] = (winner + 1) % nb;
            bank->group1_prio = !bank->group1_prio;
        }
        bank->pending_mask &= ~(1ULL << winner);
        taken |= (1ULL << bank->id);

        this->trace.msg(vp::Trace::LEVEL_DEBUG,
            "Round-robin pick (bank: %d, winner: %d, remaining_mask: 0x%llx)\n",
            bank->id, winner,
            (unsigned long long)bank->pending_mask);

        // Retry runs the master's retry handler synchronously: the
        // master re-issues, input_req (with in_election=true) forwards
        // inline to the bank and returns DONE.
#ifdef VP_ASSERT_ACTIVE
        this->retry_input = winner;
        this->retry_used = false;
#endif
        this->inputs[winner]->itf.retry();
#ifdef VP_ASSERT_ACTIVE
        this->retry_input = -1;
#endif
    }
}

void Hci::serve_wide(uint64_t &taken)
{
    for (auto &w : this->wides)
    {
        if (!w->pending) continue;
        if (w->bank_mask & taken)
        {
            this->trace.msg(vp::Trace::LEVEL_DEBUG,
                "Wide request stalled (wide input: %d, bank_mask: 0x%llx, taken: 0x%llx)\n",
                w->id,
                (unsigned long long)w->bank_mask,
                (unsigned long long)taken);
            continue;
        }

        this->trace.msg(vp::Trace::LEVEL_DEBUG,
            "Wide pick (wide input: %d, bank_mask: 0x%llx)\n",
            w->id, (unsigned long long)w->bank_mask);

        w->pending = false;
        taken |= w->bank_mask;
        // One-shot window: the synchronous re-issue triggered by this retry
        // is forwarded inline by wide_input_req.
        w->elected = true;
        w->itf.retry();
        w->elected = false;
    }
}

#ifdef VP_ASSERT_ACTIVE
void Hci::check_request_before_election(const char *side, int id)
{
    this->traces.assert(this->election_time != this->time.get_time(),
        "%s input %d issued a request after the election of its cycle: it "
        "would be granted by a second election in the same cycle (a master "
        "must issue before the election, e.g. from the next cycle once a "
        "slot freed in this one)\n", side, id);
}
#endif

#ifdef CONFIG_GVSOC_EVENT_ACTIVE
void Hci::report_denied()
{
    // Everything still pending after the election requested this cycle and
    // lost: the narrow masks only ever grow outside the election (in-window
    // requests are forwarded inline), and wide requests issued from inside
    // the election are excluded through `requested`.
    uint64_t denied_inputs = 0;
    for (auto &bank : this->banks)
    {
        int nb = __builtin_popcountll(bank->pending_mask);
        denied_inputs |= bank->pending_mask;
        for (auto &w : this->wides)
        {
            if (w->requested && w->pending && (w->bank_mask & (1ULL << bank->id))) nb++;
        }
        if (nb) bank->denied.set_and_release((uint8_t)nb);
    }

    while (denied_inputs)
    {
        int id = __builtin_ctzll(denied_inputs);
        denied_inputs &= denied_inputs - 1;
        this->inputs[id]->report(ACCESS_DENIED);
    }

    for (auto &w : this->wides)
    {
        if (w->requested && w->pending) w->report(ACCESS_DENIED);
    }
}
#endif

void Hci::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Hci *_this = (Hci *)__this;

    bool wide_requested = false;
    for (auto &w : _this->wides)
    {
#ifdef CONFIG_GVSOC_EVENT_ACTIVE
        w->requested = w->pending;
#endif
        wide_requested |= w->pending;
    }
    bool narrow_requested = false;
    for (auto &b : _this->banks) narrow_requested |= (b->pending_mask != 0);

    // Starvation turn: once the low-priority side has been stalled
    // wide_max_stall consecutive conflict cycles, the whole high-priority
    // side is masked for one cycle (hci_shallow_interconnect behavior).
    bool low_pending = _this->wide_prio ? narrow_requested : wide_requested;
    bool low_turn = _this->wide_max_stall > 0 && low_pending
        && _this->low_stall_ctr >= _this->wide_max_stall;

    // Which side wins this cycle, and whether the loser is fully masked.
    bool wide_first = _this->wide_prio ^ low_turn;

#ifdef VP_ASSERT_ACTIVE
    _this->election_time = _this->time.get_time();
#endif
    _this->in_election = true;

    uint64_t taken = 0;
    if (wide_first)
    {
        _this->serve_wide(taken);
        if (!low_turn) _this->serve_narrow(taken);
    }
    else
    {
        _this->serve_narrow(taken);
        if (!low_turn) _this->serve_wide(taken);
    }

    _this->in_election = false;

#ifdef CONFIG_GVSOC_EVENT_ACTIVE
    _this->report_denied();
#endif

    // Starvation accounting: count elections where the low side still has
    // pending requests while the high side was active; reset otherwise. A
    // taken starvation turn restarts the count (the low side got its cycle):
    // the high side is masked for exactly one cycle, not until the low side
    // fully drains — with a high side requesting every cycle, the count would
    // otherwise stay above the threshold and starve it indefinitely.
    bool wide_remaining = false;
    for (auto &w : _this->wides) wide_remaining |= w->pending;
    bool narrow_remaining = false;
    for (auto &b : _this->banks) narrow_remaining |= (b->pending_mask != 0);

    bool low_remaining  = _this->wide_prio ? narrow_remaining : wide_remaining;
    bool high_requested = _this->wide_prio ? wide_requested   : narrow_requested;
    if (low_turn)
    {
        _this->low_stall_ctr = 0;
    }
    else if (low_remaining && high_requested)
    {
        _this->low_stall_ctr++;
    }
    else
    {
        _this->low_stall_ctr = 0;
    }

    if (narrow_remaining || wide_remaining)
    {
        // Re-arm next cycle so each bank serves at most one access per
        // cycle.
        _this->next_cycle_event.enqueue(1);
    }
}

void Hci::next_cycle_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Hci *_this = (Hci *)__this;
    _this->fsm_event.enqueue(0);
}


//
// Response path: the IoV2Sync bank must never drive these.
//

vp::IoRespAck Hci::output_resp(vp::Block *__this, vp::IoReq * /*req*/, int id)
{
    Hci *_this = (Hci *)__this;
    _this->trace.fatal("Unexpected async resp() from IoV2Sync bank %d "
                       "(the synchronous sub-protocol forbids it)\n", id);
    return vp::IO_RESP_ACCEPTED;
}

void Hci::output_retry(vp::Block *__this, int id, vp::IoRetryChannel)
{
    Hci *_this = (Hci *)__this;
    _this->trace.fatal("Unexpected retry() from IoV2Sync bank %d "
                       "(the synchronous sub-protocol forbids it)\n", id);
}


//
// Backdoor debug access
//

int Hci::debug_mem_access(uint64_t addr, uint8_t *data, uint64_t size,
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
    uint64_t granule = 1ULL << this->interleaving_width;
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
    return new Hci(config);
}

// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <deque>
#include <vector>
#include <vp/vp.hpp>
#include <vp/signal.hpp>
#include <cpu/iss_v2/include/types.hpp>
#include <cpu/iss_v2/include/insn.hpp>

class Iss;

/**
 * @brief Kind of an instruction offloaded to the FP subsystem.
 *
 * Stored in the decoder item block_id (-1 for the instructions executed by
 * the integer core).
 */
enum SnitchFpSsKind
{
    // FP computation, only FP registers: sequenced
    SNITCH_FPSS_COMPUTE = 0,
    // FP load and store: sequenced, address captured at offload
    SNITCH_FPSS_LOAD = 1,
    SNITCH_FPSS_STORE = 2,
    // FP instruction writing an integer register (compare, classify,
    // conversion, move): bypasses the sequencer, only issued once it is
    // empty. The integer register stays pending in the core until the result
    // comes back.
    SNITCH_FPSS_TO_INT = 3,
    // FP instruction reading an integer register (conversion, move):
    // bypasses the sequencer, integer operand captured at offload
    SNITCH_FPSS_FROM_INT = 4,
    // FREP loop configuration, repetition count captured at offload
    SNITCH_FPSS_FREP_O = 5,
    SNITCH_FPSS_FREP_I = 6,
};

// One instruction in the FP subsystem, from its offload to its last issue
struct SnitchFpSsEntry
{
    // Instruction held in the integer core, terminated once executed
    InsnEntry *entry;
    SnitchFpSsKind kind;
    // FPU latency
    int latency;
    // Integer operand captured at offload: the access address of the FP
    // loads and stores, the repetition count of FREP, the integer input of
    // FROM_INT (whose register is int_reg).
    iss_reg_t int_value;
    int int_reg;
    // Earliest cycle the instruction can be issued (offload latency)
    int64_t ready_cycle;
    // Copies of the instruction with staggered registers, built when an FREP
    // loop issues it with an offset (index offset - 1)
    std::vector<iss_insn_t *> stagger_insns;
    // The consistency address queue entry of a load or store is still there.
    // It goes away with the first execution, the FREP repetitions are not
    // tracked.
    bool caq_pending;
    // Handler executing the instruction, the full or fast one depending on
    // the variant of the stub which offloaded it
    iss_reg_t (*handler)(Iss *, iss_insn_t *, iss_reg_t);
};

// FP load or store of the FP LSU
struct SnitchFpLsuReq
{
    // First field so that a raw vp::IoReq * can be cast back to its entry
    vp::IoReq req;
    uint64_t data;
    // Destination FP register of a load
    int reg;
    // Held instruction to terminate when the access completes, NULL if it is
    // not the last execution of the instruction (FREP)
    InsnEntry *entry;
    // Address captured at offload, for the consistency address queue, and
    // whether the access releases its entry
    iss_addr_t caq_addr;
    bool caq_release;
    // Cycle at which a synchronously answered access completes
    int64_t timestamp;
    SnitchFpLsuReq *next;
};

/**
 * @brief Snitch FP subsystem (RTL snitch_fp_ss + snitch_sequencer).
 *
 * The integer core offloads the FP instructions through offload_stub, which
 * holds them in the core and pushes them here with their integer operands.
 * Up to SEQ_BUFFER_SIZE of them wait in the sequencer buffer, plus one in the
 * offload cut; the core stalls on an offload beyond that, or behind an
 * instruction which bypasses the sequencer and is still waiting. FREP
 * configurations take one of FREP_CONFIG_SIZE slots instead.
 *
 * Every cycle, the subsystem issues at most one instruction to the FPU, in
 * order, once its FP operands and destination are free in its scoreboard
 * (no forwarding: a result is readable RESULT_LATENCY cycles after the FPU
 * latency), its SSR data is there, or its FP LSU slot is free. FREP loops
 * re-issue their body from the buffer, outer (frep.o) or inner (frep.i), with
 * the staggered register offsets. The instruction executes functionally at
 * issue; the result timing is carried by the scoreboard.
 */
class SnitchFpSs
{
public:
    SnitchFpSs(Iss &iss);

    void start();
    void reset(bool active);

    // Stub handler of all the offloaded instructions
    template<bool fast>
    static iss_reg_t offload_stub(Iss *iss, iss_insn_t *insn, iss_reg_t pc);
    // Stub handler of the integer loads, stores and atomics, which wait on the
    // consistency address queue
    template<bool fast>
    static iss_reg_t caq_stub(Iss *iss, iss_insn_t *insn, iss_reg_t pc);

    // True when no instruction is queued or in flight
    inline bool is_idle() { return this->queue.empty() && this->nb_inflight == 0; }
    // True when every offloaded instruction has been issued
    inline bool is_drained() { return this->queue.empty(); }

    // Address of the FP load or store being executed, captured at offload
    inline iss_reg_t lsu_addr() { return this->current_addr; }
    // Called by the FP load and store handlers
    void load(iss_insn_t *insn, iss_addr_t addr, int size, int reg);
    void store(iss_insn_t *insn, iss_addr_t addr, int size, int reg);

    // Cycle at which the result of the instruction being executed is
    // available, used for the SSR writes
    int64_t result_cycle;
    // Identifies the instruction being executed, for the SSR reads
    int64_t issue_id;

private:
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    static void lsu_retry(vp::Block *__this, vp::IoRetryChannel);
    static vp::IoRespAck lsu_response(vp::Block *__this, vp::IoReq *req);

    bool can_offload(SnitchFpSsKind kind);
    // Try to issue the next instruction, returns true if one was issued
    bool issue();
    // Issue the instruction of the entry at queue index, with stagger offset
    bool issue_entry(int index, int offset, bool last);
    iss_insn_t *get_stagger_insn(SnitchFpSsEntry &entry, iss_insn_t *insn, int offset);
    bool check_operands(iss_insn_t *insn, SnitchFpSsEntry &entry);
    void complete_at(InsnEntry *entry, int64_t cycle);
    void pop_entry();
    void lsu_send(SnitchFpLsuReq *req);
    void lsu_end(SnitchFpLsuReq *req);
    void loop_start(SnitchFpSsEntry &entry);
    void check_idle();

    Iss &iss;
    vp::Trace trace;
    vp::ClockEvent fsm_event;

    std::deque<SnitchFpSsEntry> queue;
    // Instructions (FREP excluded) and FREP configurations in the queue
    int nb_insns;
    int nb_freps;
    // A bypass instruction is in the queue (it holds the offload cut)
    bool pending_bypass;
    // Held instructions not terminated yet, queued or in flight
    int nb_inflight;

    // Cycle at which each FP register can be read or written again
    int64_t fp_ready[32];
    // Cycle at which the FPU division / square root is free again
    int64_t divsqrt_ready;

    // FREP loop being executed. Its body is at the head of the queue.
    bool loop_active;
    bool loop_inner;
    int loop_nb_insns;
    uint64_t loop_nb_rpt;
    int loop_stagger_max;
    int loop_stagger_mask;
    int loop_insn;
    uint64_t loop_rpt;

    // Instruction completions, (cycle, held instruction)
    std::vector<std::pair<int64_t, InsnEntry *>> completions;

    // FP LSU
    vp::IoMaster lsu_itf{&SnitchFpSs::lsu_retry, &SnitchFpSs::lsu_response};
    SnitchFpLsuReq lsu_reqs[CONFIG_GVSOC_ISS_SNITCH_FP_LSU_NB_OUTSTANDING];
    SnitchFpLsuReq *lsu_free;
    SnitchFpLsuReq *lsu_denied;
    // Synchronously answered accesses waiting for their latency
    std::vector<SnitchFpLsuReq *> lsu_sync_reqs;

    // Consistency address queue: the 64-bit words accessed by the FP loads
    // and stores offloaded and not done yet. An integer access to one of them
    // waits. One entry per offloaded instruction, removed when its last
    // execution is done.
    std::vector<iss_addr_t> caq;
    void caq_remove(iss_addr_t addr);
    iss_reg_t current_addr;
    InsnEntry *current_entry;
    bool current_caq_release;

    vp::Signal<iss_reg_t> pc_signal;
    vp::Signal<bool> loop_signal;
    vp::Signal<int> queue_signal;
};

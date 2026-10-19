// SPDX-FileCopyrightText: 2026 ETH Zurich and University of Bologna
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <vector>
#include <vp/vp.hpp>
#include <cpu/iss_v2/include/types.hpp>
#include <cpu/iss_v2/include/prefetch/prefetch_single_line.hpp>

// CVA6 frontend timing: fetch bandwidth, branch prediction and the instruction queue, on top
// of the single-line prefetcher which still provides the opcodes and the fetch-port latency.
//
// The RTL frontend (core/frontend/frontend.sv) fetches one 32-bit word per cycle and predicts
// the control flow of that word in the same cycle, from a BHT (bht.sv), a BTB (btb.sv) and a
// RAS (ras.sv). A predicted taken control flow kills the fetch already in flight, which costs
// one bubble; a mispredict is resolved by the branch unit in execute and refetches from
// there. Fetched instructions wait in the instruction queue (instr_queue.sv), so fetch runs
// ahead of a stalled back end and hides its bubbles while the queue has room: it holds 8
// instructions, of which at most 2 predicted control flows (the address FIFO).
//
// The model keeps a timeline of fetched words: fetch(pc) computes when the word(s) of the
// instruction at pc are fetched and holds the instruction until then, retire() is told about
// every dispatched instruction, checks its outcome against what the predictors say and
// decides where and when fetch continues. The predictors are looked up and updated at
// dispatch, which is when the RTL resolves the branch.
class Cva6Frontend : public PrefetchSingleLine
{
public:
    Cva6Frontend(Iss &iss);

    void reset(bool active);

    // Same contract as PrefetchSingleLine::fetch: false holds the instruction at pc this cycle
    bool fetch(iss_reg_t pc);

    // Called for every dispatched instruction, once its handler has run (the next pc is in
    // exec.current_insn)
    void retire(iss_insn_t *insn);

private:
    struct BhtEntry
    {
        bool valid;
        uint8_t counter;
    };

    struct BtbEntry
    {
        bool valid;
        iss_reg_t target;
    };

    struct RasEntry
    {
        bool valid;
        iss_reg_t ra;
    };

    enum CfType
    {
        CF_NONE,
        CF_BRANCH,
        CF_JUMP,
        CF_JALR,
        CF_RETURN,
    };

    // Control-flow class of an opcode, as instr_scan.sv sees it. is_call tells if the return
    // address goes to the RAS.
    static CfType classify(iss_opcode_t opcode, int size, bool &is_call);

    // Immediate of a branch or jump, as instr_scan.sv extracts it
    static int64_t cf_imm(iss_opcode_t opcode, int size);

    // Computes when the instruction at pc is fetched and what is predicted after it
    void fetch_compute(iss_reg_t pc);

    // Earliest cycle the word holding the next instruction can be fetched, given the
    // instruction queue occupancy
    int64_t queue_bound(bool is_cf);

    void ras_push(iss_reg_t ra);
    RasEntry ras_pop();

    inline int bht_index(iss_reg_t pc) { return (pc >> 1) & (this->bht.size() - 1); }
    inline int btb_index(iss_reg_t pc) { return (pc >> 1) & (this->btb.size() - 1); }

    Iss &iss;
    vp::Trace trace;

    std::vector<BhtEntry> bht;
    std::vector<BtbEntry> btb;
    std::vector<RasEntry> ras;

    // Address of the last fetched word and the cycle it was fetched
    iss_reg_t word_addr;
    int64_t word_cycle;
    // Fetch restarts at a new address (taken prediction, mispredict, trap): the first word of
    // the new stream is fetched at redirect_cycle
    bool redirect;
    int64_t redirect_cycle;
    // Address the frontend fetches next. Any other pc is a redirect it was not told about
    // (trap, xret, flush), fetched from the cycle it is asked for.
    iss_reg_t expected_pc;

    // Instruction whose fetch time and prediction have been computed and which is waiting
    // for dispatch
    bool pending;
    iss_reg_t pending_pc;
    int pending_size;
    int64_t pending_cycle;
    CfType pending_cf;
    // The prediction made at fetch: bp_valid is a predicted taken control flow, which costs a
    // bubble and takes an address FIFO entry
    bool pending_bp_valid;
    iss_reg_t pending_target;

    // Dispatch cycles of the last instructions and of the last predicted control flows, to
    // bound how far fetch runs ahead
    std::vector<int64_t> dispatch_cycles;
    uint64_t nb_dispatched;
    std::vector<int64_t> cf_dispatch_cycles;
    uint64_t nb_cf_dispatched;

    // Configuration, see Cva6Config
    int queue_depth;
    int queue_cf_depth;
    int queue_restart;
    int mispredict_penalty;

    // Statistics
    uint64_t nb_branches;
    uint64_t nb_mispredicts;
};

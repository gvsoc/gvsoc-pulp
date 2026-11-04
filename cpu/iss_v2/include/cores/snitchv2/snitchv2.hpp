// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <cpu/iss_v2/include/types.hpp>
#include <cpu/iss_v2/include/arch.hpp>
#include <cpu/iss_v2/include/insn.hpp>
#include <cpu/iss_v2/include/csr.hpp>
#include <cpu/iss_v2/include/cores/snitchv2/fp_ss.hpp>
#include <cpu/iss_v2/include/cores/snitchv2/ssr.hpp>

class Iss;

// Entries of each COPIFT queue (RTL snitch I2F / F2I cc_stream_fifo Depth)
#define SNITCH_COPIFT_QUEUE_DEPTH 16

// Value in a COPIFT queue and the cycle from which it can be read
struct SnitchCopiftValue
{
    iss_reg_t value;
    int64_t ready;
};

/**
 * @brief Snitch core (RTL snitch_cc) on iss_v2.
 *
 * The integer core is the generic in-order iss_v2 pipeline. Every FP
 * instruction (FP loads and stores included) and FREP is offloaded to the FP
 * subsystem (SnitchFpSs), which executes them later, decoupled from the
 * integer core, with the stream semantic registers (SnitchSsr).
 *
 * The core also has the fmode CSR (0x800, alternate FP16 / FP8 formats) and
 * the barrier CSR (0x7C2): reading it notifies the cluster barrier unit
 * through the barrier_req port and stalls the core until the unit answers on
 * barrier_ack, once every core of the cluster has arrived.
 *
 * The sc CSR (0x7C3) holds the FP registers with scalar chaining, executed by
 * the FP subsystem; the dump CSR (0x7C7) prints the values written to it,
 * like the RTL core does in simulation.
 *
 * The copift CSR (0x7C6) enables the COPIFT queues. x31 is then the end of
 * the I2F (integer to FP) and F2I (FP to integer) queues for the integer
 * instructions: a write pushes to I2F (for a load, when its data comes back),
 * a read pops from F2I, waiting while it is full or empty, and the register
 * keeps its value. In the FP subsystem, the integer operand of an instruction
 * is popped from I2F when it is issued, and its integer result is pushed to
 * F2I, whatever the register, the core not waiting for it.
 */
class SnitchV2 : public IssArch
{
public:
    SnitchV2(Iss &iss);

    void start();
    void stop() {}
    void reset(bool active);

    // Called by the decoder on every decoded instruction (IssArch hook).
    // Rebuilds the scoreboard masks.
    void decode_hook(iss_insn_t *insn);

    // Called when the core stops fetching (IssArch hook, wfi, and the barrier
    // CSR). The RTL core keeps showing the address of the next instruction on
    // its fetch port without a request, and its L0 still prefetches on it: it
    // is sent as a request of size 0, which asks for nothing.
    void idle_hook(iss_insn_t *insn);

    CsrReg fmode;

    // COPIFT queues, see the class description
    bool copift_enabled;
    std::deque<SnitchCopiftValue> i2f;
    std::deque<SnitchCopiftValue> f2i;
    // Executing an integer instruction using x31 with the queues on: its x31
    // reads return f2i_value (popped once it is done)
    bool copift_redirect;
    iss_reg_t f2i_value;
    // I2F entries taken by integer instructions writing x31 not done yet
    // (loads)
    int i2f_reserved;
    // The FP subsystem executes an instruction, its integer register accesses
    // are the real ones
    bool in_fp_ss;
    // Called by the register file on an integer access of x31 with the
    // queues on, see the class description
    inline void copift_push(iss_reg_t value);

    SnitchFpSs fp_ss;
    SnitchSsr ssr;

private:
    bool sc_update(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool copift_update(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool dump_update(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    bool barrier_update(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    static void barrier_sync(vp::Block *__this, bool value);
    static void barrier_release_handler(vp::Block *__this, vp::ClockEvent *event);
    template<bool fast>
    static iss_reg_t csr_stub(Iss *iss, iss_insn_t *insn, iss_reg_t pc);
    // Stub of the integer instructions accessing x31, for the COPIFT queues.
    // The handler of the instruction is in saved_handler.
    static iss_reg_t copift_stub(Iss *iss, iss_insn_t *insn, iss_reg_t pc);

    Iss &iss;

    // Size-0 request showing the next PC on the fetch port while idle
    vp::IoReq fetch_hint_req;

    CsrReg sc;
    CsrReg copift;
    CsrReg dump;

    CsrReg barrier;
    vp::WireMaster<bool> barrier_req_itf;
    vp::WireSlave<bool> barrier_ack_itf;
    // True between the barrier CSR read and the barrier_ack
    bool barrier_waiting;
    // True while the core is retained (stalled) on the barrier
    bool barrier_stalled;
    // Releases the core the cycle after the barrier ack, the release of the
    // RTL barrier being registered
    vp::ClockEvent barrier_release_event;
    vp::Trace barrier_trace;
};

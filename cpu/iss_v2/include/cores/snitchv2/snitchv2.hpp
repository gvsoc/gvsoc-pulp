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

    CsrReg fmode;

    SnitchFpSs fp_ss;
    SnitchSsr ssr;

private:
    bool barrier_update(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    static void barrier_sync(vp::Block *__this, bool value);
    template<bool fast>
    static iss_reg_t csr_stub(Iss *iss, iss_insn_t *insn, iss_reg_t pc);

    Iss &iss;

    CsrReg barrier;
    vp::WireMaster<bool> barrier_req_itf;
    vp::WireSlave<bool> barrier_ack_itf;
    // True between the barrier CSR read and the barrier_ack
    bool barrier_waiting;
    // True while the core is retained (stalled) on the barrier
    bool barrier_stalled;
    vp::Trace barrier_trace;
};

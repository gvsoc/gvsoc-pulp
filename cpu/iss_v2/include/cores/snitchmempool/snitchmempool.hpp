// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou

#pragma once

#include <cpu/iss_v2/include/types.hpp>
#include <cpu/iss_v2/include/insn.hpp>
#include <cpu/iss_v2/include/csr.hpp>
#if defined(CONFIG_GVSOC_ISS_USE_SPATZ)
#include <cpu/iss_v2/include/vector.hpp>
#include <cpu/iss_v2/include/cores/vector_unit/vector_unit.hpp>
#endif

class Iss;

// Snitch arch for Mempool-style clusters: Snitch stack CSRs, a memory-mapped
// barrier (wake on the barrier_ack port) and a wake-up counter for the
// barrier_sync/WFI race. With CONFIG_GVSOC_ISS_USE_SPATZ it also carries the
// Spatz vector unit.
class SnitchMempool
{
public:
    SnitchMempool(Iss &iss);

    void start() {}
    void stop() {}
    void reset(bool active);

#if defined(CONFIG_GVSOC_ISS_USE_SPATZ)
    // Vector unit, identical to Spatz (vector config only).
    Vu vu;
#endif

    // Consume a wake-up credit (from IrqMempool::wfi_handle); true if available.
    inline bool wakeup_check();

private:
    static void barrier_sync(vp::Block *__this, bool value);

    Iss &iss;

    vp::reg_8 wakeup;

    // Snitch stack CSRs 0x7d0-0x7d2 (plain storage; see the .cpp).
    CsrReg stack_conf;
    CsrReg stack_start;
    CsrReg stack_end;
    vp::WireSlave<bool> barrier_ack_itf;

    vp::Trace trace;
};

inline bool SnitchMempool::wakeup_check()
{
    if (this->wakeup.get())
    {
        this->wakeup.dec(1);
        return true;
    }
    return false;
}

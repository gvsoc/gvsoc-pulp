// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <cpu/iss_v2/include/types.hpp>
#include <cpu/iss_v2/include/insn.hpp>
#include <cpu/iss_v2/include/csr.hpp>
#include <cpu/iss_v2/include/vector.hpp>
#include <cpu/iss_v2/include/cores/vector_unit/vector_unit.hpp>

class Iss;

// Spatz arch class for Mempool-style clusters: same vector unit as the
// plain Spatz, plus a Snitch-style barrier CSR (0x7C2) and a wake-up
// counter to absorb the barrier_sync / WFI race.
//
// The race: when the last core to reach the barrier notifies the central
// barrier, the central barrier may broadcast barrier_sync back into the
// same call stack — reaching the other cores synchronously, before the
// notifying core has issued its own WFI. Without the counter the wake-up
// would be lost. With it, every barrier_sync that arrives outside a WFI
// banks one credit, and the next WFI walks straight through instead of
// sleeping (see IrqMempool::wfi_handle).
class SpatzMempool
{
public:
    SpatzMempool(Iss &iss);

    void start() {}
    void stop() {}
    void reset(bool active);

    // Vector unit, identical to Spatz.
    Vu vu;

    // Wake-up credit consumer, called from IrqMempool::wfi_handle. Returns
    // true if a credit was available — the caller then skips sleep_enter.
    inline bool wakeup_check();

private:
    bool barrier_update(iss_insn_t *insn, bool is_write, iss_reg_t &value);
    static void barrier_sync(vp::Block *__this, bool value);

    Iss &iss;

    vp::reg_8 wakeup;

    CsrReg barrier;
    vp::WireMaster<bool> barrier_req_itf;
    vp::WireSlave<bool> barrier_ack_itf;

    vp::Trace trace;
};

inline bool SpatzMempool::wakeup_check()
{
    if (this->wakeup.get())
    {
        this->wakeup.dec(1);
        return true;
    }
    return false;
}

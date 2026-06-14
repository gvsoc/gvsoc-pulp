// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <vp/vp.hpp>

class Iss;

// ri5ky LSU: the io_v2 LSU extended with the p.elw event load.
class Ri5kyLsu : public LsuV2
{
public:
    Ri5kyLsu(Iss &iss) : LsuV2(iss) {}

    void reset(bool active);

    // p.elw event load: issued like a regular load, but when the event
    // unit parks the request (event not ready), the whole core is put to
    // sleep (clock-gated in HW). Woken either by the event-unit response
    // (req_retire_hook) or by an interrupt (elw_irq_unstall), which
    // abandons the parked request and replays the instruction after the
    // handler.
    bool elw(iss_insn_t *insn, iss_addr_t addr, int size, int reg);
    void elw_irq_unstall();

    // Base-LSU extension hooks (statically dispatched through the
    // configured LSU type).
    inline void req_retire_hook(LsuReqEntry *entry)
    {
        if (unlikely(entry == this->elw_entry))
        {
            this->elw_wake();
        }
    }

    inline void irq_req_hook(int irq, bool irq_enabled)
    {
        if (unlikely(irq != -1 && this->elw_entry != NULL && irq_enabled))
        {
            this->elw_irq_unstall();
        }
    }

private:
    // The parked event load completed normally: wake the core.
    void elw_wake();

    // Parked event-load request (NULL if none). Per the event-unit
    // contract, once the EU raises an IRQ to a sleeping core it never
    // answers the parked wait (the replayed elw re-parks with a fresh
    // request), so the entry is freed at unstall time.
    LsuReqEntry *elw_entry;
    // PC of the in-flight p.elw, restored as the current instruction at
    // unstall time so the elw is replayed after the interrupt handler.
    iss_reg_t elw_insn;
    // Cycle at which the core was clock-gated by the parked p.elw. The
    // gated span (wake - park) is the "wasted" wait that RTL charges to
    // PCCR[11] (CSR_PCER_ELW) via perf_pipeline_stall_o.
    int64_t elw_park_cyclestamp;
};

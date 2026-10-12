// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include <vp/vp.hpp>

class Iss;

/* io_v2 LSU reporting the accesses of the instruction in flight to the
 * co-simulation model. The ISA handlers reach these shadows as iss->lsu.*.
 * A misaligned access is reported once, as the instruction issued it. */
class Cv32e40pLsu : public LsuV2
{
public:
    Cv32e40pLsu(Iss &iss) : LsuV2(iss), iss(iss) {}

    template<typename T>
    inline bool load(iss_insn_t *insn, iss_addr_t addr, int size, int reg);
    template<typename T>
    inline bool load_perf(iss_insn_t *insn, iss_addr_t addr, int size, int reg);
    template<typename T>
    inline bool load_signed(iss_insn_t *insn, iss_addr_t addr, int size, int reg);
    template<typename T>
    inline bool load_signed_perf(iss_insn_t *insn, iss_addr_t addr, int size, int reg);
    template<typename T>
    inline bool load_float(iss_insn_t *insn, iss_addr_t addr, int size, int reg);
    template<typename T>
    inline bool load_float_perf(iss_insn_t *insn, iss_addr_t addr, int size, int reg);
    template<typename T>
    inline bool store(iss_insn_t *insn, iss_addr_t addr, int size, int reg);
    template<typename T>
    inline bool store_perf(iss_insn_t *insn, iss_addr_t addr, int size, int reg);
    template<typename T>
    inline bool store_float(iss_insn_t *insn, iss_addr_t addr, int size, int reg);
    template<typename T>
    inline bool store_float_perf(iss_insn_t *insn, iss_addr_t addr, int size, int reg);

    // Shadows LsuV2::reset.
    void reset(bool active);

    /* cv.elw (COREV_CLUSTER): issued like a regular load, but when the event
     * unit parks the request (event not ready), the core sleeps, as the RTL
     * gates its clock in ELW_EXE. It is woken either by the event-unit
     * response (req_retire_hook) or by an interrupt (irq_req_hook), which
     * abandons the parked request and replays the cv.elw after the handler
     * (IRQ_FLUSH_ELW in cv32e40p_controller.sv). */
    bool elw(iss_insn_t *insn, iss_addr_t addr, int size, int reg);

    // Base-LSU extension hooks (statically dispatched through the LSU type).
    inline void req_retire_hook(LsuReqEntry *entry)
    {
        if (unlikely(entry == this->elw_entry))
        {
            this->elw_wake();
        }
    }
    // Called by Cv32e40pIrq when an interrupt line changes, with
    // irq_enabled true if a pending line would be taken.
    inline void irq_req_hook(int irq, bool irq_enabled)
    {
        if (unlikely(irq != -1 && this->elw_entry != NULL && irq_enabled))
        {
            this->elw_irq_unstall();
        }
    }

private:
    inline void cosim_access(bool is_store, iss_addr_t addr, int size, uint64_t data);
    inline void cosim_load(iss_addr_t addr, int size, int reg, bool is_signed);

    // The parked cv.elw completed normally: wake the core.
    void elw_wake();
    // An interrupt woke the core: drop the parked cv.elw and replay it.
    void elw_irq_unstall();

    Iss &iss;

    /* Parked cv.elw request (NULL if none). Once the event unit raises an
     * interrupt to a sleeping core it never answers the parked wait (the
     * replayed cv.elw parks again with a fresh request), so the entry is
     * freed at unstall time. */
    LsuReqEntry *elw_entry = NULL;
    // PC of the parked cv.elw, where execution resumes after an interrupt.
    iss_reg_t elw_insn = 0;
    // Cycle at which the core went to sleep on the parked cv.elw.
    int64_t elw_park_cyclestamp = 0;
};

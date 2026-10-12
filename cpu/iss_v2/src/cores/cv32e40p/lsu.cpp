// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Lorenzo Zuolo (lorenzo.zuolo@chips.it)

/* cv.elw of the CV32E40P built for a PULP cluster (COREV_CLUSTER). The
 * mechanism is the one of the ri5ky p.elw (cores/ri5ky/lsu.cpp): the event
 * load parks in the event unit until the event comes, and the core sleeps
 * meanwhile. */

#include <cpu/iss_v2/include/iss.hpp>
#include <cpu/iss_v2/include/cores/cv32e40p/lsu_implem.hpp>

void Cv32e40pLsu::reset(bool active)
{
    LsuV2::reset(active);

    if (active)
    {
        this->elw_entry = NULL;
        this->elw_insn = 0;
    }
}

bool Cv32e40pLsu::elw(iss_insn_t *insn, iss_addr_t addr, int size, int reg)
{
    // Issued like a regular aligned load, the event-unit registers are
    // word-aligned. As for the other loads, a stalled access is reported to
    // the co-simulation model again when it is retried.
    bool stalled = this->data_req_aligned(insn, addr, size, vp::IoReqOpcode::READ, false,
        reg, 0);
    this->cosim_load(addr, size, reg, true);
    if (stalled)
    {
        return true;
    }

    /* The event unit parked the request (event not ready): the core sleeps
     * (ELW_EXE, core_sleep_o) until the response or an interrupt. A
     * synchronous completion (event already there) is a regular load. */
    if (this->granted_entry != NULL)
    {
        this->elw_entry = this->granted_entry;
        this->elw_insn = insn->addr;
        this->elw_park_cyclestamp = this->iss.clock.get_cycles();
        this->iss.exec.busy_exit();
        this->iss.exec.retain_inc();
    }

    return false;
}

void Cv32e40pLsu::elw_wake()
{
    /* The RTL stays in ELW_EXE for the wait, then goes through
     * IRQ_FLUSH_ELW and refetches before the next instruction: the cv.elw
     * costs at least 3 cycles, and the part beyond the sleep is a bubble. */
    int64_t park = this->iss.clock.get_cycles() - this->elw_park_cyclestamp;
    int64_t counter = park + 1;
    if (counter < 3) counter = 3;
    this->iss.exec.stall_cycles_inc((int)(counter - park));

    this->elw_entry = NULL;
    this->elw_insn = 0;
    this->iss.exec.retain_dec();
    this->iss.exec.busy_enter();
}

void Cv32e40pLsu::elw_irq_unstall()
{
    /* Execution is redirected to the cv.elw, so that the interrupt taken
     * next saves its PC in mepc and the cv.elw is replayed after the
     * handler. The event unit never answers the parked request, which is
     * freed here. */
    LsuReqEntry *entry = this->elw_entry;
    this->elw_entry = NULL;

    this->iss.exec.current_insn = this->elw_insn;
    this->elw_insn = 0;

#ifdef CONFIG_GVSOC_ISS_REGFILE_SCOREBOARD
    // Release the destination register like a completing load, so that the
    // replay does not wait on its own scoreboard bit.
    iss_insn_t *insn = this->iss.exec.get_insn(entry->insn_entry);
    this->iss.exec.schedule_scoreboard_release(insn->sb_out_reg_mask);
    this->iss.exec.insn_terminate(entry->insn_entry, /*defer_scoreboard_release=*/true);
#else
    this->iss.exec.insn_terminate(entry->insn_entry);
#endif
    entry->misaligned_byte_offset = 0;
    this->free_req_entry(entry);

    this->iss.exec.retain_dec();
    this->iss.exec.busy_enter();
}

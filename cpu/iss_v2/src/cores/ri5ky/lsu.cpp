// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include <cpu/iss_v2/include/cores/ri5ky/lsu.hpp>

void Ri5kyLsu::reset(bool active)
{
    LsuV2::reset(active);

    if (active)
    {
        this->elw_entry = NULL;
        this->elw_insn = 0;
    }
}

bool Ri5kyLsu::elw(iss_insn_t *insn, iss_addr_t addr, int size, int reg)
{
    // Issue like a regular aligned load (p.elw addresses are word-aligned
    // event-unit registers).
    if (this->data_req_aligned(insn, addr, size, vp::IoReqOpcode::READ,
        false, reg, 0))
    {
        return true;
    }

    // If the event unit parked the request (event not ready), clock-gate
    // the whole core, as p.elw does in hardware. The core is woken either
    // by the event-unit response (req_retire_hook -> elw_wake) or by an
    // interrupt (elw_irq_unstall). A synchronous completion (event already
    // pending) behaves as a regular timed load. Note: a DENIED first issue
    // (not possible on the direct demux path to the event unit) would
    // complete through the retry path without engaging the sleep.
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

void Ri5kyLsu::elw_wake()
{
    // The core was clock-gated `park` cycles waiting for the event response.
    // RTL stays in ELW_EXE for that wait and then refills the pipeline (IF
    // refetch) before the next instruction issues; perf_pipeline_stall_o is
    // high for the whole span, feeding PCCR[11]. Reproduce both the wasted
    // cycles charged to CSR_PCER_ELW and the refill bubble:
    //   counter = max(park + 1, 3)   (RTL: max(L+1, 3))
    //   the elw's own issue cycle is already counted, so the extra timing to
    //   inject beyond the gated span is `counter - park`.
    int64_t park = this->iss.clock.get_cycles() - this->elw_park_cyclestamp;
    int64_t counter = park + 1;
    if (counter < 3) counter = 3;
    this->iss.exec.stall_cycles_inc((int)(counter - park));
    this->iss.timing.event_elw_account((int)counter, (int)park);

    this->elw_entry = NULL;
    this->elw_insn = 0;
    this->iss.exec.retain_dec();
    this->iss.exec.busy_enter();
}

void Ri5kyLsu::elw_irq_unstall()
{
    // An interrupt wakes the sleeping core: abandon the parked request
    // and redirect execution to the elw so it is replayed after the
    // handler (mepc is the elw pc). Per the event-unit contract the EU
    // never answers a parked wait once it has raised an IRQ to the core
    // (the replayed access re-parks with a fresh request), so the pool
    // entry is freed here.
    LsuReqEntry *entry = this->elw_entry;
    this->elw_entry = NULL;

    this->iss.exec.current_insn = this->elw_insn;
    this->elw_insn = 0;

#ifdef CONFIG_GVSOC_ISS_REGFILE_SCOREBOARD
    // Release the elw destination register like a completing load so the
    // replay does not deadlock on its own scoreboard bits.
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

// SPDX-FileCopyrightText: 2026 Fondazione Chips-it
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include "cpu/iss_v2/include/cores/cv32e40p/csr.hpp"
#include <vp/vp.hpp>
#include <cpu/iss_v2/include/cores/cv32e40p/events.hpp>
#include <cpu/iss_v2/include/event/event_implem.hpp>

inline void Cv32e40pEvents::event_load_account(int incr)
{
    Events::event_load_account(incr);
    this->pending_events |= CV32E40P_HPM_LD;
}

inline void Cv32e40pEvents::event_store_account(int incr)
{
    Events::event_store_account(incr);
    this->pending_events |= CV32E40P_HPM_ST;
}

inline void Cv32e40pEvents::event_branch_account()
{
    Events::event_branch_account();
    this->pending_events |= CV32E40P_HPM_BRANCH;
}

inline void Cv32e40pEvents::event_taken_branch_account()
{
    /* A taken branch fires both RTL event lines (base cascades to
     * event_branch_account, the explicit OR keeps that non-load-bearing). */
    Events::event_taken_branch_account();
    this->pending_events |= CV32E40P_HPM_BRANCH | CV32E40P_HPM_BRANCH_TAKEN;
}

inline void Cv32e40pEvents::event_jump_account()
{
    Events::event_jump_account();
    this->pending_events |= CV32E40P_HPM_JUMP;
}

inline void Cv32e40pEvents::event_retire_account(iss_insn_t *insn)
{
    Events::event_retire_account(insn);
    /* Encoding for the RVVI INS compare. The fetched word carries the NEXT
     * parcel in its upper half on RVC rows, so truncate to the insn size
     * (the bridge masks the DUT side the same way). */
    iss_reg_t enc = (insn->size == 2) ? (insn->opcode & 0xFFFF) : insn->opcode;
#ifdef CONFIG_GVSOC_ISS_EXEC_INORDER_COMMIT
    if (this->iss.exec.queue_head != NULL)
    {
        /* Parked in the commit FIFO (held, or sync follower behind a held
         * head): visible at drain time, through insn_stall_account, in
         * this same program order. */
        this->inflight_pc[this->inflight_push % COMMIT_RING] = insn->addr;
        this->inflight_insn[this->inflight_push % COMMIT_RING] = enc;
        this->inflight_trap_seq[this->inflight_push % COMMIT_RING] = this->trap_seq;
        this->inflight_trapped[this->inflight_push % COMMIT_RING] =
            this->iss.exec.has_exception;
        this->inflight_push++;
    }
    else
#endif
    {
        this->commit_pc[this->commit_push % COMMIT_RING] = insn->addr;
        this->commit_insn[this->commit_push % COMMIT_RING] = enc;
        this->commit_trap_seq[this->commit_push % COMMIT_RING] = this->trap_seq;
        this->commit_trapped[this->commit_push % COMMIT_RING] =
            this->iss.exec.has_exception;
        this->commit_push++;
    }
    /* A trapping instruction does not retire: drop its event lines. */
    if (this->iss.exec.has_exception)
    {
        this->pending_events = 0;
        return;
    }
    /* RTL minstret event (cv32e40p_id_stage.sv:1639) excludes EBREAK
     * unconditionally; the compressed-retired event shares the gate
     * (:1664, minstret && is_compressed). The trapping ebreak forms were
     * dropped above with has_exception - this covers the debug-entry
     * ebreak (dcsr.ebreakm=1), which retires without an architectural
     * trap yet must not count. */
    bool count_instr = !(enc == 0x00100073u
                         || (insn->size == 2 && enc == 0x9002u));
    uint32_t events = this->pending_events
        | (count_instr ? (CV32E40P_HPM_INSTR
            | (insn->size == 2 ? CV32E40P_HPM_COMP_INSTR : 0)) : 0);
    this->pending_events = 0;
    this->iss.csr.hpm_commit(events, count_instr);
}

inline void Cv32e40pEvents::insn_stall_account()
{
    /* Fires only from ExecInOrder::drain_entry: one commit-FIFO entry
     * retired, writeback done. The guard covers entries flushed by
     * commit_stream_flush while their drain was still pending. */
    if (this->inflight_pop < this->inflight_push)
    {
        this->commit_pc[this->commit_push % COMMIT_RING] =
            this->inflight_pc[this->inflight_pop % COMMIT_RING];
        this->commit_insn[this->commit_push % COMMIT_RING] =
            this->inflight_insn[this->inflight_pop % COMMIT_RING];
        this->commit_trap_seq[this->commit_push % COMMIT_RING] =
            this->inflight_trap_seq[this->inflight_pop % COMMIT_RING];
        this->commit_trapped[this->commit_push % COMMIT_RING] =
            this->inflight_trapped[this->inflight_pop % COMMIT_RING];
        this->inflight_pop++;
        this->commit_push++;
    }
}

inline void Cv32e40pEvents::commit_stream_flush()
{
    this->commit_pop = this->commit_push;
    this->inflight_pop = this->inflight_push;
}

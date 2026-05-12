// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include "cpu/iss_v2/include/cores/ri5ky/csr.hpp"
#include <vp/vp.hpp>
#include <cpu/iss_v2/include/cores/ri5ky/events.hpp>
#include <cpu/iss_v2/include/event/event_implem.hpp>

inline void Ri5kyEvents::event_load_account(int incr)
{
    Events::event_load_account(incr);
    this->iss.csr.pccr_account(CSR_PCER_LD, incr);
}

inline void Ri5kyEvents::event_rvc_account(int incr)
{
    Events::event_rvc_account(incr);
    this->iss.csr.pccr_account(CSR_PCER_RVC, incr);
}

inline void Ri5kyEvents::event_store_account(int incr)
{
    Events::event_store_account(incr);
    this->iss.csr.pccr_account(CSR_PCER_ST, incr);
}

inline void Ri5kyEvents::event_branch_account()
{
    Events::event_branch_account();
    this->iss.csr.pccr_account(CSR_PCER_BRANCH, 1);
}

inline void Ri5kyEvents::event_load_load_account(int incr)
{
    Events::event_load_load_account(incr);
    this->iss.csr.pccr_account(CSR_PCER_LD_STALL, 1);
}

inline void Ri5kyEvents::event_instr_account()
{
    Events::event_instr_account();
    this->iss.csr.pccr_account(CSR_PCER_INSTR, 1);
}

inline void Ri5kyEvents::event_cycle_enable() {
    Events::event_cycle_enable();
    this->cycles_start_cyclestamp = this->iss.clock.get_cycles();

}

inline void Ri5kyEvents::event_cycle_disable() {
    Events::event_cycle_disable();
    this->iss.csr.pccr_account(CSR_PCER_CYCLES,
        this->iss.clock.get_cycles() - this->cycles_start_cyclestamp);
    this->cycles_start_cyclestamp = -1;
}

inline void Ri5kyEvents::event_imiss_account(int cycles)
{
    Events::event_imiss_account(cycles);
    this->iss.exec.stall_cycles_inc(cycles);
    this->iss.csr.pccr_account(CSR_PCER_IMISS, cycles);
}

inline void Ri5kyEvents::event_imiss_start()
{
    Events::event_imiss_start();
    this->imiss_start_cyclestamp = this->iss.clock.get_cycles();
}

inline void Ri5kyEvents::event_imiss_stop()
{
    Events::event_imiss_stop();
    this->iss.csr.pccr_account(CSR_PCER_IMISS,
        this->iss.clock.get_cycles() - this->imiss_start_cyclestamp);
}

inline void Ri5kyEvents::event_taken_branch_account()
{
    Events::event_taken_branch_account();
    this->iss.exec.stall_cycles_inc(2);
    this->iss.csr.pccr_account(CSR_PCER_TAKEN_BRANCH, 1);
}

inline void Ri5kyEvents::event_jump_account()
{
    Events::event_jump_account();
    this->iss.exec.stall_cycles_inc(1);
    this->iss.csr.pccr_account(CSR_PCER_JUMP, 1);
}

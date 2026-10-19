// SPDX-FileCopyrightText: 2026 ETH Zurich and University of Bologna
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <vp/vp.hpp>
#include <cpu/iss_v2/include/cores/cva6/events.hpp>
#include <cpu/iss_v2/include/event/event_implem.hpp>

inline void Cva6Events::event_taken_branch_account()
{
    Events::event_taken_branch_account();
    this->branch_taken = true;
}

inline void Cva6Events::event_retire_account(iss_insn_t *insn)
{
    this->iss.prefetch.retire(insn);
    this->branch_taken = false;
}

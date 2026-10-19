// SPDX-FileCopyrightText: 2026 ETH Zurich and University of Bologna
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <vp/vp.hpp>
#include <cpu/iss_v2/include/event/event.hpp>

// Forwards to the CVA6 frontend what it needs to resolve its predictions: every dispatched
// instruction, and the direction of conditional branches (a taken branch to the next
// instruction cannot be told from a not taken one by the next pc).
class Cva6Events : public Events
{
public:
    Cva6Events(Iss &iss) : Events(iss) {}

    inline void event_taken_branch_account();
    inline void event_retire_account(iss_insn_t *insn);

    // Set by a taken conditional branch, consumed when it retires
    bool branch_taken = false;
};

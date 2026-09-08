// SPDX-FileCopyrightText: 2026 Fondazione Chips-it
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include "cpu/iss_v2/include/cores/cv32e40p/exec.hpp"
#include "cpu/iss_v2/include/exec/exec_inorder.hpp"

inline bool Cv32e40pExec::can_switch_to_fast_mode()
{
    if (!ExecInOrder::can_switch_to_fast_mode()) return false;

    if (this->commit_stream_observed) return false;

    /* The event lines only fire from the full handlers: stay there while
     * any implemented counter is enabled (see Cv32e40pCsr::hpm_counting). */
    return !this->iss.csr.hpm_counting();
}

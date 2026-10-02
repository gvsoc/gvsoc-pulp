// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include "cpu/iss_v2/include/cores/cv32e40p/exec.hpp"
#include "cpu/iss_v2/include/exec/exec_inorder.hpp"
#include "cpu/iss_v2/include/cores/cv32e40p/cosim_model.hpp"

inline bool Cv32e40pExec::can_switch_to_fast_mode()
{
    if (!ExecInOrder::can_switch_to_fast_mode()) return false;

    // The co-simulation records are built by the full handlers.
    if (this->cosim->enabled()) return false;

    // The event lines fire from the full handlers only (Cv32e40pCsr::hpm_counting).
    return !this->iss.csr.hpm_counting();
}

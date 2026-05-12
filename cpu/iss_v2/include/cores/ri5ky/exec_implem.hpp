// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include "cpu/iss_v2/include/cores/ri5ky/exec.hpp"
#include "cpu/iss_v2/include/exec/exec_inorder.hpp"

inline bool Ri5kyExec::can_switch_to_fast_mode()
{
    if (!ExecInOrder::can_switch_to_fast_mode()) return false;

    return !this->iss.csr.counters_enabled();
}

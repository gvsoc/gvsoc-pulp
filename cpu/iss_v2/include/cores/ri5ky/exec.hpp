// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <vp/vp.hpp>

class Iss;

class Ri5kyExec : public ExecInOrder
{
public:
    Ri5kyExec(Iss &iss) : ExecInOrder(iss) {}

    inline bool can_switch_to_fast_mode();
};

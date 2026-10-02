// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include <vp/vp.hpp>
#include <cpu/iss_v2/include/core.hpp>

class Cv32e40pCore : public Core
{
public:
    Cv32e40pCore(Iss &iss);

    // Called from Iss::reset, after the generic register-file reset.
    void reset(bool active);

    // Shadows Core::mret_handle (CONFIG_GVSOC_ISS_CORE) to keep mcause across MRET.
    iss_reg_t mret_handle();

private:
    Iss &iss;
};

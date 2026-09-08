// SPDX-FileCopyrightText: 2026 Fondazione Chips-it
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
    Cv32e40pCore(Iss &iss) : Core(iss), iss(iss) {}

    /* MRET with the mcause hold-over of the RTL; shadows the generic
     * handler (static dispatch via CONFIG_GVSOC_ISS_CORE). */
    iss_reg_t mret_handle();

private:
    Iss &iss;
};

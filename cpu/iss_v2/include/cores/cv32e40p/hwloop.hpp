// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include <vp/vp.hpp>
#include <cpu/iss_v2/include/hwloop/hwloop.hpp>

class Cv32e40pHwloop : public Hwloop
{
public:
    Cv32e40pHwloop(Iss &iss) : Hwloop(iss), iss(iss) {}

    /* Shadows Hwloop::check (CONFIG_GVSOC_ISS_HWLOOP_OBJ). The RTL kills a
     * loop-end instruction that traps, or an ebreak that enters debug mode,
     * before the loop update, so the counter does not move. */
    inline iss_reg_t check(iss_reg_t pc, iss_reg_t next_pc);

private:
    Iss &iss;
};

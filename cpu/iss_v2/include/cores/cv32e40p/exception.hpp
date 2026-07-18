// SPDX-FileCopyrightText: 2026 Fondazione Chips-it
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include <vp/vp.hpp>
#include <cpu/iss_v2/include/exception.hpp>

class Cv32e40pException : public Exception
{
public:
    Cv32e40pException(Iss &iss) : Exception(iss), iss(iss) {}

    /* Exception entry at the mtvec base; shadows the generic raise
     * (static dispatch via CONFIG_GVSOC_ISS_EXCEPTION). */
    void raise(iss_reg_t pc, int id);

private:
    Iss &iss;
};

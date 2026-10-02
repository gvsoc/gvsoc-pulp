// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
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
    Cv32e40pException(Iss &iss);

    // Shadows Exception::raise (CONFIG_GVSOC_ISS_EXCEPTION).
    void raise(iss_reg_t pc, int id);

    iss_addr_t debug_exception_handler_addr;

private:
    Iss &iss;
};

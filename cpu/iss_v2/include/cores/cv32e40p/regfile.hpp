// SPDX-FileCopyrightText: 2026 Fondazione Chips-it
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include <vp/vp.hpp>
#include <cpu/iss_v2/include/regfile.hpp>

/* CV32E40P register-file personality.
 *
 * A trapped instruction writes no destination register on the RTL. The
 * reserved-rounding-mode raise (isa_lib int.h) fires inside the value
 * expression of the write-back macro, so the write that follows must be
 * dropped: the raise site arms wb_suppress and the next set_reg/set_freg
 * consumes it (one-shot — the instruction's own write always follows the
 * raise within the same handler, so unrelated writes are never dropped). */
class Cv32e40pRegfile : public Regfile
{
public:
    Cv32e40pRegfile(Iss &iss) : Regfile(iss) {}

    void reset(bool active)
    {
        this->wb_suppress = false;
        this->Regfile::reset(active);
    }

    inline void wb_suppress_arm() { this->wb_suppress = true; }

    /* Shadows the base setters (static dispatch via CONFIG_GVSOC_ISS_REGFILE). */
    inline void set_reg(int reg, uint64_t value)
    {
        if (this->wb_suppress)
        {
            this->wb_suppress = false;
            return;
        }
        this->Regfile::set_reg(reg, value);
    }

    inline void set_freg(int reg, uint64_t value)
    {
        if (this->wb_suppress)
        {
            this->wb_suppress = false;
            return;
        }
        this->Regfile::set_freg(reg, value);
    }

private:
    bool wb_suppress = false;
};

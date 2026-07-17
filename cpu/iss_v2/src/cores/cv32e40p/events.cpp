// SPDX-FileCopyrightText: 2026 Fondazione Chips-it
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#include <cpu/iss_v2/include/cores/cv32e40p/csr.hpp>

void Cv32e40pEvents::reset(bool active)
{
    Events::reset(active);
    if (active)
    {
        this->pending_events = 0;
    }
}

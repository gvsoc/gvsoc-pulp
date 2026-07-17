// SPDX-FileCopyrightText: 2026 Fondazione Chips-it
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include <vp/vp.hpp>

class Iss;

class Cv32e40pExec : public ExecInOrder
{
public:
    Cv32e40pExec(Iss &iss) : ExecInOrder(iss) {}

    inline bool can_switch_to_fast_mode();
};

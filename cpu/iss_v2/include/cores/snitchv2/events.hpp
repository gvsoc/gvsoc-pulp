// SPDX-FileCopyrightText: 2026 ETH Zurich and University of Bologna
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <vp/vp.hpp>
#include <cpu/iss_v2/include/event/event.hpp>

// Snitch core events: the count of retired instructions, read by the cluster
// performance counters (retired_instr metric, offloaded instructions included).
class SnitchV2Events : public Events
{
public:
    SnitchV2Events(Iss &iss) : Events(iss) {}

    void reset(bool active)
    {
        Events::reset(active);
        if (active)
        {
            this->retired_instr = 0;
        }
    }

    inline void event_retire_account(iss_insn_t *insn) { this->retired_instr++; }

    uint64_t retired_instr = 0;
};

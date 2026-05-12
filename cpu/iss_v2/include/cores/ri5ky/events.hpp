// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <vp/vp.hpp>
#include <cpu/iss_v2/include/event/event.hpp>

class Ri5kyEvents : public Events
{
public:
    Ri5kyEvents(Iss &iss) : Events(iss) {}

    void reset(bool active);

    inline void event_cycle_enable();
    inline void event_cycle_disable();
    inline void event_imiss_account(int cycles);
    inline void event_imiss_start();
    inline void event_imiss_stop();
    inline void event_taken_branch_account();
    inline void event_instr_account();
    inline void event_load_account(int incr);
    inline void event_rvc_account(int incr);
    inline void event_store_account(int incr);
    inline void event_branch_account();
    inline void event_jump_account();
    inline void event_load_load_account(int incr);

    void flush_cycles();

private:

    int64_t imiss_start_cyclestamp;
    int64_t cycles_start_cyclestamp;
};

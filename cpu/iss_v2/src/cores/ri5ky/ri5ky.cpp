// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include <cpu/iss_v2/include/iss.hpp>

Ri5ky::Ri5ky(Iss &iss)
: iss(iss)
{
}

void Ri5ky::start()
{
    // RI5CY's MULH / MULHU / MULHSU run a 4-step multiplier FSM
    // (riscv_mult.sv: IDLE -> STEP0 -> STEP1 -> STEP2 -> FINISH) behind
    // a structurally serialised issue port: mult_ready stays low for
    // the 4 STEPx cycles, which gates ex_ready_o (riscv_ex_stage.sv:
    // 598-601), which gates id_ready_o (riscv_id_stage.sv:1781). The
    // follower in ID is therefore stuck regardless of any data
    // dependency on the result.
    //
    // Encode that by attaching the 4 extra stall cycles to the decoder
    // items for the "mulh" tag (isa_riscv_gen.py:651-653 — the same
    // tag covers all three MULH variants). The generic iss_v2 exec
    // path delivers the value to Ri5kyEvents::event_insn_latency_account,
    // which turns it into an unconditional stall_cycles_inc().
    if (!__iss_isa_set.initialized)
    {
        __iss_isa_set.initialized = true;
        for (iss_decoder_item_t *item :
             *this->iss.decode.get_insns_from_tag("mulh"))
        {
            item->u.insn.latency = 4;
        }
    }
}

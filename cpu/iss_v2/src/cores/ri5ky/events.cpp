// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include <cpu/iss_v2/include/cores/ri5ky/csr.hpp>

void Ri5kyEvents::reset(bool active)
{
    Events::reset(active);
    if (active) {
        this->cycles_start_cyclestamp = -1;
        this->prev_dest_reg = -1;
    } else {
        // Anchor the cycle counter at reset deassert so PCCR[0] starts
        // counting from cycle 0 regardless of whether the core ever
        // enters/leaves WFI. Without this, `flush_cycles` (called on
        // PCCR/PCER/PCMR reads) would see -1 and silently skip the
        // accumulation, leaving PCCR[0] at 0 for continuously-running
        // workloads — which doesn't match the RTL behaviour.
        this->cycles_start_cyclestamp = this->iss.clock.get_cycles();
    }
}

void Ri5kyEvents::flush_cycles()
{
    if (this->cycles_start_cyclestamp != -1) {
        this->iss.csr.pccr_account(CSR_PCER_CYCLES,
            this->iss.clock.get_cycles() - this->cycles_start_cyclestamp);
        this->cycles_start_cyclestamp = this->iss.clock.get_cycles();
    }
}

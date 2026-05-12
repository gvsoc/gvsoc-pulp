// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <vp/vp.hpp>
#include <cpu/iss_v2/include/cores/ri5ky/csr.hpp>

inline void Ri5kyCsr::pccr_account(unsigned int id, int incr)
{
    if (this->pcmr.value & CSR_PCMR_ACTIVE && (this->pcer.value & (1 << id)))
    {
        this->pccr[id].value += incr;
    }
}

inline bool Ri5kyCsr::counters_enabled() {
    return this->iss.csr.pcmr.value & CSR_PCMR_ACTIVE;
}

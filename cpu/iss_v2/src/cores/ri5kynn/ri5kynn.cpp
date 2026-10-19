// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include <cpu/iss_v2/include/iss.hpp>

void Ri5kyNn::reset(bool active)
{
    Ri5ky::reset(active);
    if (active)
    {
        this->nn = {};
    }
}

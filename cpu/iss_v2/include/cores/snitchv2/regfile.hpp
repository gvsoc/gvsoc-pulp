// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <cpu/iss_v2/include/regfile.hpp>

/**
 * @brief Snitch register file.
 *
 * With the FP subsystem, the FP register accesses of the instructions
 * (FREG_GET / FREG_SET) to ft0..ft(NB_SSRS-1) go to the stream semantic
 * registers while they are enabled. The bodies are in snitchv2_implem.hpp
 * since they need the complete Iss.
 */
class SnitchV2Regfile : public Regfile
{
public:
    SnitchV2Regfile(Iss &iss) : Regfile(iss), iss(iss) {}

    inline uint64_t get_freg(int reg);
    inline void set_freg(int reg, uint64_t value);

private:
    Iss &iss;
};

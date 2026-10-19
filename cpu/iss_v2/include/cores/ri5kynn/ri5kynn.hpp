// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <cpu/iss_v2/include/types.hpp>
#include <cpu/iss_v2/include/cores/ri5ky/ri5ky.hpp>

// Registers of the PULP-NN extension, outside the register file (see
// cpu/iss_v2/include/isa/pulp_nn.hpp)
struct PulpNnState
{
    // Special-purpose registers of the mac-load instructions: activations
    // 0-1, weights 2-5
    iss_reg_t spr[6];
    // SPRs whose load is in flight, one bit each
    uint32_t spr_busy;

    // pv.qnt.n: step of the binary search (0 when none is going on), address
    // of the next threshold, bits found so far
    int qnt_step;
    iss_addr_t qnt_addr;
    iss_reg_t qnt_out;
    // Threshold of the current step, true once its load is issued, and
    // non-zero while it is in flight
    iss_reg_t qnt_threshold;
    bool qnt_loaded;
    uint32_t qnt_busy;
};

// Ri5ky with the PULP-NN extension
class Ri5kyNn : public Ri5ky
{
public:
    Ri5kyNn(Iss &iss) : Ri5ky(iss) {}

    void reset(bool active);

    PulpNnState nn;
};

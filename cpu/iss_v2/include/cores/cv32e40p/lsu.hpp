// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include <vp/vp.hpp>

class Iss;

/* io_v2 LSU reporting the accesses of the instruction in flight to the
 * co-simulation model. The ISA handlers reach these shadows as iss->lsu.*.
 * A misaligned access is reported once, as the instruction issued it. */
class Cv32e40pLsu : public LsuV2
{
public:
    Cv32e40pLsu(Iss &iss) : LsuV2(iss), iss(iss) {}

    template<typename T>
    inline bool load(iss_insn_t *insn, iss_addr_t addr, int size, int reg);
    template<typename T>
    inline bool load_perf(iss_insn_t *insn, iss_addr_t addr, int size, int reg);
    template<typename T>
    inline bool load_signed(iss_insn_t *insn, iss_addr_t addr, int size, int reg);
    template<typename T>
    inline bool load_signed_perf(iss_insn_t *insn, iss_addr_t addr, int size, int reg);
    template<typename T>
    inline bool load_float(iss_insn_t *insn, iss_addr_t addr, int size, int reg);
    template<typename T>
    inline bool load_float_perf(iss_insn_t *insn, iss_addr_t addr, int size, int reg);
    template<typename T>
    inline bool store(iss_insn_t *insn, iss_addr_t addr, int size, int reg);
    template<typename T>
    inline bool store_perf(iss_insn_t *insn, iss_addr_t addr, int size, int reg);
    template<typename T>
    inline bool store_float(iss_insn_t *insn, iss_addr_t addr, int size, int reg);
    template<typename T>
    inline bool store_float_perf(iss_insn_t *insn, iss_addr_t addr, int size, int reg);

private:
    inline void cosim_access(bool is_store, iss_addr_t addr, int size, uint64_t data);
    inline void cosim_load(iss_addr_t addr, int size, int reg, bool is_signed);

    Iss &iss;
};

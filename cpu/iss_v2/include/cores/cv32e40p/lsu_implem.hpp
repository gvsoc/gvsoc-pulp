// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

#pragma once

#include <cpu/iss_v2/include/cores/cv32e40p/lsu.hpp>
#include <cpu/iss_v2/include/lsu_v2_implem.hpp>
#include <cpu/iss_v2/include/cores/cv32e40p/cosim_model.hpp>

inline void Cv32e40pLsu::cosim_access(bool is_store, iss_addr_t addr, int size, uint64_t data)
{
    // A stalled access is dropped by the retry and reported again.
    if (this->iss.exec.cosim->enabled())
    {
        uint32_t mask = size >= 4 ? 0xFFFFFFFFu : ((1u << (8 * size)) - 1);
        this->iss.exec.cosim->mem(is_store, addr, size, (uint32_t)data & mask);
    }
}

inline void Cv32e40pLsu::cosim_load(iss_addr_t addr, int size, int reg, bool is_signed)
{
    this->cosim_access(false, addr, size, 0);
    if (this->iss.exec.cosim->enabled())
    {
        this->iss.exec.cosim->load(addr, size, reg, is_signed);
    }
}

template<typename T>
inline bool Cv32e40pLsu::load(iss_insn_t *insn, iss_addr_t addr, int size, int reg)
{
    bool result = this->LsuV2::load<T>(insn, addr, size, reg);
    this->cosim_load(addr, size, reg, false);
    return result;
}

template<typename T>
inline bool Cv32e40pLsu::load_perf(iss_insn_t *insn, iss_addr_t addr, int size, int reg)
{
    bool result = this->LsuV2::load_perf<T>(insn, addr, size, reg);
    this->cosim_load(addr, size, reg, false);
    return result;
}

template<typename T>
inline bool Cv32e40pLsu::load_signed(iss_insn_t *insn, iss_addr_t addr, int size, int reg)
{
    bool result = this->LsuV2::load_signed<T>(insn, addr, size, reg);
    this->cosim_load(addr, size, reg, true);
    return result;
}

template<typename T>
inline bool Cv32e40pLsu::load_signed_perf(iss_insn_t *insn, iss_addr_t addr, int size, int reg)
{
    bool result = this->LsuV2::load_signed_perf<T>(insn, addr, size, reg);
    this->cosim_load(addr, size, reg, true);
    return result;
}

template<typename T>
inline bool Cv32e40pLsu::load_float(iss_insn_t *insn, iss_addr_t addr, int size, int reg)
{
    bool result = this->LsuV2::load_float<T>(insn, addr, size, reg);
    this->cosim_load(addr, size, reg, false);
    return result;
}

template<typename T>
inline bool Cv32e40pLsu::load_float_perf(iss_insn_t *insn, iss_addr_t addr, int size, int reg)
{
    bool result = this->LsuV2::load_float_perf<T>(insn, addr, size, reg);
    this->cosim_load(addr, size, reg, false);
    return result;
}

template<typename T>
inline bool Cv32e40pLsu::store(iss_insn_t *insn, iss_addr_t addr, int size, int reg)
{
    bool result = this->LsuV2::store<T>(insn, addr, size, reg);
    this->cosim_access(true, addr, size, this->iss.regfile.get_reg(reg));
    return result;
}

template<typename T>
inline bool Cv32e40pLsu::store_perf(iss_insn_t *insn, iss_addr_t addr, int size, int reg)
{
    bool result = this->LsuV2::store_perf<T>(insn, addr, size, reg);
    this->cosim_access(true, addr, size, this->iss.regfile.get_reg(reg));
    return result;
}

template<typename T>
inline bool Cv32e40pLsu::store_float(iss_insn_t *insn, iss_addr_t addr, int size, int reg)
{
    bool result = this->LsuV2::store_float<T>(insn, addr, size, reg);
    this->cosim_access(true, addr, size, this->iss.regfile.get_freg(reg));
    return result;
}

template<typename T>
inline bool Cv32e40pLsu::store_float_perf(iss_insn_t *insn, iss_addr_t addr, int size, int reg)
{
    bool result = this->LsuV2::store_float_perf<T>(insn, addr, size, reg);
    this->cosim_access(true, addr, size, this->iss.regfile.get_freg(reg));
    return result;
}

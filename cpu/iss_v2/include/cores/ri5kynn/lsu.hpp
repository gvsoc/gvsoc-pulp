// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <cpu/iss_v2/include/cores/ri5ky/lsu.hpp>

// Number of registers outside the register file which can be loaded (the 6
// PULP-NN special-purpose registers and the pv.qnt threshold)
#define RI5KYNN_LSU_NB_EXT 7
// Slot of the pv.qnt threshold, the SPRs having slots 0 to 5
#define RI5KYNN_LSU_EXT_QNT 6

// Register number given to the LSU for a load into ext register `slot`,
// after the register file ones so that the base LSU never writes it
#define RI5KYNN_LSU_EXT_REG(slot) (ISS_DUMMY_REG + 1 + (slot))

// ri5ky LSU with the loads of PULP-NN into registers of the core outside the
// register file.
class Ri5kyNnLsu : public Ri5kyLsu
{
public:
    Ri5kyNnLsu(Iss &iss) : Ri5kyLsu(iss) {}

    // Load into a register of the core which is not in the register file:
    // the value goes to *dest. `busy_mask` is set in *busy while the load is
    // in flight, so that the instructions reading `dest` can wait for it.
    // `slot` identifies the register, only one load per slot can be in
    // flight. Returns true, as the other accesses, if the access could not
    // be issued (the instruction is retried).
    inline bool load_ext(iss_insn_t *insn, iss_addr_t addr, int size, int slot,
        iss_reg_t *dest, uint32_t *busy, uint32_t busy_mask)
    {
        this->ext[slot] = { dest, busy, busy_mask };
        // Busy until the response, which may come back before this returns
        *busy |= busy_mask;
        bool stalled = this->data_req_virtual(insn, addr, size, vp::IoReqOpcode::READ, false,
            RI5KYNN_LSU_EXT_REG(slot));
        if (stalled)
        {
            *busy &= ~busy_mask;
        }
        return stalled;
    }

    inline bool load_end_hook(LsuReqEntry *entry, iss_reg_t data)
    {
        if (likely(entry->reg <= ISS_DUMMY_REG))
        {
            return false;
        }

        ExtReg &ext = this->ext[entry->reg - RI5KYNN_LSU_EXT_REG(0)];
        *ext.dest = data;
        *ext.busy &= ~ext.busy_mask;
        return true;
    }

private:
    struct ExtReg
    {
        iss_reg_t *dest;
        uint32_t *busy;
        uint32_t busy_mask;
    };

    ExtReg ext[RI5KYNN_LSU_NB_EXT];
};

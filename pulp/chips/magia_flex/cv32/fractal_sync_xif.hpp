/*
 * Copyright (C) 2025 Fondazione Chips-IT
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)
 */

#pragma once

#include "cpu/iss/include/iss_core.hpp"
#include "cpu/iss/include/isa_lib/int.h"
#include "cpu/iss/include/isa_lib/macros.h"

static inline iss_reg_t fsync_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    IssOffloadInsn<iss_reg_t> offload_insn = {
        .opcode=insn->opcode,
        .arg_a=REG_GET(0),
        .arg_b=REG_GET(1),
    };
    iss->exec.offload_insn(&offload_insn);
    if (!offload_insn.granted) {
        iss->exec.stall_reg = REG_OUT(0); //here any value is good... I don't see any usage of stall_reg in the exec class... so OK... let's keep the REG_OUT(0) val for now...
        iss->exec.insn_stall();
    }
    return iss_insn_next(iss, insn, pc);
}
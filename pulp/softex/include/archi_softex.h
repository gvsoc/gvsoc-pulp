/*
* Copyright (C) 2026 ETH Zurich, University of Bologna, and Fondazione Chips-IT
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
*
* Authors:  Alessandro Nadalini <alessandro.nadalini3@unibo.it>
*/

#ifndef __ARCHI_SOFTEX_GVSOC_H__
#define __ARCHI_SOFTEX_GVSOC_H__

#define SOFTEX_N_ROWS           8     // Softex DATA_W/16 (FP16ALT elements per beat)

// Control regs (below SOFTEX_REG_OFFS)
#define SOFTEX_TRIGGER     0x00
#define SOFTEX_ACQUIRE     0x04
#define SOFTEX_FINISHED    0x08
#define SOFTEX_STATUS      0x0C
#define SOFTEX_RUNNING_JOB 0x10
#define SOFTEX_SOFT_CLEAR  0x14

#define SOFTEX_REG_OFFS    0x20

// Job regs (per-context, above SOFTEX_REG_OFFS)
#define SOFTEX_N_JOB_REGS      6
#define SOFTEX_IN_ADDR         (SOFTEX_REG_OFFS + 0x00)
#define SOFTEX_OUT_ADDR        (SOFTEX_REG_OFFS + 0x04)
#define SOFTEX_TOT_LEN         (SOFTEX_REG_OFFS + 0x08)
#define SOFTEX_COMMANDS        (SOFTEX_REG_OFFS + 0x0C)
#define SOFTEX_CACHE_BASE_ADDR (SOFTEX_REG_OFFS + 0x10)
#define SOFTEX_CAST_CTRL       (SOFTEX_REG_OFFS + 0x14)

// COMMANDS register bit layout
#define SOFTEX_CMD_ACC_ONLY        (1 << 0)
#define SOFTEX_CMD_DIV_ONLY        (1 << 1)
#define SOFTEX_CMD_ACQUIRE_SLOT    (1 << 2)
#define SOFTEX_CMD_LAST            (1 << 3)
#define SOFTEX_CMD_SET_CACHE_ADDR  (1 << 4)
#define SOFTEX_CMD_NO_OP           (1 << 5)
#define SOFTEX_CMD_INT_INPUT       (1 << 6)
#define SOFTEX_CMD_INT_OUTPUT      (1 << 7)
// bits [31:16] of COMMANDS carry the state-slot id ("current_slot" in softex_ctrl.sv)
#define SOFTEX_CMD_SLOT_ID_SHIFT   16
#define SOFTEX_CMD_SLOT_ID_MASK    0xFFFF0000

// CAST_CTRL register bit layout
#define SOFTEX_CAST_IN_INT_BITS_MASK   0x0000007F  // [6:0]
#define SOFTEX_CAST_IN_SIGNED_BIT      (1 << 7)    // [7]
#define SOFTEX_CAST_OUT_INT_BITS_SHIFT 8
#define SOFTEX_CAST_OUT_INT_BITS_MASK  0x00007F00  // [14:8]
#define SOFTEX_CAST_OUT_SIGNED_BIT     (1 << 15)   // [15]

// Number of double-buffered register contexts (softex_pkg::N_CTRL_CNTX)
#define SOFTEX_N_CONTEXT    2

#endif

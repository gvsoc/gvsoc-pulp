#ifndef __ARCHI_REDMULE_V1_H__
#define __ARCHI_REDMULE_V1_H__

/*
 * Copyright (C) 2018 ETH Zurich and University of Bologna
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

#define REDMULE_NB_REGS 14	 

#define REDMULE_TRIGGER             0x00
#define REDMULE_ACQUIRE             0x04
#define REDMULE_FINISHED_JOBS       0x08
#define REDMULE_STATUS              0x0c
#define REDMULE_RUNNING_TASK        0x10
#define REDMULE_SOFT_CLEAR          0x14
#define REDMULE_CHECK_STATE         0x18

#define REDMULE_J_X_ADDR            0x20
#define REDMULE_J_W_ADDR            0x24
#define REDMULE_J_Z_ADDR            0x28
#define REDMULE_J_M                 0x2C
#define REDMULE_J_N                 0x30
#define REDMULE_J_K                 0x34

#endif

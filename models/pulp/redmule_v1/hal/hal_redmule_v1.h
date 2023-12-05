#ifndef __HAL_REDMULE_V1_H__
#define __HAL_REDMULE_V1_H__

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


#include "pmsis.h"

/* LOW-LEVEL HAL */
#define REDMULE_ADDR_BASE ARCHI_REDMULE_ADDR
#define REDMULE_ADDR_SPACE 0x00000100

// For all the following functions we use __builtin_pulp_OffsetedWrite and __builtin_pulp_OffsetedRead
// instead of classic load/store because otherwise the compiler is not able to correctly factorize
// the HWME base in case several accesses are done, ending up with twice more code

#if defined(__riscv__) && !defined(RV_ISA_RV32)
#define REDMULE_WRITE(value, offset) __builtin_pulp_OffsetedWrite(value, (int *)REDMULE_ADDR_BASE, offset)
#define REDMULE_READ(offset) __builtin_pulp_OffsetedRead((int *)REDMULE_ADDR_BASE, offset)
#else
#define REDMULE_WRITE(value, offset) pulp_write32(REDMULE_ADDR_BASE + (offset), value)
#define REDMULE_READ(offset) pulp_read32(REDMULE_ADDR_BASE + (offset))
#endif


static inline void redmule_trigger_job() {
  REDMULE_WRITE(0, REDMULE_TRIGGER);
}
static inline int redmule_acquire() {
  return REDMULE_READ(REDMULE_ACQUIRE);
}
static inline int redmule_get_finished_jobs() {
  return REDMULE_READ(REDMULE_FINISHED_JOBS);
}
static inline int redmule_get_status() {
  return REDMULE_READ(REDMULE_STATUS);
}
static inline int redmule_get_running_task_id() {
  return REDMULE_READ(REDMULE_RUNNING_TASK);
}
static inline void redmule_soft_clear() {
  REDMULE_WRITE(0,REDMULE_SOFT_CLEAR);
}
static inline int redmule_get_state() {
  return REDMULE_READ(REDMULE_CHECK_STATE);
}

static inline void redmule_sleep_eot() {
  while(redmule_get_status())
  {
    eu_evt_maskWaitAndClr(1<<ARCHI_CL_EVT_ACC2);
  }
}

static inline void redmule_sleep_eoacquire() {
  while(redmule_acquire() == -1)
  {
    eu_evt_maskWaitAndClr(1<<ARCHI_CL_EVT_ACC3);
  }
}

static inline void redmule_set_job_params(
  unsigned int M,
  unsigned int N,
  unsigned int K) {

  REDMULE_WRITE(M, REDMULE_J_M);
  REDMULE_WRITE(N, REDMULE_J_N);
  REDMULE_WRITE(K, REDMULE_J_K);
}

static inline void redmule_set_streamer_params(
  unsigned int x_addr,
  unsigned int w_addr,
  unsigned int z_addr) {

  REDMULE_WRITE(x_addr, REDMULE_J_X_ADDR);
  REDMULE_WRITE(w_addr, REDMULE_J_W_ADDR);
  REDMULE_WRITE(z_addr, REDMULE_J_Z_ADDR);
}

#endif

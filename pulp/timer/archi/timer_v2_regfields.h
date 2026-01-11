
/* THIS FILE HAS BEEN GENERATED, DO NOT MODIFY IT.
 */

/*
 * Copyright (C) 2020 GreenWaves Technologies
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

#ifndef __ARCHI_TIMER_V2_REGFIELD__
#define __ARCHI_TIMER_V2_REGFIELD__

#if !defined(LANGUAGE_ASSEMBLY) && !defined(__ASSEMBLER__)


#endif




//
// REGISTERS FIELDS
//

// Timer low enable configuration bitfield: - 1'b0: disabled - 1'b1: enabled (access: R/W)
#define TIMER_V2_CFG_LO_ENABLE_BIT                                   0
#define TIMER_V2_CFG_LO_ENABLE_WIDTH                                 1
#define TIMER_V2_CFG_LO_ENABLE_MASK                                  0x1
#define TIMER_V2_CFG_LO_ENABLE_RESET                                 0x0

// Timer low counter reset command bitfield. Cleared after Timer Low reset execution. (access: R/W)
#define TIMER_V2_CFG_LO_RESET_BIT                                    1
#define TIMER_V2_CFG_LO_RESET_WIDTH                                  1
#define TIMER_V2_CFG_LO_RESET_MASK                                   0x2
#define TIMER_V2_CFG_LO_RESET_RESET                                  0x0

// Timer low compare match interrupt enable configuration bitfield: - 1'b0: disabled - 1'b1: enabled (access: R/W)
#define TIMER_V2_CFG_LO_IRQEN_BIT                                    2
#define TIMER_V2_CFG_LO_IRQEN_WIDTH                                  1
#define TIMER_V2_CFG_LO_IRQEN_MASK                                   0x4
#define TIMER_V2_CFG_LO_IRQEN_RESET                                  0x0

// Timer low input event mask configuration bitfield: - 1'b0: disabled - 1'b1: enabled (access: R/W)
#define TIMER_V2_CFG_LO_IEM_BIT                                      3
#define TIMER_V2_CFG_LO_IEM_WIDTH                                    1
#define TIMER_V2_CFG_LO_IEM_MASK                                     0x8
#define TIMER_V2_CFG_LO_IEM_RESET                                    0x0

// Timer low continuous mode configuration bitfield: - 1'b0: Continue mode - continue incrementing Timer low counter when compare match with CMP_LO occurs. - 1'b1: Cycle mode - reset Timer low counter when compare match with CMP_LO occurs. (access: R/W)
#define TIMER_V2_CFG_LO_MODE_BIT                                     4
#define TIMER_V2_CFG_LO_MODE_WIDTH                                   1
#define TIMER_V2_CFG_LO_MODE_MASK                                    0x10
#define TIMER_V2_CFG_LO_MODE_RESET                                   0x0

// Timer low one shot configuration bitfield: - 1'b0: let Timer low enabled counting when compare match with CMP_LO occurs. - 1'b1: disable Timer low when compare match with CMP_LO occurs. (access: R/W)
#define TIMER_V2_CFG_LO_ONE_S_BIT                                    5
#define TIMER_V2_CFG_LO_ONE_S_WIDTH                                  1
#define TIMER_V2_CFG_LO_ONE_S_MASK                                   0x20
#define TIMER_V2_CFG_LO_ONE_S_RESET                                  0x0

// Timer low prescaler enable configuration bitfield:- 1'b0: disabled - 1'b1: enabled (access: R/W)
#define TIMER_V2_CFG_LO_PEN_BIT                                      6
#define TIMER_V2_CFG_LO_PEN_WIDTH                                    1
#define TIMER_V2_CFG_LO_PEN_MASK                                     0x40
#define TIMER_V2_CFG_LO_PEN_RESET                                    0x0

// Timer low clock source configuration bitfield: - 1'b0: FLL or FLL+Prescaler - 1'b1: Reference clock at 32kHz (access: R/W)
#define TIMER_V2_CFG_LO_CCFG_BIT                                     7
#define TIMER_V2_CFG_LO_CCFG_WIDTH                                   1
#define TIMER_V2_CFG_LO_CCFG_MASK                                    0x80
#define TIMER_V2_CFG_LO_CCFG_RESET                                   0x0

// Timer low prescaler value bitfield. Ftimer = Fclk / (1 + PRESC_VAL) (access: R/W)
#define TIMER_V2_CFG_LO_PVAL_BIT                                     8
#define TIMER_V2_CFG_LO_PVAL_WIDTH                                   8
#define TIMER_V2_CFG_LO_PVAL_MASK                                    0xff00
#define TIMER_V2_CFG_LO_PVAL_RESET                                   0x0

// Timer low  + Timer high 64bit cascaded mode configuration bitfield. (access: R/W)
#define TIMER_V2_CFG_LO_CASC_BIT                                     31
#define TIMER_V2_CFG_LO_CASC_WIDTH                                   1
#define TIMER_V2_CFG_LO_CASC_MASK                                    0x80000000
#define TIMER_V2_CFG_LO_CASC_RESET                                   0x0

// Timer high enable configuration bitfield: - 1'b0: disabled - 1'b1: enabled (access: R/W)
#define TIMER_V2_CFG_HI_ENABLE_BIT                                   0
#define TIMER_V2_CFG_HI_ENABLE_WIDTH                                 1
#define TIMER_V2_CFG_HI_ENABLE_MASK                                  0x1
#define TIMER_V2_CFG_HI_ENABLE_RESET                                 0x0

// Timer high counter reset command bitfield. Cleared after Timer high reset execution. (access: W)
#define TIMER_V2_CFG_HI_RESET_BIT                                    1
#define TIMER_V2_CFG_HI_RESET_WIDTH                                  1
#define TIMER_V2_CFG_HI_RESET_MASK                                   0x2
#define TIMER_V2_CFG_HI_RESET_RESET                                  0x0

// Timer high compare match interrupt enable configuration bitfield: - 1'b0: disabled - 1'b1: enabled (access: R/W)
#define TIMER_V2_CFG_HI_IRQEN_BIT                                    2
#define TIMER_V2_CFG_HI_IRQEN_WIDTH                                  1
#define TIMER_V2_CFG_HI_IRQEN_MASK                                   0x4
#define TIMER_V2_CFG_HI_IRQEN_RESET                                  0x0

// Timer high input event mask configuration bitfield: - 1'b0: disabled - 1'b1: enabled (access: R/W)
#define TIMER_V2_CFG_HI_IEM_BIT                                      3
#define TIMER_V2_CFG_HI_IEM_WIDTH                                    1
#define TIMER_V2_CFG_HI_IEM_MASK                                     0x8
#define TIMER_V2_CFG_HI_IEM_RESET                                    0x0

// Timer high continuous mode configuration bitfield: - 1'b0: Continue mode - continue incrementing Timer high counter when compare match with CMP_LO occurs. - 1'b1: Cycle mode - reset Timer high counter when compare match with CMP_LO occurs. (access: R/W)
#define TIMER_V2_CFG_HI_MODE_BIT                                     4
#define TIMER_V2_CFG_HI_MODE_WIDTH                                   1
#define TIMER_V2_CFG_HI_MODE_MASK                                    0x10
#define TIMER_V2_CFG_HI_MODE_RESET                                   0x0

// Timer high one shot configuration bitfield: - 1'b0: let Timer high enabled counting when compare match with CMP_LO occurs. - 1'b1: disable Timer high when compare match with CMP_LO occurs. (access: R/W)
#define TIMER_V2_CFG_HI_ONE_S_BIT                                    5
#define TIMER_V2_CFG_HI_ONE_S_WIDTH                                  1
#define TIMER_V2_CFG_HI_ONE_S_MASK                                   0x20
#define TIMER_V2_CFG_HI_ONE_S_RESET                                  0x0

// Timer high prescaler enable configuration bitfield: - 1'b0: disabled - 1'b1: enabled (access: R/W)
#define TIMER_V2_CFG_HI_PEN_BIT                                      6
#define TIMER_V2_CFG_HI_PEN_WIDTH                                    1
#define TIMER_V2_CFG_HI_PEN_MASK                                     0x40
#define TIMER_V2_CFG_HI_PEN_RESET                                    0x0

// Timer high clock source configuration bitfield: - 1'b0: FLL or FLL+Prescaler - 1'b1: Reference clock at 32kHz (access: R/W)
#define TIMER_V2_CFG_HI_CLKCFG_BIT                                   7
#define TIMER_V2_CFG_HI_CLKCFG_WIDTH                                 1
#define TIMER_V2_CFG_HI_CLKCFG_MASK                                  0x80
#define TIMER_V2_CFG_HI_CLKCFG_RESET                                 0x0

// Timer Low counter value bitfield. (access: R/W)
#define TIMER_V2_CNT_LO_CNT_LO_BIT                                   0
#define TIMER_V2_CNT_LO_CNT_LO_WIDTH                                 32
#define TIMER_V2_CNT_LO_CNT_LO_MASK                                  0xffffffff
#define TIMER_V2_CNT_LO_CNT_LO_RESET                                 0x0

// Timer High counter value bitfield. (access: R/W)
#define TIMER_V2_CNT_HI_CNT_HI_BIT                                   0
#define TIMER_V2_CNT_HI_CNT_HI_WIDTH                                 32
#define TIMER_V2_CNT_HI_CNT_HI_MASK                                  0xffffffff
#define TIMER_V2_CNT_HI_CNT_HI_RESET                                 0x0

// Timer Low comparator value bitfield. (access: R/W)
#define TIMER_V2_CMP_LO_CMP_LO_BIT                                   0
#define TIMER_V2_CMP_LO_CMP_LO_WIDTH                                 32
#define TIMER_V2_CMP_LO_CMP_LO_MASK                                  0xffffffff
#define TIMER_V2_CMP_LO_CMP_LO_RESET                                 0x0

// Timer High comparator value bitfield. (access: R/W)
#define TIMER_V2_CMP_HI_CMP_HI_BIT                                   0
#define TIMER_V2_CMP_HI_CMP_HI_WIDTH                                 32
#define TIMER_V2_CMP_HI_CMP_HI_MASK                                  0xffffffff
#define TIMER_V2_CMP_HI_CMP_HI_RESET                                 0x0

// Timer Low start command bitfield. When executed, CFG_LO.ENABLE is set. (access: W)
#define TIMER_V2_START_LO_STRT_LO_BIT                                0
#define TIMER_V2_START_LO_STRT_LO_WIDTH                              1
#define TIMER_V2_START_LO_STRT_LO_MASK                               0x1
#define TIMER_V2_START_LO_STRT_LO_RESET                              0x0

// Timer High start command bitfield. When executed, CFG_HI.ENABLE is set. (access: W)
#define TIMER_V2_START_HI_STRT_HI_BIT                                0
#define TIMER_V2_START_HI_STRT_HI_WIDTH                              1
#define TIMER_V2_START_HI_STRT_HI_MASK                               0x1
#define TIMER_V2_START_HI_STRT_HI_RESET                              0x0

// Timer Low counter reset command bitfield. When executed, CFG_LO.RESET is set. (access: W)
#define TIMER_V2_RESET_LO_RST_LO_BIT                                 0
#define TIMER_V2_RESET_LO_RST_LO_WIDTH                               1
#define TIMER_V2_RESET_LO_RST_LO_MASK                                0x1
#define TIMER_V2_RESET_LO_RST_LO_RESET                               0x0

// Timer High counter reset command bitfield. When executed, CFG_HI.RESET is set. (access: W)
#define TIMER_V2_RESET_HI_RST_HI_BIT                                 0
#define TIMER_V2_RESET_HI_RST_HI_WIDTH                               1
#define TIMER_V2_RESET_HI_RST_HI_MASK                                0x1
#define TIMER_V2_RESET_HI_RST_HI_RESET                               0x0

#endif

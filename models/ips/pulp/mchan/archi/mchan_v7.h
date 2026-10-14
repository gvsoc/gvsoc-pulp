// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// GAP9 cluster DMA (mchan v7) register map. Two registers, accessed per
// channel (one channel per cluster PE plus one for the FC):
//
//   CMD (0x0):
//     read  -> allocate a transfer id (TID), stalls when none is free
//     write -> push one word of a command sequence:
//                word 0: command (fields below)
//                word 1: TCDM (local) address
//                word 2: external address
//                word 3: 2D count   (only when _2D_EXT is set)
//                word 4: 2D stride  (only when _2D_EXT is set)
//   STATUS (0x4):
//     read  -> bits [15:0]  TID busy mask (transfer still in flight)
//              bits [31:16] TID allocated mask
//     write -> bits set free the corresponding TIDs

#pragma once

#define MCHAN_CMD_OFFSET    0x0
#define MCHAN_STATUS_OFFSET 0x4

// CMD word 0 fields
#define MCHAN_CMD_CMD_LEN_BIT       0   // transfer length in bytes
#define MCHAN_CMD_CMD_LEN_WIDTH     17
#define MCHAN_CMD_CMD_TYPE_BIT      17  // 0: loc->ext, 1: ext->loc
#define MCHAN_CMD_CMD_INC_BIT       18  // 1: incremental addressing
#define MCHAN_CMD_CMD__2D_EXT_BIT   19  // 1: 2D transfer on the ext side
#define MCHAN_CMD_CMD_ELE_BIT       20  // 1: raise event line at end of transfer
#define MCHAN_CMD_CMD_ILE_BIT       21  // 1: raise irq line at end of transfer
#define MCHAN_CMD_CMD_BLE_BIT       22  // 1: broadcast event/irq to all cores
#define MCHAN_CMD_CMD__2D_TCDM_BIT  23  // 1: 2D transfer on the TCDM side (unsupported)

#define MCHAN_CMD_FIELD_GET(value, bit, width) \
    (((value) >> (bit)) & ((1u << (width)) - 1))

#define MCHAN_CMD_CMD_LEN_GET(value)      MCHAN_CMD_FIELD_GET(value, MCHAN_CMD_CMD_LEN_BIT, MCHAN_CMD_CMD_LEN_WIDTH)
#define MCHAN_CMD_CMD_TYPE_GET(value)     MCHAN_CMD_FIELD_GET(value, MCHAN_CMD_CMD_TYPE_BIT, 1)
#define MCHAN_CMD_CMD_INC_GET(value)      MCHAN_CMD_FIELD_GET(value, MCHAN_CMD_CMD_INC_BIT, 1)
#define MCHAN_CMD_CMD__2D_EXT_GET(value)  MCHAN_CMD_FIELD_GET(value, MCHAN_CMD_CMD__2D_EXT_BIT, 1)
#define MCHAN_CMD_CMD_ELE_GET(value)      MCHAN_CMD_FIELD_GET(value, MCHAN_CMD_CMD_ELE_BIT, 1)
#define MCHAN_CMD_CMD_ILE_GET(value)      MCHAN_CMD_FIELD_GET(value, MCHAN_CMD_CMD_ILE_BIT, 1)
#define MCHAN_CMD_CMD_BLE_GET(value)      MCHAN_CMD_FIELD_GET(value, MCHAN_CMD_CMD_BLE_BIT, 1)

/*
 * Copyright (C) 2020-2022  GreenWaves Technologies, ETH Zurich, University of Bologna
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
 * Authors: Cyrill Durrer, ETH Zurich (cdurrer@iis.ee.ethz.ch)
 */

#ifndef __DATAMOVER_HPP__
#define __DATAMOVER_HPP__

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>

#include "datamover_streamer.hpp"

// MASKS
#define L1_MASK                         0x0003FFFF
#define CLUSTER_MASK                    0x0FFFFFFF

#define L1_TRANSACTION_SIZE             1

// General hwpe register offsets
#define HWPE_REGISTER_OFFS              0x00    // Standard HWPE register offset

#define DATAMOVER_COMMIT_AND_TRIGGER    0x00    // Trigger commit
#define DATAMOVER_ACQUIRE               0x04    // Acquire command
#define DATAMOVER_FINISHED              0x08    // Finished signal
#define DATAMOVER_STATUS                0x0C    // Status register
#define DATAMOVER_RUNNING_JOB           0x10    // Running job ID
#define DATAMOVER_SOFT_CLEAR            0x14    // Soft clear

// Job-specific registers
#define DATAMOVER_REGISTER_OFFS        0x40 // Register base offset

#define DATAMOVER_REG_IN_PTR           0  // Input pointer
#define DATAMOVER_REG_OUT_PTR          1  // Output pointer
#define DATAMOVER_REG_TOT_LEN          2  // Total length in number of accesses (BW)
#define DATAMOVER_REG_IN_D0            3  // [31:16] in_d0_stride; [15:0] in_d0_len
#define DATAMOVER_REG_IN_D1            4  // [31:16] in_d1_stride; [15:0] in_d1_len
#define DATAMOVER_REG_IN_D2            5  // [31:16] in_d2_stride; [15:0] in_d2_len
#define DATAMOVER_REG_IN_D3            6  // [31:16] in_d3_stride; [15:0] in_d3_len
#define DATAMOVER_REG_OUT_D0           7  // [31:16] out_d0_stride; [15:0] out_d0_len
#define DATAMOVER_REG_OUT_D1           8  // [31:16] out_d1_stride; [15:0] out_d1_len
#define DATAMOVER_REG_OUT_D2           9  // [31:16] out_d2_stride; [15:0] out_d2_len
#define DATAMOVER_REG_OUT_D3           10 // [31:16] out_d3_stride; [15:0] out_d3_len
#define DATAMOVER_REG_IN_OUT_D4_STRIDE 11 // [31:16] out_d4_stride; [15:0] in_d4_stride (d4_len unnecessary due to tot_len)
#define DATAMOVER_REG_MATRIX_DIM       12 // [31:16] matrix_dim_n; [15:0] matrix_dim_m
#define DATAMOVER_REG_CHANNELS         13 // [31:11] total_elements = num_channels * dim_m * dim_n; [10:0] num_channels (for unfolding/folding)
#define DATAMOVER_REG_CTRL_ENGINE      14 // [15:12] write_dim_en; [11:8] read_dim_en; [7:3] datamover_mode; [2:0] transp_mode (LSB: 000=none, 001=1 elem, 010=2 elem, 100=4 elem)

#define DATAMOVER_NB_REG 15

#define DATAMOVER_BANDWIDTH 512
#define DATAMOVER_ELEM_WIDTH 8
#define DATAMOVER_BANDWIDTH_ELEMS (DATAMOVER_BANDWIDTH / DATAMOVER_ELEM_WIDTH)



class Datamover : public vp::Component
{
    public:
        Datamover(vp::ComponentConf &config);
        vp::IoMaster l1;
        vp::Trace trace;
        vp::IoReq io_req;
    private:
        void clear();

        static vp::IoReqStatus handle_req(vp::Block *__this, vp::IoReq *req);

        // REGISTER FILE member functions
        int hwpe_regfile_rd(int addr);
        int regfile_rd(int addr);
        // void hwpe_regfile_wr(int addr, int value);
        void regfile_wr(int addr, int value);
        void unpack_config_from_reg();
        void update_streamer_config();
        void fill_elem_matrix(int num_rows);
        void copy();
        void transpose(uint8_t transpose_mode);
        void cim_layout_conversion();
        void unfold();
        void printout();

        // HWPE REGISTER FILE
        uint32_t datamover_acquire;
        uint32_t datamover_finished;
        uint32_t datamover_status;
        uint32_t datamover_running_job;

        // REGISTER FILE configuration parameters
        uint32_t in_ptr;          // Input pointer
        uint32_t out_ptr;         // Output pointer
        uint32_t tot_len;         // Total length in number of accesses (BW)
        uint32_t in_d0;           // [31:16] in_d0_stride; [15:0] in_d0_len
        uint32_t in_d1;           // [31:16] in_d1_stride; [15:0] in_d1_len
        uint32_t in_d2;           // [31:16] in_d2_stride; [15:0] in_d2_len
        uint32_t in_d3;           // [31:16] in_d3_stride; [15:0] in_d3_len
        uint32_t out_d0;          // [31:16] out_d0_stride; [15:0] out_d0_len
        uint32_t out_d1;          // [31:16] out_d1_stride; [15:0] out_d1_len
        uint32_t out_d2;          // [31:16] out_d2_stride; [15:0] out_d2_len
        uint32_t out_d3;          // [31:16] out_d3_stride; [15:0] out_d3_len
        uint32_t in_out_d4_stride;// [31:16] out_d4_stride; [15:0] in_d4_stride (d4_len unnecessary due to tot_len)
        uint32_t matrix_dim;      // [31:16] matrix_dim_n; [15:0] matrix_dim_m
        uint32_t channels;        // [31:11] total_elements = num_channels * dim_m * dim_n; [10:0] num_channels

        uint32_t ctrl_engine;     // [15:12] write_dim_en; [11:8] read_dim_en; [7:3] datamover_mode; [2:0] transp_mode

        // CONFIGURAION PARAMETERS unpacked
        uint32_t in_d0_stride;
        uint32_t in_d0_len;
        uint32_t in_d1_stride;
        uint32_t in_d1_len;
        uint32_t in_d2_stride;
        uint32_t in_d2_len;
        uint32_t in_d3_stride;
        uint32_t in_d3_len;
        uint32_t in_d4_stride;
        uint32_t in_dim_enable;
        uint32_t out_d0_stride;
        uint32_t out_d0_len;
        uint32_t out_d1_stride;
        uint32_t out_d1_len;
        uint32_t out_d2_stride;
        uint32_t out_d2_len;
        uint32_t out_d3_stride;
        uint32_t out_d3_len;
        uint32_t out_d4_stride;
        uint32_t out_dim_enable;
        uint32_t matrix_dim_m;
        uint32_t matrix_dim_n;
        uint32_t total_elements;
        uint32_t num_channels;

        //=========================================================
        // MEMORY TRANSACTION HELPERS
        // These functions model bandwidth constraints while using the same physical port
        // Functional-only memory access (no timing model)
        //=========================================================
        void load_from_memory_functional(uint32_t addr, uint8_t *data, uint32_t size, uint32_t skip_prefix_bytes = 0, int64_t *latency_out = nullptr);
        void store_to_memory_functional(uint32_t addr, uint8_t *data, uint32_t size, int64_t *latency_out = nullptr);

        // CONFIG and IRQ
        vp::IoSlave cfg_port;
        vp::WireMaster<bool> irq;

        // Internal Buffer
        uint8_t elem_matrix[DATAMOVER_BANDWIDTH_ELEMS][DATAMOVER_BANDWIDTH_ELEMS];

        // STREAMERS
        HWPEStreamer<Datamover, uint8_t> streamer_in;
        HWPEStreamer<Datamover, uint8_t> streamer_out;
};

#endif /* __DATAMOVER_HPP__ */

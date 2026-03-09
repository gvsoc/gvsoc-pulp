/*
 * Copyright (C) 2026, ETH Zurich
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

#include <datamover.hpp>

void Datamover::unpack_config_from_reg() {
  this->in_d0_len = this->in_d0 & 0xFFFF;
  this->in_d0_stride = (this->in_d0 >> 16) & 0xFFFF;
  this->in_d1_len = this->in_d1 & 0xFFFF;
  this->in_d1_stride = (this->in_d1 >> 16) & 0xFFFF;
  this->in_d2_len = this->in_d2 & 0xFFFF;
  this->in_d2_stride = (this->in_d2 >> 16) & 0xFFFF;
  this->in_d3_len = this->in_d3 & 0xFFFF;
  this->in_d3_stride = (this->in_d3 >> 16) & 0xFFFF;
  this->in_d4_stride = this->in_out_d4_stride & 0xFFFF;
  this->in_dim_enable = this->dim_enable & 0xF;

  this->out_d0_len = this->out_d0 & 0xFFFF;
  this->out_d0_stride = (this->out_d0 >> 16) & 0xFFFF;
  this->out_d1_len = this->out_d1 & 0xFFFF;
  this->out_d1_stride = (this->out_d1 >> 16) & 0xFFFF;
  this->out_d2_len = this->out_d2 & 0xFFFF;
  this->out_d2_stride = (this->out_d2 >> 16) & 0xFFFF;
  this->out_d3_len = this->out_d3 & 0xFFFF;
  this->out_d3_stride = (this->out_d3 >> 16) & 0xFFFF;
  this->out_d4_stride = (this->in_out_d4_stride >> 16) & 0xFFFF;
  this->out_dim_enable = (this->dim_enable >> 4) & 0xF;
}

void Datamover::update_streamer_config() {
  Datamover::unpack_config_from_reg();

  this->streamer_in.update_params(
      this->in_ptr,
      this->in_d0_stride,
      this->in_d1_stride,
      this->in_d2_stride,
      this->in_d3_stride,
      this->in_d4_stride,
      this->in_d0_len,
      this->in_d1_len,
      this->in_d2_len,
      this->in_d3_len,
      this->tot_len,
      this->in_dim_enable
  );
  this->streamer_in.print_config();

  this->streamer_out.update_params(
      this->out_ptr,
      this->out_d0_stride,
      this->out_d1_stride,
      this->out_d2_stride,
      this->out_d3_stride,
      this->out_d4_stride,
      this->out_d0_len,
      this->out_d1_len,
      this->out_d2_len,
      this->out_d3_len,
      this->tot_len,
      this->out_dim_enable
  );
  this->streamer_out.print_config();
}

void Datamover::fill_elem_matrix(int num_rows) {
  for(int i = 0; i < num_rows; i++) {
    this->streamer_in.print_state();
    uint32_t in_addr = this->streamer_in.iterate();
    this->load_from_memory_functional(in_addr, elem_matrix[i], DATAMOVER_BANDWIDTH_ELEMS, 0, nullptr);
    printf("fill_elem_matrix(): read row %d: 0x%08x from in_addr(%p)\n", i, *(uint64_t*)elem_matrix[i], in_addr);
  }
}

void Datamover::copy() {
  // uint8_t elem_matrix[DATAMOVER_BANDWIDTH_ELEMS][DATAMOVER_BANDWIDTH_ELEMS];    // ToDo: make buffer persistent (global and static), clear in clear() method

  printf("copy(): Starting data copy from %p to %p, length %d bytes\n", this->in_ptr, this->out_ptr, this->tot_len);
  this->trace.msg(vp::Trace::LEVEL_INFO, "copy(): Starting data copy from %p to %p, length %d bytes\n", this->in_ptr, this->out_ptr, this->tot_len);

  update_streamer_config();

  int num_tiles = this->tot_len / DATAMOVER_BANDWIDTH_ELEMS;
  int leftover_rows = this->tot_len % DATAMOVER_BANDWIDTH_ELEMS;

  for(int tile_counter = 0; tile_counter < num_tiles; tile_counter++) {
    printf("copy(): Processing tile %d\n", tile_counter);
    fill_elem_matrix(DATAMOVER_BANDWIDTH_ELEMS);
    for(int i = 0; i < DATAMOVER_BANDWIDTH_ELEMS; i++) {
      this->streamer_out.print_state();
      uint32_t out_addr = this->streamer_out.iterate();
      this->store_to_memory_functional(out_addr, elem_matrix[i], DATAMOVER_BANDWIDTH_ELEMS, nullptr);
      printf("copy(): wrote row %d: 0x%08x to out_addr(0x%08x)\n", i, *(uint64_t*)elem_matrix[i], out_addr);
    }
  }

  if(leftover_rows > 0) {
    printf("copy(): Processing leftover rows, count: %d\n", leftover_rows);
    fill_elem_matrix(leftover_rows);
    for(int i = 0; i < leftover_rows; i++) {
      this->streamer_out.print_state();
      uint32_t out_addr = this->streamer_out.iterate();
      this->store_to_memory_functional(out_addr, elem_matrix[i], DATAMOVER_BANDWIDTH_ELEMS, nullptr);
      printf("copy(): wrote leftover row %d: 0x%08x to out_addr(0x%08x)\n", i, *(uint64_t*)elem_matrix[i], out_addr);
    }
  }

  // for(int i = 0; i < this->tot_len; i++) { // tot_len is in 512-bit accesses
  //   this->streamer_in.print_state();
  //   uint32_t in_addr = this->streamer_in.iterate();
  //   this->load_from_memory_functional(in_addr, elem_matrix[i % DATAMOVER_BANDWIDTH_ELEMS], DATAMOVER_BANDWIDTH_ELEMS, 0, nullptr);
  //   printf("copy(): read element %d: 0x%02x from in_addr(0x%08x)\n", i, elem_matrix[i % DATAMOVER_BANDWIDTH_ELEMS][0], in_addr);
  //   this->streamer_out.print_state();
  //   uint32_t out_addr = this->streamer_out.iterate();
  //   this->store_to_memory_functional(out_addr, elem_matrix[i % DATAMOVER_BANDWIDTH_ELEMS], DATAMOVER_BANDWIDTH_ELEMS, nullptr);
  //   printf("copy(): wrote element %d: 0x%02x to out_addr(0x%08x)\n", i, elem_matrix[i % DATAMOVER_BANDWIDTH_ELEMS][0], out_addr);
  // }

  // Write to HWPE register to indicate completion
  printf("copy(): Data copy completed, writing to finished register\n");
  this->datamover_finished = 1;
}

void Datamover::transpose(uint8_t transpose_mode) {
  printf("transpose(): Starting data transpose from %p to %p, length %d bytes\n", this->in_ptr, this->out_ptr, this->tot_len);
  this->trace.msg(vp::Trace::LEVEL_INFO, "transpose(): Starting data transpose from %p to %p, length %d bytes\n", this->in_ptr, this->out_ptr, this->tot_len);

  if(transpose_mode != 1 && transpose_mode != 2 && transpose_mode != 4) {
    printf("transpose(): Unsupported transpose mode: %d\n", transpose_mode);
    this->trace.msg(vp::Trace::LEVEL_WARNING, "transpose(): Unsupported transpose mode: %d, must be 0(copy), 1, 2, 4\n", transpose_mode);
    return;
  }

  update_streamer_config();

  int num_tiles = this->tot_len / DATAMOVER_BANDWIDTH_ELEMS;
  int leftover_rows = this->tot_len % DATAMOVER_BANDWIDTH_ELEMS;
  uint8_t out_row[DATAMOVER_BANDWIDTH_ELEMS];

  for(int tile_counter = 0; tile_counter < num_tiles; tile_counter++) {
    printf("transpose(): Processing tile %d\n", tile_counter);
    fill_elem_matrix(DATAMOVER_BANDWIDTH_ELEMS);

    for(int c = 0; c < transpose_mode; c++) {
      for(int i = 0; i < DATAMOVER_BANDWIDTH_ELEMS; i+=transpose_mode) {
        for(int j = 0; j < DATAMOVER_BANDWIDTH_ELEMS / transpose_mode; j++) {
          for(int elem_cnt = 0; elem_cnt < transpose_mode; elem_cnt++) {
            out_row[transpose_mode*j + elem_cnt] = elem_matrix[c*(DATAMOVER_BANDWIDTH_ELEMS / transpose_mode) + j][i + elem_cnt];
          }
        }
        this->streamer_out.print_state();
        uint32_t out_addr = this->streamer_out.iterate();
        this->store_to_memory_functional(out_addr, out_row, DATAMOVER_BANDWIDTH_ELEMS, nullptr);
        printf("transpose(): wrote row %d: 0x%08x to out_addr(0x%08x)\n", i, *(uint64_t*)out_row, out_addr);
      }
    }
  }

  // if(leftover_rows > 0) {
  //   printf("transpose(): Processing leftover rows, count: %d\n", leftover_rows);
  //   fill_elem_matrix(leftover_rows);
  //   for(int i = 0; i < leftover_rows; i++) {
  //     this->streamer_out.print_state();
  //     uint32_t out_addr = this->streamer_out.iterate();
  //     this->store_to_memory_functional(out_addr, elem_matrix[i], DATAMOVER_BANDWIDTH_ELEMS, nullptr);
  //     printf("transpose(): wrote leftover row %d: 0x%08x to out_addr(0x%08x)\n", i, *(uint64_t*)elem_matrix[i], out_addr);
  //   }
  // }

  // Write to HWPE register to indicate completion
  printf("transpose(): Data transpose completed, writing to finished register\n");
  this->datamover_finished = 1;
}

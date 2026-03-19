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

  this->matrix_dim_m = (this->ctrl_engine >> 3) & 0xFFF;
  this->matrix_dim_n = (this->ctrl_engine >> 15) & 0xFFF;
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
    // this->streamer_in.print_state();
    uint32_t in_addr = this->streamer_in.iterate();
    this->load_from_memory_functional(in_addr, elem_matrix[i], DATAMOVER_BANDWIDTH_ELEMS, 0, nullptr);
    printf("fill_elem_matrix(): read row %d: 0x...%08x from in_addr(%p)\n", i, *(uint64_t*)elem_matrix[i], in_addr);
  }
}

void Datamover::copy() {
  printf("copy(): Starting data copy from %p to %p, length %d bytes\n", this->in_ptr, this->out_ptr, this->tot_len);
  this->trace.msg(vp::Trace::LEVEL_INFO, "copy(): Starting data copy from %p to %p, length %d bytes\n", this->in_ptr, this->out_ptr, this->tot_len);

  update_streamer_config();

  int num_tiles = this->tot_len / DATAMOVER_BANDWIDTH_ELEMS;
  int leftover_rows = this->tot_len % DATAMOVER_BANDWIDTH_ELEMS;
  int total_elems = this->matrix_dim_m * this->matrix_dim_n;
  int leftover_elems = total_elems % DATAMOVER_BANDWIDTH_ELEMS;

  for(int tile_counter = 0; tile_counter < num_tiles; tile_counter++) {
    printf("copy(): Processing tile %d\n", tile_counter);
    fill_elem_matrix(DATAMOVER_BANDWIDTH_ELEMS);
    for(int i = 0; i < DATAMOVER_BANDWIDTH_ELEMS; i++) {
      this->streamer_out.print_state();
      uint32_t out_addr = this->streamer_out.iterate();
      this->store_to_memory_functional(out_addr, elem_matrix[i], DATAMOVER_BANDWIDTH_ELEMS, nullptr);
      printf("copy(): wrote row %d: 0x...%08x to out_addr(%p)\n", i+1, *(uint64_t*)elem_matrix[i], out_addr);
    }
  }

  if(leftover_rows > 0) {
    printf("copy(): Processing leftover rows, count: %d\n", leftover_rows);
    fill_elem_matrix(leftover_rows);
    for(int i = 0; i < leftover_rows; i++) {
      this->streamer_out.print_state();
      uint32_t out_addr = this->streamer_out.iterate();
      if(i == leftover_rows - 1 && leftover_elems > 0) {  // last row with leftover elements
        this->store_to_memory_functional(out_addr, elem_matrix[i], leftover_elems, nullptr);
        printf("copy(): wrote leftover row %d with leftover elems %d: 0x...%08x to out_addr(%p)\n", i+1, leftover_elems, *(uint64_t*)elem_matrix[i], out_addr);
      }
      else {
        this->store_to_memory_functional(out_addr, elem_matrix[i], DATAMOVER_BANDWIDTH_ELEMS, nullptr);
        printf("copy(): wrote leftover row %d: 0x...%08x to out_addr(%p)\n", i+1, *(uint64_t*)elem_matrix[i], out_addr);
      }
      // this->store_to_memory_functional(out_addr, elem_matrix[i], DATAMOVER_BANDWIDTH_ELEMS, nullptr);
      // printf("copy(): wrote leftover row %d: 0x%08x to out_addr(0x%08x)\n", i, *(uint64_t*)elem_matrix[i], out_addr);
    }
  }

  // Write to HWPE register to indicate completion
  printf("copy(): Data copy completed, writing to finished register\n");
  this->datamover_finished = 1;
}

void Datamover::transpose(uint8_t transpose_mode) {
  if(transpose_mode != 1 && transpose_mode != 2 && transpose_mode != 4) {
    printf("transpose(): Unsupported transpose mode: %d\n", transpose_mode);
    this->trace.msg(vp::Trace::LEVEL_WARNING, "transpose(): Unsupported transpose mode: %d, must be 0(copy), 1, 2, 4\n", transpose_mode);
    return;
  }

  update_streamer_config();

  printf("transpose(): Starting matrix (dim_m=%d, dim_n=%d) transpose with mode %d\n", this->matrix_dim_m, this->matrix_dim_n, transpose_mode);
  this->trace.msg(vp::Trace::LEVEL_INFO, "transpose(): Starting matrix (dim_m=%d, dim_n=%d) transpose with mode %d\n", this->matrix_dim_m, this->matrix_dim_n, transpose_mode);

  // int total_elems = this->matrix_dim_m * this->matrix_dim_n;
  // int num_tiles = this->tot_len / DATAMOVER_BANDWIDTH_ELEMS;
  int m_tiles = (this->matrix_dim_m + DATAMOVER_BANDWIDTH_ELEMS - 1) / DATAMOVER_BANDWIDTH_ELEMS;   // including partial tiles
  int n_tiles = (this->matrix_dim_n + DATAMOVER_BANDWIDTH_ELEMS - 1) / DATAMOVER_BANDWIDTH_ELEMS;   // including partial tiles
  // int complete_m_tiles = this->matrix_dim_m / DATAMOVER_BANDWIDTH_ELEMS;
  // int complete_n_tiles = this->matrix_dim_n / DATAMOVER_BANDWIDTH_ELEMS;
  // int num_complete_tiles = complete_m_tiles * complete_n_tiles;
  int leftover_rows = this->matrix_dim_m % DATAMOVER_BANDWIDTH_ELEMS;
  int leftover_cols = this->matrix_dim_n % DATAMOVER_BANDWIDTH_ELEMS;
  // int leftover_elems = total_elems % DATAMOVER_BANDWIDTH_ELEMS;
  uint8_t out_row[DATAMOVER_BANDWIDTH_ELEMS];

  printf("transpose(): matrix_dim_m=%d, matrix_dim_n=%d, leftover_rows=%d, leftover_cols=%d\n",
    this->matrix_dim_m, this->matrix_dim_n, leftover_rows, leftover_cols);

  for(int n_tile_counter = 0; n_tile_counter < n_tiles; n_tile_counter++) {
    for(int m_tile_counter = 0; m_tile_counter < m_tiles; m_tile_counter++) {
      printf("transpose(): Processing tile (m_tile_counter = %d / %d, n_tile_counter = %d / %d)\n", m_tile_counter+1, m_tiles, n_tile_counter+1, n_tiles);
      if((m_tile_counter == m_tiles - 1 && leftover_rows > 0) && (n_tile_counter == n_tiles - 1 && leftover_cols > 0)) {
        printf("transpose(): Processing last tile with leftover rows and cols (m_tile=%d / %d, n_tile=%d / %d, leftover_rows=%d, leftover_cols=%d)\n", m_tile_counter+1, m_tiles, n_tile_counter+1, n_tiles, leftover_rows, leftover_cols);
        fill_elem_matrix(DATAMOVER_BANDWIDTH_ELEMS);  // Note: fill_elem_matrix(leftover_rows) would be sufficient, but that would introduce problems with the streamer configuration
        int subtiles = (leftover_rows + (DATAMOVER_BANDWIDTH_ELEMS / transpose_mode) - 1) / (DATAMOVER_BANDWIDTH_ELEMS / transpose_mode); // number of subtiles needed to cover leftover rows (ceil division)
        int j_max, write_elems;
        for(int c = 0; c < subtiles; c++) {
          for(int i = 0; i < leftover_cols; i+=transpose_mode) {
            if((c == subtiles - 1) && (leftover_rows % (DATAMOVER_BANDWIDTH_ELEMS / transpose_mode) != 0)) {
              j_max = leftover_rows % (DATAMOVER_BANDWIDTH_ELEMS / transpose_mode);
              write_elems = j_max * transpose_mode;
            }
            else {
              j_max = DATAMOVER_BANDWIDTH_ELEMS / transpose_mode;
              write_elems = DATAMOVER_BANDWIDTH_ELEMS;
            }
            for(int j = 0; j < j_max; j++) {
              for(int elem_cnt = 0; elem_cnt < transpose_mode; elem_cnt++) {
                out_row[transpose_mode*j + elem_cnt] = elem_matrix[c*(DATAMOVER_BANDWIDTH_ELEMS / transpose_mode) + j][i + elem_cnt];
                // printf("transpose(): DEBUG: subtile %d: out_row[%d] = elem_matrix[%d][%d] = 0x%02x, j_max=%d, write_elems=%d\n", c, transpose_mode*j + elem_cnt, c*(DATAMOVER_BANDWIDTH_ELEMS / transpose_mode) + j, i + elem_cnt, elem_matrix[c*(DATAMOVER_BANDWIDTH_ELEMS / transpose_mode) + j][i + elem_cnt], j_max, write_elems);
              }
            }
            this->streamer_out.print_state();
            uint32_t out_addr = this->streamer_out.iterate();
            this->store_to_memory_functional(out_addr, out_row, write_elems, nullptr);
            printf("transpose(): wrote row %d: 0x...%08x to out_addr(%p)\n", i+1, *(uint64_t*)out_row, out_addr);
          }
          for(int i = leftover_cols; i < DATAMOVER_BANDWIDTH_ELEMS; i+=transpose_mode) {
            this->streamer_out.iterate();  // skip writing remaining rows of the tile     ToDo: optimize in RTL if possible
            printf("transpose(): skipped writing row %d\n", i+1);
          }
        }
      }
      else if(m_tile_counter == m_tiles - 1 && leftover_rows > 0) {  // last tiles in m-dimension with leftover rows
        printf("transpose(): Processing last M-tile (%d / %d) with leftover rows (%d)\n", m_tile_counter+1, m_tiles, leftover_rows);
        fill_elem_matrix(DATAMOVER_BANDWIDTH_ELEMS);    // Note: fill_elem_matrix(leftover_rows) would be sufficient, but that would introduce problems with the streamer configuration
        int subtiles = (leftover_rows + (DATAMOVER_BANDWIDTH_ELEMS / transpose_mode) - 1) / (DATAMOVER_BANDWIDTH_ELEMS / transpose_mode); // number of subtiles needed to cover leftover rows (ceil division)
        int j_max, write_elems;
        for(int c = 0; c < subtiles; c++) {
          for(int i = 0; i < DATAMOVER_BANDWIDTH_ELEMS; i+=transpose_mode) {
            if((c == subtiles - 1) && (leftover_rows % (DATAMOVER_BANDWIDTH_ELEMS / transpose_mode) != 0)) {
              j_max = leftover_rows % (DATAMOVER_BANDWIDTH_ELEMS / transpose_mode);
              write_elems = j_max * transpose_mode;
            }
            else {
              j_max = DATAMOVER_BANDWIDTH_ELEMS / transpose_mode;
              write_elems = DATAMOVER_BANDWIDTH_ELEMS;
            }
            for(int j = 0; j < j_max; j++) {
              for(int elem_cnt = 0; elem_cnt < transpose_mode; elem_cnt++) {
                out_row[transpose_mode*j + elem_cnt] = elem_matrix[c*(DATAMOVER_BANDWIDTH_ELEMS / transpose_mode) + j][i + elem_cnt];
                // printf("transpose(): DEBUG: subtile %d: out_row[%d] = elem_matrix[%d][%d] = 0x%02x, j_max=%d, write_elems=%d\n", c, transpose_mode*j + elem_cnt, c*(DATAMOVER_BANDWIDTH_ELEMS / transpose_mode) + j, i + elem_cnt, elem_matrix[c*(DATAMOVER_BANDWIDTH_ELEMS / transpose_mode) + j][i + elem_cnt], j_max, write_elems);
              }
            }
            this->streamer_out.print_state();
            uint32_t out_addr = this->streamer_out.iterate();
            this->store_to_memory_functional(out_addr, out_row, write_elems, nullptr);
            printf("transpose(): wrote row %d: 0x...%08x to out_addr(%p)\n", i+1, *(uint64_t*)out_row, out_addr);
          }
        }
        if(subtiles < transpose_mode) {  // if we have fewer subtiles than the transpose mode, we need to skip writing remaining rows of the tile
          for(int c = subtiles; c < transpose_mode; c++) {
            for(int i = 0; i < DATAMOVER_BANDWIDTH_ELEMS; i+=transpose_mode) {
              this->streamer_out.iterate();  // skip writing remaining rows of the tile     ToDo: optimize in RTL if possible
              printf("transpose(): skipped writing row %d\n", i+1);
            }
          }
        }
      }
      else if(n_tile_counter == n_tiles - 1 && leftover_cols > 0) {  // last tiles in n-dimension with leftover cols
        printf("transpose(): Processing last N-tile (%d / %d) with leftover cols (%d)\n", n_tile_counter+1, n_tiles, leftover_cols);
        fill_elem_matrix(DATAMOVER_BANDWIDTH_ELEMS);
        for(int c = 0; c < transpose_mode; c++) {
          for(int i = 0; i < leftover_cols; i+=transpose_mode) {
            for(int j = 0; j < DATAMOVER_BANDWIDTH_ELEMS / transpose_mode; j++) {
              for(int elem_cnt = 0; elem_cnt < transpose_mode; elem_cnt++) {
                out_row[transpose_mode*j + elem_cnt] = elem_matrix[c*(DATAMOVER_BANDWIDTH_ELEMS / transpose_mode) + j][i + elem_cnt];
                // printf("transpose(): DEBUG: out_row[%d] = elem_matrix[%d][%d] = 0x%02x\n", transpose_mode*j + elem_cnt, c*(DATAMOVER_BANDWIDTH_ELEMS / transpose_mode) + j, i + elem_cnt, elem_matrix[c*(DATAMOVER_BANDWIDTH_ELEMS / transpose_mode) + j][i + elem_cnt]);
              }
            }
            this->streamer_out.print_state();
            uint32_t out_addr = this->streamer_out.iterate();
            this->store_to_memory_functional(out_addr, out_row, DATAMOVER_BANDWIDTH_ELEMS, nullptr);
            printf("transpose(): wrote row %d: 0x...%08x to out_addr(%p)\n", i+1, *(uint64_t*)out_row, out_addr);
          }
          for(int i = leftover_cols; i < DATAMOVER_BANDWIDTH_ELEMS; i+=transpose_mode) {
            this->streamer_out.iterate();  // skip writing remaining rows of the tile     ToDo: optimize in RTL if possible
            printf("transpose(): skipped writing row %d\n", i+1);
          }
        }
      }
      else {  // complete tiles
        printf("transpose(): Processing complete tile (m_tile=%d, n_tile=%d)\n", m_tile_counter+1, n_tile_counter+1);
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
            printf("transpose(): wrote row %d: 0x...%08x to out_addr(%p)\n", i+1, *(uint64_t*)out_row, out_addr);
          }
        }
      }
    }
  }

  // Write to HWPE register to indicate completion
  printf("transpose(): Data transpose completed, writing to finished register\n");
  this->datamover_finished = 1;
}

void Datamover::cim_layout_conversion() {   // ToDo: Rewrite HAL to support partial tiles
  printf("cim_layout_conversion(): Starting data conversion from %p to %p, length %d bytes\n", this->in_ptr, this->out_ptr, this->tot_len);
  this->trace.msg(vp::Trace::LEVEL_INFO, "cim_layout_conversion(): Starting data conversion from %p to %p, length %d bytes\n", this->in_ptr, this->out_ptr, this->tot_len);

  update_streamer_config();

  // int num_tiles = this->tot_len / DATAMOVER_BANDWIDTH_ELEMS;
  // int m_tiles = (this->matrix_dim_m + DATAMOVER_BANDWIDTH_ELEMS - 1) / DATAMOVER_BANDWIDTH_ELEMS;
  // int n_tiles = (this->matrix_dim_n + DATAMOVER_BANDWIDTH_ELEMS - 1) / DATAMOVER_BANDWIDTH_ELEMS;
  // int total_elems = this->matrix_dim_m * this->matrix_dim_n;
  // int leftover_rows = this->matrix_dim_m % DATAMOVER_BANDWIDTH_ELEMS;
  // int leftover_cols = this->matrix_dim_n % DATAMOVER_BANDWIDTH_ELEMS;
  // int leftover_elems = total_elems % DATAMOVER_BANDWIDTH_ELEMS;

  // printf("cim_layout_conversion(): matrix_dim_m=%d, matrix_dim_n=%d, total_elems=%d, m_tiles=%d, n_tiles=%d, leftover_rows=%d, leftover_cols=%d, leftover_elems=%d\n",
  //   this->matrix_dim_m, this->matrix_dim_n, total_elems, m_tiles, n_tiles, leftover_rows, leftover_cols, leftover_elems);

  // for(int n_tile_counter = 0; n_tile_counter < n_tiles; n_tile_counter++) {
  //   for(int m_tile_counter = 0; m_tile_counter < m_tiles; m_tile_counter++) {
  //     printf("cim_layout_conversion(): Processing tile (m_tile_counter = %d / %d, n_tile_counter = %d / %d)\n", m_tile_counter+1, m_tiles, n_tile_counter+1, n_tiles);
  //     if((m_tile_counter == m_tiles - 1 && leftover_rows > 0) && (n_tile_counter == n_tiles - 1 && leftover_cols > 0)) {
  //       printf("cim_layout_conversion(): Processing last tile with leftover rows and cols (m_tile=%d / %d, n_tile=%d / %d, leftover_rows=%d, leftover_cols=%d)\n", m_tile_counter+1, m_tiles, n_tile_counter+1, n_tiles, leftover_rows, leftover_cols);
  //       fill_elem_matrix(DATAMOVER_BANDWIDTH_ELEMS);  // Note: fill_elem_matrix(leftover_rows) would be sufficient, but that would introduce problems with the streamer configuration
  //       for(int i = 0; i < leftover_rows; i++) {
  //         this->streamer_out.print_state();
  //         uint32_t out_addr = this->streamer_out.iterate();
  //         this->store_to_memory_functional(out_addr, elem_matrix[i], leftover_cols, nullptr);
  //         printf("cim_layout_conversion(): wrote row %d: 0x...%08x to out_addr(%p)\n", i+1, *(uint64_t*)elem_matrix[i], out_addr);
  //       }
  //       for(int i = leftover_rows; i < DATAMOVER_BANDWIDTH_ELEMS; i++) {
  //         this->streamer_out.iterate();  // skip writing remaining rows of the tile     ToDo: optimize in RTL if possible
  //         printf("cim_layout_conversion(): skipped writing row %d\n", i+1);
  //       }
  //     }
  //     else if(m_tile_counter == m_tiles - 1 && leftover_rows > 0) {  // last tiles in m-dimension with leftover rows
  //       printf("cim_layout_conversion(): Processing last M-tile (%d / %d) with leftover rows (%d)\n", m_tile_counter+1, m_tiles, leftover_rows);
  //       fill_elem_matrix(DATAMOVER_BANDWIDTH_ELEMS);    // Note: fill_elem_matrix(leftover_rows) would be sufficient, but that would introduce problems with the streamer configuration
  //       for(int i = 0; i < leftover_rows; i++) {
  //         this->streamer_out.print_state();
  //         uint32_t out_addr = this->streamer_out.iterate();
  //         this->store_to_memory_functional(out_addr, elem_matrix[i], DATAMOVER_BANDWIDTH_ELEMS, nullptr);
  //         printf("cim_layout_conversion(): wrote row %d: 0x...%08x to out_addr(%p)\n", i+1, *(uint64_t*)elem_matrix[i], out_addr);
  //       }
  //       for(int i = leftover_rows; i < DATAMOVER_BANDWIDTH_ELEMS; i++) {
  //         this->streamer_out.iterate();  // skip writing remaining rows of the tile     ToDo: optimize in RTL if possible
  //         printf("cim_layout_conversion(): skipped writing row %d\n", i+1);
  //       }
  //     }
  //     else if(n_tile_counter == n_tiles - 1 && leftover_cols > 0) {  // last tiles in n-dimension with leftover cols
  //       printf("cim_layout_conversion(): Processing last N-tile (%d / %d) with leftover cols (%d)\n", n_tile_counter+1, n_tiles, leftover_cols);
  //       fill_elem_matrix(DATAMOVER_BANDWIDTH_ELEMS);
  //       for(int i = 0; i < DATAMOVER_BANDWIDTH_ELEMS; i++) {
  //         this->streamer_out.print_state();
  //         uint32_t out_addr = this->streamer_out.iterate();
  //         this->store_to_memory_functional(out_addr, elem_matrix[i], leftover_cols, nullptr);
  //         printf("cim_layout_conversion(): wrote row %d: 0x...%08x to out_addr(%p)\n", i+1, *(uint64_t*)elem_matrix[i], out_addr);
  //       }
  //     }
  //     else {  // complete tiles
  //       printf("cim_layout_conversion(): Processing complete tile (%d,%d)\n", m_tile_counter, n_tile_counter);
  //       fill_elem_matrix(DATAMOVER_BANDWIDTH_ELEMS);
  //       for(int i = 0; i < DATAMOVER_BANDWIDTH_ELEMS; i++) {
  //         this->streamer_out.print_state();
  //         uint32_t out_addr = this->streamer_out.iterate();
  //         this->store_to_memory_functional(out_addr, elem_matrix[i], DATAMOVER_BANDWIDTH_ELEMS, nullptr);
  //         printf("cim_layout_conversion(): wrote row %d: 0x...%08x to out_addr(%p)\n", i+1, *(uint64_t*)elem_matrix[i], out_addr);
  //       }
  //     }
  //   }
  // }

  // Same as copy() ------------------------------
  int num_tiles = this->tot_len / DATAMOVER_BANDWIDTH_ELEMS;
  int leftover_rows = this->tot_len % DATAMOVER_BANDWIDTH_ELEMS;
  int total_elems = this->matrix_dim_m * this->matrix_dim_n;
  int leftover_elems = total_elems % DATAMOVER_BANDWIDTH_ELEMS;

  for(int tile_counter = 0; tile_counter < num_tiles; tile_counter++) {
    printf("cim_layout_conversion(): Processing tile %d\n", tile_counter);
    fill_elem_matrix(DATAMOVER_BANDWIDTH_ELEMS);
    for(int i = 0; i < DATAMOVER_BANDWIDTH_ELEMS; i++) {
      this->streamer_out.print_state();
      uint32_t out_addr = this->streamer_out.iterate();
      this->store_to_memory_functional(out_addr, elem_matrix[i], DATAMOVER_BANDWIDTH_ELEMS, nullptr);
      printf("cim_layout_conversion(): wrote row %d: 0x...%08x to out_addr(%p)\n", i+1, *(uint64_t*)elem_matrix[i], out_addr);
    }
  }

  if(leftover_rows > 0) {
    printf("cim_layout_conversion(): Processing leftover rows, count: %d\n", leftover_rows);
    fill_elem_matrix(leftover_rows);
    for(int i = 0; i < leftover_rows; i++) {
      this->streamer_out.print_state();
      uint32_t out_addr = this->streamer_out.iterate();
      if(i == leftover_rows - 1 && leftover_elems > 0) {  // last row with leftover elements
        this->store_to_memory_functional(out_addr, elem_matrix[i], leftover_elems, nullptr);
        printf("cim_layout_conversion(): wrote leftover row %d with leftover elems %d: 0x...%08x to out_addr(%p)\n", i+1, leftover_elems, *(uint64_t*)elem_matrix[i], out_addr);
      }
      else {
        this->store_to_memory_functional(out_addr, elem_matrix[i], DATAMOVER_BANDWIDTH_ELEMS, nullptr);
        printf("cim_layout_conversion(): wrote leftover row %d: 0x...%08x to out_addr(%p)\n", i+1, *(uint64_t*)elem_matrix[i], out_addr);
      }
      // this->store_to_memory_functional(out_addr, elem_matrix[i], DATAMOVER_BANDWIDTH_ELEMS, nullptr);
      // printf("cim_layout_conversion(): wrote leftover row %d: 0x%08x to out_addr(0x%08x)\n", i, *(uint64_t*)elem_matrix[i], out_addr);
    }
  }
  // --------------------------------------------

  // Write to HWPE register to indicate completion
  printf("cim_layout_conversion(): Data conversion completed, writing to finished register\n");
  this->datamover_finished = 1;
}

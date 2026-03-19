/*
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
 * Authors: Soumyo Bhattacharjee (sbhattacharj@student.ethz.ch)
 *          Cyrill Durrer, ETH Zurich (cdurrer@iis.ee.ethz.ch)
 *
 */

#include <cstring>
#include <vector>
#include <datamover.hpp>

// =========================================================
// Functional TCDM helpers used by GEMM/depthwise load/store paths.
// Address generation stays in streamer/ops code.
// Misalignment handling is done here via `skip_prefix_bytes`:
// callers pass an aligned base address plus prefix bytes to discard.
// =========================================================

// Functional read helper for activation/weight paths.
// If skip_prefix_bytes==0: direct read into destination.
// If skip_prefix_bytes>0: read [prefix + payload] into temp, then copy payload.

void Datamover::load_from_memory_functional(uint32_t addr, uint8_t *data, uint32_t size, uint32_t skip_prefix_bytes, int64_t *latency_out) {
    const uint32_t masked_addr = addr & CLUSTER_MASK;
    int64_t max_latency = 0;

    auto run_reads = [&](uint8_t *dst, uint32_t total_size) {
        uint32_t offset = 0;
        const uint32_t transactions = (total_size + L1_TRANSACTION_SIZE - 1) / L1_TRANSACTION_SIZE;
        for (uint32_t txn = 0; txn < transactions; txn++) {
            const uint32_t txn_size = ((total_size - offset) > L1_TRANSACTION_SIZE)
                                    ? L1_TRANSACTION_SIZE : (total_size - offset);

            this->io_req.init();
            this->io_req.set_addr(masked_addr + offset);
            this->io_req.set_size(txn_size);
            this->io_req.set_data(dst + offset);
            this->io_req.set_is_write(0);

            const int err = this->l1.req(&this->io_req);
            if (err != vp::IO_REQ_OK) {
                this->trace.fatal("Functional memory read failed at 0x%08x\n", masked_addr + offset);
            }
            const int64_t txn_latency = this->io_req.get_latency();
            if (txn_latency > max_latency) {
                max_latency = txn_latency;
            }
            offset += txn_size;
        }
    };

    if (skip_prefix_bytes == 0) {
        run_reads(data, size);
    } else {
        const uint32_t requested_size = size;
        const uint32_t read_size = requested_size + skip_prefix_bytes;
        std::vector<uint8_t> temp(read_size);
        run_reads(temp.data(), read_size);
        std::memcpy(data, temp.data() + skip_prefix_bytes, requested_size);
    }

    if (latency_out != nullptr) {
        *latency_out = max_latency + 1;
    }
}

// Functional write helper for output path.
void Datamover::store_to_memory_functional(uint32_t addr, uint8_t *data, uint32_t size, int64_t *latency_out) {
    const uint32_t masked_addr = addr & CLUSTER_MASK;
    uint32_t offset = 0;
    const uint32_t transactions = (size + L1_TRANSACTION_SIZE - 1) / L1_TRANSACTION_SIZE;
    int64_t max_latency = 0;

    for (uint32_t txn = 0; txn < transactions; txn++) {
        const uint32_t txn_size = ((size - offset) > L1_TRANSACTION_SIZE)
                                ? L1_TRANSACTION_SIZE : (size - offset);

        this->io_req.init();
        this->io_req.set_addr(masked_addr + offset);
        this->io_req.set_size(txn_size);
        this->io_req.set_data(data + offset);
        this->io_req.set_is_write(1);

        const int err = this->l1.req(&this->io_req);
        if (err != vp::IO_REQ_OK) {
            this->trace.fatal("Functional memory write failed at 0x%08x\n", masked_addr + offset);
        }
        const int64_t txn_latency = this->io_req.get_latency();
        if (txn_latency > max_latency) {
            max_latency = txn_latency;
        }
        offset += txn_size;
    }
    if (latency_out != nullptr) {
        *latency_out = max_latency + 1;
    }
}

/*
 * Authors: Soumyo Bhattacharjee (sbhattacharj@student.ethz.ch)
 *          Cyrill Durrer, ETH Zurich (cdurrer@iis.ee.ethz.ch)
 *
 */

#ifndef __DATAMOVER_STREAMER_HPP__
#define __DATAMOVER_STREAMER_HPP__

#include <cstdint>
#include <stdexcept>

#include <datamover.hpp>

// Address generation configuration
struct AddressGenConfig {
    uint32_t base_addr;
    uint32_t d0_len, d1_len, d2_len, d3_len;
    int32_t d0_stride, d1_stride, d2_stride, d3_stride, d4_stride;
    uint32_t tot_len;
    uint8_t dim_enable_1h;

    AddressGenConfig()
        : base_addr(0), d0_len(0), d1_len(1), d2_len(1), d3_len(1),
          d0_stride(0), d1_stride(0), d2_stride(0), d3_stride(0), d4_stride(0),
          tot_len(0), dim_enable_1h(0) {}
};

//========================================
// RTL-accurate HWPE Address Generator
// Closely matches hwpe_stream_addressgen_v4
//
// Misaligned Accesses :    // ToDo(cdurrer)
// - This streamer only generates byte addresses.
// - It does not align addresses or fix unaligned accesses.
// - Unaligned read handling is performed by Datamover::load_from_memory_functional()
//   in datamover_memory.cpp via aligned base address + skip_prefix_bytes.
//========================================

template<typename HwpeType, typename DataType>
class HWPEStreamer {
    private:
        AddressGenConfig cfg_;
        HwpeType* accel_instance_;

        uint32_t d0_addr_q_;
        uint32_t d1_addr_q_;
        uint32_t d2_addr_q_;
        uint32_t d3_addr_q_;
        uint32_t d4_addr_q_;

        uint32_t d0_counter_q_;
        uint32_t d1_counter_q_;
        uint32_t d2_counter_q_;
        uint32_t d3_counter_q_;
        uint32_t d4_counter_q_;

        uint32_t overall_counter_q_;

        // Compute current address
        uint32_t compute_address() const {
            return cfg_.base_addr + d4_addr_q_ + d3_addr_q_ + d2_addr_q_ + d1_addr_q_ + d0_addr_q_;
        }

    public:
        HWPEStreamer(HwpeType* accel = nullptr)
            : accel_instance_(accel) {
            reset();
        }

        void reset() {
            d0_addr_q_ = 0;
            d1_addr_q_ = 0;
            d2_addr_q_ = 0;
            d3_addr_q_ = 0;
            d4_addr_q_ = 0;
            d0_counter_q_ = 1;      // ToDo: different to RTL where counters start at 0
            d1_counter_q_ = 1;
            d2_counter_q_ = 1;
            d3_counter_q_ = 1;
            d4_counter_q_ = 1;

            overall_counter_q_ = 0;
        }

        void update_params(uint32_t base_addr,
                        int32_t d0_stride, int32_t d1_stride, int32_t d2_stride, int32_t d3_stride, int32_t d4_stride,
                        uint32_t d0_len, uint32_t d1_len, uint32_t d2_len, uint32_t d3_len,
                        uint32_t tot_len, uint8_t dim_enable_1h) {
            cfg_.base_addr = base_addr;
            cfg_.d0_stride = d0_stride;
            cfg_.d1_stride = d1_stride;
            cfg_.d2_stride = d2_stride;
            cfg_.d3_stride = d3_stride;
            cfg_.d4_stride = d4_stride;
            cfg_.d0_len = d0_len;
            cfg_.d1_len = d1_len;
            cfg_.d2_len = d2_len;
            cfg_.d3_len = d3_len;
            cfg_.tot_len = tot_len;
            cfg_.dim_enable_1h = dim_enable_1h;
            reset();
        }

        // Set associated accelerator instance for logging
        void set_accel(HwpeType* accel) {
            accel_instance_ = accel;
        }

        // Generate next address and advance state
        uint32_t iterate() {
            if (is_done()) {
                if (accel_instance_) {
                    accel_instance_->trace.fatal("Address generator already complete! Count=%d, Tot=%d\n",
                                                overall_counter_q_, cfg_.tot_len);
                }
                throw std::runtime_error("Address generation complete!");
            }

            uint32_t d0_addr_d         = d0_addr_q_;
            uint32_t d1_addr_d         = d1_addr_q_;
            uint32_t d2_addr_d         = d2_addr_q_;
            uint32_t d3_addr_d         = d3_addr_q_;
            uint32_t d4_addr_d         = d4_addr_q_;
            uint32_t d0_counter_d      = d0_counter_q_;
            uint32_t d1_counter_d      = d1_counter_q_;
            uint32_t d2_counter_d      = d2_counter_q_;
            uint32_t d3_counter_d      = d3_counter_q_;
            uint32_t d4_counter_d      = d4_counter_q_;
            uint32_t overall_counter_d = overall_counter_q_;

            // Compute current address from _q state (before update)
            uint32_t address = compute_address();

            if ((d0_counter_q_ < cfg_.d0_len) || !(cfg_.dim_enable_1h & 0x1)) {
                // Advance d0
                d0_addr_d    = d0_addr_q_ + cfg_.d0_stride;
                d0_counter_d = d0_counter_q_ + 1;
            }
            else if ((d1_counter_q_ < cfg_.d1_len) || !(cfg_.dim_enable_1h & 0x2)) {
                // Advance d1
                d0_addr_d    = 0;
                d1_addr_d    = d1_addr_q_ + cfg_.d1_stride;
                d0_counter_d = 1;
                d1_counter_d = d1_counter_q_ + 1;
            }
            else if ((d2_counter_q_ < cfg_.d2_len) || !(cfg_.dim_enable_1h & 0x4)) {
                // Advance d2
                d0_addr_d    = 0;
                d1_addr_d    = 0;
                d2_addr_d    = d2_addr_q_ + cfg_.d2_stride;
                d0_counter_d = 1;
                d1_counter_d = 1;
                d2_counter_d = d2_counter_q_ + 1;
            }
            else if ((d3_counter_q_ < cfg_.d3_len) || !(cfg_.dim_enable_1h & 0x8)) {
                // Advance d3
                d0_addr_d    = 0;
                d1_addr_d    = 0;
                d2_addr_d    = 0;
                d3_addr_d    = d3_addr_q_ + cfg_.d3_stride;
                d0_counter_d = 1;
                d1_counter_d = 1;
                d2_counter_d = 1;
                d3_counter_d = d3_counter_q_ + 1;
            }
            else {
                // Advance d4
                d0_addr_d    = 0;
                d1_addr_d    = 0;
                d2_addr_d    = 0;
                d3_addr_d    = 0;
                d4_addr_d    = d4_addr_q_ + cfg_.d4_stride;
                d0_counter_d = 1;
                d1_counter_d = 1;
                d2_counter_d = 1;
                d3_counter_d = 1;
                d4_counter_d = d4_counter_q_ + 1;
            }
            overall_counter_d = overall_counter_q_ + 1;

            // "Clock edge": commit _d into _q_
            d0_addr_q_         = d0_addr_d;
            d1_addr_q_         = d1_addr_d;
            d2_addr_q_         = d2_addr_d;
            d3_addr_q_         = d3_addr_d;
            d4_addr_q_         = d4_addr_d;
            d0_counter_q_      = d0_counter_d;
            d1_counter_q_      = d1_counter_d;
            d2_counter_q_      = d2_counter_d;
            d3_counter_q_      = d3_counter_d;
            d4_counter_q_      = d4_counter_d;
            overall_counter_q_ = overall_counter_d;

            return address;
        }

        // Check if address generation is complete
        bool is_done() const {
            return overall_counter_q_ >= cfg_.tot_len;
        }

        // Current number of transactions generated
        uint32_t get_count() const {
            return overall_counter_q_;
        }

        // Total number of transactions to be generated
        uint32_t get_total() const {
            return cfg_.tot_len;
        }

        // Print current state for debugging
        void print_state() const {
            accel_instance_->trace.msg("[STREAMER] Count: %d/%d\n", overall_counter_q_, cfg_.tot_len);
            accel_instance_->trace.msg("[STREAMER] d0: addr=%d cnt=%d/%d\n", d0_addr_q_, d0_counter_q_, cfg_.d0_len);
            accel_instance_->trace.msg("[STREAMER] d1: addr=%d cnt=%d/%d\n", d1_addr_q_, d1_counter_q_, cfg_.d1_len);
            accel_instance_->trace.msg("[STREAMER] d2: addr=%d cnt=%d/%d\n", d2_addr_q_, d2_counter_q_, cfg_.d2_len);
            accel_instance_->trace.msg("[STREAMER] d3: addr=%d cnt=%d/%d\n", d3_addr_q_, d3_counter_q_, cfg_.d3_len);
            accel_instance_->trace.msg("[STREAMER] d4: addr=%d cnt=%d\n", d4_addr_q_, d4_counter_q_);
            accel_instance_->trace.msg("[STREAMER] Current addr: 0x%08x\n", compute_address());
        }

        // Print configuration for debugging
        void print_config() const {
            accel_instance_->trace.msg("[STREAMER_CFG] Base: 0x%08x\n", cfg_.base_addr);
            accel_instance_->trace.msg("[STREAMER_CFG] Tot len: %d\n", cfg_.tot_len);
            accel_instance_->trace.msg("[STREAMER_CFG] Dim enable: 0x%x\n", cfg_.dim_enable_1h);
            accel_instance_->trace.msg("[STREAMER_CFG] d0: len=%d stride=%d\n", cfg_.d0_len, cfg_.d0_stride);
            accel_instance_->trace.msg("[STREAMER_CFG] d1: len=%d stride=%d\n", cfg_.d1_len, cfg_.d1_stride);
            accel_instance_->trace.msg("[STREAMER_CFG] d2: len=%d stride=%d\n", cfg_.d2_len, cfg_.d2_stride);
            accel_instance_->trace.msg("[STREAMER_CFG] d3: len=%d stride=%d\n", cfg_.d3_len, cfg_.d3_stride);
            accel_instance_->trace.msg("[STREAMER_CFG] d4: stride=%d\n", cfg_.d4_stride);
        }
};

#endif

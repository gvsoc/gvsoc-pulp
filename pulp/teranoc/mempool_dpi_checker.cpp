/*
 * Copyright (C) 2026 ETH Zurich and University of Bologna
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
 * Description: GVSoC native checker for the MemPool/TeraNoC DPI check ABI.
 * Author: Yinrong Li (ETH Zurich) (yinrli@student.ethz.ch)
 */

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <vp/vp.hpp>
#include <vp/itf/wire.hpp>


enum
{
    MEMPOOL_DPI_CHECK_MAX      = 32,
    MEMPOOL_DPI_CHECK_DESC_SZ  = 24,
    MEMPOOL_DPI_CHECK_TYPE_I8  = 1,
    MEMPOOL_DPI_CHECK_TYPE_I16 = 2,
    MEMPOOL_DPI_CHECK_TYPE_I32 = 3,
    MEMPOOL_DPI_CHECK_TYPE_F8  = 4,
    MEMPOOL_DPI_CHECK_TYPE_F16 = 5,
    MEMPOOL_DPI_CHECK_TYPE_F32 = 6
};


class MempoolDpiChecker : public vp::Component
{
public:
    MempoolDpiChecker(vp::ComponentConf &config);

private:
    static void check_sync_back(vp::Block *__this, int *errors);

    uint8_t read_l2_byte(uint64_t addr);
    void read_l2_bytes(uint64_t addr, size_t length, std::vector<uint8_t> &buffer);
    uint32_t read_l2_u32(uint64_t addr);
    int run_checks();

    int compare_i8(const uint8_t *result, const uint8_t *golden, int count,
        int tolerance, bool verbose);
    int compare_i16(const uint8_t *result, const uint8_t *golden, int count,
        int tolerance, bool verbose);
    int compare_i32(const uint8_t *result, const uint8_t *golden, int count,
        int tolerance, bool verbose);
    int compare_f8(const uint8_t *result, const uint8_t *golden, int count,
        uint8_t tolerance, bool verbose);
    int compare_f16(const uint8_t *result, const uint8_t *golden, int count,
        float tolerance, bool verbose);
    int compare_f32(const uint8_t *result, const uint8_t *golden, int count,
        float tolerance, bool verbose);

    static uint16_t load_u16(const uint8_t *data);
    static uint32_t load_u32(const uint8_t *data);
    static float bits_to_f32(uint32_t bits);
    static float fp16_to_float(uint16_t value);
    static float fp8_to_float(uint8_t value);
    static unsigned int clog2_floor_power2(uint64_t value);
    static uint64_t bit_mask(unsigned int bits);
    static unsigned int elem_size_for_type(uint32_t check_type);

    vp::Trace trace;
    vp::WireSlave<int> input_itf;
    std::vector<vp::WireMaster<void *>> meminfo_itfs;
    std::vector<uint8_t *> bank_data;

    uint32_t nb_banks;
    uint32_t bank_width;
    uint32_t interleave;
    uint32_t l2_base;
    uint32_t l2_size;
    uint32_t bank_size;
    uint32_t check_count_addr;
    uint32_t check_table_addr;
    unsigned int l2_addr_bits;
    unsigned int constant_bits;
    unsigned int scramble_bits;
};


MempoolDpiChecker::MempoolDpiChecker(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->input_itf.set_sync_back_meth(&MempoolDpiChecker::check_sync_back);
    this->new_slave_port("input", &this->input_itf);

    this->nb_banks = this->get_js_config()->get_child_int("nb_banks");
    this->bank_width = this->get_js_config()->get_child_int("bank_width");
    this->interleave = this->get_js_config()->get_child_int("interleave");
    this->l2_base = this->get_js_config()->get_uint("l2_base");
    this->l2_size = this->get_js_config()->get_uint("l2_size");
    this->check_count_addr = this->get_js_config()->get_uint("check_count_addr");
    this->check_table_addr = this->get_js_config()->get_uint("check_table_addr");

    if (this->nb_banks == 0 || this->bank_width == 0 || this->interleave == 0)
    {
        this->trace.fatal("Invalid DPI checker geometry: banks=%u bank_width=%u interleave=%u\n",
            this->nb_banks, this->bank_width, this->interleave);
    }
    if ((this->l2_size % this->nb_banks) != 0)
    {
        this->trace.fatal("L2 size 0x%x is not divisible by %u banks\n",
            this->l2_size, this->nb_banks);
    }

    this->bank_size = this->l2_size / this->nb_banks;
    this->l2_addr_bits = MempoolDpiChecker::clog2_floor_power2(this->l2_size);
    this->constant_bits =
        MempoolDpiChecker::clog2_floor_power2((uint64_t)this->bank_width * this->interleave);
    this->scramble_bits = this->nb_banks == 1 ?
        1 : MempoolDpiChecker::clog2_floor_power2(this->nb_banks);
    if (this->constant_bits + this->scramble_bits > this->l2_addr_bits)
    {
        this->trace.fatal("Invalid DPI checker L2 mapping geometry\n");
    }

    this->meminfo_itfs.resize(this->nb_banks);
    this->bank_data.resize(this->nb_banks);
    for (uint32_t i = 0; i < this->nb_banks; i++)
    {
        this->new_master_port("meminfo_" + std::to_string(i), &this->meminfo_itfs[i]);
    }
}


void MempoolDpiChecker::check_sync_back(vp::Block *__this, int *errors)
{
    MempoolDpiChecker *_this = (MempoolDpiChecker *)__this;
    *errors = _this->run_checks();
}


unsigned int MempoolDpiChecker::clog2_floor_power2(uint64_t value)
{
    unsigned int result = 0;
    while (value > 1)
    {
        value >>= 1;
        result++;
    }
    return result;
}


uint64_t MempoolDpiChecker::bit_mask(unsigned int bits)
{
    return bits >= 64 ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
}


unsigned int MempoolDpiChecker::elem_size_for_type(uint32_t check_type)
{
    switch (check_type)
    {
        case MEMPOOL_DPI_CHECK_TYPE_I8:
        case MEMPOOL_DPI_CHECK_TYPE_F8:
            return 1;
        case MEMPOOL_DPI_CHECK_TYPE_I16:
        case MEMPOOL_DPI_CHECK_TYPE_F16:
            return 2;
        case MEMPOOL_DPI_CHECK_TYPE_I32:
        case MEMPOOL_DPI_CHECK_TYPE_F32:
            return 4;
        default:
            return 0;
    }
}


uint16_t MempoolDpiChecker::load_u16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}


uint32_t MempoolDpiChecker::load_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}


float MempoolDpiChecker::bits_to_f32(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}


float MempoolDpiChecker::fp16_to_float(uint16_t value)
{
    const float sign = (value & 0x8000) ? -1.0f : 1.0f;
    const int exp = (value >> 10) & 0x1f;
    const int mant = value & 0x3ff;

    if (exp == 0)
    {
        return sign * std::ldexp((float)mant, -24);
    }
    if (exp == 0x1f)
    {
        return mant == 0 ? sign * std::numeric_limits<float>::infinity()
                         : std::numeric_limits<float>::quiet_NaN();
    }
    return sign * std::ldexp((float)(0x400 | mant), exp - 25);
}


float MempoolDpiChecker::fp8_to_float(uint8_t value)
{
    const float sign = (value & 0x80) ? -1.0f : 1.0f;
    const int exp = (value >> 2) & 0x1f;
    const int mant = value & 0x3;

    if (exp == 0)
    {
        return sign * std::ldexp((float)mant, -16);
    }
    if (exp == 0x1f)
    {
        return mant == 0 ? sign * std::numeric_limits<float>::infinity()
                         : std::numeric_limits<float>::quiet_NaN();
    }
    return sign * std::ldexp((float)(0x4 | mant), exp - 17);
}


uint8_t MempoolDpiChecker::read_l2_byte(uint64_t addr)
{
    uint64_t l2_limit = (uint64_t)this->l2_base + this->l2_size;
    if (addr < this->l2_base || addr >= l2_limit)
    {
        this->trace.fatal("[DPI_CHECK] L2 read address 0x%08lx is outside L2\n",
            (unsigned long)addr);
    }

    uint64_t rel_addr = addr - this->l2_base;
    uint32_t bank_id = 0;
    uint64_t high_field = rel_addr >> (this->constant_bits + this->scramble_bits);
    uint64_t local_offset = (rel_addr & MempoolDpiChecker::bit_mask(this->constant_bits)) |
        (high_field << this->constant_bits);

    if (this->nb_banks != 1)
    {
        bank_id = (rel_addr >> this->constant_bits) &
            MempoolDpiChecker::bit_mask(this->scramble_bits);
    }
    else
    {
        unsigned int high_field_bits =
            this->l2_addr_bits - this->constant_bits - this->scramble_bits;
        uint64_t low_field = (rel_addr >> this->constant_bits) &
            MempoolDpiChecker::bit_mask(this->scramble_bits);
        local_offset |= low_field << (this->constant_bits + high_field_bits);
    }

    if (bank_id >= this->nb_banks)
    {
        this->trace.fatal("[DPI_CHECK] L2 bank index %u is out of range for address 0x%08lx\n",
            bank_id, (unsigned long)addr);
    }
    if (local_offset >= this->bank_size)
    {
        this->trace.fatal("[DPI_CHECK] L2 local offset 0x%lx is out of range for bank %u\n",
            (unsigned long)local_offset, bank_id);
    }
    if (this->bank_data[bank_id] == nullptr)
    {
        this->trace.fatal("[DPI_CHECK] L2 bank %u memory pointer is null\n", bank_id);
    }

    return this->bank_data[bank_id][local_offset];
}


void MempoolDpiChecker::read_l2_bytes(uint64_t addr, size_t length,
    std::vector<uint8_t> &buffer)
{
    buffer.resize(length);
    for (size_t i = 0; i < length; i++)
    {
        buffer[i] = this->read_l2_byte(addr + i);
    }
}


uint32_t MempoolDpiChecker::read_l2_u32(uint64_t addr)
{
    std::vector<uint8_t> buffer;
    this->read_l2_bytes(addr, 4, buffer);
    return MempoolDpiChecker::load_u32(buffer.data());
}


int MempoolDpiChecker::compare_i8(const uint8_t *result, const uint8_t *golden,
    int count, int tolerance, bool verbose)
{
    int errors = 0;
    for (int i = 0; i < count; i++)
    {
        int exp = (int)(int8_t)golden[i];
        int res = (int)(int8_t)result[i];
        int diff = exp - res;
        bool error = (diff > tolerance) || (diff < -tolerance);
        if (error)
        {
            errors++;
        }
        if (error || verbose)
        {
            std::printf("CHECK(%d): EXP = %02X - RESP = %02X\n", i, golden[i], result[i]);
        }
    }
    return errors;
}


int MempoolDpiChecker::compare_i16(const uint8_t *result, const uint8_t *golden,
    int count, int tolerance, bool verbose)
{
    int errors = 0;
    for (int i = 0; i < count; i++)
    {
        uint16_t exp_bits = MempoolDpiChecker::load_u16(&golden[2 * i]);
        uint16_t res_bits = MempoolDpiChecker::load_u16(&result[2 * i]);
        int diff = (int)(int16_t)exp_bits - (int)(int16_t)res_bits;
        bool error = (diff > tolerance) || (diff < -tolerance);
        if (error)
        {
            errors++;
        }
        if (error || verbose)
        {
            std::printf("CHECK(%d): EXP = %04X - RESP = %04X\n", i, exp_bits, res_bits);
        }
    }
    return errors;
}


int MempoolDpiChecker::compare_i32(const uint8_t *result, const uint8_t *golden,
    int count, int tolerance, bool verbose)
{
    int errors = 0;
    for (int i = 0; i < count; i++)
    {
        uint32_t exp_bits = MempoolDpiChecker::load_u32(&golden[4 * i]);
        uint32_t res_bits = MempoolDpiChecker::load_u32(&result[4 * i]);
        int64_t diff = (int64_t)(int32_t)exp_bits - (int64_t)(int32_t)res_bits;
        bool error = (diff > tolerance) || (diff < -tolerance);
        if (error)
        {
            errors++;
        }
        if (error || verbose)
        {
            std::printf("CHECK(%d): EXP = %08X - RESP = %08X\n", i, exp_bits, res_bits);
        }
    }
    return errors;
}


int MempoolDpiChecker::compare_f8(const uint8_t *result, const uint8_t *golden,
    int count, uint8_t tolerance, bool verbose)
{
    int errors = 0;
    float tol = MempoolDpiChecker::fp8_to_float(tolerance);
    for (int i = 0; i < count; i++)
    {
        float diff = MempoolDpiChecker::fp8_to_float(result[i]) -
            MempoolDpiChecker::fp8_to_float(golden[i]);
        bool error = (diff > tol) || (diff < -tol);
        if (error)
        {
            errors++;
        }
        if (error || verbose)
        {
            std::printf("CHECK(%d): EXP = %02X - RESP = %02X\n", i, golden[i], result[i]);
        }
    }
    return errors;
}


int MempoolDpiChecker::compare_f16(const uint8_t *result, const uint8_t *golden,
    int count, float tolerance, bool verbose)
{
    int errors = 0;
    for (int i = 0; i < count; i++)
    {
        uint16_t exp_bits = MempoolDpiChecker::load_u16(&golden[2 * i]);
        uint16_t res_bits = MempoolDpiChecker::load_u16(&result[2 * i]);
        float diff = MempoolDpiChecker::fp16_to_float(res_bits) -
            MempoolDpiChecker::fp16_to_float(exp_bits);
        bool error = (diff > tolerance) || (diff < -tolerance);
        if (error)
        {
            errors++;
        }
        if (error || verbose)
        {
            std::printf("CHECK(%d): EXP = %08X - RESP = %08X\n", i, exp_bits, res_bits);
        }
    }
    return errors;
}


int MempoolDpiChecker::compare_f32(const uint8_t *result, const uint8_t *golden,
    int count, float tolerance, bool verbose)
{
    int errors = 0;
    for (int i = 0; i < count; i++)
    {
        uint32_t exp_bits = MempoolDpiChecker::load_u32(&golden[4 * i]);
        uint32_t res_bits = MempoolDpiChecker::load_u32(&result[4 * i]);
        float diff = MempoolDpiChecker::bits_to_f32(res_bits) -
            MempoolDpiChecker::bits_to_f32(exp_bits);
        bool error = (diff > tolerance) || (diff < -tolerance);
        if (error)
        {
            errors++;
        }
        if (error || verbose)
        {
            std::printf("CHECK(%d): EXP = %08X - RESP = %08X\n", i, exp_bits, res_bits);
        }
    }
    return errors;
}


int MempoolDpiChecker::run_checks()
{
    if (this->check_count_addr == 0 || this->check_table_addr == 0)
    {
        return 0;
    }

    for (uint32_t i = 0; i < this->nb_banks; i++)
    {
        void *ptr = nullptr;
        this->meminfo_itfs[i].sync_back(&ptr);
        this->bank_data[i] = (uint8_t *)ptr;
    }

    uint32_t check_count = this->read_l2_u32(this->check_count_addr);
    if (check_count == 0)
    {
        return 0;
    }
    if (check_count > MEMPOOL_DPI_CHECK_MAX)
    {
        this->trace.fatal("[DPI_CHECK] Descriptor count %u exceeds max %u\n",
            check_count, MEMPOOL_DPI_CHECK_MAX);
    }

    std::vector<uint8_t> desc_buffer;
    std::vector<uint8_t> result_buffer;
    std::vector<uint8_t> golden_buffer;
    uint32_t total_checks = 0;
    int total_errors = 0;

    for (uint32_t i = 0; i < check_count; i++)
    {
        this->read_l2_bytes(this->check_table_addr + i * MEMPOOL_DPI_CHECK_DESC_SZ,
            MEMPOOL_DPI_CHECK_DESC_SZ, desc_buffer);

        uint32_t check_type = MempoolDpiChecker::load_u32(&desc_buffer[0]);
        uint32_t elements = MempoolDpiChecker::load_u32(&desc_buffer[4]);
        uint32_t tolerance = MempoolDpiChecker::load_u32(&desc_buffer[8]);
        uint32_t result_addr = MempoolDpiChecker::load_u32(&desc_buffer[12]);
        uint32_t golden_addr = MempoolDpiChecker::load_u32(&desc_buffer[16]);
        uint32_t verbose = MempoolDpiChecker::load_u32(&desc_buffer[20]);

        unsigned int elem_size = MempoolDpiChecker::elem_size_for_type(check_type);
        if (elem_size == 0)
        {
            this->trace.fatal("[DPI_CHECK] Descriptor %u has unsupported type %u\n",
                i, check_type);
        }

        uint64_t nbytes_64 = (uint64_t)elements * elem_size;
        if (nbytes_64 > (uint64_t)std::numeric_limits<size_t>::max())
        {
            this->trace.fatal("[DPI_CHECK] Descriptor %u byte count is too large\n", i);
        }
        size_t nbytes = (size_t)nbytes_64;
        this->read_l2_bytes(result_addr, nbytes, result_buffer);
        this->read_l2_bytes(golden_addr, nbytes, golden_buffer);

        int errors = 0;
        switch (check_type)
        {
            case MEMPOOL_DPI_CHECK_TYPE_I8:
                errors = this->compare_i8(result_buffer.data(), golden_buffer.data(),
                    elements, (int16_t)tolerance, verbose != 0);
                break;
            case MEMPOOL_DPI_CHECK_TYPE_I16:
                errors = this->compare_i16(result_buffer.data(), golden_buffer.data(),
                    elements, (int16_t)tolerance, verbose != 0);
                break;
            case MEMPOOL_DPI_CHECK_TYPE_I32:
                errors = this->compare_i32(result_buffer.data(), golden_buffer.data(),
                    elements, (int32_t)tolerance, verbose != 0);
                break;
            case MEMPOOL_DPI_CHECK_TYPE_F8:
                errors = this->compare_f8(result_buffer.data(), golden_buffer.data(),
                    elements, (uint8_t)tolerance, verbose != 0);
                break;
            case MEMPOOL_DPI_CHECK_TYPE_F16:
                errors = this->compare_f16(result_buffer.data(), golden_buffer.data(),
                    elements, MempoolDpiChecker::bits_to_f32(tolerance), verbose != 0);
                break;
            case MEMPOOL_DPI_CHECK_TYPE_F32:
                errors = this->compare_f32(result_buffer.data(), golden_buffer.data(),
                    elements, MempoolDpiChecker::bits_to_f32(tolerance), verbose != 0);
                break;
            default:
                std::printf("[DPI_CHECK] Unsupported check type %u\n", check_type);
                errors = elements;
                break;
        }

        std::printf("[DPI_CHECK] Check %u: %d ERRORS out of %u CHECKS\n",
            i, errors, elements);
        total_errors += errors;
        total_checks += elements;
    }

    std::printf("[DPI_CHECK] %d ERRORS out of %u CHECKS\n", total_errors, total_checks);
    return total_errors;
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new MempoolDpiChecker(config);
}

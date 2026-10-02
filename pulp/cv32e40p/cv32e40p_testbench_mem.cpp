// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

/*
 * Memory of the CV32E40P UVM testbench of core-v-verif outside the main memory and the debug
 * memory, with the virtual peripherals of the testbench (cv32e40p/env/uvme/uvme_cv32e40p_env.sv).
 * As in the OBI memory agent of the testbench (uvma_obi_memory_slv_seq), an access at the address
 * of a peripheral register goes to the peripheral, and any other access goes to the memory, which
 * reads 0 where it was never written. The router keeps the absolute address. The registers are:
 *   0x10000000 - 0x10000028  print: the low byte of a write goes to stdout
 *   0x15000000, 0x15000004   interrupt timer: interrupt lines, delay
 *   0x15000008               debug control
 *   0x15001000               random number
 *   0x15001004               cycle counter: a read gives it, a write sets it
 *   0x15001008               cycle counter print
 *   0x20000000               test status: 123456789 passed, 1 failed
 *   0x20000004               exit: the value written is the exit status
 *   0x20000008, 0x2000000C   signature start and end addresses
 *   0x20000010               signature write: ends the test with status 0
 * A register reads 0, except the random number and the cycle counter, and a write to it does not
 * reach the memory. This testbench has no interrupt or debug request source, so the interrupt
 * timer and the debug control ignore their writes. The random numbers are a fixed sequence and the
 * signature is not dumped. The first status, exit or signature write ends the simulation, as it
 * ends the UVM test, unless stop_on_exit is false.
 */

#include <cstdint>
#include <cstdio>
#include <random>
#include <unordered_map>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <pulp/cv32e40p/cv32e40p_testbench_mem/cv32e40p_testbench_mem_config.hpp>

#define CV32E40P_TB_PRINT           0x10000000
#define CV32E40P_TB_PRINT_END       0x1000002C
#define CV32E40P_TB_IRQ_TIMER_LINES 0x15000000
#define CV32E40P_TB_IRQ_TIMER_DELAY 0x15000004
#define CV32E40P_TB_DEBUG_CONTROL   0x15000008
#define CV32E40P_TB_RAND_NUM        0x15001000
#define CV32E40P_TB_CYCLE_COUNTER   0x15001004
#define CV32E40P_TB_CYCLE_PRINT     0x15001008
#define CV32E40P_TB_TEST_STATUS     0x20000000
#define CV32E40P_TB_EXIT            0x20000004
#define CV32E40P_TB_SIG_START       0x20000008
#define CV32E40P_TB_SIG_END         0x2000000C
#define CV32E40P_TB_SIG_WRITE       0x20000010

#define CV32E40P_TB_TEST_PASSED 123456789
#define CV32E40P_TB_TEST_FAILED 1

class Cv32e40pTestbenchMem : public vp::Component
{
public:
    Cv32e40pTestbenchMem(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static bool is_register(uint64_t address);
    uint32_t register_read(uint64_t address);
    void register_write(uint64_t address, uint32_t value);
    void print(const char *text, int size);
    void end(int status);

    Cv32e40pTestbenchMemConfig cfg;
    vp::Trace   trace;
    vp::IoSlave in{&Cv32e40pTestbenchMem::req};
    std::unordered_map<uint64_t, uint8_t> bytes;
    std::mt19937 rand_num;
    int64_t cycle_counter_start = 0;
};

Cv32e40pTestbenchMem::Cv32e40pTestbenchMem(vp::ComponentConf &config)
    : vp::Component(config, this->cfg)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->new_slave_port("input", &this->in);
}

bool Cv32e40pTestbenchMem::is_register(uint64_t address)
{
    if (address >= CV32E40P_TB_PRINT && address < CV32E40P_TB_PRINT_END)
    {
        return (address & 3) == 0;
    }

    switch (address)
    {
        case CV32E40P_TB_IRQ_TIMER_LINES:
        case CV32E40P_TB_IRQ_TIMER_DELAY:
        case CV32E40P_TB_DEBUG_CONTROL:
        case CV32E40P_TB_RAND_NUM:
        case CV32E40P_TB_CYCLE_COUNTER:
        case CV32E40P_TB_CYCLE_PRINT:
        case CV32E40P_TB_TEST_STATUS:
        case CV32E40P_TB_EXIT:
        case CV32E40P_TB_SIG_START:
        case CV32E40P_TB_SIG_END:
        case CV32E40P_TB_SIG_WRITE:
            return true;
        default:
            return false;
    }
}

vp::IoReqStatus Cv32e40pTestbenchMem::req(vp::Block *__this, vp::IoReq *req)
{
    Cv32e40pTestbenchMem *_this = (Cv32e40pTestbenchMem *)__this;
    uint64_t address = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();

    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Access (address: 0x%lx, size: 0x%lx, is_write: %d)\n",
        address, size, req->get_is_write());

    if (req->get_opcode() != vp::IoReqOpcode::READ && req->get_opcode() != vp::IoReqOpcode::WRITE)
    {
        // The core has no A extension.
        req->set_resp_status(vp::IO_RESP_INVALID);
        return vp::IO_REQ_DONE;
    }

    if (is_register(address))
    {
        if (req->get_is_write())
        {
            uint32_t value = 0;
            for (uint64_t i = 0; i < size && i < 4; i++)
            {
                value |= (uint32_t)data[i] << (8 * i);
            }
            _this->register_write(address, value);
        }
        else
        {
            uint32_t value = _this->register_read(address);
            for (uint64_t i = 0; i < size; i++)
            {
                data[i] = i < 4 ? (uint8_t)(value >> (8 * i)) : 0;
            }
        }
        return vp::IO_REQ_DONE;
    }

    if (req->get_is_write())
    {
        for (uint64_t i = 0; i < size; i++)
        {
            _this->bytes[address + i] = data[i];
        }
    }
    else
    {
        for (uint64_t i = 0; i < size; i++)
        {
            auto it = _this->bytes.find(address + i);
            data[i] = it != _this->bytes.end() ? it->second : 0;
        }
    }

    return vp::IO_REQ_DONE;
}

uint32_t Cv32e40pTestbenchMem::register_read(uint64_t address)
{
    switch (address)
    {
        case CV32E40P_TB_RAND_NUM:
            return (uint32_t)this->rand_num();
        case CV32E40P_TB_CYCLE_COUNTER:
            return (uint32_t)(this->clock.get_cycles() - this->cycle_counter_start);
        default:
            return 0;
    }
}

void Cv32e40pTestbenchMem::register_write(uint64_t address, uint32_t value)
{
    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Register write (address: 0x%lx, value: 0x%x)\n",
        address, value);

    switch (address)
    {
        case CV32E40P_TB_CYCLE_COUNTER:
            this->cycle_counter_start = this->clock.get_cycles() - value;
            break;
        case CV32E40P_TB_CYCLE_PRINT:
        {
            char text[64];
            int size = snprintf(text, sizeof(text), "Cycle count is %ld\n",
                (long)(this->clock.get_cycles() - this->cycle_counter_start));
            this->print(text, size);
            break;
        }
        case CV32E40P_TB_TEST_STATUS:
            if (value == CV32E40P_TB_TEST_PASSED)
            {
                this->end(0);
            }
            else if (value == CV32E40P_TB_TEST_FAILED)
            {
                this->end(1);
            }
            break;
        case CV32E40P_TB_EXIT:
            this->end((int)value);
            break;
        case CV32E40P_TB_SIG_WRITE:
            this->end(0);
            break;
        case CV32E40P_TB_IRQ_TIMER_LINES:
        case CV32E40P_TB_IRQ_TIMER_DELAY:
        case CV32E40P_TB_DEBUG_CONTROL:
        case CV32E40P_TB_RAND_NUM:
        case CV32E40P_TB_SIG_START:
        case CV32E40P_TB_SIG_END:
            break;
        default:
        {
            // One of the print registers.
            char c = (char)(value & 0xff);
            this->print(&c, 1);
            break;
        }
    }
}

void Cv32e40pTestbenchMem::print(const char *text, int size)
{
    if (this->cfg.print_stdout)
    {
        this->stdout_write(text, size);
    }
}

void Cv32e40pTestbenchMem::end(int status)
{
    if (this->cfg.stop_on_exit)
    {
        this->time.get_engine()->quit(status);
    }
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Cv32e40pTestbenchMem(config);
}

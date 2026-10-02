// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

// CV32E40P co-simulation interface.
//
// A checker uses it to run this core in lock-step with the RTL, without knowing the Iss
// class. The core publishes one record per executed instruction. The checker only gives it
// inputs: the pin levels, the points where the RTL sampled them, the points where the RTL
// decided on interrupts and debug, and the data of the loads from memory that the model does
// not own. No architectural state goes from the checker into the core.
//
// The ABI is made of plain structs and pure virtual methods. A minor version may append
// fields to a struct and methods to the class. Every struct starts with struct_size, the size
// the caller was compiled with, and the core never accesses beyond it. Any other change needs
// a new major version, with a new acquire symbol.
//
// The protocol assumes one hart, reset only at time 0. The checker calls configure() once,
// before anything else. Then, in RTL time order, it calls input() for each pin change,
// sample() for each sample of a domain, and opportunity() for each decision point, which
// names by its sequence the instruction it precedes. The core applies them in call order
// when that instruction is the next one, so it decides at the same point as the RTL and with
// the same samples.
//
// arm(n) lets the core execute up to sequence n, once the decision points of these
// instructions are reported. A checker that arms on retire does so, since the RTL decides on
// an instruction before it retires it. An instruction without a decision point is an error.
// poll() returns the records and the boundaries (interrupt take, debug entry, wake-up) in
// program order. REJECTED and ERROR are final and mean that the test fails.
//
// The counters are not in the records, because an instruction is checked on the value it
// reads. A CSR declared volatile (volatile_csr()) depends on timing that the model does not
// reproduce, so the value the RTL read is an input of the instruction (volatile_read()).
//
// A load from an external region (configure(), region()), such as a testbench peripheral,
// completes with any data. Its record waits at the head of the events (EXTERNAL_LOAD,
// external_load_query()) until external_load_response() gives the data the RTL loaded. No
// later instruction has executed, so none has read the destination register yet.

#pragma once

#include <stdint.h>

#define CV32E40P_COSIM_MAJOR 1
#define CV32E40P_COSIM_MINOR 0
#define CV32E40P_COSIM_ACQUIRE "cv32e40p_cosim_acquire_v1"

enum
{
    CV32E40P_COSIM_MAX_GPR = 4,
    CV32E40P_COSIM_MAX_FPR = 2,
    CV32E40P_COSIM_MAX_CSR = 16,
    CV32E40P_COSIM_MAX_MEM = 2,
    CV32E40P_COSIM_MAX_REGIONS = 16,
    CV32E40P_COSIM_MAX_BOUNDARY_CSR = 8,
    CV32E40P_COSIM_ERROR_LEN = 160,
};

enum Cv32e40pCosimStatus : uint32_t
{
    CV32E40P_COSIM_OK = 0,
    CV32E40P_COSIM_EVENT,          // poll() returned an event
    CV32E40P_COSIM_RUNNING,        // needs more simulated time to reach the next commit
    CV32E40P_COSIM_SLEEPING,       // in WFI, waiting for an input
    CV32E40P_COSIM_EXTERNAL_LOAD,  // waiting for external_load_response()
    CV32E40P_COSIM_FINISHED,       // the simulated software has exited
    CV32E40P_COSIM_REJECTED,       // the call is not valid in the current state
    CV32E40P_COSIM_ERROR,          // see Cv32e40pCosimSnapshot::error
};

// Why a CSR changed. Only CV32E40P_COSIM_CSR_INSN is an effect of the instruction itself.
enum Cv32e40pCosimCsrOrigin : uint32_t
{
    CV32E40P_COSIM_CSR_INSN = 0,
    CV32E40P_COSIM_CSR_TRAP,
    CV32E40P_COSIM_CSR_RETIRE,       // hardware-loop state updated at retire
    CV32E40P_COSIM_CSR_FP_STATE,     // fflags and mstatus.FS/SD
    CV32E40P_COSIM_CSR_TAKE,         // interrupt take
    CV32E40P_COSIM_CSR_DEBUG_ENTRY,
};

struct Cv32e40pCosimRegWrite { uint32_t index, value; };
struct Cv32e40pCosimCsrWrite { uint32_t address, value, mask, origin; };

// One memory access of an instruction, with its address and size. A misaligned access is
// one entry, although the data port splits it in two. Bit i of byte_enable stands for the
// byte at (address & ~3) + i, and goes past bit 3 when the access crosses a word.
struct Cv32e40pCosimMemOp
{
    uint64_t transaction_id;
    uint32_t is_store, address, size, byte_enable;
    uint32_t data;                   // least significant byte first, size <= 4
};

enum
{
    CV32E40P_COSIM_REC_TRAPPED = 1u,
    CV32E40P_COSIM_REC_DEBUG_BEFORE = 2u,
    CV32E40P_COSIM_REC_DEBUG_AFTER = 4u,
    CV32E40P_COSIM_REC_OVERFLOW = 0x80000000u,  // a list overflowed and ERROR follows
};

// Effects of one executed instruction, in program order.
struct Cv32e40pCosimCommit
{
    uint32_t struct_size, flags;
    uint64_t sequence;               // 1 for the first instruction after reset, +1 per record
    int64_t cycle;                   // core cycle at retire, -1 when unknown
    uint64_t minstret;               // {minstreth, minstret} after this record (the counters
                                     // are not in the CSR list, they change on every retire)
    uint32_t pc, next_pc, insn, insn_len;
    uint32_t trap_cause, trap_target; // valid with CV32E40P_COSIM_REC_TRAPPED (synchronous only)
    uint32_t n_gpr, n_fpr, n_csr, n_mem;
    Cv32e40pCosimRegWrite gpr[CV32E40P_COSIM_MAX_GPR];   // x0 is never reported
    Cv32e40pCosimRegWrite fpr[CV32E40P_COSIM_MAX_FPR];   // empty with Zfinx or without FPU
    Cv32e40pCosimCsrWrite csr[CV32E40P_COSIM_MAX_CSR];   // in first-write order, last value
    Cv32e40pCosimMemOp mem[CV32E40P_COSIM_MAX_MEM];
};

// Pin levels driven by the testbench. Each call carries input_sequence = previous + 1.
struct Cv32e40pCosimInput
{
    uint32_t struct_size;
    uint64_t input_sequence;
    uint32_t irq_level;              // irq_i[31:0], unmasked
    uint32_t debug_req;
    uint32_t fetch_enable;
};

enum Cv32e40pCosimDomain : uint32_t { CV32E40P_COSIM_DOMAIN_IRQ = 0, CV32E40P_COSIM_DOMAIN_DEBUG };

// The RTL sampled the inputs of one domain, with the levels of the last input(). The IRQ
// domain is the irq_q flop on the gated clock, which the interrupt decisions read through
// mip. Only the wake-up from sleep reads the pins. The DEBUG domain is the debug_req_q latch
// on the free-running clock, loaded with the pin while the pin is high or the core is in
// debug mode. A debug request is pending while the pin or the latch is set.
struct Cv32e40pCosimSample
{
    uint32_t struct_size, domain;
    uint64_t sample_sequence;        // previous + 1
};

// Decision points of the RTL controller (cv32e40p_controller.sv).
enum Cv32e40pCosimOpportunityKind : uint32_t
{
    CV32E40P_COSIM_OPP_DISPATCH = 0, // DECODE checks the debug request, the trigger, then
                                     // the interrupts, before it issues a valid instruction
    CV32E40P_COSIM_OPP_FIRST_FETCH,  // FIRST_FETCH checks the interrupts, unless debug is pending
    CV32E40P_COSIM_OPP_SLEEP,        // SLEEP, or FLUSH_WB with a WFI, wakes up the core, or
                                     // enters debug mode if a debug request is pending
    CV32E40P_COSIM_OPP_BOOT,         // BOOT_SET checks the debug request before the first
                                     // instruction
};

// The RTL made an interrupt or debug decision before the instruction whose sequence is ordinal.
struct Cv32e40pCosimOpportunity
{
    uint32_t struct_size, kind;
    uint64_t opportunity_id;         // previous + 1
    uint64_t ordinal;
};

enum Cv32e40pCosimBoundaryKind : uint32_t
{
    CV32E40P_COSIM_BOUNDARY_IRQ_TAKE = 1,
    CV32E40P_COSIM_BOUNDARY_DEBUG_ENTER,
    CV32E40P_COSIM_BOUNDARY_WAKE,
};

// What the core did at a decision point, published before the next record.
struct Cv32e40pCosimBoundary
{
    uint32_t struct_size, kind;
    uint64_t opportunity_id, after_sequence;
    uint32_t cause, irq_id;
    uint32_t pc, next_pc;
    uint32_t n_csr;
    Cv32e40pCosimCsrWrite csr[CV32E40P_COSIM_MAX_BOUNDARY_CSR];
};

enum Cv32e40pCosimEventType : uint32_t
{
    CV32E40P_COSIM_EVENT_COMMIT = 1,
    CV32E40P_COSIM_EVENT_BOUNDARY,
};

struct Cv32e40pCosimEvent
{
    uint32_t struct_size, type;
    union
    {
        Cv32e40pCosimCommit commit;
        Cv32e40pCosimBoundary boundary;
    } u;
};

// A load from an external region waits for its data, with its record complete but not yet
// published. The response gives the low size bytes, which the core extends as the instruction
// does and writes to the register reg (0-31 for x0-x31, 32-63 for f0-f31) and to the record.
struct Cv32e40pCosimExternalLoad
{
    uint32_t struct_size;
    uint64_t sequence;
    uint32_t address, size, reg;
};

// The value the RTL read from the volatile CSR address in the instruction with this sequence,
// used for that read only. A value the instruction does not read is an error, and so is a
// read of a volatile CSR into a register other than x0 without a value.
struct Cv32e40pCosimVolatileRead
{
    uint32_t struct_size, address;
    uint64_t sequence;
    uint32_t value;
};

enum Cv32e40pCosimRegionKind : uint32_t
{
    CV32E40P_COSIM_REGION_RAM = 0,        // owned by the model
    CV32E40P_COSIM_REGION_EXTERNAL,       // loads take their data from the testbench
    CV32E40P_COSIM_REGION_STORE_ONLY,     // stores are compared, loads are external
};

struct Cv32e40pCosimRegion { uint32_t base, length, kind; };  // inside Cv32e40pCosimConfig

// Given to configure(). region() can add regions before the first instruction.
struct Cv32e40pCosimConfig
{
    uint32_t struct_size, n_regions;
    Cv32e40pCosimRegion region[CV32E40P_COSIM_MAX_REGIONS];
};

struct Cv32e40pCosimInfo
{
    uint32_t struct_size, minor;
    uint32_t pulp, cluster, fpu, zfinx, num_mhpmcounters;
    uint32_t irq_mask, boot_addr, debug_handler, debug_exception;
    int64_t clock_period_ps;         // core clock, valid after configure()
};

struct Cv32e40pCosimSnapshot
{
    uint32_t struct_size, next_pc, debug_mode, sleeping;
    uint64_t sequence, input_sequence, sample_sequence, opportunity_id;
    int64_t time_ps;
    char error[CV32E40P_COSIM_ERROR_LEN];
};

class Cv32e40pCosim
{
public:
    virtual const Cv32e40pCosimInfo *info() const = 0;
    virtual Cv32e40pCosimStatus configure(const Cv32e40pCosimConfig *config) = 0;
    // Allow the core to execute up to the instruction with this sequence number.
    virtual Cv32e40pCosimStatus arm(uint64_t sequence) = 0;
    virtual Cv32e40pCosimStatus poll(Cv32e40pCosimEvent *event) = 0;
    virtual Cv32e40pCosimStatus status() const = 0;
    virtual Cv32e40pCosimStatus input(const Cv32e40pCosimInput *input) = 0;
    virtual Cv32e40pCosimStatus sample(const Cv32e40pCosimSample *sample) = 0;
    virtual Cv32e40pCosimStatus opportunity(const Cv32e40pCosimOpportunity *opportunity) = 0;
    virtual Cv32e40pCosimStatus volatile_csr(uint32_t address) = 0;
    virtual Cv32e40pCosimStatus volatile_read(const Cv32e40pCosimVolatileRead *read) = 0;
    virtual Cv32e40pCosimStatus external_load_query(Cv32e40pCosimExternalLoad *load) = 0;
    virtual Cv32e40pCosimStatus external_load_response(uint64_t sequence, uint32_t data) = 0;
    // Read-only access, for end-of-test checks and diagnostics.
    virtual Cv32e40pCosimStatus read_gpr(uint32_t index, uint32_t *value) const = 0;
    // Returns REJECTED when there are no F registers, without an FPU or with Zfinx.
    virtual Cv32e40pCosimStatus read_fpr(uint32_t index, uint32_t *value) const = 0;
    virtual Cv32e40pCosimStatus read_csr(uint32_t address, uint32_t *value) const = 0;
    virtual Cv32e40pCosimStatus read_memory(uint32_t address, uint32_t size,
        uint8_t *data) const = 0;
    virtual Cv32e40pCosimStatus snapshot(Cv32e40pCosimSnapshot *snapshot) const = 0;
    virtual Cv32e40pCosimStatus finish(Cv32e40pCosimSnapshot *snapshot) = 0;
    // Adds a region to the configuration, before the first instruction.
    virtual Cv32e40pCosimStatus region(const Cv32e40pCosimRegion *region) = 0;

protected:
    ~Cv32e40pCosim() {}
};

// Exported by the model library. component is gv::Gvsoc::get_component() of the core.
// Returns NULL when the component is not a CV32E40P core or the major version differs.
extern "C" typedef Cv32e40pCosim *(*Cv32e40pCosimAcquire)(void *component, uint32_t major,
                                                         uint32_t *minor);

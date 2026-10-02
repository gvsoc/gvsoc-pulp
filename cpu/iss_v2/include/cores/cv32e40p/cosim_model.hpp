// SPDX-FileCopyrightText: 2026 Fondazione Chips-IT
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Marco Paci (marco.paci@chips.it)

/* Implementation of the Cv32e40pCosim interface (cosim.hpp), owned by
 * Cv32e40pExec. The slots report what the instruction in flight does through
 * the hooks below, which build one record per instruction and publish the
 * records in program order. The hooks do nothing until a checker calls
 * configure(). */

#pragma once

#include <bitset>
#include <deque>
#include <string>
#include <cpu/iss_v2/include/types.hpp>
#include <cpu/iss_v2/include/cores/cv32e40p/cosim.hpp>

class Iss;

class Cv32e40pCosimModel : public Cv32e40pCosim
{
public:
    Cv32e40pCosimModel(Iss &iss);

    // Cv32e40pCosim
    const Cv32e40pCosimInfo *info() const override;
    Cv32e40pCosimStatus configure(const Cv32e40pCosimConfig *config) override;
    Cv32e40pCosimStatus arm(uint64_t sequence) override;
    Cv32e40pCosimStatus poll(Cv32e40pCosimEvent *event) override;
    Cv32e40pCosimStatus status() const override;
    Cv32e40pCosimStatus input(const Cv32e40pCosimInput *input) override;
    Cv32e40pCosimStatus sample(const Cv32e40pCosimSample *sample) override;
    Cv32e40pCosimStatus opportunity(const Cv32e40pCosimOpportunity *opportunity) override;
    Cv32e40pCosimStatus volatile_csr(uint32_t address) override;
    Cv32e40pCosimStatus volatile_read(const Cv32e40pCosimVolatileRead *read) override;
    Cv32e40pCosimStatus external_load_query(Cv32e40pCosimExternalLoad *load) override;
    Cv32e40pCosimStatus external_load_response(uint64_t sequence, uint32_t data) override;
    Cv32e40pCosimStatus read_gpr(uint32_t index, uint32_t *value) const override;
    Cv32e40pCosimStatus read_fpr(uint32_t index, uint32_t *value) const override;
    Cv32e40pCosimStatus read_csr(uint32_t address, uint32_t *value) const override;
    Cv32e40pCosimStatus read_memory(uint32_t address, uint32_t size, uint8_t *data) const override;
    Cv32e40pCosimStatus snapshot(Cv32e40pCosimSnapshot *snapshot) const override;
    Cv32e40pCosimStatus finish(Cv32e40pCosimSnapshot *snapshot) override;
    Cv32e40pCosimStatus region(const Cv32e40pCosimRegion *region) override;

    // Hooks, called by the slots.
    bool enabled() const { return this->configured; }
    /* Called before the instruction executes, to apply its decision points
     * (Cv32e40pRegfile::scoreboard_insn_check). Returns true to keep it
     * waiting, when it is not armed yet or when an interrupt or a debug entry
     * moved the PC. */
    bool dispatch(iss_insn_t *insn);
    /* The instruction stalled and will be dispatched again
     * (Cv32e40pRegfile::scoreboard_insn_clear), or it trapped as illegal. */
    void retry(iss_insn_t *insn);
    void gpr(int reg, uint32_t value);
    void fpr(int reg, uint32_t value);
    void csr(uint32_t address, uint32_t origin);
    // Called on a CSR read (Cv32e40pCsr::access). A volatile CSR gives the value the RTL read.
    void csr_read(iss_insn_t *insn, uint32_t address, iss_reg_t &value);
    void trap(uint32_t cause, uint32_t target, bool debug_mode_trap);
    void mem(bool is_store, uint32_t address, uint32_t size, uint32_t data);
    /* A load into reg, a unified register index (Cv32e40pLsu). For a load
     * from an external region, the record waits for external_load_response(). */
    void load(uint32_t address, uint32_t size, int reg, bool is_signed);
    // The instruction executed (Cv32e40pEvents::event_retire_account).
    void retire(iss_insn_t *insn);
    // A held instruction left the commit FIFO (Cv32e40pEvents::insn_stall_account).
    void drain();
    // Interrupt take and debug entry, before the next dispatch (Cv32e40pIrq).
    void boundary_begin(uint32_t kind, uint32_t cause, uint32_t irq_id, uint32_t pc);
    void boundary_end(uint32_t next_pc);

private:
    void fail(const std::string &message);
    void publish(const Cv32e40pCosimCommit &commit);
    void add_csr(Cv32e40pCosimCsrWrite *list, uint32_t &count, uint32_t max,
                 uint32_t address, uint32_t origin);
    uint32_t csr_value(uint32_t address) const;
    void wfi_wake();
    void push(const Cv32e40pCosimEvent &event);
    /* Applies the queued inputs and decision points up to the next
     * instruction, or while the core sleeps, until it wakes up. */
    bool consume();

    Iss &iss;
    Cv32e40pCosimInfo info_;
    Cv32e40pCosimConfig config_;
    bool configured = false;
    std::string error;

    uint64_t armed = 0;
    uint64_t last_sequence = 0;       // last sequence given to a record
    uint64_t input_sequence = 0;
    uint64_t sample_sequence = 0;
    uint64_t opportunity_id = 0;

    // CSRs declared volatile, and the values the RTL read from them, in order.
    struct VolatileRead
    {
        uint64_t sequence;
        uint32_t address, value;
        bool used;
    };
    std::bitset<4096> volatile_csrs;
    std::deque<VolatileRead> volatile_reads;

    // Load from an external region, whose record waits at the head of the events.
    struct ExternalLoad
    {
        uint64_t sequence;
        uint32_t address, size, reg;
        bool is_signed;
    };
    bool external_pending = false;
    ExternalLoad external;

    // Record of the instruction in flight, open from dispatch to retire.
    bool open = false;
    Cv32e40pCosimCommit current;
    uint32_t fflags_before = 0, mstatus_before = 0;
    bool debug_before = false;

    // Inputs, samples and decision points not applied yet, in call order.
    struct Pending
    {
        enum { INPUT, SAMPLE, DECISION } type;
        uint32_t irq_level, debug_req, domain, kind;
        uint64_t ordinal, opportunity_id;
    };
    std::deque<Pending> pending;
    // Instruction and id of the last decision point applied.
    uint64_t decided = 0, decided_id = 0;

    // Records of the instructions held in the commit FIFO, and the boundaries after them.
    std::deque<Cv32e40pCosimEvent> held;
    /* WFIs in the commit FIFO. Their record is published when they execute,
     * so their drain only adds the wake-up. */
    unsigned int wfi_drains = 0;

    bool in_boundary = false;
    Cv32e40pCosimBoundary boundary;

    std::deque<Cv32e40pCosimEvent> events;
};

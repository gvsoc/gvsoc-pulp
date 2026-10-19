// SPDX-FileCopyrightText: 2026 ETH Zurich and University of Bologna
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include <cpu/iss_v2/include/iss.hpp>

Cva6Frontend::Cva6Frontend(Iss &iss)
: PrefetchSingleLine(iss), iss(iss),
  bht(CONFIG_GVSOC_CVA6_BHT_ENTRIES), btb(CONFIG_GVSOC_CVA6_BTB_ENTRIES),
  ras(CONFIG_GVSOC_CVA6_RAS_DEPTH), dispatch_cycles(CONFIG_GVSOC_CVA6_QUEUE_DEPTH),
  cf_dispatch_cycles(CONFIG_GVSOC_CVA6_QUEUE_CF_DEPTH)
{
    this->iss.traces.new_trace("frontend", &this->trace, vp::DEBUG);

    this->queue_depth = CONFIG_GVSOC_CVA6_QUEUE_DEPTH;
    this->queue_cf_depth = CONFIG_GVSOC_CVA6_QUEUE_CF_DEPTH;
    this->queue_restart = CONFIG_GVSOC_CVA6_QUEUE_RESTART;
    this->mispredict_penalty = CONFIG_GVSOC_CVA6_MISPREDICT_PENALTY;
}

void Cva6Frontend::reset(bool active)
{
    PrefetchSingleLine::reset(active);

    if (active)
    {
        for (BhtEntry &entry : this->bht)
        {
            entry.valid = false;
            entry.counter = 0;
        }
        for (BtbEntry &entry : this->btb)
        {
            entry.valid = false;
        }
        for (RasEntry &entry : this->ras)
        {
            entry.valid = false;
        }

        this->word_addr = -1;
        this->word_cycle = 0;
        this->redirect = false;
        this->expected_pc = -1;
        this->pending = false;
        this->nb_dispatched = 0;
        this->nb_cf_dispatched = 0;
        this->nb_branches = 0;
        this->nb_mispredicts = 0;
    }
}

bool Cva6Frontend::fetch(iss_reg_t pc)
{
    if (!PrefetchSingleLine::fetch(pc))
    {
        return false;
    }

    // The same instruction is asked for again as long as it is not dispatched (scoreboard
    // stall, data access retried), its fetch is computed once
    if (!this->pending || this->pending_pc != pc)
    {
        this->fetch_compute(pc);
    }

    int64_t cycles = this->iss.clock.get_cycles();
    if (this->pending_cycle > cycles)
    {
        // Not fetched yet: this cycle is lost, and the remaining ones are skipped
        this->iss.exec.stall_cycles_inc(this->pending_cycle - cycles - 1);
        return false;
    }

    return true;
}

void Cva6Frontend::fetch_compute(iss_reg_t pc)
{
    int64_t cycles = this->iss.clock.get_cycles();
    iss_insn_t *insn = this->iss.insn_cache.get_insn(pc);
    iss_opcode_t opcode = insn->opcode;
    int size = (opcode & 3) == 3 ? 4 : 2;

    if (pc != this->expected_pc)
    {
        // Trap, xret, flush or any redirect the frontend did not predict nor was told about:
        // fetch restarts now
        this->redirect = true;
        this->redirect_cycle = cycles;
    }

    // Prediction, done when the word is fetched (frontend.sv)
    bool is_call;
    CfType cf = this->classify(opcode, size, is_call);
    bool bp_valid = false;
    iss_reg_t target = pc + size;

    switch (cf)
    {
        case CF_BRANCH:
        {
            BhtEntry &entry = this->bht[this->bht_index(pc)];
            int64_t imm = this->cf_imm(opcode, size);
            // A counter never written falls back to the static prediction: backward taken
            bp_valid = entry.valid ? entry.counter >= 2 : imm < 0;
            if (bp_valid)
            {
                target = pc + imm;
            }
            break;
        }

        case CF_JUMP:
            bp_valid = true;
            target = pc + this->cf_imm(opcode, size);
            break;

        case CF_RETURN:
            if (this->ras[0].valid)
            {
                bp_valid = true;
                target = this->ras_pop().ra;
            }
            break;

        case CF_JALR:
        {
            BtbEntry &entry = this->btb[this->btb_index(pc)];
            if (entry.valid)
            {
                bp_valid = true;
                target = entry.target;
            }
            break;
        }

        default:
            break;
    }

    if (is_call)
    {
        this->ras_push(pc + size);
    }

    // When the words of the instruction are fetched: one word per cycle, from where the
    // stream (re)started, within the room the instruction queue leaves
    iss_reg_t first_word = pc & ~(iss_reg_t)3;
    iss_reg_t last_word = (pc + size - 1) & ~(iss_reg_t)3;
    int64_t bound = this->queue_bound(bp_valid);
    int64_t cycle;

    if (this->redirect)
    {
        this->redirect = false;
        cycle = std::max(this->redirect_cycle, bound);
    }
    else if (first_word == this->word_addr)
    {
        cycle = this->word_cycle;
    }
    else
    {
        cycle = std::max(this->word_cycle + 1, bound);
    }

    if (last_word != first_word)
    {
        cycle = std::max(cycle + 1, bound);
    }

    this->word_addr = last_word;
    this->word_cycle = cycle;

    this->pending = true;
    this->pending_pc = pc;
    this->pending_size = size;
    this->pending_cycle = cycle;
    this->pending_cf = cf;
    this->pending_bp_valid = bp_valid;
    this->pending_target = target;

    // The frontend goes on along the predicted path. A predicted taken control flow kills
    // the fetch of the next word, the target is fetched one cycle later.
    this->expected_pc = target;
    if (bp_valid)
    {
        this->redirect = true;
        this->redirect_cycle = cycle + 2;
    }

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Fetch (pc: 0x%lx, cycle: %ld, cf: %d, "
        "predicted: %d, target: 0x%lx)\n", pc, cycle, cf, bp_valid, target);
}

void Cva6Frontend::retire(iss_insn_t *insn)
{
    if (!this->pending)
    {
        return;
    }
    this->pending = false;

    int64_t cycles = this->iss.clock.get_cycles();
    iss_reg_t pc = this->pending_pc;
    iss_reg_t next_pc = this->iss.exec.current_insn;

    this->dispatch_cycles[this->nb_dispatched++ % this->queue_depth] = cycles;
    if (this->pending_bp_valid)
    {
        this->cf_dispatch_cycles[this->nb_cf_dispatched++ % this->queue_cf_depth] = cycles;
    }

    if (this->pending_cf == CF_NONE)
    {
        return;
    }

    // Resolution in the branch unit (branch_unit.sv): a conditional branch by its direction,
    // a jalr by its target, and a jalr nothing predicted is always a mispredict, even if it
    // jumps to the next instruction
    bool mispredict = next_pc != this->pending_target ||
        ((this->pending_cf == CF_JALR || this->pending_cf == CF_RETURN) && !this->pending_bp_valid);

    if (this->pending_cf == CF_BRANCH)
    {
        this->nb_branches++;
        bool taken = this->iss.timing.branch_taken;
        mispredict = taken != this->pending_bp_valid;
        BhtEntry &entry = this->bht[this->bht_index(pc)];
        entry.valid = true;
        if (taken && entry.counter < 3)
        {
            entry.counter++;
        }
        else if (!taken && entry.counter > 0)
        {
            entry.counter--;
        }
    }
    else if (this->pending_cf == CF_JALR && mispredict)
    {
        // Only mispredicted jalr update the BTB, returns never do
        BtbEntry &entry = this->btb[this->btb_index(pc)];
        entry.valid = true;
        entry.target = next_pc;
    }

    if (mispredict)
    {
        this->nb_mispredicts++;
        this->expected_pc = next_pc;
        this->redirect = true;
        this->redirect_cycle = cycles + this->mispredict_penalty;
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "Mispredict (pc: 0x%lx, predicted: 0x%lx, "
            "actual: 0x%lx)\n", pc, this->pending_target, next_pc);
    }
}

int64_t Cva6Frontend::queue_bound(bool is_cf)
{
    // The instruction being fetched is the next one to be dispatched: the queue has room for
    // it once the one queue_depth instructions earlier is dispatched, and for a predicted
    // control flow once the predicted control flow queue_cf_depth earlier is dispatched.
    // Fetch then restarts, which takes queue_restart cycles.
    int64_t bound = 0;
    if (this->nb_dispatched >= (uint64_t)this->queue_depth)
    {
        bound = this->dispatch_cycles[this->nb_dispatched % this->queue_depth] +
            this->queue_restart;
    }
    if (is_cf && this->nb_cf_dispatched >= (uint64_t)this->queue_cf_depth)
    {
        bound = std::max(bound, this->cf_dispatch_cycles[this->nb_cf_dispatched %
            this->queue_cf_depth] + this->queue_restart);
    }
    return bound;
}

void Cva6Frontend::ras_push(iss_reg_t ra)
{
    for (int i = this->ras.size() - 1; i > 0; i--)
    {
        this->ras[i] = this->ras[i - 1];
    }
    this->ras[0] = { true, ra };
}

Cva6Frontend::RasEntry Cva6Frontend::ras_pop()
{
    RasEntry top = this->ras[0];
    for (int i = 0; i < (int)this->ras.size() - 1; i++)
    {
        this->ras[i] = this->ras[i + 1];
    }
    this->ras[this->ras.size() - 1].valid = false;
    return top;
}

Cva6Frontend::CfType Cva6Frontend::classify(iss_opcode_t opcode, int size, bool &is_call)
{
    is_call = false;

    if (size == 4)
    {
        int rd = (opcode >> 7) & 0x1f;
        int rs1 = (opcode >> 15) & 0x1f;
        bool rd_link = rd == 1 || rd == 5;

        switch (opcode & 0x7f)
        {
            case 0x63:
                return CF_BRANCH;
            case 0x6f:
                is_call = rd_link;
                return CF_JUMP;
            case 0x67:
                is_call = rd_link;
                return (rs1 == 1 || rs1 == 5) && rs1 != rd ? CF_RETURN : CF_JALR;
            default:
                return CF_NONE;
        }
    }

    int funct3 = (opcode >> 13) & 0x7;
    switch (opcode & 0x3)
    {
        case 0x1:
            // c.j, c.beqz, c.bnez (c.jal is RV32 only)
            if (funct3 == 0x5) return CF_JUMP;
            if (funct3 == 0x6 || funct3 == 0x7) return CF_BRANCH;
            return CF_NONE;
        case 0x2:
        {
            // c.jr and c.jalr
            int rs1 = (opcode >> 7) & 0x1f;
            if (funct3 != 0x4 || ((opcode >> 2) & 0x1f) != 0 || rs1 == 0) return CF_NONE;
            if ((opcode >> 12) & 1)
            {
                is_call = true;
                return CF_JALR;
            }
            return rs1 == 1 || rs1 == 5 ? CF_RETURN : CF_JALR;
        }
        default:
            return CF_NONE;
    }
}

int64_t Cva6Frontend::cf_imm(iss_opcode_t opcode, int size)
{
    auto bit = [opcode](int pos) -> uint64_t { return (opcode >> pos) & 1; };
    auto bits = [opcode](int hi, int lo) -> uint64_t {
        return (opcode >> lo) & ((1ULL << (hi - lo + 1)) - 1); };
    auto sext = [](uint64_t value, int width) -> int64_t {
        return (int64_t)(value << (64 - width)) >> (64 - width); };

    if (size == 4)
    {
        if ((opcode & 0x7f) == 0x6f)
        {
            return sext((bit(31) << 20) | (bits(19, 12) << 12) | (bit(20) << 11) |
                (bits(30, 21) << 1), 21);
        }
        return sext((bit(31) << 12) | (bit(7) << 11) | (bits(30, 25) << 5) |
            (bits(11, 8) << 1), 13);
    }

    if (bit(14))
    {
        // c.beqz, c.bnez
        return sext((bit(12) << 8) | (bits(6, 5) << 6) | (bit(2) << 5) | (bits(11, 10) << 3) |
            (bits(4, 3) << 1), 9);
    }
    // c.j
    return sext((bit(12) << 11) | (bit(8) << 10) | (bits(10, 9) << 8) | (bit(6) << 7) |
        (bit(7) << 6) | (bit(2) << 5) | (bit(11) << 4) | (bits(5, 3) << 1), 12);
}

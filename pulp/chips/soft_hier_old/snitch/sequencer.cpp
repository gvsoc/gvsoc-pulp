/*
 * Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and
 *                    University of Bologna
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
 * Authors: Germain Haugou, GreenWaves Technologies (germain.haugou@greenwaves-technologies.com)
 *          Kexin Li, ETH Zurich (likexi@ethz.ch)
 */

// Temporary workaround to let this component include ISS headers
#include <../../../../isa_snitch_rv32imfdva.hpp>

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <vp/proxy.hpp>
#include <stdio.h>
#include <math.h>

#define RISCY
#define CONFIG_GVSOC_ISS_SNITCH
#ifdef CONFIG_GVSOC_ISS_SNITCH
#define ISS_WORD_32
#endif
#include <cpu/iss/include/types.hpp>

class sequencer;

// Data structure for ISS requests
class OffloadReq
{
public:
    iss_reg_t pc;
    iss_insn_t insn;
    bool is_write;
    unsigned int frm;
    unsigned int fmode;
};


// Define a data structure for frep configuration.
class FrepConfig
{
public:
    
    // Inner/outer loop
    bool is_outer = false;      
    // Number of iterations             
    int max_rpt = -1;
    // Number of instructions
    int max_inst = -1;
    // Stagger register index
    int stagger_max = 0;
    unsigned int stagger_mask = 0x0; 
};


// Define a data structure for every entry in ring buffer.
class BufferEntry 
{
    friend class FrepConfig;

public:

    BufferEntry() {}
    BufferEntry(OffloadReq req, bool is_outer, int max_inst, int max_rpt, int stagger_max, unsigned int stagger_mask, int base_entry, int next_entry, FrepConfig config);

    // Main content of the entry
    OffloadReq req;      
    // Whether the instruction is in frep sequence
    bool isn_sequence = true;
    // Inner/outer loop
    bool is_outer = false;      
    // Number of instructions in this configuration
    int max_inst = -1;
    // Number of iterations, max_rpt=0 means it's an invalid entry.  
    int max_rpt = -1;
    // Stagger register index
    int stagger_max = 0;
    unsigned int stagger_mask = 0x0; 
    // Index of base instruction in this configuration
    int base_entry = -1;
    // Index of the next instruction
    int next_entry = -1;  
    // The corresponding frep configuration
    FrepConfig config;
};

BufferEntry::BufferEntry(OffloadReq req, bool is_outer, int max_inst, int max_rpt, int stagger_max, unsigned int stagger_mask, int base_entry, int next_entry, FrepConfig config) : 
req(req), is_outer(is_outer), max_inst(max_inst), max_rpt(max_rpt), stagger_max(stagger_max), stagger_mask(stagger_mask), base_entry(base_entry), next_entry(next_entry), config(config) {}


// Class for fp sequencer
class sequencer : public vp::Component
{
    friend class BufferEntry;
    friend class FrepConfig;

public:

    sequencer(vp::ComponentConf &conf);

    OffloadReq acc_req;

private:

    // Functions for read/write logic
    void write_entry(BufferEntry entry);
    void update_entry(int index);
    BufferEntry read_entry(int index);
    BufferEntry gen_entry(OffloadReq *req, FrepConfig *frep_config);
    // Functions determining the state of the buffer
    bool isFull();
    bool isEmpty();

    // Request and response interface, handshaking functions, event controller
    static void req(vp::Block *__this, OffloadReq *req);
    static void response(vp::Block *__this, OffloadReq *req);
    static vp::IoReqStatus rsp_state(vp::Block *__this, vp::IoReq *req);
    static void offload_event(vp::Block *__this, vp::ClockEvent *event);

    bool check_state();
    inline void stalled_dec();
    inline void stalled_inc();
    void reset(bool active);

    vp::Trace     trace;
    vp::ClockEvent *event;

    // Request and response interface
    vp::WireMaster<OffloadReq *> out;
    vp::WireSlave<OffloadReq *> in;
    // Handshaking interface
    vp::IoSlave ready_itf;
    vp::IoMaster ready_o_itf;

    // Check whether sequencer is ready to accept new instructions
    bool acc_req_ready = true;

    // Check whether sequencer is ready to offload instruction to subsystem
    bool acc_req_ready_o = true;
    vp::IoReq check_req;

    // Control the offload event queue of sequencer
    bool stalled = true;

    // Generic latency of sequencer module
    int latency = 0;

    // Build a ring buffer table with 16 entries
    static const int size = 16;
    BufferEntry RingBuffer[size];
    // Important index for read/write operations
    int write_id = 0;
    int read_id = 0;
    int base_id = 0;
    // State of the buffer
    int nb_entries = 0;
    bool rb_empty = true;
    bool rb_full = false;

    // Store latest frep configuration
    FrepConfig frep_config;
};


sequencer::sequencer(vp::ComponentConf &config)
    : vp::Component(config)
{
    traces.new_trace("trace", &trace, vp::DEBUG);

    event = event_new((vp::Block *)this, offload_event);

    in.set_sync_meth(&sequencer::req);
    new_slave_port("input", &in, (vp::Block *)this);

    out.set_sync_meth(&sequencer::response);
    new_master_port("output", &out, (vp::Block *)this);

    ready_itf.set_req_meth(&sequencer::rsp_state);
    new_slave_port("acc_req_ready", &this->ready_itf, (vp::Block *)this);

    new_master_port("acc_req_ready_o", &this->ready_o_itf, (vp::Block *)this);

    this->latency = get_js_config()->get_child_int("latency");

}


// Build an event to send instructions every cycle by default if the buffer is not empty.
// Use .enable() to activate events, because it automates itself to offload instructions but isn't triggered by request.
void sequencer::offload_event(vp::Block *__this, vp::ClockEvent *event)
{
    sequencer *_this = (sequencer *)__this;

    // Check whether the buffer is empty, stall the event if the buffer is empty,
    // which means there's nothing to offload.
    if (_this->isEmpty()) {}

    // Check if the subsystem is idle
    _this->acc_req_ready_o = _this->check_state();
    _this->trace.msg(vp::Trace::LEVEL_TRACE, "Sequencer receives acceleration request output handshaking signal: %d\n", _this->acc_req_ready_o);

    // If the buffer has some entries, offload entry one by one each cycle
    BufferEntry offload_entry;
    int buf_id = _this->read_id;

    // The buffer is not empty and the subsystem is ready, offload the request from the buffer.
    if (!_this->isEmpty() && _this->acc_req_ready_o)
    {
        // Read out an entry from the buffer.
        offload_entry = _this->read_entry(buf_id);

        // Output handshaking, between sequencer and fp subsystem
        _this->acc_req = offload_entry.req;
        _this->trace.msg("Offload to fp subsystem from buffer index %d (opcode: 0x%lx, pc: 0x%lx)\n", buf_id, _this->acc_req.insn.opcode, _this->acc_req.pc);

        // Offload request if the port is connected
        if (_this->out.is_bound())
        {
            _this->out.sync(&_this->acc_req);
        }

        // Update the buffer after each read operation.
        _this->update_entry(buf_id);

        // The entry becomes invalid if it won't be repeated any more.
        if (_this->RingBuffer[buf_id].max_rpt < 0)
        {
            _this->nb_entries--;
        }
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "Number of entries in the ring buffer: %d\n", _this->nb_entries);
    }

    // Stall the event if the buffer is empty or the next entry to be read is invalid.
    if ((_this->isEmpty() | _this->RingBuffer[_this->read_id].max_rpt < 0) && !_this->stalled)
    {
        _this->stalled_inc();
    }
}


// Get called at the reset period of the system.
void sequencer::reset(bool active)
{
    // Everytime the core is reset, the stall counter is automatically set to 0, so
    // we need to set it again to stall the core until the reset is deasserted
    if (active)
    {
        this->stalled_inc();
    }
    else
    {
        this->stalled_dec();
    }
}


// Get called when the sequencer offload event is enabled again.
inline void sequencer::stalled_dec()
{
    if (this->stalled == false)
    {
        return;
    }

    this->stalled = false;
    this->event->enable();
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Activate the sequencer offloading\n");
}


// Get called when the sequencer needs to be stalled.
inline void sequencer::stalled_inc()
{
    if (this->stalled == false)
    {
        this->event->disable();
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Stall the sequencer offloading\n");
    }
    this->stalled = true;
}


// Get called when the sequencer receives a request from the integer core.
void sequencer::req(vp::Block *__this, OffloadReq *req)
{
    sequencer *_this = (sequencer *)__this;
    
    // Input handshaking, between integer core and sequencer
    // Obtain arguments from request.
    iss_reg_t pc = req->pc;
    bool isRead = !req->is_write;
    iss_insn_t insn = req->insn;
    unsigned int frm = req->frm;
    iss_opcode_t opcode = insn.opcode;

    _this->trace.msg("Received IO request (opcode: 0x%llx, pc: 0x%llx, isRead: %d)\n", opcode, pc, isRead);

    // Instructions are assigned to three paths dependent on the type of the instruction.
    // 1. Bypass lane.
    // 2. FPU sequence buffer.
    // 3. An frep instruction indicates the loop configuration.


    // 3. An frep instruction indicates the loop configuration.
    // frep instruction won't be written into ring buffer.
    // Update frep_config when there is new frep instruction coming.
    if (unlikely(insn.desc->tags[ISA_TAG_FREP_ID]))
    {
        _this->frep_config.is_outer = insn.is_outer;
        _this->frep_config.max_inst = insn.uim[2];
        _this->frep_config.max_rpt = insn.max_rpt;
        _this->frep_config.stagger_max = insn.uim[1];
        _this->frep_config.stagger_mask = insn.uim[0];

        _this->trace.msg("Frep Config (is_outer: %d, max_inst: %d, max_rpt: %d, stagger_max: %d, stagger_mask: 0x%llx)\n", 
            insn.is_outer, insn.uim[2], insn.max_rpt, insn.uim[1], insn.uim[0]);

        // Assign value of base_id, current write_id is the new base_id when new configuration comes.
        // base_id only updates when new frep instruction comes.
        _this->base_id = _this->write_id;
    }


    // 1. Bypass lane instructions.
    // Offload bypass line instructions only if the ring buffer is empty, where write_id == base_id.
    if (!insn.desc->tags[ISA_TAG_FREP_ID] && insn.desc->tags[ISA_TAG_NSEQ_ID])
    {
        // Output handshaking also finishes inside rsp_state function, between sequencer and fp subsystem
        // This means the sequencer and subsystem are both ready for a bypass instruction.
        // The request is sent from integer core -> sequencer -> subsystem in one cycle without stall.
        _this->trace.msg("Offload to fp subsystem from bypass lane (opcode: 0x%lx, pc: 0x%lx)\n", req->insn.opcode, req->pc);

        if (_this->out.is_bound())
        {
            _this->out.sync(req);
        }
    }
    

    // 2. Instructions are sequenced from the FPU sequence buffer. Write a new entry into the buffer.
    BufferEntry new_entry;
    if(!insn.desc->tags[ISA_TAG_FREP_ID] && !insn.desc->tags[ISA_TAG_NSEQ_ID])
    {
        // The buffer must have vacancy after rsp_state() function.
        new_entry = _this->gen_entry(req, &_this->frep_config);
        // Write in new requests into the buffer.
        _this->write_entry(new_entry);

        // If the sequencer stalls, re-activate if there's new instruction.
        if(_this->stalled == true)
        {
            _this->stalled_dec();
        }
    }
    
}


void sequencer::response(vp::Block *__this, OffloadReq *req)
{

}


// Get called when the master wants to know whether the buffer is full.
vp::IoReqStatus sequencer::rsp_state(vp::Block *__this, vp::IoReq *req)
{
    sequencer *_this = (sequencer *)__this;
    // Check whether the buffer has empty space.
    // Ready to accept new request if the buffer still has vacancy.

    // Two conditions for instruction dependent on the type:
    // 1. Sequenceable instructions: check whether the buffer has empty space
    // 2. Bypass instructions: check whether the buffer has no entry and the subsystem is also idle;
    // The bypass instruction gets executed only if all previous sequenceable instructions in the buffer have benn finished.
    bool is_bypass = !req->get_is_write();

    if (is_bypass)
    {
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "Go through bypass lane\n");
        if (_this->isEmpty())
        {
            // Check the state of the subsystem.
            bool acc_req_ready_o = _this->check_state();
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Sequencer receives acceleration request output handshaking signal: %d\n", acc_req_ready_o);
            _this->acc_req_ready = acc_req_ready_o;
        }
        else
        {
            _this->acc_req_ready = false;
        }
    }
    else
    {
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "Go through sequenceable lane\n");
        // Check whether the buffer has empty space.
        _this->acc_req_ready = !_this->isFull();
    }

    bool acc_req_ready = _this->acc_req_ready;
    _this->trace.msg(vp::Trace::LEVEL_TRACE, "Sequence buffer request input handshaking signal: %d\n", acc_req_ready);

    if (acc_req_ready)
    {
       return vp::IO_REQ_OK; 
    }
    else
    {
        return vp::IO_REQ_INVALID;
    }
}


// Get called when we need to offload instructions from the sequencer to the subsystem.
bool sequencer::check_state()
{
    this->check_req.init();

    int idle = this->ready_o_itf.req(&this->check_req);

    if (idle == vp::IO_REQ_OK)
    {
        return true;
    }
    else if (idle == vp::IO_REQ_INVALID)
    {
        return false;
    }

    return false;
}


// Get called when we need to generate new buffer entry according to new request and freg configuration.
BufferEntry sequencer::gen_entry(OffloadReq *req, FrepConfig *config)
{
    BufferEntry new_entry;

    // Observe whether it's inside a sequence defined by a frep configuration
    bool sequence = true;
    if (((this->write_id > ((this->base_id + config->max_inst) % this->size)) & (this->write_id < this->base_id))
        | config->max_inst < 0)
    {
        sequence = false;
    }
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Entry arguments write_id: %d, base_id: %d, config->max_inst: %d, size:%d, sequence: %d\n", 
            this->write_id, this->base_id, config->max_inst, this->size, sequence);

    if (!sequence)
    {
        // 1. Sequenceable instructions, but not in a sequence
        // It's executed individually without repeating.
        // If the instruction isn't covered in frep configuration, write this instruction in buffer index write_id.
        new_entry.req = *req;
        new_entry.isn_sequence = true;
        new_entry.max_rpt = 0;
        new_entry.base_entry = this->write_id;
        new_entry.next_entry = (this->write_id + 1) % this->size;
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Generate IO req in buffer index %d (opcode: 0x%llx, pc: 0x%llx)\n", this->write_id, new_entry.req.insn.opcode, new_entry.req.pc);
    }
    else
    {
        // 2. Sequenceable instructions, and are sequenced from the FPU sequence buffer under frep.
        // FREP.I and FREP.O repeat the max_inst + 1 instructions following the FREP instruction for max_rpt + 1 times. 
        // The FREP.I instruction (I stands for inner) repeats every instruction the specified number of times and moves on to executing and repeating the next. 
        // The FREP.O instruction (O stands for outer) repeats the whole sequence of instructions max_rpt + 1 times. 
        // Register staggering can be enabled and configured via the stagger_mask and stagger_max immediates.
        new_entry.req = *req;
        new_entry.isn_sequence = false;
        new_entry.is_outer = config->is_outer;
        new_entry.max_inst = config->max_inst;
        new_entry.max_rpt = config->max_rpt;
        new_entry.stagger_max = config->stagger_max;
        new_entry.stagger_mask = config->stagger_mask;
        new_entry.base_entry = this->base_id;
        new_entry.config = *config;

        // Find whether it's the last instruction/last iteration in frep config,
        // it will affect the value of next_id in this entry.
        bool insn_last = false;
        if (this->write_id == ((new_entry.base_entry + new_entry.max_inst) % this->size))
        {
            insn_last = true;
        }

        bool rpt_last = false;
        if (!new_entry.max_rpt)
        {
            rpt_last = true;
        }

        // Find the index of next instruction 
        if (config->is_outer)
        {
            if (!insn_last)
            {
                // There is still instruction following in this configuration.
                new_entry.next_entry = (this->write_id + 1) % this->size; 
            }
            else
            {
                // This is the last instruction in this configuration.
                if (!rpt_last)
                {
                    // If there's still iteration left, go back to the initial instruction index.
                    new_entry.next_entry = this->base_id; 
                }
                else
                {
                    // No iteration, move to the following entry in buffer.
                    new_entry.next_entry = (this->write_id + 1) % this->size; 
                }
            }
        }
        else 
        {
            // inner loop
            if(rpt_last)
            {
                // No iteration, move to the following entry in buffer.
                new_entry.next_entry = (this->write_id + 1) % this->size; 
            }
            else
            {
                // Repeat itself, next instruction remains itself
                new_entry.next_entry = this->write_id;
            }
        }
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Generate sequenceable IO req in buffer index %d (opcode: 0x%llx, pc: 0x%llx, base_id: %d, next_id: %d)\n", 
            this->write_id, new_entry.req.insn.opcode, new_entry.req.pc, new_entry.base_entry, new_entry.next_entry);
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Sequence frep configuration in buffer index %d (is_outer: %d, max_inst: %d, max_rpt: %d, stagger_max: %d, stagger_mask: 0x%llx)\n", 
            this->write_id, new_entry.config.is_outer, new_entry.config.max_inst, new_entry.config.max_rpt, new_entry.config.stagger_max, new_entry.config.stagger_mask);
        
        // Reset frep config if this sequence is over
        if (this->write_id == ((this->base_id + config->max_inst) % this->size))
        {
            config->max_inst = -1;
            config->max_rpt = -1;
            config->stagger_max = 0;
            config->stagger_mask = 0x0;
        }
    }

    return new_entry;
}


// Get called when there is a new entry that needs to be written into the buffer.
void sequencer::write_entry(BufferEntry entry)
{
    if (this->isFull()) 
    {
        this->acc_req_ready = false;
        this->trace.msg("Sequence buffer is full and no longer accepts new instructions\n");
    }
    else
    {
        // Write new entry to write_id
        this->RingBuffer[this->write_id] = entry;
        this->trace.msg("Wrote IO request in buffer index %d (opcode: 0x%llx, pc: 0x%llx)\n", this->write_id, this->RingBuffer[this->write_id].req.insn.opcode, this->RingBuffer[this->write_id].req.pc);

        // Update write_id for the next write operation
        this->write_id = (this->write_id + 1) % this->size;
        this->nb_entries++;
    }
}


// Get called when we read out an entry from the buffer.
BufferEntry sequencer::read_entry(int index)
{
    if (this->isEmpty()) 
    {
        this->trace.msg("Sequence buffer is empty and no instruction can be read\n");
        // Todo: write a return here to avoid accident happening.
        // For now return a dummy one to avoid gcc warning
        return BufferEntry();
    }
    else
    {
        BufferEntry entry = this->RingBuffer[index];
        this->trace.msg("Read IO request in buffer index %d (opcode: 0x%llx, pc: 0x%llx)\n", index, this->RingBuffer[index].req.insn.opcode, this->RingBuffer[index].req.pc);

        // Assign read_id to index of next instruction
        this->read_id = entry.next_entry;
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Update buffer read_id to %d after a read\n", this->read_id);

        // Update register index for trace if this instruction's stagger_max>0
        if (!entry.isn_sequence & entry.config.stagger_max > 0)
        {
            int nb_args = entry.req.insn.decoder_item->u.insn.nb_args;
            for (int i = 0; i < nb_args; i++)
            {
                iss_decoder_arg_t *arg = &entry.req.insn.decoder_item->u.insn.args[i];
                iss_insn_arg_t *insn_arg = &entry.req.insn.args[i];
                if ((arg->type == ISS_DECODER_ARG_TYPE_OUT_REG || arg->type == ISS_DECODER_ARG_TYPE_IN_REG) && (insn_arg->u.reg.index != 0 || arg->flags & ISS_DECODER_ARG_FLAG_FREG))
                {
                    if (arg->type == ISS_DECODER_ARG_TYPE_OUT_REG)
                    {
                        insn_arg->u.reg.index = entry.req.insn.out_regs[arg->u.reg.id];
                    }
                    else if (arg->type == ISS_DECODER_ARG_TYPE_IN_REG)
                    {
                        insn_arg->u.reg.index = entry.req.insn.in_regs[arg->u.reg.id];
                    }
                }
            }
        }

        return entry;
    }
}


// Get called after a read operation, the iteration variables and next_entry index need to be updated for the next read operation.
void sequencer::update_entry(int index)
{
    BufferEntry *entry = &this->RingBuffer[index];

    // Number of iterations decreases after a read
    entry->max_rpt--;

    if(!entry->isn_sequence)
    {
        // Update next_entry index for the next iteration
        bool insn_last = false;
        // base_entry and config.max_inst are const variables after configuration.
        if (index == ((entry->base_entry + entry->config.max_inst) % this->size))
        {
            insn_last = true;
        }

        bool rpt_last = false;
        if (entry->max_rpt == 0)
        {
            // The next iteration is the last iterations
            rpt_last = true;
        }

        // Find the index of next instruction 
        if (entry->config.is_outer)
        {
            if (!insn_last)
            {
                // There is still instruction following in this configuration.
                entry->next_entry = (index + 1) % this->size; 
            }
            else
            {
                // This is the last instruction in this configuration.
                if (!rpt_last)
                {
                    // If there's still iteration left, go back to the initial instruction index.
                    entry->next_entry = entry->base_entry; 
                }
                else
                {
                    // No iteration, move to the following entry in buffer.
                    entry->next_entry = (index + 1) % this->size; 
                }
            }
        }
        else 
        {
            // inner loop
            if(rpt_last)
            {
                // No iteration, move to the following entry in buffer.
                entry->next_entry = (index + 1) % this->size; 
            }
            else
            {
                // Repeat itself, next instruction remains itself
                entry->next_entry = index;
            }
        }

        // Update stagger related information
        int temp_out = entry->req.insn.out_regs[0] + entry->stagger_max;
        int temp_in1 = entry->req.insn.in_regs[0] + entry->stagger_max;
        int temp_in2 = entry->req.insn.in_regs[1] + entry->stagger_max;
        int temp_in3 = entry->req.insn.in_regs[2] + entry->stagger_max;
    
        entry->stagger_max --;
        int stagger_cnt = entry->stagger_max;

        if (entry->stagger_mask & 0x1)
        {
            if (stagger_cnt < 0)
            {
                entry->stagger_max = entry->config.stagger_max;
                entry->req.insn.out_regs[0] = temp_out - entry->stagger_max;
            }
            else
            {
                entry->req.insn.out_regs[0]++;
            }
            this->trace.msg(vp::Trace::LEVEL_TRACE, "Update rd to %d (stagger_mask: 0x%llx)\n", entry->req.insn.out_regs[0], entry->stagger_mask);
        }

        if (entry->stagger_mask & 0x2)
        {
            if (stagger_cnt < 0)
            {
                entry->stagger_max = entry->config.stagger_max;
                entry->req.insn.in_regs[0] = temp_in1 - entry->stagger_max;
            }
            else
            {
                entry->req.insn.in_regs[0]++;
            }
            this->trace.msg(vp::Trace::LEVEL_TRACE, "Update rs1 to %d (stagger_mask: 0x%llx)\n", entry->req.insn.in_regs[0], entry->stagger_mask);
        }

        if (entry->stagger_mask & 0x4)
        {
            if (stagger_cnt < 0)
            {
                entry->stagger_max = entry->config.stagger_max;
                entry->req.insn.in_regs[1] = temp_in2 - entry->stagger_max;
            }
            else
            {
                entry->req.insn.in_regs[1]++;
            }
            this->trace.msg(vp::Trace::LEVEL_TRACE, "Update rs2 to %d (stagger_mask: 0x%llx)\n", entry->req.insn.in_regs[1], entry->stagger_mask);
        }

        if (entry->stagger_mask & 0x8)
        {
            if (stagger_cnt < 0)
            {
                entry->stagger_max = entry->config.stagger_max;
                entry->req.insn.in_regs[2] = temp_in3 - entry->stagger_max;
            }
            else
            {
                entry->req.insn.in_regs[2]++;
            }
            this->trace.msg(vp::Trace::LEVEL_TRACE, "Update rs3 to %d (stagger_mask: 0x%llx)\n", entry->req.insn.in_regs[2], entry->stagger_mask);
        }

        this->trace.msg(vp::Trace::LEVEL_TRACE, "Update sequence frep configuration in buffer index %d (is_outer: %d, max_inst: %d, max_rpt: %d, stagger_max: %d, stagger_mask: 0x%llx, base_id: %d, next_id: %d)\n", 
            index, entry->is_outer, entry->max_inst, entry->max_rpt, entry->stagger_max, entry->stagger_mask, entry->base_entry, entry->next_entry);
    }

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Update buffer index %d (opcode: 0x%llx, pc: 0x%llx)\n", index, this->RingBuffer[index].req.insn.opcode, this->RingBuffer[index].req.pc);

}


// Get called when we need to check whether the buffer is full.
bool sequencer::isFull()
{
    if (this->nb_entries >= this->size)
    {
        return true;
    }
    else
    {
        return false;
    }
}


// Get called when we need to check whether the buffer is empty.
bool sequencer::isEmpty()
{
    if(!this->nb_entries)
    {
        return true;
    }
    else
    {
        return false;
    }
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
  return new sequencer(config);
}


/*
 * Copyright (C) 2025 Fondazione Chips-IT
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
 * Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <stdio.h>
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <stdint.h>

#include <cpu/iss/include/offload.hpp>

enum fsm_state {
    IDLE,
    POLL_STS_REG
};

// IDMA UTILS
#define OP_CUSTOM1 0b0101011
#define XDMA_FUNCT3 0b000
#define DMSRC_FUNCT7 0b0000000
#define DMDST_FUNCT7 0b0000001
#define DMCPYI_FUNCT7 0b0000010
#define DMCPYC_FUNCT7 0b0000011
#define DMSTATI_FUNCT7 0b0000100
#define DMMASK_FUNCT7 0b0000101
#define DMSTR_FUNCT7 0b0000110
#define DMREP_FUNCT7 0b0000111

#define R_TYPE_ENCODE(funct7, rs2, rs1, funct3, rd, opcode)                    \
    ((funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | \
     (opcode))

/*****************************************************
*                   Class Definition                 *
*****************************************************/


class Magia_iDMA_Ctrl : public vp::Component
{

public:
    Magia_iDMA_Ctrl(vp::ComponentConf &config);

protected:

    static void fsm_handler_dma0(vp::Block *__this, vp::ClockEvent *event);
    static void fsm_handler_dma1(vp::Block *__this, vp::ClockEvent *event);

    static void offload_sync_m(vp::Block *__this, IssOffloadInsn<uint32_t> *insn);
    vp::WireSlave<IssOffloadInsn<uint32_t> *> offload_itf_m;
    vp::WireMaster<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf_m;

    vp::WireMaster<IssOffloadInsn<uint32_t> *> offload_itf_idma0;
    static void grant_sync_idma0(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result);
    vp::WireSlave<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf_idma0;

    vp::WireMaster<IssOffloadInsn<uint32_t> *> offload_itf_idma1;
    static void grant_sync_idma1(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result);
    vp::WireSlave<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf_idma1;


    vp::WireMaster<bool> done_dma0;
    vp::WireMaster<bool> done_dma1;

    vp::ClockEvent *fsm_event_dma0;
    vp::ClockEvent *fsm_event_dma1;
    vp::reg_32 state_dma0;
    vp::reg_32 state_dma1;

    uint32_t len_dma0;
    uint32_t len_dma1;

    uint32_t reps2_dma0;
    uint32_t reps2_dma1;

    uint64_t dma0_transfer_time_start;
    uint64_t dma0_transfer_time_stop;

    uint64_t dma1_transfer_time_start;
    uint64_t dma1_transfer_time_stop;

    vp::Trace trace;
};

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Magia_iDMA_Ctrl(config);
}

Magia_iDMA_Ctrl::Magia_iDMA_Ctrl(vp::ComponentConf &config)
    : vp::Component(config)
{
    //Initialize interface
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->offload_itf_m.set_sync_meth(&Magia_iDMA_Ctrl::offload_sync_m);
    this->new_slave_port("offload_m", &this->offload_itf_m, this);
    this->new_master_port("offload_grant_m", &this->offload_grant_itf_m, this);

    this->new_master_port("offload_idma0_axi2obi", &this->offload_itf_idma0, this);
    //this->offload_grant_itf_idma0.set_sync_meth(&Magia_iDMA_Ctrl::grant_sync_idma0);
    this->new_slave_port("offload_grant_idma0_axi2obi", &this->offload_grant_itf_idma0, this);

    this->new_master_port("offload_idma1_obi2axi", &this->offload_itf_idma1, this);
    //this->offload_grant_itf_idma1.set_sync_meth(&Magia_iDMA_Ctrl::grant_sync_idma1);
    this->new_slave_port("offload_grant_idma1_obi2axi", &this->offload_grant_itf_idma1, this);

    this->new_master_port("idma0_done_irq", &this->done_dma0, this);
    this->new_master_port("idma1_done_irq", &this->done_dma1, this);

    this->fsm_event_dma0 = this->event_new(&Magia_iDMA_Ctrl::fsm_handler_dma0);
    this->fsm_event_dma1 = this->event_new(&Magia_iDMA_Ctrl::fsm_handler_dma1);
    //Initialize FSMs
    this->state_dma0.set(IDLE);
    this->state_dma1.set(IDLE);

    this->len_dma0=0;
    this->len_dma1=0;

    this->reps2_dma0=0;
    this->reps2_dma1=0;

    this->dma0_transfer_time_start=0;
    this->dma0_transfer_time_stop=0;

    this->dma1_transfer_time_start=0;
    this->dma1_transfer_time_stop=0;

    this->trace.msg(vp::Trace::LEVEL_TRACE,"[Magia iDMA Ctrl] Instantiated\n");

}

void Magia_iDMA_Ctrl::fsm_handler_dma0(vp::Block *__this, vp::ClockEvent *event) {
    Magia_iDMA_Ctrl *_this = (Magia_iDMA_Ctrl *)__this;

    IssOffloadInsn<uint32_t> dmstati_dma0;

    switch (_this->state_dma0.get()) {
        case IDLE:
        {    
            _this->dma0_transfer_time_stop=_this->clock.get_cycles(); 
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[Magia iDMA Ctrl] DMA0 In IDLE. Transfer cycles are %ld\n",_this->dma0_transfer_time_stop-_this->dma0_transfer_time_start);
            // IssOffloadInsnGrant<uint32_t> offload_grant = {
            //     .result=0x0
            // };
            // _this->offload_grant_itf_m.sync(&offload_grant);
            _this->done_dma0.sync(false); //clear interrupt
            break;
        }
        case POLL_STS_REG:
        {
            dmstati_dma0.opcode=R_TYPE_ENCODE(DMSTATI_FUNCT7, 0b10, 0, XDMA_FUNCT3, 5, OP_CUSTOM1);
            dmstati_dma0.arg_b=0b10;
            _this->offload_itf_idma0.sync(&dmstati_dma0); //send dmstati
            if(dmstati_dma0.result==0) {
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[Magia iDMA Ctrl] DMA0 transfer completed\n");
                _this->done_dma0.sync(true); //send interrupt
                _this->state_dma0.set(IDLE);
                _this->event_enqueue(_this->fsm_event_dma0, 1); //trigger fsm
            } 
            else {
                _this->state_dma0.set(POLL_STS_REG);
                _this->event_enqueue(_this->fsm_event_dma0, 1); //trigger fsm
            }
            break;
        }
    }
}

void Magia_iDMA_Ctrl::fsm_handler_dma1(vp::Block *__this, vp::ClockEvent *event) {
    Magia_iDMA_Ctrl *_this = (Magia_iDMA_Ctrl *)__this;

    IssOffloadInsn<uint32_t> dmstati_dma1;

    switch (_this->state_dma1.get()) {
        case IDLE:
        {    
            _this->dma1_transfer_time_stop=_this->clock.get_cycles();
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[Magia iDMA Ctrl] DMA1 In IDLE. Transfer cycles are %ld\n",_this->dma1_transfer_time_stop-_this->dma1_transfer_time_start);
            // IssOffloadInsnGrant<uint32_t> offload_grant = {
            //     .result=0x0
            // };
            // _this->offload_grant_itf_m.sync(&offload_grant);
            _this->done_dma1.sync(false); //clear interrupt
            break;
        }
        case POLL_STS_REG:
        {
            dmstati_dma1.opcode=R_TYPE_ENCODE(DMSTATI_FUNCT7, 0b10, 0, XDMA_FUNCT3, 5, OP_CUSTOM1);
            dmstati_dma1.arg_b=0b10;
            _this->offload_itf_idma1.sync(&dmstati_dma1); //send dmstati
            if(dmstati_dma1.result==0) {
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[Magia iDMA Ctrl] DMA1 transfer completed\n");
                _this->done_dma1.sync(true); //send interrupt
                _this->state_dma1.set(IDLE);
                _this->event_enqueue(_this->fsm_event_dma1, 1); //trigger fsm
            } 
            else {
                _this->state_dma1.set(POLL_STS_REG);
                _this->event_enqueue(_this->fsm_event_dma1, 1); //trigger fsm
            }
            break;
        }
    }
}

void Magia_iDMA_Ctrl::offload_sync_m(vp::Block *__this, IssOffloadInsn<uint32_t> *insn)
{
    Magia_iDMA_Ctrl *_this = (Magia_iDMA_Ctrl *)__this;
    uint32_t opc = insn->opcode & 0x7F;
    uint32_t func3 = (insn->opcode >> 12) & 0x7;
    bool dir = (insn->opcode >> 25) & 0x1;

    IssOffloadInsn<uint32_t> dmsrc; 
    IssOffloadInsn<uint32_t> dmdst;
    IssOffloadInsn<uint32_t> dmstr;
    IssOffloadInsn<uint32_t> dmrep;
    IssOffloadInsn<uint32_t> dmcpyi;

    uint32_t src_addr;
    uint32_t dst_addr;
    uint32_t src_stride;
    uint32_t dst_stride;

    switch (opc)
    {
        case 0b1011011:
        {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[Magia iDMA Ctrl] received opcode dmcnf for IDMA - DIR: %d\n",dir);
            insn->granted = true; //immeditaly grant back the core
            insn->result = 0x0;
            //nothing to configure for now...
            break;
        }
        case 0b1111011:
        {
            if (func3==0b111) {
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[Magia iDMA Ctrl] received opcode dmstr for IDMA - DIR: %d\n",dir);
                insn->granted = true; //immeditaly grant back the core
                insn->result = 0x0;
                if (!dir) { //dma0
                    //set dmrep
                    dmrep.opcode=R_TYPE_ENCODE(DMREP_FUNCT7, 0, 17, XDMA_FUNCT3, 0, OP_CUSTOM1);
                    dmrep.arg_a=_this->reps2_dma0;
                    //set dmcpyi
                    if (_this->reps2_dma0==0) {
                        dmcpyi.opcode=R_TYPE_ENCODE(DMCPYI_FUNCT7, 0b00000, 14, XDMA_FUNCT3, 10, OP_CUSTOM1);
                        dmcpyi.arg_a=_this->len_dma0;
                        dmcpyi.arg_b=0b00000; //configure the transfer as a 1d transfer
                    }
                    else {
                        dmcpyi.opcode=R_TYPE_ENCODE(DMCPYI_FUNCT7, 0b00010, 14, XDMA_FUNCT3, 10, OP_CUSTOM1);
                        dmcpyi.arg_a=_this->len_dma0;
                        dmcpyi.arg_b=0b00010; //configure the transfer as a 2d transfer
                    }
                    if (_this->state_dma0.get()!=IDLE) {
                        _this->trace.fatal("[Magia iDMA Ctrl] Received dmstr for IDMA - DIR: %d when not in IDLE state\n",dir);
                    }
                    else {
                        _this->dma0_transfer_time_start=_this->clock.get_cycles();
                        _this->offload_itf_idma0.sync(&dmrep);
                        _this->offload_itf_idma0.sync(&dmcpyi);
                        _this->state_dma0.set(POLL_STS_REG);
                        _this->event_enqueue(_this->fsm_event_dma0, 1); //trigger fsm
                    }
                }
                else { //dma1
                    //set dmrep
                    dmrep.opcode=R_TYPE_ENCODE(DMREP_FUNCT7, 0, 17, XDMA_FUNCT3, 0, OP_CUSTOM1);
                    dmrep.arg_a=_this->reps2_dma1;
                    //set dmcpyi
                    if (_this->reps2_dma1==0) {
                        dmcpyi.opcode=R_TYPE_ENCODE(DMCPYI_FUNCT7, 0b00000, 14, XDMA_FUNCT3, 10, OP_CUSTOM1);
                        dmcpyi.arg_a=_this->len_dma1;
                        dmcpyi.arg_b=0b00000; //configure the transfer as a 1d transfer
                    }
                    else {
                        dmcpyi.opcode=R_TYPE_ENCODE(DMCPYI_FUNCT7, 0b00010, 14, XDMA_FUNCT3, 10, OP_CUSTOM1);
                        dmcpyi.arg_a=_this->len_dma1;
                        dmcpyi.arg_b=0b00010; //configure the transfer as a 2d transfer
                    }
                    if (_this->state_dma1.get()!=IDLE) {
                        _this->trace.fatal("[Magia iDMA Ctrl] Received dmstr for IDMA - DIR: %d when not in IDLE state\n",dir);
                    }
                    else {
                        _this->dma1_transfer_time_start=_this->clock.get_cycles();
                        _this->offload_itf_idma1.sync(&dmrep);
                        _this->offload_itf_idma1.sync(&dmcpyi);
                        _this->state_dma1.set(POLL_STS_REG);
                        _this->event_enqueue(_this->fsm_event_dma1, 1); //trigger fsm
                    }
                }
                break;
            }
            else {
                switch (func3)
                {
                    case 0b000: //1d transfer
                    {
                        _this->trace.msg(vp::Trace::LEVEL_TRACE,"[Magia iDMA Ctrl] received opcode dm1d2d3d for IDMA - 1D [SRC=0x%08x - DST=0x%08x - LEN=%d - DIR: %d]\n",insn->arg_b,insn->arg_c,insn->arg_a,dir);
                        insn->granted = true; //immeditaly grant back the core
                        insn->result = 0x0;
                        (!dir) ? (_this->len_dma0 = insn->arg_a) : (_this->len_dma1 = insn->arg_a);
                        src_addr=insn->arg_b;
                        dst_addr=insn->arg_c;
                        //set src addr
                        dmsrc.opcode=R_TYPE_ENCODE(DMSRC_FUNCT7, 13, 12, XDMA_FUNCT3, 0, OP_CUSTOM1);
                        dmsrc.arg_a=src_addr; //low addr
                        dmsrc.arg_b=0x0; //high addr
                        dmsrc.granted=true;
                        //set dst addr
                        dmdst.opcode=R_TYPE_ENCODE(DMDST_FUNCT7, 11, 10, XDMA_FUNCT3, 0, OP_CUSTOM1);
                        dmdst.arg_a=dst_addr; //low addr
                        dmdst.arg_b=0x0; //high addr
                        dmdst.granted=true;
                        if(!dir) {
                            _this->offload_itf_idma0.sync(&dmsrc);
                            _this->offload_itf_idma0.sync(&dmdst);
                        }
                        else {
                            _this->offload_itf_idma1.sync(&dmsrc);
                            _this->offload_itf_idma1.sync(&dmdst);
                        }
                        break;
                    }
                    case 0b001: //2d transfer
                    {
                        _this->trace.msg(vp::Trace::LEVEL_TRACE,"[Magia iDMA Ctrl] received opcode dm1d2d3d for IDMA - 2D - [SRC_STR=0x%08x - DST_STR=0x%08x - REP=%d - DIR: %d]\n",insn->arg_b,insn->arg_c,insn->arg_a,dir);
                        insn->granted = true; //immeditaly grant back the core
                        insn->result = 0x0;
                        (!dir) ? (_this->reps2_dma0 = insn->arg_a) : (_this->reps2_dma1 = insn->arg_a);
                        src_stride=insn->arg_b;
                        dst_stride=insn->arg_c;
                        //set dmstr
                        dmstr.opcode=R_TYPE_ENCODE(DMSTR_FUNCT7, 15, 16, XDMA_FUNCT3, 0, OP_CUSTOM1);
                        dmstr.arg_a=src_stride;
                        dmstr.arg_b=dst_stride;
                        dmstr.granted=true;
                        if(!dir) {
                            _this->offload_itf_idma0.sync(&dmstr);
                        }
                        else {
                            _this->offload_itf_idma1.sync(&dmstr);
                        }
                        break;
                    }
                    case 0b010:
                        _this->trace.msg(vp::Trace::LEVEL_TRACE,"[Magia iDMA Ctrl] received opcode dm1d2d3d for IDMA - 3D - DIR: %d\n",dir);
                        insn->granted = true; //immeditaly grant back the core
                        insn->result = 0x0;
                        //3d data movement are not supported yet.
                        break;
                    default:
                        _this->trace.fatal("[Magia iDMA Ctrl] Received wrong func3\n");
                        break;
                }
            }
            break;
        }
        default:
            _this->trace.fatal("[Magia iDMA Ctrl] Received wrong opcode\n");
            break;
    }
}


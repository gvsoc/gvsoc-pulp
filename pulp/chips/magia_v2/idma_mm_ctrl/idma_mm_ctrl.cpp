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

// iDMA Memory-Mapped Register Base Addresses
#define IDMA_MM_DIRECTION_OFFSET (0x200) //direction=0x00, L2 to L1  direction=0x200, L1 to L2

#define IDMA_CONF_OFFSET          (0x00)
#define IDMA_STATUS_OFFSET        (0x04)  
#define IDMA_NEXT_ID_OFFSET       (0x44)  
#define IDMA_DONE_ID_OFFSET       (0x84)  
#define IDMA_DST_ADDR_LOW_OFFSET  (0xD0)
#define IDMA_SRC_ADDR_LOW_OFFSET  (0xD8)
#define IDMA_LENGTH_LOW_OFFSET    (0xE0)
#define IDMA_DST_STRIDE_2_LOW_OFFSET (0xE8)
#define IDMA_SRC_STRIDE_2_LOW_OFFSET (0xF0)
#define IDMA_REPS_2_LOW_OFFSET    (0xF8)
#define IDMA_DST_STRIDE_3_LOW_OFFSET (0x100)
#define IDMA_SRC_STRIDE_3_LOW_OFFSET (0x108)
#define IDMA_REPS_3_LOW_OFFSET    (0x110)

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


class iDMA_mm_ctrl : public vp::Component
{

public:
    iDMA_mm_ctrl(vp::ComponentConf &config);

protected:

    static void fsm_handler_dma0(vp::Block *__this, vp::ClockEvent *event);
    static void fsm_handler_dma1(vp::Block *__this, vp::ClockEvent *event);

    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

    vp::WireMaster<IssOffloadInsn<uint32_t> *> offload_itf_idma0;
    static void grant_sync_idma0(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result);
    vp::WireSlave<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf_idma0;

    vp::WireMaster<IssOffloadInsn<uint32_t> *> offload_itf_idma1;
    static void grant_sync_idma1(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result);
    vp::WireSlave<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf_idma1;

    vp::IoSlave         input_itf;

    //DMA0 registers and ports
    vp::WireMaster<bool> done_dma0;

    vp::ClockEvent *fsm_event_dma0;
    vp::reg_32 fsm_state_dma0;

    vp::reg_32 config_reg_dma0;
    vp::reg_32 status_reg_dma0;
    vp::reg_32 next_id_reg_dma0;
    vp::reg_32 done_id_reg_dma0;
    vp::reg_32 src_addr_reg_dma0;
    vp::reg_32 dst_addr_reg_dma0;
    vp::reg_32 len_reg_dma0;
    vp::reg_32 src_stride_2_reg_dma0;
    vp::reg_32 dst_stride_2_reg_dma0;
    vp::reg_32 reps2_reg_dma0;
    vp::reg_32 src_stride_3_reg_dma0;
    vp::reg_32 dst_stride_3_reg_dma0;
    vp::reg_32 reps3_reg_dma0;

    uint64_t dma0_transfer_time_start;
    uint64_t dma0_transfer_time_stop;

    //DMA1 registers and ports
    vp::WireMaster<bool> done_dma1;
    
    vp::ClockEvent *fsm_event_dma1;
    vp::reg_32 fsm_state_dma1;

    vp::reg_32 config_reg_dma1;
    vp::reg_32 status_reg_dma1;
    vp::reg_32 next_id_reg_dma1;
    vp::reg_32 done_id_reg_dma1;
    vp::reg_32 src_addr_reg_dma1;
    vp::reg_32 dst_addr_reg_dma1;
    vp::reg_32 len_reg_dma1;
    vp::reg_32 src_stride_2_reg_dma1;
    vp::reg_32 dst_stride_2_reg_dma1;
    vp::reg_32 reps2_reg_dma1;
    vp::reg_32 src_stride_3_reg_dma1;
    vp::reg_32 dst_stride_3_reg_dma1;
    vp::reg_32 reps3_reg_dma1;

    uint64_t dma1_transfer_time_start;
    uint64_t dma1_transfer_time_stop;

    vp::Trace trace;
};

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new iDMA_mm_ctrl(config);
}

iDMA_mm_ctrl::iDMA_mm_ctrl(vp::ComponentConf &config)
    : vp::Component(config)
{
    //Initialize interface
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->input_itf.set_req_meth(&iDMA_mm_ctrl::req);
    this->new_slave_port("input", &this->input_itf);

    this->new_master_port("offload_idma0_axi2obi", &this->offload_itf_idma0, this);
    //this->offload_grant_itf_idma0.set_sync_meth(&iDMA_mm_ctrl::grant_sync_idma0);
    this->new_slave_port("offload_grant_idma0_axi2obi", &this->offload_grant_itf_idma0, this);

    this->new_master_port("offload_idma1_obi2axi", &this->offload_itf_idma1, this);
    //this->offload_grant_itf_idma1.set_sync_meth(&iDMA_mm_ctrl::grant_sync_idma1);
    this->new_slave_port("offload_grant_idma1_obi2axi", &this->offload_grant_itf_idma1, this);

    this->new_master_port("idma0_done_irq", &this->done_dma0, this);
    this->new_master_port("idma1_done_irq", &this->done_dma1, this);

    this->fsm_event_dma0 = this->event_new(&iDMA_mm_ctrl::fsm_handler_dma0);
    this->fsm_event_dma1 = this->event_new(&iDMA_mm_ctrl::fsm_handler_dma1);

    //Initialize FSMs and Regs for iDMA0
    this->fsm_state_dma0.set(IDLE);

    this->config_reg_dma0.set(0x00000000);
    this->src_addr_reg_dma0.set(0x00000000);
    this->dst_addr_reg_dma0.set(0x00000000);
    this->len_reg_dma0.set(0x00000000);
    this->dst_stride_2_reg_dma0.set(0x00000000);
    this->src_stride_2_reg_dma0.set(0x00000000);
    this->reps2_reg_dma0.set(0x00000000);
    this->src_stride_3_reg_dma0.set(0x00000000);
    this->dst_stride_3_reg_dma0.set(0x00000000);
    this->reps3_reg_dma0.set(0x00000000);

    this->dma0_transfer_time_start=0;
    this->dma0_transfer_time_stop=0;

    //Initialize FSMs and Regs for iDMA1
    this->fsm_state_dma1.set(IDLE);

    this->config_reg_dma1.set(0x00000000);
    this->src_addr_reg_dma1.set(0x00000000);
    this->dst_addr_reg_dma1.set(0x00000000);
    this->len_reg_dma1.set(0x00000000);
    this->dst_stride_2_reg_dma1.set(0x00000000);
    this->src_stride_2_reg_dma1.set(0x00000000);
    this->reps2_reg_dma1.set(0x00000000);
    this->src_stride_3_reg_dma1.set(0x00000000);
    this->dst_stride_3_reg_dma1.set(0x00000000);
    this->reps3_reg_dma1.set(0x00000000);

    this->dma1_transfer_time_start=0;
    this->dma1_transfer_time_stop=0;

    this->trace.msg(vp::Trace::LEVEL_TRACE,"[Magia iDMA Ctrl] Instantiated\n");

}

void iDMA_mm_ctrl::fsm_handler_dma0(vp::Block *__this, vp::ClockEvent *event) {
    iDMA_mm_ctrl *_this = (iDMA_mm_ctrl *)__this;

    IssOffloadInsn<uint32_t> dmstati_dma0;

    switch (_this->fsm_state_dma0.get()) {
        case IDLE:
        {   
            _this->dma0_transfer_time_stop=_this->clock.get_cycles(); 
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[Magia iDMA Ctrl] DMA0 In IDLE. Transfer cycles are %ld\n",_this->dma0_transfer_time_stop-_this->dma0_transfer_time_start);
            _this->done_dma0.sync(false); //clear interrupt
            break;
        }
        case POLL_STS_REG:
        {
            dmstati_dma0.opcode=R_TYPE_ENCODE(DMSTATI_FUNCT7, 0b10, 0, XDMA_FUNCT3, 5, OP_CUSTOM1);
            dmstati_dma0.arg_b=0b10;
            _this->offload_itf_idma0.sync(&dmstati_dma0); //send dmstati
            _this->status_reg_dma0.set(dmstati_dma0.result);
            if(dmstati_dma0.result==0) {
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[Magia iDMA Ctrl] DMA0 transfer completed\n");
                //update done register
                dmstati_dma0.arg_b=0b0;
                _this->offload_itf_idma0.sync(&dmstati_dma0); //send dmstati
                _this->done_id_reg_dma0.set(dmstati_dma0.result);
                //send interrupt
                _this->done_dma0.sync(true);
                //set fsm to idle
                _this->fsm_state_dma0.set(IDLE);
                //trigger fsm
                _this->event_enqueue(_this->fsm_event_dma0, 1);
            } 
            else {
                _this->fsm_state_dma0.set(POLL_STS_REG);
                _this->event_enqueue(_this->fsm_event_dma0, 1); //trigger fsm
            }
            break;
        }
    }
}

void iDMA_mm_ctrl::fsm_handler_dma1(vp::Block *__this, vp::ClockEvent *event) {
    iDMA_mm_ctrl *_this = (iDMA_mm_ctrl *)__this;

    IssOffloadInsn<uint32_t> dmstati_dma1;

    switch (_this->fsm_state_dma1.get()) {
        case IDLE:
        {    
            _this->dma1_transfer_time_stop=_this->clock.get_cycles();
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[Magia iDMA Ctrl] DMA1 In IDLE. Transfer cycles are %ld\n",_this->dma1_transfer_time_stop-_this->dma1_transfer_time_start);
            _this->done_dma1.sync(false); //clear interrupt
            break;
        }
        case POLL_STS_REG:
        {
            dmstati_dma1.opcode=R_TYPE_ENCODE(DMSTATI_FUNCT7, 0b10, 0, XDMA_FUNCT3, 5, OP_CUSTOM1);
            dmstati_dma1.arg_b=0b10;
            _this->offload_itf_idma1.sync(&dmstati_dma1); //send dmstati
            _this->status_reg_dma1.set(dmstati_dma1.result);
            if(dmstati_dma1.result==0) {
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[Magia iDMA Ctrl] DMA1 transfer completed\n");
                //update done register
                dmstati_dma1.arg_b=0b0;
                _this->offload_itf_idma1.sync(&dmstati_dma1); //send dmstati
                _this->done_id_reg_dma1.set(dmstati_dma1.result);
                //send interrupt
                _this->done_dma1.sync(true);
                //set fsm to idle
                _this->fsm_state_dma1.set(IDLE);
                //trigger fsm
                _this->event_enqueue(_this->fsm_event_dma1, 1);
            } 
            else {
                _this->fsm_state_dma1.set(POLL_STS_REG);
                _this->event_enqueue(_this->fsm_event_dma1, 1); //trigger fsm
            }
            break;
        }
    }
}

vp::IoReqStatus iDMA_mm_ctrl::req(vp::Block *__this, vp::IoReq *req)
{
    iDMA_mm_ctrl *_this = (iDMA_mm_ctrl *)__this;

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    if (size!=4) {
         _this->trace.fatal("[Magia iDMA Ctrl] Memory mapped interface supports only 32 bits (4 bytes) buses\n");
    }

    if(offset & IDMA_MM_DIRECTION_OFFSET) { // Dir=1 L1 to L2
        offset = offset & 0x1ff;
        if (offset == IDMA_CONF_OFFSET) {
            if (is_write == 1) {
                uint32_t cnf_w;
                memcpy((uint8_t*)&cnf_w,data,size);
                _this->config_reg_dma1.set(cnf_w);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Writing CONFIG_REG=0x%08x\n",cnf_w);
            }
            else {
                uint32_t cnf_r =  _this->config_reg_dma1.get();
                memcpy((void *)data, (void *)&cnf_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Reading CONFIG_REG=0x%08x\n",cnf_r);
            }
        }
        else if (offset == IDMA_STATUS_OFFSET) {
            if (is_write == 1) {
                _this->trace.fatal("[Magia iDMA Ctrl] IDMA1 Writing on STATUS_REG is not permitted\n");
            }
            else {
                uint32_t sts_r =  _this->status_reg_dma1.get();
                memcpy((void *)data, (void *)&sts_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Reading STATUS_REG=0x%08x\n",sts_r);
            }
        }
        else if (offset == IDMA_NEXT_ID_OFFSET) {
            if (is_write == 1) {
                _this->trace.fatal("[Magia iDMA Ctrl] IDMA1 Writing on IDMA_NEXT_ID_OFFSET is not permitted\n");
            }
            else {
                IssOffloadInsn<uint32_t> dmstati_dma1;
                dmstati_dma1.opcode=R_TYPE_ENCODE(DMSTATI_FUNCT7, 0b01, 0, XDMA_FUNCT3, 5, OP_CUSTOM1);
                dmstati_dma1.arg_b=0b01; //type of status register we are getting form iDMA. 0b01 means next transfer id
                _this->offload_itf_idma1.sync(&dmstati_dma1); //send dmstati
                _this->next_id_reg_dma1.set(dmstati_dma1.result - 1); //fixed next id to be coherent with RTL value
                uint32_t next_id_r =  _this->next_id_reg_dma1.get();
                memcpy((void *)data, (void *)&next_id_r, size);
                if (_this->fsm_state_dma1.get()!=IDLE) {
                    _this->trace.fatal("[Magia iDMA Ctrl] IDMA1 Received read IDMA_NEXT_ID_OFFSET for IDMA - DIR: 0 when not in IDLE state\n");
                }
                else {
                    _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Reading IDMA_NEXT_ID_OFFSET=0x%08x\n",next_id_r);
                    _this->dma1_transfer_time_start=_this->clock.get_cycles();
                    IssOffloadInsn<uint32_t> dmsrc; 
                    IssOffloadInsn<uint32_t> dmdst;
                    IssOffloadInsn<uint32_t> dmrep;
                    IssOffloadInsn<uint32_t> dmstr;
                    IssOffloadInsn<uint32_t> dmcpyi;
                    //set src addr
                    dmsrc.opcode=R_TYPE_ENCODE(DMSRC_FUNCT7, 13, 12, XDMA_FUNCT3, 0, OP_CUSTOM1);
                    dmsrc.arg_a=_this->src_addr_reg_dma1.get(); //low addr
                    dmsrc.arg_b=0x0; //high addr
                    dmsrc.granted=true;
                    _this->offload_itf_idma1.sync(&dmsrc);
                    //set dst addr
                    dmdst.opcode=R_TYPE_ENCODE(DMDST_FUNCT7, 11, 10, XDMA_FUNCT3, 0, OP_CUSTOM1);
                    dmdst.arg_a=_this->dst_addr_reg_dma1.get();; //low addr
                    dmdst.arg_b=0x0; //high addr
                    dmdst.granted=true;
                    _this->offload_itf_idma1.sync(&dmdst);
                    //set dmrep
                    dmrep.opcode=R_TYPE_ENCODE(DMREP_FUNCT7, 0, 17, XDMA_FUNCT3, 0, OP_CUSTOM1);
                    dmrep.arg_a=_this->reps2_reg_dma1.get();
                    _this->offload_itf_idma1.sync(&dmrep);
                    //set dmstr
                    dmstr.opcode=R_TYPE_ENCODE(DMSTR_FUNCT7, 15, 16, XDMA_FUNCT3, 0, OP_CUSTOM1);
                    dmstr.arg_a=_this->src_stride_2_reg_dma1.get();
                    dmstr.arg_b=_this->dst_stride_2_reg_dma1.get();
                    dmstr.granted=true;
                    _this->offload_itf_idma1.sync(&dmstr);
                    //set dmcpyi 0b00010
                    //dmcpyi.opcode=R_TYPE_ENCODE(DMCPYI_FUNCT7, _this->config_reg_dma1.get(), 14, XDMA_FUNCT3, 10, OP_CUSTOM1);
                    dmcpyi.opcode=R_TYPE_ENCODE(DMCPYI_FUNCT7, 0b00010, 14, XDMA_FUNCT3, 10, OP_CUSTOM1);
                    dmcpyi.arg_a=_this->len_reg_dma1.get();
                    dmcpyi.arg_b=0b00010; //this is actually redundant as it is already passed in the R_TYPE_ENCODE() however let's keep it for the sake of clarity
                    //dmcpyi.arg_b=_this->config_reg_dma1.get(); //this is actually redundant as it is already passed in the R_TYPE_ENCODE() however let's keep it for the sake of clarity
                    _this->offload_itf_idma1.sync(&dmcpyi);
                    //trigger internal fsm
                    _this->fsm_state_dma1.set(POLL_STS_REG);
                    _this->event_enqueue(_this->fsm_event_dma1, 1); //trigger fsm
                }
            }
        }
        else if (offset == IDMA_DONE_ID_OFFSET) {
            if (is_write == 1) {
                _this->trace.fatal("[Magia iDMA Ctrl] IDMA1 Writing on IDMA_DONE_ID_OFFSET is not permitted\n");
            }
            else {
                uint32_t done_id_r =  _this->done_id_reg_dma1.get();
                memcpy((void *)data, (void *)&done_id_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Reading IDMA_DONE_ID_OFFSET=0x%08x\n",done_id_r);
            }
        }
        else if (offset == IDMA_DST_ADDR_LOW_OFFSET) {
            if (is_write == 1) {
                uint32_t dst_low_w;
                memcpy((uint8_t*)&dst_low_w,data,size);
                _this->dst_addr_reg_dma1.set(dst_low_w);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Writing IDMA_DST_ADDR_LOW_OFFSET=0x%08x\n",dst_low_w);
            }
            else {
                uint32_t dst_low_r =  _this->dst_addr_reg_dma1.get();
                memcpy((void *)data, (void *)&dst_low_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Reading IDMA_DST_ADDR_LOW_OFFSET=0x%08x\n",dst_low_r);
            }
        }
        else if (offset == IDMA_SRC_ADDR_LOW_OFFSET) {
            if (is_write == 1) {
                uint32_t src_low_w;
                memcpy((uint8_t*)&src_low_w,data,size);
                _this->src_addr_reg_dma1.set(src_low_w);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Writing IDMA_SRC_ADDR_LOW_OFFSET=0x%08x\n",src_low_w);
            }
            else {
                uint32_t src_low_r =  _this->src_addr_reg_dma1.get();
                memcpy((void *)data, (void *)&src_low_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Reading IDMA_SRC_ADDR_LOW_OFFSET=0x%08x\n",src_low_r);
            }
        }
        else if (offset == IDMA_LENGTH_LOW_OFFSET) {
            if (is_write == 1) {
                uint32_t len_low_w;
                memcpy((uint8_t*)&len_low_w,data,size);
                _this->len_reg_dma1.set(len_low_w);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Writing IDMA_LENGTH_LOW_OFFSET=0x%08x\n",len_low_w);
            }
            else {
                uint32_t len_low_r =  _this->len_reg_dma1.get();
                memcpy((void *)data, (void *)&len_low_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Reading IDMA_LENGTH_LOW_OFFSET=0x%08x\n",len_low_r);
            }
        }
        else if (offset == IDMA_DST_STRIDE_2_LOW_OFFSET) {
            if (is_write == 1) {
                uint32_t dst_2_low_w;
                memcpy((uint8_t*)&dst_2_low_w,data,size);
                _this->dst_stride_2_reg_dma1.set(dst_2_low_w);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Writing IDMA_DST_STRIDE_2_LOW_OFFSET=0x%08x\n",dst_2_low_w);
            }
            else {
                uint32_t dst_2_low_r =  _this->dst_stride_2_reg_dma1.get();
                memcpy((void *)data, (void *)&dst_2_low_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Reading IDMA_DST_STRIDE_2_LOW_OFFSET=0x%08x\n",dst_2_low_r);
            }
        }
        else if (offset == IDMA_SRC_STRIDE_2_LOW_OFFSET) {
            if (is_write == 1) {
                uint32_t src_2_low_w;
                memcpy((uint8_t*)&src_2_low_w,data,size);
                _this->src_stride_2_reg_dma1.set(src_2_low_w);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Writing IDMA_SRC_STRIDE_2_LOW_OFFSET=0x%08x\n",src_2_low_w);
            }
            else {
                uint32_t src_2_low_r =  _this->src_stride_2_reg_dma1.get();
                memcpy((void *)data, (void *)&src_2_low_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Reading IDMA_SRC_STRIDE_2_LOW_OFFSET=0x%08x\n",src_2_low_r);
            }
        }
        else if (offset == IDMA_REPS_2_LOW_OFFSET) {
            if (is_write == 1) {
                uint32_t reps_2_low_w;
                memcpy((uint8_t*)&reps_2_low_w,data,size);
                _this->reps2_reg_dma1.set(reps_2_low_w);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Writing IDMA_REPS_2_LOW_OFFSET=0x%08x\n",reps_2_low_w);
            }
            else {
                uint32_t reps_2_low_r =  _this->reps2_reg_dma1.get();
                memcpy((void *)data, (void *)&reps_2_low_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Reading IDMA_REPS_2_LOW_OFFSET=0x%08x\n",reps_2_low_r);
            }
        }
        else if (offset == IDMA_DST_STRIDE_3_LOW_OFFSET) {
            if (is_write == 1) {
                uint32_t dst_3_low_w;
                memcpy((uint8_t*)&dst_3_low_w,data,size);
                _this->dst_stride_3_reg_dma1.set(dst_3_low_w);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Writing IDMA_DST_STRIDE_3_LOW_OFFSET=0x%08x\n",dst_3_low_w);
            }
            else {
                uint32_t dst_3_low_r =  _this->dst_stride_3_reg_dma1.get();
                memcpy((void *)data, (void *)&dst_3_low_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Reading IDMA_DST_STRIDE_3_LOW_OFFSET=0x%08x\n",dst_3_low_r);
            }
        }
        else if (offset == IDMA_SRC_STRIDE_3_LOW_OFFSET) {
            if (is_write == 1) {
                uint32_t src_3_low_w;
                memcpy((uint8_t*)&src_3_low_w,data,size);
                _this->src_stride_3_reg_dma1.set(src_3_low_w);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Writing IDMA_SRC_STRIDE_3_LOW_OFFSET=0x%08x\n",src_3_low_w);
            }
            else {
                uint32_t src_3_low_r =  _this->src_stride_3_reg_dma1.get();
                memcpy((void *)data, (void *)&src_3_low_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Reading IDMA_SRC_STRIDE_3_LOW_OFFSET=0x%08x\n",src_3_low_r);
            }
        }
        else if (offset == IDMA_REPS_3_LOW_OFFSET) {
            if (is_write == 1) {
                uint32_t reps_3_low_w;
                memcpy((uint8_t*)&reps_3_low_w,data,size);
                _this->reps3_reg_dma1.set(reps_3_low_w);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Writing IDMA_REPS_3_LOW_OFFSET=0x%08x\n",reps_3_low_w);
            }
            else {
                uint32_t reps_3_low_r =  _this->reps3_reg_dma1.get();
                memcpy((void *)data, (void *)&reps_3_low_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA1 Reading IDMA_REPS_3_LOW_OFFSET=0x%08x\n",reps_3_low_r);
            }
        }
        else {
            _this->trace.fatal("[Magia iDMA Ctrl] IDMA1 wrong offset\n");
        }
    }
    else { // Dir=0 L2 to L1
        if (offset == IDMA_CONF_OFFSET) {
            if (is_write == 1) {
                uint32_t cnf_w;
                memcpy((uint8_t*)&cnf_w,data,size);
                _this->config_reg_dma0.set(cnf_w);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Writing CONFIG_REG=0x%08x\n",cnf_w);
            }
            else {
                uint32_t cnf_r =  _this->config_reg_dma0.get();
                memcpy((void *)data, (void *)&cnf_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Reading CONFIG_REG=0x%08x\n",cnf_r);
            }
        }
        else if (offset == IDMA_STATUS_OFFSET) {
            if (is_write == 1) {
                _this->trace.fatal("[Magia iDMA Ctrl] IDMA0 Writing on STATUS_REG is not permitted\n");
            }
            else {
                uint32_t sts_r =  _this->status_reg_dma0.get();
                memcpy((void *)data, (void *)&sts_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Reading STATUS_REG=0x%08x\n",sts_r);
            }
        }
        else if (offset == IDMA_NEXT_ID_OFFSET) {
            if (is_write == 1) {
                _this->trace.fatal("[Magia iDMA Ctrl] IDMA0 Writing on IDMA_NEXT_ID_OFFSET is not permitted\n");
            }
            else {
                IssOffloadInsn<uint32_t> dmstati_dma0;
                dmstati_dma0.opcode=R_TYPE_ENCODE(DMSTATI_FUNCT7, 0b01, 0, XDMA_FUNCT3, 5, OP_CUSTOM1);
                dmstati_dma0.arg_b=0b01; //type of status register we are getting form iDMA. 0b01 means next transfer id
                _this->offload_itf_idma0.sync(&dmstati_dma0); //send dmstati
                _this->next_id_reg_dma0.set(dmstati_dma0.result - 1); //fixed next id to be coherent with RTL value
                uint32_t next_id_r =  _this->next_id_reg_dma0.get();
                memcpy((void *)data, (void *)&next_id_r, size);
                if (_this->fsm_state_dma0.get()!=IDLE) {
                    _this->trace.fatal("[Magia iDMA Ctrl] IDMA0 Received read IDMA_NEXT_ID_OFFSET for IDMA - DIR: 0 when not in IDLE state\n");
                }
                else {
                    _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Reading IDMA_NEXT_ID_OFFSET=0x%08x\n",next_id_r);
                    _this->dma0_transfer_time_start=_this->clock.get_cycles();
                    IssOffloadInsn<uint32_t> dmsrc; 
                    IssOffloadInsn<uint32_t> dmdst;
                    IssOffloadInsn<uint32_t> dmrep;
                    IssOffloadInsn<uint32_t> dmstr;
                    IssOffloadInsn<uint32_t> dmcpyi;
                    //set src addr
                    dmsrc.opcode=R_TYPE_ENCODE(DMSRC_FUNCT7, 13, 12, XDMA_FUNCT3, 0, OP_CUSTOM1);
                    dmsrc.arg_a=_this->src_addr_reg_dma0.get(); //low addr
                    dmsrc.arg_b=0x0; //high addr
                    dmsrc.granted=true;
                    _this->offload_itf_idma0.sync(&dmsrc);
                    //set dst addr
                    dmdst.opcode=R_TYPE_ENCODE(DMDST_FUNCT7, 11, 10, XDMA_FUNCT3, 0, OP_CUSTOM1);
                    dmdst.arg_a=_this->dst_addr_reg_dma0.get();; //low addr
                    dmdst.arg_b=0x0; //high addr
                    dmdst.granted=true;
                    _this->offload_itf_idma0.sync(&dmdst);
                    //set dmrep
                    dmrep.opcode=R_TYPE_ENCODE(DMREP_FUNCT7, 0, 17, XDMA_FUNCT3, 0, OP_CUSTOM1);
                    dmrep.arg_a=_this->reps2_reg_dma0.get();
                    _this->offload_itf_idma0.sync(&dmrep);
                    //set dmstr
                    dmstr.opcode=R_TYPE_ENCODE(DMSTR_FUNCT7, 15, 16, XDMA_FUNCT3, 0, OP_CUSTOM1);
                    dmstr.arg_a=_this->src_stride_2_reg_dma0.get();
                    dmstr.arg_b=_this->dst_stride_2_reg_dma0.get();
                    dmstr.granted=true;
                    _this->offload_itf_idma0.sync(&dmstr);
                    //set dmcpyi 0b00010
                    //dmcpyi.opcode=R_TYPE_ENCODE(DMCPYI_FUNCT7, _this->config_reg_dma0.get(), 14, XDMA_FUNCT3, 10, OP_CUSTOM1);
                    dmcpyi.opcode=R_TYPE_ENCODE(DMCPYI_FUNCT7, 0b00010, 14, XDMA_FUNCT3, 10, OP_CUSTOM1);
                    dmcpyi.arg_a=_this->len_reg_dma0.get();
                    dmcpyi.arg_b=0b00010; //this is actually redundant as it is already passed in the R_TYPE_ENCODE() however let's keep it for the sake of clarity
                    //dmcpyi.arg_b=_this->config_reg_dma0.get(); //this is actually redundant as it is already passed in the R_TYPE_ENCODE() however let's keep it for the sake of clarity
                    _this->offload_itf_idma0.sync(&dmcpyi);
                    //trigger internal fsm
                    _this->fsm_state_dma0.set(POLL_STS_REG);
                    _this->event_enqueue(_this->fsm_event_dma0, 1); //trigger fsm
                }
            }
        }
        else if (offset == IDMA_DONE_ID_OFFSET) {
            if (is_write == 1) {
                _this->trace.fatal("[Magia iDMA Ctrl] IDMA0 Writing on IDMA_DONE_ID_OFFSET is not permitted\n");
            }
            else {
                uint32_t done_id_r =  _this->done_id_reg_dma0.get();
                memcpy((void *)data, (void *)&done_id_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Reading IDMA_DONE_ID_OFFSET=0x%08x\n",done_id_r);
            }
        }
        else if (offset == IDMA_DST_ADDR_LOW_OFFSET) {
            if (is_write == 1) {
                uint32_t dst_low_w;
                memcpy((uint8_t*)&dst_low_w,data,size);
                _this->dst_addr_reg_dma0.set(dst_low_w);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Writing IDMA_DST_ADDR_LOW_OFFSET=0x%08x\n",dst_low_w);
            }
            else {
                uint32_t dst_low_r =  _this->dst_addr_reg_dma0.get();
                memcpy((void *)data, (void *)&dst_low_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Reading IDMA_DST_ADDR_LOW_OFFSET=0x%08x\n",dst_low_r);
            }
        }
        else if (offset == IDMA_SRC_ADDR_LOW_OFFSET) {
            if (is_write == 1) {
                uint32_t src_low_w;
                memcpy((uint8_t*)&src_low_w,data,size);
                _this->src_addr_reg_dma0.set(src_low_w);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Writing IDMA_SRC_ADDR_LOW_OFFSET=0x%08x\n",src_low_w);
            }
            else {
                uint32_t src_low_r =  _this->src_addr_reg_dma0.get();
                memcpy((void *)data, (void *)&src_low_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Reading IDMA_SRC_ADDR_LOW_OFFSET=0x%08x\n",src_low_r);
            }
        }
        else if (offset == IDMA_LENGTH_LOW_OFFSET) {
            if (is_write == 1) {
                uint32_t len_low_w;
                memcpy((uint8_t*)&len_low_w,data,size);
                _this->len_reg_dma0.set(len_low_w);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Writing IDMA_LENGTH_LOW_OFFSET=0x%08x\n",len_low_w);
            }
            else {
                uint32_t len_low_r =  _this->len_reg_dma0.get();
                memcpy((void *)data, (void *)&len_low_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Reading IDMA_LENGTH_LOW_OFFSET=0x%08x\n",len_low_r);
            }
        }
        else if (offset == IDMA_DST_STRIDE_2_LOW_OFFSET) {
            if (is_write == 1) {
                uint32_t dst_2_low_w;
                memcpy((uint8_t*)&dst_2_low_w,data,size);
                _this->dst_stride_2_reg_dma0.set(dst_2_low_w);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Writing IDMA_DST_STRIDE_2_LOW_OFFSET=0x%08x\n",dst_2_low_w);
            }
            else {
                uint32_t dst_2_low_r =  _this->dst_stride_2_reg_dma0.get();
                memcpy((void *)data, (void *)&dst_2_low_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Reading IDMA_DST_STRIDE_2_LOW_OFFSET=0x%08x\n",dst_2_low_r);
            }
        }
        else if (offset == IDMA_SRC_STRIDE_2_LOW_OFFSET) {
            if (is_write == 1) {
                uint32_t src_2_low_w;
                memcpy((uint8_t*)&src_2_low_w,data,size);
                _this->src_stride_2_reg_dma0.set(src_2_low_w);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Writing IDMA_SRC_STRIDE_2_LOW_OFFSET=0x%08x\n",src_2_low_w);
            }
            else {
                uint32_t src_2_low_r =  _this->src_stride_2_reg_dma0.get();
                memcpy((void *)data, (void *)&src_2_low_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Reading IDMA_SRC_STRIDE_2_LOW_OFFSET=0x%08x\n",src_2_low_r);
            }
        }
        else if (offset == IDMA_REPS_2_LOW_OFFSET) {
            if (is_write == 1) {
                uint32_t reps_2_low_w;
                memcpy((uint8_t*)&reps_2_low_w,data,size);
                _this->reps2_reg_dma0.set(reps_2_low_w);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Writing IDMA_REPS_2_LOW_OFFSET=0x%08x\n",reps_2_low_w);
            }
            else {
                uint32_t reps_2_low_r =  _this->reps2_reg_dma0.get();
                memcpy((void *)data, (void *)&reps_2_low_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Reading IDMA_REPS_2_LOW_OFFSET=0x%08x\n",reps_2_low_r);
            }
        }
        else if (offset == IDMA_DST_STRIDE_3_LOW_OFFSET) {
            if (is_write == 1) {
                uint32_t dst_3_low_w;
                memcpy((uint8_t*)&dst_3_low_w,data,size);
                _this->dst_stride_3_reg_dma0.set(dst_3_low_w);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Writing IDMA_DST_STRIDE_3_LOW_OFFSET=0x%08x\n",dst_3_low_w);
            }
            else {
                uint32_t dst_3_low_r =  _this->dst_stride_3_reg_dma0.get();
                memcpy((void *)data, (void *)&dst_3_low_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Reading IDMA_DST_STRIDE_3_LOW_OFFSET=0x%08x\n",dst_3_low_r);
            }
        }
        else if (offset == IDMA_SRC_STRIDE_3_LOW_OFFSET) {
            if (is_write == 1) {
                uint32_t src_3_low_w;
                memcpy((uint8_t*)&src_3_low_w,data,size);
                _this->src_stride_3_reg_dma0.set(src_3_low_w);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Writing IDMA_SRC_STRIDE_3_LOW_OFFSET=0x%08x\n",src_3_low_w);
            }
            else {
                uint32_t src_3_low_r =  _this->src_stride_3_reg_dma0.get();
                memcpy((void *)data, (void *)&src_3_low_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Reading IDMA_SRC_STRIDE_3_LOW_OFFSET=0x%08x\n",src_3_low_r);
            }
        }
        else if (offset == IDMA_REPS_3_LOW_OFFSET) {
            if (is_write == 1) {
                uint32_t reps_3_low_w;
                memcpy((uint8_t*)&reps_3_low_w,data,size);
                _this->reps3_reg_dma0.set(reps_3_low_w);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Writing IDMA_REPS_3_LOW_OFFSET=0x%08x\n",reps_3_low_w);
            }
            else {
                uint32_t reps_3_low_r =  _this->reps3_reg_dma0.get();
                memcpy((void *)data, (void *)&reps_3_low_r, size);
                _this->trace.msg("[Magia iDMA Ctrl] IDMA0 Reading IDMA_REPS_3_LOW_OFFSET=0x%08x\n",reps_3_low_r);
            }
        }
        else {
            _this->trace.fatal("[Magia iDMA Ctrl] IDMA0 wrong offset\n");
        }
    }
    return vp::IO_REQ_OK;
}

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
#include "../fractal_sync/fractal_sync.hpp"

/* Memory-mapped FractalSync register offsets */
#define FSYNC_MM_AGGR_REG_OFFSET    (0x00)
#define FSYNC_MM_ID_REG_OFFSET      (0x04)
#define FSYNC_MM_CONTROL_REG_OFFSET (0x08)
#define FSYNC_MM_STATUS_REG_OFFSET  (0x0C)

enum status_reg_bits {
    DONE_BIT,
    ERROR_BIT,
    BUSY_BIT
};

enum fractal_directions {
    EAST_WEST, //horizontal = 0
    NORD_SUD,   //vertical = 1
    NEIGHBOUR_EAST_WEST, //neighbour_horizontal = 2
    NEIGHBOUR_NORD_SUD   //neighbour_vertical = 3
};

/*****************************************************
*                   Class Definition                 *
*****************************************************/


class FSync_mm_ctrl : public vp::Component
{

public:
    FSync_mm_ctrl(vp::ComponentConf &config);

protected:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

    static void fsm_irq_clear(vp::Block *__this, vp::ClockEvent *event);
    static void fractal_output_method(vp::Block *__this, PortResp<uint32_t> *req, int id);

    vp::WireMaster<PortReq<uint32_t> *> fractal_ew_input_port;
    vp::WireSlave<PortResp<uint32_t> *> fractal_ew_output_port;

    vp::WireMaster<PortReq<uint32_t> *> fractal_ns_input_port;
    vp::WireSlave<PortResp<uint32_t> *> fractal_ns_output_port;

    vp::WireMaster<PortReq<uint32_t> *> neighbour_fractal_ew_input_port;
    vp::WireSlave<PortResp<uint32_t> *> neighbour_fractal_ew_output_port;

    vp::WireMaster<PortReq<uint32_t> *> neighbour_fractal_ns_input_port;
    vp::WireSlave<PortResp<uint32_t> *> neighbour_fractal_ns_output_port;

    vp::WireMaster<bool> done_irq;
    
    vp::IoSlave         input_itf;
    vp::Trace trace;

    vp::reg_32 aggr_reg;
    vp::reg_32 id_reg;
    vp::reg_32 ctrl_reg;
    vp::reg_32 status_reg;

    vp::ClockEvent *clear_irq;

};

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new FSync_mm_ctrl(config);
}

FSync_mm_ctrl::FSync_mm_ctrl(vp::ComponentConf &config)
    : vp::Component(config)
{
    //Initialize interface
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->input_itf.set_req_meth(&FSync_mm_ctrl::req);
    this->new_slave_port("input", &this->input_itf);

    this->fractal_ew_output_port.set_sync_meth_muxed(&FSync_mm_ctrl::fractal_output_method,fractal_directions::EAST_WEST);
    this->fractal_ns_output_port.set_sync_meth_muxed(&FSync_mm_ctrl::fractal_output_method,fractal_directions::NORD_SUD);
    this->neighbour_fractal_ew_output_port.set_sync_meth_muxed(&FSync_mm_ctrl::fractal_output_method,fractal_directions::NEIGHBOUR_EAST_WEST);
    this->neighbour_fractal_ns_output_port.set_sync_meth_muxed(&FSync_mm_ctrl::fractal_output_method,fractal_directions::NEIGHBOUR_NORD_SUD);
    this->new_slave_port("fractal_ew_output_port", &this->fractal_ew_output_port, this);
    this->new_slave_port("fractal_ns_output_port", &this->fractal_ns_output_port, this);
    this->new_slave_port("neighbour_fractal_ew_output_port", &this->neighbour_fractal_ew_output_port, this);
    this->new_slave_port("neighbour_fractal_ns_output_port", &this->neighbour_fractal_ns_output_port, this);

    this->new_master_port("fractal_ew_input_port", &this->fractal_ew_input_port, this);
    this->new_master_port("fractal_ns_input_port", &this->fractal_ns_input_port, this);
    this->new_master_port("neighbour_fractal_ew_input_port", &this->neighbour_fractal_ew_input_port, this);
    this->new_master_port("neighbour_fractal_ns_input_port", &this->neighbour_fractal_ns_input_port, this);

    this->new_master_port("fsync_done_irq", &this->done_irq, this);
    
    this->aggr_reg.set(0x00);
    this->id_reg.set(0x00);
    this->ctrl_reg.set(0x00);
    this->status_reg.set(0x00);

    this->clear_irq = this->event_new(&FSync_mm_ctrl::fsm_irq_clear);

    this->trace.msg(vp::Trace::LEVEL_TRACE,"[FSync_mm_ctrl] Instantiated\n");
}

void FSync_mm_ctrl::fsm_irq_clear(vp::Block *__this, vp::ClockEvent *event) {
    FSync_mm_ctrl *_this = (FSync_mm_ctrl *)__this;
    _this->done_irq.sync(false);
}

void FSync_mm_ctrl::fractal_output_method(vp::Block *__this, PortResp<uint32_t> *req, int id) {
    FSync_mm_ctrl *_this = (FSync_mm_ctrl *)__this;

    if ((req->wake) && (!req->error)) {
        _this->status_reg.set_field(0b0,BUSY_BIT,1); //set bit 2 (busy) to 0
        if (id==fractal_directions::EAST_WEST) {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FSync_mm_ctrl] received wake response from EAST-WEST Fractal [id=%d aggr=0x%08x]\n",req->id_rsp,req->lvl);
            _this->status_reg.set_field(0b0,ERROR_BIT,1); //set bit 1 (error) to 0
        }
        else if (id==fractal_directions::NORD_SUD) {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FSync_mm_ctrl] received wake response from NORD-SUD Fractal [id=%d aggr=0x%08x]\n",req->id_rsp,req->lvl);
            _this->status_reg.set_field(0b0,ERROR_BIT,1); //set bit 1 (error) to 0
        }
        else if (id==fractal_directions::NEIGHBOUR_EAST_WEST) {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FSync_mm_ctrl] received wake response from NEIGHBOUR-EAST-WEST Fractal [id=%d aggr=0x%08x]\n",req->id_rsp,req->lvl);
            _this->status_reg.set_field(0b0,ERROR_BIT,1); //set bit 1 (error) to 0
        }
        else if (id==fractal_directions::NEIGHBOUR_NORD_SUD) {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FSync_mm_ctrl] received wake response from NEIGHBOUR-NORD-SUD Fractal [id=%d aggr=0x%08x]\n",req->id_rsp,req->lvl);
            _this->status_reg.set_field(0b0,ERROR_BIT,1); //set bit 1 (error) to 0
        }
        else {
            _this->trace.fatal("[FSync_mm_ctrl] wrong direction\n");
            _this->status_reg.set_field(0b0,ERROR_BIT,1); //set bit 1 (error) to 1
            
        }
        _this->status_reg.set_field(0b1,DONE_BIT,1); //set bit 0 (done) to 1
        _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FSync_mm_ctrl] End of Response. STATUS_REG=0x%08x\n",_this->status_reg.get());
        _this->done_irq.sync(true);
        _this->event_enqueue(_this->clear_irq, 1);
    }
    else if (req->error){
        _this->status_reg.set_field(0b0,BUSY_BIT,1); //set bit 2 (busy) to 0
        _this->status_reg.set_field(0b1,ERROR_BIT,1); //set bit 1 (error) to 1
        _this->status_reg.set_field(0b1,DONE_BIT,1); //set bit 0 (done) to 1
        _this->trace.fatal("[FSync_mm_ctrl] received error response from Fractal\n");
    }
}


vp::IoReqStatus FSync_mm_ctrl::req(vp::Block *__this, vp::IoReq *req)
{
    FSync_mm_ctrl *_this = (FSync_mm_ctrl *)__this;

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    if (size!=4) {
         _this->trace.fatal("[FSync_mm_ctrl] Memory mapped interface supports only 32 bits (4 bytes) buses\n");
    }

    if(offset == FSYNC_MM_AGGR_REG_OFFSET) {
        if ((is_write == 1) && (_this->status_reg.get_field(BUSY_BIT,1) == 0b0)) {
            uint32_t aggr_w;
            memcpy((uint8_t*)&aggr_w,data,size);
            _this->aggr_reg.set(aggr_w);
            _this->trace.msg("[FSync_mm_ctrl] Writing AGGR_REG=0x%08x\n",aggr_w);
        }
        else if ((is_write == 1) && (_this->status_reg.get_field(BUSY_BIT,1) == 0b0)) {
            _this->trace.fatal("[FSync_mm_ctrl] Writing on register when not in idle is not permitted\n");
        }
        else {
            uint32_t aggr_r =  _this->aggr_reg.get();
            memcpy((void *)data, (void *)&aggr_r, size);
            _this->trace.msg("[FSync_mm_ctrl] Reading AGGR_REG=0x%08x\n",aggr_r);
        }
    }
    else if (offset == FSYNC_MM_ID_REG_OFFSET) {
        if ((is_write == 1) && (_this->status_reg.get_field(BUSY_BIT,1) == 0b0)) {
            uint32_t id_w;
            memcpy((uint8_t*)&id_w,data,size);
            _this->id_reg.set(id_w);
            _this->trace.msg("[FSync_mm_ctrl] Writing ID_REG=0x%08x\n",id_w);
        }
        else if ((is_write == 1) && (_this->status_reg.get_field(BUSY_BIT,1) == 0b0)) {
            _this->trace.fatal("[FSync_mm_ctrl] Writing on register when not in idle is not permitted\n");
        }
        else {
            uint32_t id_r =  _this->id_reg.get();
            memcpy((void *)data, (void *)&id_r, size);
            _this->trace.msg("[FSync_mm_ctrl] Reading ID_REG=0x%08x\n",id_r);
        }
    }
    else if (offset == FSYNC_MM_CONTROL_REG_OFFSET) {
        //Note: No check on what the user writes on the control register. It is just used as a "trigger" to start the sync procedure.
        if ((is_write == 1) && (_this->status_reg.get_field(BUSY_BIT,1) == 0b0)) {
            PortReq<uint32_t> req = {
                .sync=true,
                .aggr=_this->aggr_reg.get(),
                .id_req=_this->id_reg.get()
            };
            if (req.aggr==0b1) {
                if (req.id_req == 0) {
                    _this->trace.msg("[FSync_mm_ctrl] Enable fsync EAST-WEST\n");
                    _this->fractal_ew_input_port.sync(&req);
                }
                else if (req.id_req == 1) {
                    _this->trace.msg("[FSync_mm_ctrl] Enable fsync NORD-SUD\n");
                    _this->fractal_ns_input_port.sync(&req);
                }
                else if (req.id_req == 2) {
                    _this->trace.msg("[FSync_mm_ctrl] Enable neighbour-fsync EAST-WEST\n");
                    _this->neighbour_fractal_ew_input_port.sync(&req);
                }
                else if (req.id_req == 3) {
                    _this->trace.msg("[FSync_mm_ctrl] Enable neighbour-fsync NORD-SUD\n");
                    _this->neighbour_fractal_ns_input_port.sync(&req);
                }
                else
                    _this->trace.fatal("[FSync_mm_ctrl] wrong direction with aggr=0b1");
            }
            else {
                if (req.id_req % 2 == 0)
                    _this->fractal_ew_input_port.sync(&req);
                else
                    _this->fractal_ns_input_port.sync(&req);
            }
            _this->status_reg.set_field(0b1,BUSY_BIT,1); //set bit 2 (busy) to 1
            _this->status_reg.set_field(0b0,ERROR_BIT,1); //set bit 1 (error) to 0
            _this->status_reg.set_field(0b0,DONE_BIT,1); //set bit 0 (done) to 0
        }
        else {
            uint32_t ctrl_r =  _this->ctrl_reg.get();
            memcpy((void *)data, (void *)&ctrl_r, size);
            _this->trace.msg("[FSync_mm_ctrl] Reading CTRL_REG=0x%08x\n",ctrl_r);
        }
    }
    else if (offset == FSYNC_MM_STATUS_REG_OFFSET) {
        if (is_write == 1) {
            _this->trace.fatal("[FSync_mm_ctrl] write on status register is not allowed\n");
        }
        else {
            uint32_t status = _this->status_reg.get();
            memcpy((void *)data, (void *)&status, size);
            _this->trace.msg("[FSync_mm_ctrl] Reading STATUS_REG=0x%08x\n",status);
        }
    }
    else {
        _this->trace.fatal("[FSync_mm_ctrl] wrong offset\n");
    }
    return vp::IO_REQ_OK;
}


/*
 * Copyright (C) 2020-2022  GreenWaves Technologies, ETH Zurich, University of Bologna
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
 * Authors: Cyrill Durrer, ETH Zurich (cdurrer@iis.ee.ethz.ch)
 */

#include <datamover.hpp>

Datamover::Datamover(vp::ComponentConf &config) : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->new_slave_port("config", &this->cfg_port);
    this->new_master_port("irq", &this->irq);
    this->new_master_port("l1", &this->l1);
    this->cfg_port.set_req_meth(&Datamover::handle_req);

    this->streamer_in.set_accel(this);
    this->streamer_out.set_accel(this);
}

void Datamover::clear()
{
    printf("clear(): Clearing Datamover state\n");
    this->trace.msg(vp::Trace::LEVEL_INFO, "clear(): Clearing Datamover state\n");

    this->datamover_acquire = 0;
    this->datamover_finished = 0;
    this->datamover_status = 0;
    this->datamover_running_job = 0;

    this->in_ptr = 0;
    this->out_ptr = 0;
    this->tot_len = 0;
    this->in_d0 = 0;
    this->in_d1 = 0;
    this->in_d2 = 0;
    this->in_d3 = 0;
    this->out_d0 = 0;
    this->out_d1 = 0;
    this->out_d2 = 0;
    this->out_d3 = 0;
    this->in_out_d4_stride = 0;
    this->matrix_dim = 0;
    this->channels = 0;
    this->ctrl_engine = 0;

    this->streamer_in.reset();
    this->streamer_out.reset();

    memset(this->elem_matrix, 0, sizeof(this->elem_matrix));
}

vp::IoReqStatus Datamover::handle_req(vp::Block *__this, vp::IoReq *req)
{
    Datamover *_this = (Datamover *)__this;
    // printf("Datamover::handle_req(): Received request (addr: 0x%lx, size: 0x%lx, is_write: %d, data: 0x%lx)\n", req->get_addr(), req->get_size(), req->get_is_write(), *(uint64_t *)(req->get_data()));
    // _this->trace.msg("Datamover::handle_req(): Received request (addr: 0x%x, size: 0x%x, is_write: %d, data: 0x%x)\n", req->get_addr(), req->get_size(), req->get_is_write(), *(uint32_t *)(req->get_data()));

    uint8_t datamover_mode = (_this->ctrl_engine >> 3) & 0x1F;
    uint8_t transpose_mode = _this->ctrl_engine & 0x7;

    printf("Datamover::handle_req(): datamover_mode=%d, transpose_mode=%d\n", datamover_mode, transpose_mode);

    if (req->get_size() != 4) return vp::IO_REQ_INVALID;

    if (req->get_is_write())
    {
        if((req->get_addr() & 0xfff) == DATAMOVER_COMMIT_AND_TRIGGER) {      // trigger operation when writing to the control register at offset 0  ToDo(cdurrer): mask necessary?
            switch(datamover_mode) {
                case 0:     // copy
                    printf("Datamover::handle_req(): Triggering copy operation\n");
                    _this->trace.msg(vp::Trace::LEVEL_INFO, "Datamover::handle_req(): Triggering copy operation\n");
                    _this->copy();
                    break;
                case 1:     // transpose
                    if (transpose_mode == 1 || transpose_mode == 2 || transpose_mode == 4) {
                        printf("Datamover::handle_req(): Triggering transpose operation (mode=%d)\n", transpose_mode);
                        _this->trace.msg(vp::Trace::LEVEL_INFO, "Datamover::handle_req(): Triggering transpose operation (mode=%d)\n", transpose_mode);
                        _this->transpose(transpose_mode);
                    }
                    else {
                        printf("Datamover::handle_req(): Unsupported operation triggered (ctrl_engine=0x%08x)\n", _this->ctrl_engine);
                        _this->trace.msg(vp::Trace::LEVEL_WARNING, "Datamover::handle_req(): Unsupported operation triggered (ctrl_engine=0x%08x)\n", _this->ctrl_engine);
                        return vp::IO_REQ_INVALID;
                    }
                    break;
                case 2:     // CIM layout conversion (row tile: 64 elements) (ToDo)
                    printf("Datamover::handle_req(): Triggering CIM layout conversion operation\n");
                    _this->trace.msg(vp::Trace::LEVEL_INFO, "Datamover::handle_req(): Triggering CIM layout conversion operation\n");
                    _this->cim_layout_conversion();
                    break;
                case 3:
                    printf("Datamover::handle_req(): CIM layout conversion NOT IMPLEMENTED!\n");
                    _this->trace.msg(vp::Trace::LEVEL_WARNING, "Datamover::handle_req(): CIM layout conversion NOT IMPLEMENTED!\n");
                    break;
                case 4:
                    printf("Datamover::handle_req(): Triggering unfold operation\n");
                    _this->trace.msg(vp::Trace::LEVEL_INFO, "Datamover::handle_req(): Triggering unfold operation\n");
                    _this->unfold();
                    break;
                default:
                    printf("Datamover::handle_req(): Unsupported operation triggered (ctrl_engine=0x%08x)\n", _this->ctrl_engine);
                    _this->trace.msg(vp::Trace::LEVEL_WARNING, "Datamover::handle_req(): Unsupported operation triggered (ctrl_engine=0x%08x)\n", _this->ctrl_engine);
                    return vp::IO_REQ_INVALID;
            }
        }
        else if((req->get_addr() & 0xfff) == DATAMOVER_SOFT_CLEAR) {
            _this->clear();
            _this->trace.msg(vp::Trace::LEVEL_INFO, "Datamover::handle_req(): Received soft clear command, clearing state\n");
        }
        else if(((req->get_addr() & 0xfff) >= DATAMOVER_REGISTER_OFFS) && ((req->get_addr() & 0xfff) < DATAMOVER_REGISTER_OFFS + DATAMOVER_NB_REG*4)) {
            // printf("Datamover::handle_req(): Write to register address 0x%x, value 0x%08x\n", req->get_addr(), *(uint32_t *) req->get_data());
            _this->regfile_wr(((req->get_addr() & 0xfff) - DATAMOVER_REGISTER_OFFS) >> 2, *(uint32_t *) req->get_data());
        }
        else {
            printf("Datamover::handle_req(): Write to invalid register address 0x%x\n", req->get_addr());
            _this->trace.msg(vp::Trace::LEVEL_WARNING, "Datamover::handle_req(): Write to invalid register address 0x%x\n", req->get_addr());
        }
    }
    else
    {
        if((req->get_addr() & 0xfff) >= HWPE_REGISTER_OFFS && ((req->get_addr() & 0xfff) < DATAMOVER_REGISTER_OFFS)) {
            *(uint32_t *)req->get_data() = _this->hwpe_regfile_rd((req->get_addr() & 0xfff - HWPE_REGISTER_OFFS) >> 2);
            printf("Datamover::handle_req(): Read from HWPE register address 0x%x: value=0x%x\n", req->get_addr(), *(uint32_t *)req->get_data());
        }
        else if((req->get_addr() & 0xfff) >= DATAMOVER_REGISTER_OFFS && ((req->get_addr() & 0xfff) < DATAMOVER_REGISTER_OFFS + DATAMOVER_NB_REG * 4)) {
            // printf("Datamover::handle_req(): Read from Datamover register address 0x%x\n", req->get_addr());
            *(uint32_t *)req->get_data() = _this->regfile_rd(((req->get_addr() & 0xfff) - DATAMOVER_REGISTER_OFFS) >> 2);
        }
        else {
            printf("Datamover::handle_req(): Read from invalid register address 0x%x\n", req->get_addr());
            _this->trace.msg(vp::Trace::LEVEL_WARNING, "Datamover::handle_req(): Read from invalid register address 0x%x\n", req->get_addr());
        }
    }

    return vp::IO_REQ_OK;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Datamover(config);
}

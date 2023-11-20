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

#include <vector>
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>



class Cluster_registers : public vp::Component
{

public:

    Cluster_registers(vp::ComponentConf &config);

    void reset(bool active);

    static vp::IoReqStatus req(void *__this, vp::IoReq *req);

private:
    static void barrier_sync(void *__this, bool value, int id);

    vp::Trace     trace;

    vp::IoSlave in;
    uint32_t bootaddr;
    uint32_t status;
    int nb_cores;
    vp::reg_32 barrier_status;

    std::vector<vp::WireSlave<bool>> barrier_req_itf;
    vp::WireMaster<bool> barrier_ack_itf;
};

Cluster_registers::Cluster_registers(vp::ComponentConf &config)
: vp::Component(config)
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);

    this->in.set_req_meth(&Cluster_registers::req);
    this->new_slave_port("input", &this->in);

    this->bootaddr = this->get_js_config()->get("boot_addr")->get_int();
    this->nb_cores = this->get_js_config()->get("nb_cores")->get_int();

    this->barrier_req_itf.resize(this->nb_cores);
    for (int i=0; i<this->nb_cores; i++)
    {
        this->barrier_req_itf[i].set_sync_meth_muxed(&Cluster_registers::barrier_sync, i);
        this->new_slave_port("barrier_req_" + std::to_string(i), &this->barrier_req_itf[i]);
    }

    this->new_master_port("barrier_ack", &this->barrier_ack_itf);


}

vp::IoReqStatus Cluster_registers::req(void *__this, vp::IoReq *req)
{
    Cluster_registers *_this = (Cluster_registers *)__this;
    uint64_t offset = req->get_addr();
    bool is_write = req->get_is_write();
    uint64_t size = req->get_size();

    _this->trace.msg("Received IO req (offset: 0x%llx, size: 0x%llx, is_write: %d)\n", offset, size, is_write);

    if (size == 4)
    {
        if (offset == 0x58)
        {
            if (is_write)
            {
                _this->bootaddr = *(uint32_t *)req->get_data();
            }
            else
            {
                *(uint32_t *)req->get_data() = _this->bootaddr;
            }

            return vp::IO_REQ_OK;
        }
        else if (offset == 0x40)
        {
            if (!is_write)
            {
                *(uint32_t *)req->get_data() =  0;
                return vp::IO_REQ_OK;
            }
        }
        else if (offset == 0x50)
        {
            if (is_write)
            {
                _this->status = *(uint32_t *)req->get_data();
            }
            else
            {
                *(uint32_t *)req->get_data() = _this->status;
            }

            return vp::IO_REQ_OK;
        }
    }

    return vp::IO_REQ_INVALID;
}

void Cluster_registers::barrier_sync(void *__this, bool value, int id)
{
    Cluster_registers *_this = (Cluster_registers *)__this;
    _this->barrier_status.set(_this->barrier_status.get() | (value << id));

    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Barrier sync (id: %d, status: 0x%x)\n", id, _this->barrier_status.get());

    if (_this->barrier_status.get() == (1ULL << _this->nb_cores) - 1)
    {
        _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Barrier reached\n");

        _this->barrier_status.set(0);
        _this->barrier_ack_itf.sync(1);
    }
}

void Cluster_registers::reset(bool active)
{
    this->new_reg("barrier_status", &this->barrier_status, 0, true);
}



extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Cluster_registers(config);
}



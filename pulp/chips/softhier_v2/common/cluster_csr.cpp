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

#include <string>
#include <vector>
#include <iostream>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/itf/wire.hpp>
#include <pulp/chips/softhier_v2/common/cluster_csr/cluster_csr_config.hpp>

/**
 * SoftHier cluster control registers and hardware barrier.
 *
 * Registers (32-bit):
 * - 0x0: cluster ID (read)
 * - 0x4, 0x8: read as 1 and 0 (AMO probes of the runtime)
 * - 0xC: character output, a line is printed on '\n' (write)
 *
 * The barrier collects one barrier_req notification per core and, once
 * every core has arrived, releases them all through barrier_ack.
 */
class ClusterCSR : public vp::Component
{
public:
    ClusterCSR(vp::ComponentConf &config);

    void reset(bool active) override;

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static void barrier_sync(vp::Block *__this, bool value, int id);

    ClusterCsrConfig cfg;
    vp::Trace trace;
    vp::IoSlave in{&ClusterCSR::req};
    vp::reg_32 barrier_status;
    std::string buffer;
    std::vector<vp::WireSlave<bool>> barrier_req_itf;
    vp::WireMaster<bool> barrier_ack_itf;
};

ClusterCSR::ClusterCSR(vp::ComponentConf &config)
    : vp::Component(config, this->cfg)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->new_slave_port("input", &this->in);

    this->barrier_req_itf.resize(this->cfg.nb_cores);
    for (int i = 0; i < this->cfg.nb_cores; i++)
    {
        this->barrier_req_itf[i].set_sync_meth_muxed(&ClusterCSR::barrier_sync, i);
        this->new_slave_port("barrier_req_" + std::to_string(i), &this->barrier_req_itf[i]);
    }
    this->new_master_port("barrier_ack", &this->barrier_ack_itf);

    this->new_reg("barrier_status", &this->barrier_status, 0, true);
}

vp::IoReqStatus ClusterCSR::req(vp::Block *__this, vp::IoReq *req)
{
    ClusterCSR *_this = (ClusterCSR *)__this;
    uint64_t offset = req->get_addr();
    bool is_write = req->get_is_write();
    uint32_t *data = (uint32_t *)req->get_data();

    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Access (offset: 0x%lx, size: 0x%lx, is_write: %d)\n",
        offset, req->get_size(), is_write);

    if (!is_write && offset == 0)
    {
        data[0] = _this->cfg.cluster_id;
    }
    else if (offset == 4)
    {
        data[0] = 1;
    }
    else if (offset == 8)
    {
        data[0] = 0;
    }
    else if (offset == 12 && is_write)
    {
        char c = (char)data[0];
        if (c == '\n')
        {
            std::cout << _this->buffer << std::endl;
            _this->buffer.clear();
        }
        else
        {
            _this->buffer += c;
        }
    }

    return vp::IO_REQ_DONE;
}

void ClusterCSR::barrier_sync(vp::Block *__this, bool value, int id)
{
    ClusterCSR *_this = (ClusterCSR *)__this;
    _this->barrier_status.set(_this->barrier_status.get() | (value << id));
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Barrier sync (id: %d, status: 0x%x)\n", id,
        _this->barrier_status.get());

    if (_this->barrier_status.get() == (1ULL << _this->cfg.nb_cores) - 1)
    {
        _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Barrier reached\n");
        _this->barrier_status.set(0);
        _this->barrier_ack_itf.sync(1);
    }
}

void ClusterCSR::reset(bool active)
{
    if (active)
    {
        this->buffer.clear();
    }
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ClusterCSR(config);
}

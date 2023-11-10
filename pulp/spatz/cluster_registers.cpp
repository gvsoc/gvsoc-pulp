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

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>



class Cluster_registers : public vp::component
{

public:

    Cluster_registers(js::config *config);

    int build();

    static vp::io_req_status_e req(void *__this, vp::io_req *req);

private:
    vp::trace     trace;

    vp::io_slave in;
    uint32_t bootaddr;
    uint32_t status;
};

Cluster_registers::Cluster_registers(js::config *config)
: vp::component(config)
{

}

vp::io_req_status_e Cluster_registers::req(void *__this, vp::io_req *req)
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



int Cluster_registers::build()
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);

    this->in.set_req_meth(&Cluster_registers::req);
    this->new_slave_port("input", &this->in);

    this->bootaddr = this->get_js_config()->get("boot_addr")->get_int();

  return 0;
}



extern "C" vp::component *vp_constructor(js::config *config)
{
    return new Cluster_registers(config);
}



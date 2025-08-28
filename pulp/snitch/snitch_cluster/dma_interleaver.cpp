/*
 * Copyright (C) 2024 ETH Zurich and University of Bologna
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
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <math.h>

class DmaInterleaver : public vp::Component
{

public:
    DmaInterleaver(vp::ComponentConf &config);

    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

private:
    vp::Trace trace;

    std::vector<vp::IoMaster> output_ports;
    vp::IoSlave input_port;

    int id_shift;
    uint64_t id_mask;
    int offset_right_shift;
    int offset_left_shift;
    int bank_width;
};

DmaInterleaver::DmaInterleaver(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);

    int nb_banks = this->get_js_config()->get_child_int("nb_banks");
    int bank_width = this->get_js_config()->get_child_int("bank_width");

    this->bank_width = bank_width;
    this->id_shift = log2(bank_width);
    this->id_mask = (1 << (int)log2(nb_banks)) - 1;
    this->offset_right_shift = log2(bank_width) + log2(nb_banks);
    this->offset_left_shift = log2(bank_width);

    this->output_ports.resize(nb_banks);
    for (int i=0; i<nb_banks; i++)
    {
        this->new_master_port("out_" + std::to_string(i), &this->output_ports[i]);
    }

    this->input_port.set_req_meth(&DmaInterleaver::req);
    this->new_slave_port("input", &this->input_port);
}

vp::IoReqStatus DmaInterleaver::req(vp::Block *__this, vp::IoReq *req)
{
    DmaInterleaver *_this = (DmaInterleaver *)__this;
    uint64_t offset = req->get_addr();
    bool is_write = req->get_is_write();
    uint64_t size = req->get_size();
    uint8_t *data = req->get_data();
    uint8_t *second_data = req->get_second_data(); //needed for amo
    vp::IoReqOpcode opcode = req->get_opcode(); //needed for amo

    _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received IO req (req 0x%p, offset: 0x%llx, size: 0x%llx, is_write: %d)\n", req, offset, size, is_write);

    vp::IoReq bank_req;

    bank_req.init();
    uint64_t max_delay = 0;
    while (size)
    {
        int bank_size = std::min(_this->bank_width - (offset & (_this->bank_width - 1)), size);

        int bank_id = (offset >> _this->id_shift) & _this->id_mask;
        uint64_t bank_offset = ((offset >> _this->offset_right_shift) << _this->offset_left_shift) +
            (offset & ((1<< _this->offset_left_shift) - 1));

        bank_req.set_addr(bank_offset);
        bank_req.set_size(bank_size);
        bank_req.set_data(data);
        bank_req.set_second_data(second_data); //needed for amo
        bank_req.set_is_write(is_write);
        bank_req.set_opcode(opcode); //needed for amo
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "Forwarding bank request to bank %d (req x%p, offset: 0x%llx, size: 0x%llx)\n", bank_id, &bank_req, bank_offset, bank_size);
        _this->output_ports[bank_id].req_forward(&bank_req);
        max_delay = std::max(max_delay, bank_req.get_latency()); // Report back the maximum latency of all banks
        offset += bank_size;
        size -= bank_size;
        data += bank_size;
        bank_req.set_latency(0); // Reset the latency because we always use the same request object
    }
    req->inc_latency(max_delay);
    _this->trace.msg(vp::Trace::LEVEL_TRACE, "Increasing latency for req %p by %ld \n", req, max_delay);
    // martinjo: Note: I dont think the following note is correct. The latency was previously not reported on the Core side
    // Note that we ignore the latency reported by the bank requests since DMA is always
    // having the priority. Ignoring the latency will just report it on the core side
    return vp::IoReqStatus::IO_REQ_OK;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new DmaInterleaver(config);
}

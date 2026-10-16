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
#include <vp/itf/io_v2.hpp>

/*
 * Memory-mapped controller of the two iDMA channels of the tile, as the RTL
 * idma_obi_ctrl_decoder: the first 0x200 bytes of the window go to the
 * register front-end of the AXI to OBI channel (L2 to L1), the next 0x200 to
 * the one of the OBI to AXI channel (L1 to L2). The access is forwarded with
 * the offset inside the channel, and the grant, the response and the retries
 * of the front-end go back unchanged, so that a launch denied by a full
 * request FIFO is retried as on the front-end itself.
 *
 * The requests reach this block with the offset in the window as address
 * (the crossbar removes the base). Only the registers of idma_reg32_3d are
 * accepted, as in the RTL decoder; anything else is answered with an error.
 */

// Channels, in the order of their windows
enum {
    IDMA_AXI2OBI = 0,
    IDMA_OBI2AXI = 1,
    IDMA_NB_CHANNELS = 2,
};

// Size of the window of one channel (RTL DIRECTION_OFFSET)
#define IDMA_CHANNEL_SIZE 0x200

class iDMA_mm_ctrl : public vp::Component
{
public:
    iDMA_mm_ctrl(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static vp::IoRespAck channel_resp(vp::Block *__this, vp::IoReq *req, int channel);
    static void channel_retry(vp::Block *__this, int channel, vp::IoRetryChannel retry_channel);
    // True if the offset is one of the idma_reg32_3d registers
    static bool is_valid_offset(uint64_t offset);

    vp::Trace trace;
    vp::IoSlave input_itf{&iDMA_mm_ctrl::req};
    vp::IoMaster axi2obi_itf{IDMA_AXI2OBI, &iDMA_mm_ctrl::channel_retry,
        &iDMA_mm_ctrl::channel_resp};
    vp::IoMaster obi2axi_itf{IDMA_OBI2AXI, &iDMA_mm_ctrl::channel_retry,
        &iDMA_mm_ctrl::channel_resp};
    // Register port of each channel, indexed by channel
    vp::IoMaster *channel_itf[IDMA_NB_CHANNELS] = {&axi2obi_itf, &obi2axi_itf};
};

iDMA_mm_ctrl::iDMA_mm_ctrl(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->new_slave_port("input", &this->input_itf);
    this->new_master_port("axi2obi", &this->axi2obi_itf);
    this->new_master_port("obi2axi", &this->obi2axi_itf);
}

bool iDMA_mm_ctrl::is_valid_offset(uint64_t offset)
{
    if ((offset & 0x3) != 0) return false;
    // CONF, then the STATUS, NEXT_ID and DONE_ID multiregs
    if (offset <= 0xC0) return true;
    switch (offset)
    {
        case 0xD0: case 0xD8: case 0xE0:
        case 0xE8: case 0xF0: case 0xF8:
        case 0x100: case 0x108: case 0x110:
            return true;
    }
    return false;
}

vp::IoReqStatus iDMA_mm_ctrl::req(vp::Block *__this, vp::IoReq *req)
{
    iDMA_mm_ctrl *_this = (iDMA_mm_ctrl *)__this;
    uint64_t addr = req->get_addr();
    int channel = addr / IDMA_CHANNEL_SIZE;
    uint64_t offset = addr % IDMA_CHANNEL_SIZE;

    if (channel >= IDMA_NB_CHANNELS || !is_valid_offset(offset))
    {
        _this->trace.force_warning("Invalid iDMA register access (offset: 0x%lx, is_write: %d)\n",
            addr, req->get_is_write());
        req->set_resp_status(vp::IO_RESP_INVALID);
        return vp::IO_REQ_DONE;
    }

    _this->trace.msg(vp::Trace::LEVEL_TRACE, "Forwarding access (channel: %s, offset: 0x%lx, "
        "is_write: %d)\n", channel == IDMA_AXI2OBI ? "axi2obi" : "obi2axi", offset,
        req->get_is_write());

    req->set_addr(offset);
    vp::IoReqStatus status = _this->channel_itf[channel]->req(req);
    if (status != vp::IO_REQ_GRANTED)
    {
        // Done or denied: the request is back to the initiator, which may
        // re-send it on a retry, so it gets its address back
        req->set_addr(addr);
    }
    return status;
}

vp::IoRespAck iDMA_mm_ctrl::channel_resp(vp::Block *__this, vp::IoReq *req, int channel)
{
    iDMA_mm_ctrl *_this = (iDMA_mm_ctrl *)__this;
    req->set_addr(req->get_addr() + channel * IDMA_CHANNEL_SIZE);
    return _this->input_itf.resp(req);
}

void iDMA_mm_ctrl::channel_retry(vp::Block *__this, int channel, vp::IoRetryChannel retry_channel)
{
    iDMA_mm_ctrl *_this = (iDMA_mm_ctrl *)__this;
    _this->input_itf.retry(retry_channel);
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new iDMA_mm_ctrl(config);
}

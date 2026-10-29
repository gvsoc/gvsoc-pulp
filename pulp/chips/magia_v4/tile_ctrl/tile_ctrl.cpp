/*
 * Copyright (C) 2026 Fondazione Chips-IT
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
#include <vp/itf/wire.hpp>

/*
 * Control registers of a MAGIA v4 tile, at 0x1700: the three RTL slaves
 * behind the control demux, one window here. Offsets from 0x1700:
 *
 * Spatz (obi_slave_ctrl_spatz)
 *   0x000 SPATZ_CLK_EN   bit 0, drives spatz_clock_en
 *   0x004 SPATZ_READY    bit 0
 *   0x008 SPATZ_START    bit 0, drives spatz_start (a level)
 *   0x00C SPATZ_TASKBIN  stored
 *   0x010 SPATZ_DATA     stored
 *   0x014 SPATZ_RETURN   stored
 *   0x018 SPATZ_DONE     writing bit 0 pulses spatz_done for one cycle
 *
 * PULP cluster (obi_slave_ctrl_cluster)
 *   0x040 CLUSTER_FETCH_EN  != 0 raises cluster_fetchen; clears READY
 *   0x044 CLUSTER_BINARY    boot address of the cluster cores (cluster_entry),
 *                           0xCC000080 at reset
 *   0x048 CLUSTER_DONE      a write pulses cluster_done for one cycle
 *   0x04C CLUSTER_TASKBIN   stored
 *   0x050 CLUSTER_DATA      stored
 *   0x054 CLUSTER_START     != 0: START = 1 and a cluster_start pulse (event of
 *                           core 0 in the cluster event unit); 0: START = 0
 *   0x058 CLUSTER_READY     a write counts one core, up to the cluster size;
 *                           reads 1 once every core counted
 *   0x05C CLUSTER_RETURN    stored
 *
 * Collectives (obi_slave_ctrl_coll), registers only: the collectives are not
 * modelled
 *   0x100 COLL_MASK
 *   0x104 COLL_OP           bits 3:0
 *
 * The other offsets of a block read 0xDEADBEEF and ignore writes, as the RTL.
 * The RTL answers one cycle after the request (registered rvalid), which the
 * cut in front of the control demux adds.
 */

#define SPATZ_CLK_EN      0x000
#define SPATZ_READY       0x004
#define SPATZ_START       0x008
#define SPATZ_TASKBIN     0x00C
#define SPATZ_DATA        0x010
#define SPATZ_RETURN      0x014
#define SPATZ_DONE        0x018
#define SPATZ_END         0x01C

#define CLUSTER_FETCH_EN  0x040
#define CLUSTER_BINARY    0x044
#define CLUSTER_DONE      0x048
#define CLUSTER_TASKBIN   0x04C
#define CLUSTER_DATA      0x050
#define CLUSTER_START     0x054
#define CLUSTER_READY     0x058
#define CLUSTER_RETURN    0x05C
#define CLUSTER_END       0x060

#define COLL_MASK         0x100
#define COLL_OP           0x104
#define COLL_END          0x108

#define TILE_CTRL_SIZE    0x200

// Reset value of CLUSTER_BINARY (obi_slave_ctrl_cluster entry_point_q)
#define CLUSTER_BINARY_RESET 0xCC000080

class TileCtrl : public vp::Component
{
public:
    TileCtrl(vp::ComponentConf &config);
    void reset(bool active) override;

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    // End of the one-cycle pulses
    static void pulse_end_handler(vp::Block *__this, vp::ClockEvent *event);
    // True if the offset is a register of one of the three blocks
    static bool is_register(uint64_t offset);
    uint32_t read(uint64_t offset);
    void write(uint64_t offset, uint32_t value);
    void pulse(vp::WireMaster<bool> *itf);

    vp::Trace trace;
    vp::IoSlave input_itf{&TileCtrl::req};

    vp::WireMaster<bool> spatz_clock_en_itf;
    vp::WireMaster<bool> spatz_start_itf;
    vp::WireMaster<bool> spatz_done_itf;
    vp::WireMaster<bool> cluster_fetchen_itf;
    vp::WireMaster<uint64_t> cluster_entry_itf;
    vp::WireMaster<bool> cluster_done_itf;
    vp::WireMaster<bool> cluster_start_itf;

    int nb_cluster_cores;
    // Register values, regs[offset / 4]
    uint32_t regs[TILE_CTRL_SIZE / 4];
    // Cores which wrote CLUSTER_READY since the last CLUSTER_FETCH_EN
    int nb_ready;

    // The pulses raised in this cycle, lowered by pulse_end_event
    bool spatz_done_pulse;
    bool cluster_done_pulse;
    bool cluster_start_pulse;
    vp::ClockEvent pulse_end_event;
};

TileCtrl::TileCtrl(vp::ComponentConf &config)
    : vp::Component(config), pulse_end_event(this, &TileCtrl::pulse_end_handler)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->nb_cluster_cores = this->get_js_config()->get_child_int("nb_cluster_cores");

    this->new_slave_port("input", &this->input_itf);
    this->new_master_port("spatz_clock_en", &this->spatz_clock_en_itf);
    this->new_master_port("spatz_start", &this->spatz_start_itf);
    this->new_master_port("spatz_done", &this->spatz_done_itf);
    this->new_master_port("cluster_fetchen", &this->cluster_fetchen_itf);
    this->new_master_port("cluster_entry", &this->cluster_entry_itf);
    this->new_master_port("cluster_done", &this->cluster_done_itf);
    this->new_master_port("cluster_start", &this->cluster_start_itf);
}

void TileCtrl::reset(bool active)
{
    if (active)
    {
        for (int i = 0; i < TILE_CTRL_SIZE / 4; i++)
        {
            this->regs[i] = 0;
        }
        this->regs[CLUSTER_BINARY / 4] = CLUSTER_BINARY_RESET;
        this->nb_ready = 0;
        this->spatz_done_pulse = false;
        this->cluster_done_pulse = false;
        this->cluster_start_pulse = false;
    }
    else
    {
        // The boot address of the cluster cores is a register output
        if (this->cluster_entry_itf.is_bound())
        {
            this->cluster_entry_itf.sync(this->regs[CLUSTER_BINARY / 4]);
        }
    }
}

bool TileCtrl::is_register(uint64_t offset)
{
    return offset < SPATZ_END || (offset >= CLUSTER_FETCH_EN && offset < CLUSTER_END)
        || (offset >= COLL_MASK && offset < COLL_END);
}

vp::IoReqStatus TileCtrl::req(vp::Block *__this, vp::IoReq *req)
{
    TileCtrl *_this = (TileCtrl *)__this;
    uint64_t offset = req->get_addr();
    uint32_t *data = (uint32_t *)req->get_data();

    if (req->get_size() != 4 || (offset & 0x3) != 0 || offset >= TILE_CTRL_SIZE)
    {
        _this->trace.force_warning("Invalid access (offset: 0x%lx, size: %ld)\n", offset,
            req->get_size());
        req->set_resp_status(vp::IO_RESP_INVALID);
        return vp::IO_REQ_DONE;
    }

    if (req->get_is_write())
    {
        _this->write(offset, *data);
    }
    else
    {
        *data = _this->read(offset);
    }
    return vp::IO_REQ_DONE;
}

uint32_t TileCtrl::read(uint64_t offset)
{
    uint32_t value;
    if (!is_register(offset))
    {
        value = 0xDEADBEEF;
    }
    else if (offset == CLUSTER_READY)
    {
        value = this->nb_ready == this->nb_cluster_cores;
    }
    else if (offset == SPATZ_DONE)
    {
        value = this->spatz_done_pulse;
    }
    else if (offset == CLUSTER_DONE)
    {
        value = this->cluster_done_pulse;
    }
    else
    {
        value = this->regs[offset / 4];
    }
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Read (offset: 0x%03lx, value: 0x%08x)\n", offset,
        value);
    return value;
}

void TileCtrl::write(uint64_t offset, uint32_t value)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Write (offset: 0x%03lx, value: 0x%08x)\n", offset,
        value);

    switch (offset)
    {
        case SPATZ_CLK_EN:
            this->regs[offset / 4] = value & 1;
            this->spatz_clock_en_itf.sync(value & 1);
            break;
        case SPATZ_READY:
            this->regs[offset / 4] = value & 1;
            break;
        case SPATZ_START:
            this->regs[offset / 4] = value & 1;
            this->spatz_start_itf.sync(value & 1);
            break;
        case SPATZ_DONE:
            if (value & 1)
            {
                this->spatz_done_pulse = true;
                this->pulse(&this->spatz_done_itf);
            }
            break;
        case CLUSTER_FETCH_EN:
            this->regs[offset / 4] = value != 0;
            this->nb_ready = 0;
            this->cluster_fetchen_itf.sync(value != 0);
            break;
        case CLUSTER_BINARY:
            this->regs[offset / 4] = value;
            this->cluster_entry_itf.sync(value);
            break;
        case CLUSTER_DONE:
            this->cluster_done_pulse = true;
            this->pulse(&this->cluster_done_itf);
            break;
        case CLUSTER_START:
            this->regs[offset / 4] = value != 0;
            if (value != 0)
            {
                this->cluster_start_pulse = true;
                this->pulse(&this->cluster_start_itf);
            }
            break;
        case CLUSTER_READY:
            if (this->nb_ready < this->nb_cluster_cores)
            {
                this->nb_ready++;
            }
            break;
        case COLL_OP:
            this->regs[offset / 4] = value & 0xF;
            break;
        case SPATZ_TASKBIN: case SPATZ_DATA: case SPATZ_RETURN:
        case CLUSTER_TASKBIN: case CLUSTER_DATA: case CLUSTER_RETURN:
        case COLL_MASK:
            this->regs[offset / 4] = value;
            break;
        default:
            // Unused offset of a block: ignored, as the RTL
            break;
    }
}

void TileCtrl::pulse(vp::WireMaster<bool> *itf)
{
    if (itf->is_bound())
    {
        itf->sync(true);
    }
    if (!this->pulse_end_event.is_enqueued())
    {
        this->pulse_end_event.enqueue(1);
    }
}

void TileCtrl::pulse_end_handler(vp::Block *__this, vp::ClockEvent *event)
{
    TileCtrl *_this = (TileCtrl *)__this;
    if (_this->spatz_done_pulse && _this->spatz_done_itf.is_bound())
    {
        _this->spatz_done_itf.sync(false);
    }
    if (_this->cluster_done_pulse && _this->cluster_done_itf.is_bound())
    {
        _this->cluster_done_itf.sync(false);
    }
    if (_this->cluster_start_pulse && _this->cluster_start_itf.is_bound())
    {
        _this->cluster_start_itf.sync(false);
    }
    _this->spatz_done_pulse = false;
    _this->cluster_done_pulse = false;
    _this->cluster_start_pulse = false;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new TileCtrl(config);
}

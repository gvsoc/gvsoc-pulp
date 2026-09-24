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
 * Authors: Germain Haugou, ETH Zurich (germain.haugou@iis.ee.ethz.ch)
            Yichao  Zhang , ETH Zurich (yiczhang@iis.ee.ethz.ch)
            Chi     Zhang , ETH Zurich (chizhang@iis.ee.ethz.ch)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <cpu/iss/include/offload.hpp>


#define MAX_CLUSTERS 16
#define CLUSTER_LOCAL_EOC_NOTIFY_OFFSET 24

class ClusterRegisters : public vp::Component
{

public:
    ClusterRegisters(vp::ComponentConf &config);

    void reset(bool active);

    static vp::IoReqStatus barrier_reg_req(vp::Block *__this, vp::IoReq *req);

private:
    // Mempool
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static void wakeup_event_handler(vp::Block *__this, vp::ClockEvent *event);
    
    // Inter-cluster
    static vp::IoReqStatus global_barrier_sync(vp::Block *__this, vp::IoReq *req);
    static void grant(vp::Block *__this, vp::IoReq *req);
    static void response(vp::Block *__this, vp::IoReq *req);
    static void hbm_preload_done_handler(vp::Block *__this, bool value);
    static void inst_preheat_done_handler(vp::Block *__this, bool value);
    void fetch_start_check();

    vp::Trace trace;

    // Mempool (0x40000000)
    vp::IoSlave input_itf;
    vp::WireMaster<bool> barrier_ack_itf;
    vp::WireMaster<IssOffloadInsn<uint32_t>*> rocache_cfg_itf;
    vp::ClockEvent * wakeup_event;
    int wakeup_latency;
    uint32_t soc_register_base;

    // Inter-cluster (ARCH_CLUSTER_REG_BASE)
    vp::IoSlave barrier_reg_input_itf;
    uint32_t cluster_id;
    vp::reg_32 barrier_status;
    uint32_t num_cluster_x;
    uint32_t num_cluster_y;

    vp::IoSlave  global_barrier_slave_itf;
    vp::IoMaster global_barrier_master_itf;
    vp::IoReq*   global_barrier_master_req;
    uint32_t     global_barrier_addr;
    uint8_t *    global_barrier_buffer;

    vp::WireSlave<bool> hbm_preload_done_itf;
    vp::WireSlave<bool> inst_preheat_done_itf;
    vp::WireMaster<bool> fetch_start_itf;
    uint32_t hbm_preload_done;
    uint32_t inst_preheat_done;
    uint32_t fetch_started;

    vp::IoReq * global_barrier_query;
    int global_barrier_mutex;

    uint16_t global_sync_enable;
    uint64_t global_sync_timestamp;

    // Pre-allocated IoReq for cluster-local EOC notification to top-level ctrl_registers.
    bool eoc_reached;
    vp::IoMaster cluster_eoc_itf;
    vp::IoReq eoc_notify_req;
    uint32_t eoc_notify_data;
};



ClusterRegisters::ClusterRegisters(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    // Mempool (0x40000000)
    this->input_itf.set_req_meth(&ClusterRegisters::req);
    this->new_slave_port("input", &this->input_itf);
    this->new_master_port("barrier_ack", &this->barrier_ack_itf);
    this->new_master_port("rocache_cfg", &this->rocache_cfg_itf);
    this->new_master_port("cluster_eoc", &this->cluster_eoc_itf);
    this->soc_register_base = get_js_config()->get_child_int("soc_register_base");
    this->wakeup_event = this->event_new(&ClusterRegisters::wakeup_event_handler);
    wakeup_latency = get_js_config()->get_child_int("wakeup_latency");
    eoc_reached = false;

    // Inter-cluster (ARCH_CLUSTER_REG_BASE)
    this->barrier_reg_input_itf.set_req_meth(&ClusterRegisters::barrier_reg_req);
    this->new_slave_port("barrier_reg_input", &this->barrier_reg_input_itf);

    this->cluster_id = this->get_js_config()->get("cluster_id")->get_int();
    this->num_cluster_x = this->get_js_config()->get("num_cluster_x")->get_int();
    this->num_cluster_y = this->get_js_config()->get("num_cluster_y")->get_int();

    this->global_barrier_slave_itf.set_req_meth(&ClusterRegisters::global_barrier_sync);
    this->new_slave_port("global_barrier_slave", &this->global_barrier_slave_itf);
    this->new_master_port("global_barrier_master", &this->global_barrier_master_itf);
    this->global_barrier_master_req = this->global_barrier_master_itf.req_new(0, 0, 0, 0);
    this->global_barrier_addr = this->get_js_config()->get("global_barrier_addr")->get_int();
    this->global_barrier_buffer = new uint8_t[4];
    this->global_barrier_master_itf.set_resp_meth(&ClusterRegisters::response);
    this->global_barrier_master_itf.set_grant_meth(&ClusterRegisters::grant);

    this->global_barrier_query = NULL;
    this->global_barrier_mutex = 0;

    this->global_sync_enable = 0;
    this->global_sync_timestamp = 0;

    this->new_slave_port("hbm_preload_done", &this->hbm_preload_done_itf);
    this->new_slave_port("inst_preheat_done", &this->inst_preheat_done_itf);
    this->new_master_port("fetch_start", &this->fetch_start_itf);
    this->hbm_preload_done_itf.set_sync_meth(&ClusterRegisters::hbm_preload_done_handler);
    this->inst_preheat_done_itf.set_sync_meth(&ClusterRegisters::inst_preheat_done_handler);
    this->hbm_preload_done = 0;
    this->inst_preheat_done = 0;
    this->fetch_started = 0;
}

void ClusterRegisters::wakeup_event_handler(vp::Block *__this, vp::ClockEvent *event) {
    ClusterRegisters *_this = (ClusterRegisters *)__this;
    _this->barrier_ack_itf.sync(1);
    _this->trace.msg("Control registers wake up signal work and write %d to barrier ack output\n", 1);
}

vp::IoReqStatus ClusterRegisters::req(vp::Block *__this, vp::IoReq *req)
{
    ClusterRegisters *_this = (ClusterRegisters *)__this;

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();
    int initiator = req->get_initiator();

    _this->trace.msg("Control registers access (offset: 0x%x, size: 0x%x, is_write: %d, data:%x, initiator:%d)\n", offset, size, is_write, *(uint32_t *)data, initiator);

    if (is_write && size == 4)
    {
        uint32_t value = *(uint32_t *)data;
        if (offset == 0 && (value & 1) && !_this->eoc_reached)
        {
            _this->eoc_reached = true;
            std::cout << "EOC register return value: 0x" << std::hex << ((value - 1) >> 1) << std::endl;

            // Encode cluster_id in upper 16 bits and keep original mempool payload in lower 16 bits.
            _this->eoc_notify_data = (value & 0xFFFF) | ((_this->cluster_id & 0xFFFF) << 16);
            _this->eoc_notify_req.init();
            _this->eoc_notify_req.set_is_write(true);
            _this->eoc_notify_req.set_addr(_this->soc_register_base + CLUSTER_LOCAL_EOC_NOTIFY_OFFSET);
            _this->eoc_notify_req.set_size(4);
            _this->eoc_notify_req.set_data((uint8_t *)&_this->eoc_notify_data);

            // Send cluster-local EOC notification through dedicated MMIO path.
            _this->cluster_eoc_itf.req(&_this->eoc_notify_req);
        }
        if (offset == 4 && value == 0xFFFFFFFF)
        {
            _this->event_enqueue(_this->wakeup_event, _this->wakeup_latency);
        }
        if (offset == 0x48 || offset == 0x4C || offset == 0x50 || offset == 0x54)
        {
            IssOffloadInsn<uint32_t> insn;
            insn.arg_a = (offset - 0x48) >> 2;
            insn.arg_b = 0;
            insn.arg_c = value;
            _this->rocache_cfg_itf.sync(&insn);
        }
        if (offset == 0x58 || offset == 0x5C || offset == 0x60 || offset == 0x64)
        {
            IssOffloadInsn<uint32_t> insn;
            insn.arg_a = (offset - 0x58) >> 2;
            insn.arg_b = 1;
            insn.arg_c = value;
            _this->rocache_cfg_itf.sync(&insn);
        }
    }

    return vp::IO_REQ_OK;
}

vp::IoReqStatus ClusterRegisters::barrier_reg_req(vp::Block *__this, vp::IoReq *req)
{
    ClusterRegisters *_this = (ClusterRegisters *)__this;
    uint64_t offset = req->get_addr();
    bool is_write = req->get_is_write();
    uint64_t size = req->get_size();
    uint32_t *data = (uint32_t *) req->get_data();

    // _this->trace.msg("Received IO req (offset: 0x%llx, size: 0x%llx, is_write: %d) -- Cluster ID: %0d\n", offset, size, is_write, _this->cluster_id);

    if (is_write && offset == 0 && _this->global_barrier_query == NULL)
    {
        if (_this->global_barrier_mutex == 1)
        {
            _this->global_barrier_mutex = 0;
            // _this->trace.msg("[Global Sync] Core Access <After> Globale Barrier\n");
            return vp::IO_REQ_OK;
        }else{
            _this->global_barrier_query = req;
            // _this->trace.msg("[Global Sync] Core Access <Before> Globale Barrier\n");
            return vp::IO_REQ_PENDING;
        }
    }

    if (!is_write && offset == 0)
    {
        data[0] = _this->cluster_id;
    }

    if(offset == 4){
        data[0] = 1;
    }

    if(offset == 8){
        data[0] = _this->num_cluster_x * _this->num_cluster_y;
    }

    if(offset == 12){
        data[0] = _this->num_cluster_x;
    }

    if(offset == 16){
        data[0] = _this->num_cluster_y;
    }

    if(is_write && offset == 20){
        if (_this->global_sync_enable == 0)
        {
            _this->global_sync_enable = 1;
            _this->global_sync_timestamp = _this->time.get_time()/1000;
        } else {
            uint32_t type = data[0];
            _this->global_sync_enable = 0;
            _this->trace.msg("Cluster Sync: %d ns -> %d ns | period = %d ns | Type = %d\n",
                _this->global_sync_timestamp,
                _this->time.get_time()/1000,
                _this->time.get_time()/1000 - _this->global_sync_timestamp,
                type);
        }
    }

    if(offset == 24){
        data[0] = 0;
    }

    if(is_write && offset == 28){
        uint32_t value = *(uint32_t *)data;
        uint8_t row_mask = value & 0xFFFF; // Lower 16 bits
        uint8_t col_mask = value >> 16; // Upper 16 bits

        // _this->trace.msg(vp::Trace::LEVEL_DEBUG, "[Global Sync] start wakeup with row_mask: %d and col_mask: %d\n", row_mask, col_mask);

        _this->global_barrier_master_req->prepare();
        _this->global_barrier_master_req->set_is_write(true);
        _this->global_barrier_master_req->set_addr(_this->global_barrier_addr);
        _this->global_barrier_master_req->set_size(4);
        _this->global_barrier_master_req->set_data(_this->global_barrier_buffer);

        uint8_t * payload_ptr = _this->global_barrier_master_req->get_payload();
        payload_ptr[0] = 1; //broadcast
        payload_ptr[1] = row_mask;
        payload_ptr[2] = col_mask;

        vp::IoReqStatus status = _this->global_barrier_master_itf.req(_this->global_barrier_master_req);

        if (status == vp::IO_REQ_INVALID)
        {
            _this->trace.fatal("[Global Sync] There was an error while broadcating wakeup signal\n");
        }
    }

    // _this->regmap.access(offset, size, data, is_write);

    return vp::IO_REQ_OK;
}

void ClusterRegisters::hbm_preload_done_handler(vp::Block *__this, bool value)
{
    ClusterRegisters *_this = (ClusterRegisters *)__this;
    _this->hbm_preload_done = 1;
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "HBM Preloading Done\n");
    _this->fetch_start_check();
}

void ClusterRegisters::inst_preheat_done_handler(vp::Block *__this, bool value)
{
    ClusterRegisters *_this = (ClusterRegisters *)__this;
    _this->inst_preheat_done = 1;
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Instruction Preheating Done\n");
    _this->fetch_start_check();
}

void ClusterRegisters::fetch_start_check()
{
    if (this->hbm_preload_done && this->inst_preheat_done)
    {
        if (this->fetch_started == 0)
        {
            this->fetch_start_itf.sync(1);
        }
    }
}

vp::IoReqStatus ClusterRegisters::global_barrier_sync(vp::Block *__this, vp::IoReq *req)
{
    ClusterRegisters *_this = (ClusterRegisters *)__this;

    // _this->trace.msg(vp::Trace::LEVEL_DEBUG, "[Global Sync] Globale Barrier Triggered\n");

    if (_this->global_barrier_query == NULL)
    {
        _this->global_barrier_mutex = 1;
    }else{
        _this->global_barrier_mutex = 0;
        _this->global_barrier_query->get_resp_port()->resp(_this->global_barrier_query);
        _this->global_barrier_query = NULL;
    }

    return vp::IO_REQ_OK;

}

void ClusterRegisters::reset(bool active)
{
    this->new_reg("barrier_status", &this->barrier_status, 0, true);
}


void ClusterRegisters::response(vp::Block *__this, vp::IoReq *req)
{

}


void ClusterRegisters::grant(vp::Block *__this, vp::IoReq *req)
{

}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ClusterRegisters(config);
}
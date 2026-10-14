// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <stdio.h>
#include <vp/itf/wire.hpp>
#include <vp/itf/clock.hpp>
#include <string.h>
#include <ips/pulp/soc_eu/soc_eu/soc_eu_config.hpp>
#include "archi/soc_eu_v2.h"
#include "archi/soc_eu_regs.h"
#include "archi/soc_eu_regfields.h"
#include "archi/soc_eu_gvsoc.h"


using namespace std::placeholders;

#include <queue>


class soc_eu;


class soc_eu_target
{
public:
    soc_eu_target(soc_eu *top, std::string name, std::string itf_name, int id);
    static void soc_event_sync(vp::Block *_this, int ready, int id);
    void reset(bool active);

    vp::WireMaster<int> event_itf;
    std::vector<vp::Register<uint32_t> *> mask_regs;
    std::string name;
    std::queue<int> fifo;
    int nb_waiting_events[256];
};


class soc_eu : public vp::Component
{

public:
    soc_eu(vp::ComponentConf &config);

    void reset(bool active);

    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

private:
    void sw_event_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);
    void fc_mask_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write, int reg_id);
    void cl_mask_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write, int reg_id);
    void pr_mask_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write, int reg_id);
    void fc_mask_set_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);
    void fc_mask_clr_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);
    static void event_in_sync(vp::Block *__this, int event);
    static void event_slot_sync(vp::Block *__this, bool value, int id);
    static void ref_clock_sync(vp::Block *_this, bool value);
    void trigger_event(int event);

    vp::IoReqStatus trigger_event_req(int reg_offset, int size, bool is_write, uint8_t *data);
    vp::IoReqStatus mask_req(int target_id, int reg_id, int reg_offset, int size, bool is_write, uint8_t *data);

    SocEuConfig cfg;

    vp::Trace trace;
    vp::IoSlave in{&soc_eu::req};

    vp::WireSlave<int> event_in_itf;
    // Per-event-ID slots: one wire<bool> per possible event (the
    // RTL's per_events_i bus). Slot index == event ID, hard-wired
    // 1:1 with the RTL bit-position-to-event-id mapping inside
    // soc_event_generator.
    std::vector<vp::WireSlave<bool> *> event_slots;
    vp::ClockSlave ref_clock;
    vp::WireMaster<bool> ref_clock_event_itf;

    vector<soc_eu_target *> targets;
    vp_regmap_soc_eu regmap;
};


soc_eu::soc_eu(vp::ComponentConf &config)
    : vp::Component(config, this->cfg), regmap(*this, "regmap")
{
    traces.new_trace("trace", &trace, vp::DEBUG);
    new_slave_port("input", &in);

    this->ref_clock.set_sync_meth(&soc_eu::ref_clock_sync);
    this->new_slave_port("ref_clock", &this->ref_clock);

    event_in_itf.set_sync_meth(&soc_eu::event_in_sync);
    new_slave_port("event_in", &event_in_itf);

    // Declare a wire<bool> slot per possible event ID. Slot index ==
    // event ID, matching the RTL per_events_i bit-position layout.
    this->event_slots.reserve(this->cfg.nb_events);
    for (int id = 0; id < this->cfg.nb_events; id++)
    {
        vp::WireSlave<bool> *slot = new vp::WireSlave<bool>();
        slot->set_sync_meth_muxed(&soc_eu::event_slot_sync, id);
        this->new_slave_port("event_" + std::to_string(id), slot);
        this->event_slots.push_back(slot);
    }

    new_master_port("ref_clock_event", &ref_clock_event_itf);

    this->targets.push_back(new soc_eu_target(this, "FC", "fc_event_itf", 0));
    this->targets.push_back(new soc_eu_target(this, "PR", "pr_event_itf", 1));
    this->targets.push_back(new soc_eu_target(this, "CL", "cl_event_itf", 2));

    this->regmap.build(this, &this->trace);
    this->regmap.sw_event.register_callback(std::bind(&soc_eu::sw_event_req, this, _1, _2, _3, _4));
    this->regmap.fc_mask_0.register_callback(std::bind(&soc_eu::fc_mask_req, this, _1, _2, _3, _4, 0));
    this->regmap.fc_mask_1.register_callback(std::bind(&soc_eu::fc_mask_req, this, _1, _2, _3, _4, 1));
    this->regmap.fc_mask_2.register_callback(std::bind(&soc_eu::fc_mask_req, this, _1, _2, _3, _4, 2));
    this->regmap.fc_mask_3.register_callback(std::bind(&soc_eu::fc_mask_req, this, _1, _2, _3, _4, 3));
    this->regmap.fc_mask_4.register_callback(std::bind(&soc_eu::fc_mask_req, this, _1, _2, _3, _4, 4));
    this->regmap.fc_mask_5.register_callback(std::bind(&soc_eu::fc_mask_req, this, _1, _2, _3, _4, 5));
    this->regmap.fc_mask_6.register_callback(std::bind(&soc_eu::fc_mask_req, this, _1, _2, _3, _4, 6));
    this->regmap.fc_mask_7.register_callback(std::bind(&soc_eu::fc_mask_req, this, _1, _2, _3, _4, 7));
    this->regmap.cl_mask_0.register_callback(std::bind(&soc_eu::cl_mask_req, this, _1, _2, _3, _4, 0));
    this->regmap.cl_mask_1.register_callback(std::bind(&soc_eu::cl_mask_req, this, _1, _2, _3, _4, 1));
    this->regmap.cl_mask_2.register_callback(std::bind(&soc_eu::cl_mask_req, this, _1, _2, _3, _4, 2));
    this->regmap.cl_mask_3.register_callback(std::bind(&soc_eu::cl_mask_req, this, _1, _2, _3, _4, 3));
    this->regmap.cl_mask_4.register_callback(std::bind(&soc_eu::cl_mask_req, this, _1, _2, _3, _4, 4));
    this->regmap.cl_mask_5.register_callback(std::bind(&soc_eu::cl_mask_req, this, _1, _2, _3, _4, 5));
    this->regmap.cl_mask_6.register_callback(std::bind(&soc_eu::cl_mask_req, this, _1, _2, _3, _4, 6));
    this->regmap.cl_mask_7.register_callback(std::bind(&soc_eu::cl_mask_req, this, _1, _2, _3, _4, 7));
    this->regmap.pr_mask_0.register_callback(std::bind(&soc_eu::pr_mask_req, this, _1, _2, _3, _4, 0));
    this->regmap.pr_mask_1.register_callback(std::bind(&soc_eu::pr_mask_req, this, _1, _2, _3, _4, 1));
    this->regmap.pr_mask_2.register_callback(std::bind(&soc_eu::pr_mask_req, this, _1, _2, _3, _4, 2));
    this->regmap.pr_mask_3.register_callback(std::bind(&soc_eu::pr_mask_req, this, _1, _2, _3, _4, 3));
    this->regmap.pr_mask_4.register_callback(std::bind(&soc_eu::pr_mask_req, this, _1, _2, _3, _4, 4));
    this->regmap.pr_mask_5.register_callback(std::bind(&soc_eu::pr_mask_req, this, _1, _2, _3, _4, 5));
    this->regmap.pr_mask_6.register_callback(std::bind(&soc_eu::pr_mask_req, this, _1, _2, _3, _4, 6));
    this->regmap.pr_mask_7.register_callback(std::bind(&soc_eu::pr_mask_req, this, _1, _2, _3, _4, 7));
    this->regmap.fc_mask_set.register_callback(std::bind(&soc_eu::fc_mask_set_req, this, _1, _2, _3, _4));
    this->regmap.fc_mask_clr.register_callback(std::bind(&soc_eu::fc_mask_clr_req, this, _1, _2, _3, _4));

    this->targets[0]->mask_regs.push_back(&this->regmap.fc_mask_0);
    this->targets[0]->mask_regs.push_back(&this->regmap.fc_mask_1);
    this->targets[0]->mask_regs.push_back(&this->regmap.fc_mask_2);
    this->targets[0]->mask_regs.push_back(&this->regmap.fc_mask_3);
    this->targets[0]->mask_regs.push_back(&this->regmap.fc_mask_4);
    this->targets[0]->mask_regs.push_back(&this->regmap.fc_mask_5);
    this->targets[0]->mask_regs.push_back(&this->regmap.fc_mask_6);
    this->targets[0]->mask_regs.push_back(&this->regmap.fc_mask_7);

    this->targets[1]->mask_regs.push_back(&this->regmap.cl_mask_0);
    this->targets[1]->mask_regs.push_back(&this->regmap.cl_mask_1);
    this->targets[1]->mask_regs.push_back(&this->regmap.cl_mask_2);
    this->targets[1]->mask_regs.push_back(&this->regmap.cl_mask_3);
    this->targets[1]->mask_regs.push_back(&this->regmap.cl_mask_4);
    this->targets[1]->mask_regs.push_back(&this->regmap.cl_mask_5);
    this->targets[1]->mask_regs.push_back(&this->regmap.cl_mask_6);
    this->targets[1]->mask_regs.push_back(&this->regmap.cl_mask_7);

    this->targets[2]->mask_regs.push_back(&this->regmap.pr_mask_0);
    this->targets[2]->mask_regs.push_back(&this->regmap.pr_mask_1);
    this->targets[2]->mask_regs.push_back(&this->regmap.pr_mask_2);
    this->targets[2]->mask_regs.push_back(&this->regmap.pr_mask_3);
    this->targets[2]->mask_regs.push_back(&this->regmap.pr_mask_4);
    this->targets[2]->mask_regs.push_back(&this->regmap.pr_mask_5);
    this->targets[2]->mask_regs.push_back(&this->regmap.pr_mask_6);
    this->targets[2]->mask_regs.push_back(&this->regmap.pr_mask_7);

}


void soc_eu::trigger_event(int event)
{
    int word_id = event >> 5;
    int bit_id = event & 0x1f;

    for (auto target : this->targets)
    {
        if (((target->mask_regs[word_id]->get() >> bit_id) & 1) == 0)
        {
            trace.msg("Generating %s event (event: %d)\n", target->name.c_str(), event);
            if (!target->event_itf.is_bound())
                trace.msg("Trying to send event to %s while it is not instantiated\n", target->name.c_str());
            else
            {
                int ready;
                target->event_itf.sync_back(&ready);
                if (!ready)
                {
                    if (target->nb_waiting_events[event] == 4)
                    {
                        trace.force_warning("Pushing too many events (event: %d)\n", event);
                    }
                    else
                    {
                        target->nb_waiting_events[event]++;
                        target->fifo.push(event);
                    }
                }
                else
                {
                    target->event_itf.sync(event);
                }
            }
        }
    }
}


void soc_eu::sw_event_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write)
{
    this->regmap.sw_event.update(reg_offset, size, value, is_write);

    uint32_t data = this->regmap.sw_event.get();
    for (unsigned int i = 0; i < this->cfg.nb_fc_events; i++)
    {
        if ((data >> i) & 1)
        {
            this->trigger_event(i + this->cfg.first_fc_event);
        }
    }
}


void soc_eu::fc_mask_set_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write)
{
    if (size == 4 && is_write)
    {
        uint32_t data = *(uint32_t *)value;
        vp::Register<uint32_t> *fc_mask_reg = this->targets[0]->mask_regs[data / 32];
        uint32_t reg_value = fc_mask_reg->get();
        reg_value |= 1 << (data % 32);
        fc_mask_reg->update(0, 4, (uint8_t *)&reg_value, true);
    }
}


void soc_eu::fc_mask_clr_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write)
{
    if (size == 4 && is_write)
    {
        uint32_t data = *(uint32_t *)value;
        vp::Register<uint32_t> *fc_mask_reg = this->targets[0]->mask_regs[data / 32];
        uint32_t reg_value = fc_mask_reg->get();
        reg_value &= ~(1 << (data % 32));
        fc_mask_reg->update(0, 4, (uint8_t *)&reg_value, true);
    }
}


void soc_eu::event_in_sync(vp::Block *__this, int event)
{
    soc_eu *_this = (soc_eu *)__this;
    _this->trace.msg("Received incoming event (event: %d)\n", event);
    _this->trigger_event(event);
}


void soc_eu::event_slot_sync(vp::Block *__this, bool value, int id)
{
    // Rising-edge detect; the muxed `id` is already the event ID
    // (slot index == per_events_i bit position == event ID).
    if (!value) return;
    soc_eu *_this = (soc_eu *)__this;
    _this->trace.msg("Received event from slot (event: %d)\n", id);
    _this->trigger_event(id);
}


void soc_eu::ref_clock_sync(vp::Block *__this, bool value)
{
    soc_eu *_this = (soc_eu *)__this;
    _this->trace.msg("Received ref clock event, generating event (event: %ld)\n", (long)_this->cfg.ref_clock_event);
    _this->ref_clock_event_itf.sync(true);
    _this->trigger_event(_this->cfg.ref_clock_event);
}


void soc_eu::fc_mask_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write, int reg_id)
{
    vp::Register<uint32_t> *fc_mask_reg = this->targets[0]->mask_regs[reg_id];
    fc_mask_reg->update(reg_offset, size, value, is_write);
}


void soc_eu::cl_mask_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write, int reg_id)
{
}


void soc_eu::pr_mask_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write, int reg_id)
{
}


vp::IoReqStatus soc_eu::req(vp::Block *__this, vp::IoReq *req)
{
    soc_eu *_this = (soc_eu *)__this;

    if (_this->regmap.access(req->get_addr(), req->get_size(), req->get_data(), req->get_is_write()))
    {
        req->set_resp_status(vp::IO_RESP_INVALID);
        return vp::IO_REQ_DONE;
    }

    req->set_resp_status(vp::IO_RESP_OK);
    return vp::IO_REQ_DONE;
}




void soc_eu::reset(bool active)
{
    this->regmap.reset(active);
    
    for (auto x: this->targets)
    {
        x->reset(active);
    }
}



soc_eu_target::soc_eu_target(soc_eu *top, std::string name, std::string itf_name, int id)
    : name(name)
{
    this->event_itf.set_sync_meth_muxed(&soc_eu_target::soc_event_sync, id);
    top->new_master_port(itf_name, &this->event_itf, (vp::Block *)this);
}


void soc_eu_target::reset(bool active)
{
    if (!active)
    {
        for (int i=0; i<256; i++)
        {
            this->nb_waiting_events[i] = 0;
        }

        while (!this->fifo.empty())
        {
            this->fifo.pop();
        }
    }
}


void soc_eu_target::soc_event_sync(vp::Block *__this, int ready, int id)
{
    soc_eu_target *_this = (soc_eu_target *)__this;

    while (!_this->fifo.empty())
    {
        int ready;
        _this->event_itf.sync_back(&ready);
        if (!ready)
        {
            break;
        }

        int event = _this->fifo.front();
        _this->fifo.pop();
        _this->nb_waiting_events[event]--;
        _this->event_itf.sync(event);
    }
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new soc_eu(config);
}

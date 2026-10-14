// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include "vp/itf/implem/wire_class.hpp"

#include "archi/timer_unit_regfields.h"
#include "archi/timer_unit_gvsoc.h"

#define TIMER_AREA_WIDTH  10
#define TIMER_AREA_SIZE   (1 << TIMER_AREA_WIDTH)
#define TIMER_AREA_MASK   (TIMER_AREA_SIZE - 1)

using namespace std::placeholders;


class timer : public vp::Component
{

public:
    timer(vp::ComponentConf &config);

    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

private:
    vp::Trace trace;
    vp::IoSlave in{&timer::req};

    vp_regmap_timer_unit     regmap;

    static void ref_clock_sync(vp::Block *__this, bool value);

    void sync();
    void reset(bool active);
    void depack_config(int counter, uint32_t configuration);
    void timer_reset(int counter);

    void cfg_hi_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);
    void cfg_lo_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);
    void cnt_hi_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);
    void cnt_lo_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);
    void cmp_hi_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);
    void cmp_lo_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);
    void start_hi_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);
    void start_lo_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);
    void reset_hi_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);
    void reset_lo_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);

    void handle_configure(int counter);
    void check_state();
    int64_t get_remaining_cycles(bool is_64, int counter);
    void check_state_counter(bool is_64, int counter);
    static void event_handler(vp::Block *__this, vp::ClockEvent *event);
    uint64_t get_compare_value(bool is_64, int counter);
    uint64_t get_value(bool is_64, int counter);
    void set_value(bool is_64, int counter, uint64_t new_value);
    void set_enable(int counter, bool enabled);
    bool check_prescaler(int timer);

    vp::WireMaster<bool> irq_itf[2];
    vp::WireMaster<bool> busy_itf;
    vp::ClockSlave ref_clock_itf;

    vp_timer_unit_cfg_lo   *config[2];
    vp_timer_unit_cnt_lo   *value[2];
    vp_timer_unit_cmp_lo   *compare_value[2];

    std::vector<vp::Register<bool>> irq_state;
    std::vector<vp::Register<bool>> is_enabled;
    std::vector<vp::Register<bool>> irq_enabled;
    std::vector<vp::Register<bool>> iem;
    std::vector<vp::Register<bool>> cmp_clr;
    std::vector<vp::Register<bool>> one_shot;
    std::vector<vp::Register<bool>> prescaler;
    std::vector<vp::Register<bool>> ref_clock;
    std::vector<vp::Register<uint32_t>> prescaler_value;
    std::vector<vp::Register<uint32_t>> prescaler_current_value;

    vp::Register<bool> is_64;

    int64_t sync_time;

    vp::ClockEvent event;
};

timer::timer(vp::ComponentConf &config)
    : vp::Component(config), regmap(*this, "regmap"),
    is_64(*this, "subtimer_0/is_64", 1, true, 0),
    event(this, timer::event_handler)
{
    traces.new_trace("trace", &trace, vp::DEBUG);

    // Reserve before emplace_back so the vector never reallocates: the engine
    // keeps pointers into each vp::Register (trace value_bytes, add_register).
    irq_state.reserve(2);
    is_enabled.reserve(2);
    irq_enabled.reserve(2);
    iem.reserve(2);
    cmp_clr.reserve(2);
    one_shot.reserve(2);
    prescaler.reserve(2);
    ref_clock.reserve(2);
    prescaler_value.reserve(2);
    prescaler_current_value.reserve(2);

    for (int i=0; i<2; i++)
    {
        std::string name = "subtimer_" + std::to_string(i);
        this->irq_state.emplace_back(*this, name + "/irq_state", 1, true, 0);
        this->is_enabled.emplace_back(*this, name + "/enabled", 1, true, 0);
        this->irq_enabled.emplace_back(*this, name + "/irq_enabled", 1, true, 0);
        this->iem.emplace_back(*this, name + "/input_event", 1, true, 0);
        this->cmp_clr.emplace_back(*this, name + "/cmp_clear", 1, true, 0);
        this->one_shot.emplace_back(*this, name + "/one_shot", 1, true, 0);
        this->prescaler.emplace_back(*this, name + "/prescaler", 1, true, 0);
        this->ref_clock.emplace_back(*this, name + "/ref_clock", 1, true, 0);
        this->prescaler_value.emplace_back(*this, name + "/prescaler_value", 32, true, 0);
        this->prescaler_current_value.emplace_back(*this, name + "/prescaler_current_value", 32, true, 0);
    }

    this->regmap.build(this, &this->trace, "regmap");
    this->regmap.cfg_lo.register_callback(std::bind(&timer::cfg_lo_req, this, _1, _2, _3, _4));
    this->regmap.cfg_hi.register_callback(std::bind(&timer::cfg_hi_req, this, _1, _2, _3, _4));
    this->regmap.cnt_lo.register_callback(std::bind(&timer::cnt_lo_req, this, _1, _2, _3, _4));
    this->regmap.cnt_hi.register_callback(std::bind(&timer::cnt_hi_req, this, _1, _2, _3, _4));
    this->regmap.cmp_lo.register_callback(std::bind(&timer::cmp_lo_req, this, _1, _2, _3, _4));
    this->regmap.cmp_hi.register_callback(std::bind(&timer::cmp_hi_req, this, _1, _2, _3, _4));
    this->regmap.reset_lo.register_callback(std::bind(&timer::reset_lo_req, this, _1, _2, _3, _4));
    this->regmap.reset_hi.register_callback(std::bind(&timer::reset_hi_req, this, _1, _2, _3, _4));
    this->regmap.start_lo.register_callback(std::bind(&timer::start_lo_req, this, _1, _2, _3, _4));
    this->regmap.start_hi.register_callback(std::bind(&timer::start_hi_req, this, _1, _2, _3, _4));

    this->config[0] = (vp_timer_unit_cfg_lo *)&this->regmap.cfg_lo;
    this->config[1] = (vp_timer_unit_cfg_lo *)&this->regmap.cfg_hi;

    this->value[0] = (vp_timer_unit_cnt_lo *)&this->regmap.cnt_lo;
    this->value[1] = (vp_timer_unit_cnt_lo *)&this->regmap.cnt_hi;

    this->compare_value[0] = (vp_timer_unit_cmp_lo *)&this->regmap.cmp_lo;
    this->compare_value[1] = (vp_timer_unit_cmp_lo *)&this->regmap.cmp_hi;

    new_slave_port("input", &in);

    new_master_port("irq_itf_0", &irq_itf[0]);
    new_master_port("irq_itf_1", &irq_itf[1]);

    new_master_port("busy", &this->busy_itf);

    ref_clock_itf.set_sync_meth(&timer::ref_clock_sync);
    new_slave_port("ref_clock", &ref_clock_itf);

}

void timer::sync()
{
    int64_t cycles = clock.get_cycles() - sync_time;

    sync_time = clock.get_cycles();

    if (is_64.get() && is_enabled[0].get() && !ref_clock[0].get())
    {
        if (prescaler[0].get())
        {
            cycles /= prescaler_value[0].get() + 1;
        }

        uint64_t value_64 = this->get_value(true, 0);
        this->set_value(true, 0, value_64 + cycles);
    }
    else
    {
        uint32_t cycles0 = cycles;
        uint32_t cycles1 = cycles;
        if (prescaler[0].get())
        {
            cycles0 /= prescaler_value[0].get() + 1;
        }
        if (prescaler[1].get())
        {
            cycles1 /= prescaler_value[1].get() + 1;
        }

        if (is_enabled[0].get() && !ref_clock[0].get())
            value[0]->inc(cycles0);
        if (is_enabled[1].get() && !ref_clock[1].get())
            value[1]->inc(cycles1);
    }
}

int64_t timer::get_remaining_cycles(bool is_64, int counter)
{
    uint64_t cycles;

    if (is_64)
    {
        // No need to check overflow on 64 bits the engine is anyway having 64 bits timestamps
        // Note that engine cycles are signed 64 bits, so we will drop any negative event which
        // can happen when trigger timer is very long. Note an issue since the engine will stop before
        uint64_t compare_64 = (((uint64_t)compare_value[1]->get()) << 32) | compare_value[0]->get();
        uint64_t value_64 = this->get_value(true, 0);
        cycles = compare_64 - value_64;
    }
    else
    {
        cycles = (uint32_t)(compare_value[counter]->get() - value[counter]->get());
        if (cycles == 0)
            cycles = 0x100000000;
    }
    if (prescaler[counter].get())
        return cycles * (prescaler_value[counter].get() + 1);
    else
        return cycles;
}

uint64_t timer::get_compare_value(bool is_64, int counter)
{
    if (is_64)
        return (((uint64_t)compare_value[1]->get()) << 32) | compare_value[0]->get();
    else
        return compare_value[counter]->get();
}

uint64_t timer::get_value(bool is_64, int counter)
{
    if (is_64)
        return (((uint64_t)value[1]->get()) << 32) | value[0]->get();
    else
    {
        return value[counter]->get();
    }
}

void timer::set_value(bool is_64, int counter, uint64_t new_value)
{
    if (is_64)
    {
        this->value[0]->set(new_value & 0xffffffff);
        this->value[1]->set(new_value >> 32);
    }
    else
        value[counter]->set(new_value);
}

void timer::set_enable(int counter, bool enabled)
{
    this->is_enabled[counter].set(enabled);

    if (this->busy_itf.is_bound())
    {
        this->busy_itf.sync(enabled);
    }
}

void timer::check_state_counter(bool is_64, int counter)
{
    irq_state[counter].set(0);
    if (is_enabled[counter].get() && get_compare_value(is_64, counter) == get_value(is_64, counter))
    {
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "Reached compare value (timer: %d)\n", counter);

        if (cmp_clr[counter].get())
        {
            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Clearing timer due to compare value (timer: %d)\n", counter);
            set_value(is_64, counter, 0);
        }

        if (irq_enabled[counter].get())
        {
            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Raising interrupt (timer: %d)\n", counter);
            if (!irq_itf[counter].is_bound())
                this->trace.force_warning("Trying to send timer interrupt while irq port is not connected (irq: %d)\n", counter);
            else
            {
                irq_itf[counter].sync(true);
                irq_state[counter].set(1);
            }
        }

        if (one_shot[counter].get())
        {
            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Reached one-shot end (timer: %d)\n", counter);
            this->set_enable(counter, false);
        }
    }

    if (is_enabled[counter].get() && !ref_clock[counter].get() && (irq_enabled[counter].get() || cmp_clr[counter].get()))
    {
        int64_t cycles = get_remaining_cycles(is_64, counter);

        if (cycles > 0)
        {
            this->trace.msg(vp::Trace::LEVEL_TRACE, "Timer is enabled, reenqueueing event (timer: %d, diffCycles: %ld)\n", counter, cycles);
            this->event.enqueue(cycles);
        }
    }
}

void timer::event_handler(vp::Block *__this, vp::ClockEvent *event)
{
    timer *_this = (timer *)__this;
    _this->sync();
    _this->check_state();
}

void timer::check_state()
{
    if (is_64.get())
    {
        check_state_counter(true, 0);
    }
    else
    {
        check_state_counter(false, 0);
        check_state_counter(false, 1);
    }
}


bool timer::check_prescaler(int timer)
{
    if (this->prescaler[timer].get())
    {
        this->prescaler_current_value[timer].inc(1);
        if (this->prescaler_current_value[timer].get() == this->prescaler_value[timer].get())
        {
            this->prescaler_current_value[timer].set(0);
            return true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        return true;
    }
}

void timer::ref_clock_sync(vp::Block *__this, bool value)
{
    timer *_this = (timer *)__this;
    bool check = false;

    if (value)
    {
        if (_this->ref_clock[0].get() && _this->is_enabled[0].get())
        {
            if (_this->check_prescaler(0))
            {
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "Updating counter due to ref clock raising edge (counter: 0, value: %d)\n", _this->value[0]->get());

                if(_this->is_64.get())
                {
                    _this->set_value(true, 0, _this->get_value(true, 0) + 1);
                }
                else
                {
                    _this->value[0]->inc(1);
                }
                check = true;
            }
        }

        if (_this->ref_clock[1].get() && _this->is_enabled[1].get() && !_this->is_64.get())
        {
            if (_this->check_prescaler(1))
            {
                _this->value[1]->inc(1);
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "Updating counter due to ref clock raising edge (counter: 1, value: %d)\n", _this->value[1]->get());
                check = true;
            }
        }
    }

    if (check)
    {
        _this->sync();
        _this->check_state();
    }
}

void timer::timer_reset(int counter)
{
    this->trace.msg(vp::Trace::LEVEL_INFO, "Resetting timer (timer: %d)\n", counter);
    value[counter]->set(0);
}

void timer::handle_configure(int counter)
{
    depack_config(counter, config[counter]->get());

    this->trace.msg(vp::Trace::LEVEL_INFO, "Modified configuration (timer: %d, enabled: %d, irq: %d, iem: %d, cmp-clr: %d, one-shot: %d, prescaler: %d, prescaler value: 0x%x, is64: %d, ref_clock: %d)\n",
                    counter, is_enabled[counter].get(), irq_enabled[counter].get(), iem[counter].get(), cmp_clr[counter].get(), one_shot[counter].get(), prescaler[counter].get(), prescaler_value[counter].get(), is_64.get(), ref_clock[counter].get());

    if ((config[counter]->get() >> TIMER_UNIT_CFG_LO_RESET_BIT) & 1)
        timer_reset(counter);

    check_state();
}

void timer::depack_config(int counter, uint32_t configuration)
{
    this->set_enable(counter, (configuration >> TIMER_UNIT_CFG_LO_ENABLE_BIT) & 1);
    irq_enabled[counter].set((configuration >> TIMER_UNIT_CFG_LO_IRQEN_BIT) & 1);
    iem[counter].set((configuration >> TIMER_UNIT_CFG_LO_IEM_BIT) & 1);
    cmp_clr[counter].set((configuration >> TIMER_UNIT_CFG_LO_MODE_BIT) & 1);
    one_shot[counter].set((configuration >> TIMER_UNIT_CFG_LO_ONE_S_BIT) & 1);
    prescaler[counter].set((configuration >> TIMER_UNIT_CFG_LO_PEN_BIT) & 1);
    ref_clock[counter].set((configuration >> TIMER_UNIT_CFG_LO_CCFG_BIT) & 1);
    prescaler_value[counter].set((configuration >> TIMER_UNIT_CFG_LO_PVAL_BIT) & ((1 << TIMER_UNIT_CFG_LO_PVAL_WIDTH) - 1));
    if (counter == 0)
        is_64.set((configuration >> TIMER_UNIT_CFG_LO_CASC_BIT) & 1);

    this->prescaler_current_value[counter].set(0);
}



void timer::cfg_hi_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write)
{
    // As the timer model is not updating counters at each cycle, we need to synchronize
    // them anytime we want to use them
    this->sync();

    this->regmap.cfg_hi.update(reg_offset, size, value, is_write);

    if (is_write)
    {
        this->handle_configure(1);
    }
}



void timer::cfg_lo_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write)
{
    // As the timer model is not updating counters at each cycle, we need to synchronize
    // them anytime we want to use them
    this->sync();

    this->regmap.cfg_lo.update(reg_offset, size, value, is_write);

    if (is_write)
    {
        this->handle_configure(0);
    }
}



void timer::cnt_hi_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write)
{
    // As the timer model is not updating counters at each cycle, we need to synchronize
    // them anytime we want to use them
    this->sync();

    this->regmap.cnt_hi.update(reg_offset, size, value, is_write);

    if (is_write)
    {
        this->check_state();
    }
}



void timer::cnt_lo_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write)
{
    // As the timer model is not updating counters at each cycle, we need to synchronize
    // them anytime we want to use them
    this->sync();

    this->regmap.cnt_lo.update(reg_offset, size, value, is_write);

    if (is_write)
    {
        this->check_state();
    }
}



void timer::cmp_hi_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write)
{
    // As the timer model is not updating counters at each cycle, we need to synchronize
    // them anytime we want to use them
    this->sync();

    this->regmap.cmp_hi.update(reg_offset, size, value, is_write);

    if (is_write)
    {
        this->check_state();
    }
}



void timer::cmp_lo_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write)
{
    // As the timer model is not updating counters at each cycle, we need to synchronize
    // them anytime we want to use them
    this->sync();

    this->regmap.cmp_lo.update(reg_offset, size, value, is_write);

    if (is_write)
    {
        this->check_state();
    }
}



void timer::reset_hi_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write)
{
    // As the timer model is not updating counters at each cycle, we need to synchronize
    // them anytime we want to use them
    this->sync();

    if (is_write)
    {
        this->timer_reset(1);
        this->check_state();
    }
}



void timer::reset_lo_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write)
{
    // As the timer model is not updating counters at each cycle, we need to synchronize
    // them anytime we want to use them
    this->sync();

    if (is_write)
    {
        this->timer_reset(0);
        this->check_state();
    }
}



void timer::start_hi_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write)
{
    // As the timer model is not updating counters at each cycle, we need to synchronize
    // them anytime we want to use them
    this->sync();

    if (is_write)
    {
        this->set_enable(1, 1);
        this->check_state();
    }
}




void timer::start_lo_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write)
{
    // As the timer model is not updating counters at each cycle, we need to synchronize
    // them anytime we want to use them
    this->sync();

    if (is_write)
    {
        this->set_enable(0, 1);
        this->check_state();
    }
}



vp::IoReqStatus timer::req(vp::Block *__this, vp::IoReq *req)
{
    timer *_this = (timer *)__this;

    if (_this->regmap.access(req->get_addr() & TIMER_AREA_MASK, req->get_size(), req->get_data(),
        req->get_is_write()))
    {
        req->set_resp_status(vp::IO_RESP_INVALID);
        return vp::IO_REQ_DONE;
    }

    req->set_resp_status(vp::IO_RESP_OK);
    return vp::IO_REQ_DONE;
}

void timer::reset(bool active)
{
    if (active)
    {
    }
    else
    {
        for (int i = 0; i < 2; i++)
        {
            depack_config(i, config[i]->get());
        }

        sync_time = clock.get_cycles();
    }
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new timer(config);
}

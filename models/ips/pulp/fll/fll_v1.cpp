// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// PULP FLL (fll_v1), io_v2 port of `pulp.fll.fll_v1_impl`. Same registers
// (STATUS, CONF1, CONF2, INTEGRATOR) and DCO model. As in the v1 model, the
// FLL control interface is synchronous to the reference clock: an access is
// granted and answered on the next reference clock edge, one access per edge.
// The reset values of the registers come from the config tree.

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/itf/clock.hpp>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <queue>

#include <pulp/fll/archi/fll_v1.h>
#include <ips/pulp/fll/fll_v1/fll_config.hpp>

class Fll : public vp::Component
{
public:
    Fll(vp::ComponentConf &config);

    void reset(bool active) override;

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static void ref_clock_sync(vp::Block *__this, bool value);
    static void ref_clock_set_frequency(vp::Block *__this, int64_t frequency);

    bool status_req(int reg_offset, int size, bool is_write, uint8_t *data);
    bool conf1_req(int reg_offset, int size, bool is_write, uint8_t *data);
    bool conf2_req(int reg_offset, int size, bool is_write, uint8_t *data);
    bool integrator_req(int reg_offset, int size, bool is_write, uint8_t *data);
    void check_state();
    double get_dco_frequency(int dco_input);
    void handle_req();

    FllConfig cfg;

    vp::Trace trace;

    fll_reg_status_t     status_reg;
    fll_reg_conf1_t      conf1_reg;
    fll_reg_conf2_t      conf2_reg;
    fll_reg_integrator_t integrator_reg;

    double dco_freq;

    vp::ClockSlave ref_clock_itf;
    vp::ClockMaster fll_clock_itf;
    vp::IoSlave in{&Fll::req};

    int nb_stability_cycles;
    int locked;
    int dco_input;

    // Accesses waiting for the next reference clock edge
    std::queue<vp::IoReq *> pending;
};

Fll::Fll(vp::ComponentConf &config)
    : vp::Component(config, this->cfg)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->new_slave_port("input", &this->in);

    this->ref_clock_itf.set_sync_meth(&Fll::ref_clock_sync);
    this->ref_clock_itf.set_set_frequency_meth(&Fll::ref_clock_set_frequency);
    this->new_slave_port("ref_clock", &this->ref_clock_itf);

    this->new_master_port("clock_out", &this->fll_clock_itf);
}

void Fll::ref_clock_set_frequency(vp::Block *__this, int64_t frequency)
{
}

void Fll::ref_clock_sync(vp::Block *__this, bool value)
{
    Fll *_this = (Fll *)__this;

    if (value == false)
        return;

    // DCO freq is the number of pulses so we have to divide by 2
    _this->status_reg.actual_mult_factor = _this->dco_freq * 1000000 / 32768 / 2;

    if (_this->conf1_reg.mode == 1)
    {
        int delta = _this->conf1_reg.mult_factor - _this->status_reg.actual_mult_factor;
        int delta_ext = delta << 10;
        int gain = _this->conf2_reg.loop_gain;
        int delta_ext_amp = gain < 10 ? delta_ext >> gain : 0;
        int integrator = (_this->integrator_reg.state_int_part << 10) | _this->integrator_reg.state_fract_part;
        int integrator_output = integrator + delta_ext_amp;

        _this->trace.msg(vp::Trace::LEVEL_TRACE, "Adapting DCO input (factor: %d, actual_factor: %d)\n",
            _this->conf1_reg.mult_factor, _this->status_reg.actual_mult_factor);

        _this->integrator_reg.state_int_part = integrator_output >> 10;
        _this->integrator_reg.state_fract_part = integrator_output;

        _this->dco_input = _this->integrator_reg.state_int_part;
    }

    _this->check_state();

    _this->handle_req();
}

double Fll::get_dco_frequency(int dco_input)
{
    if (dco_input < 219)
        return 0.000001;
    else if (dco_input < 512)
        return 21.897 - 33.1341 - 0.752*dco_input + 0.003674*pow(dco_input, 2);
    else
        return -642.570 + 2.590*dco_input - 0.000445*pow(dco_input, 2);
}

void Fll::check_state()
{
    this->dco_freq = this->get_dco_frequency(this->dco_input);

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Setting DCO frequency (frequency: %f MHz, dco_in: %d)\n",
        this->dco_freq, this->dco_input);

    int frequency = (int)(this->dco_freq / (1 << this->conf1_reg.clock_out_divider) * 1000000);

    if (this->conf1_reg.mode == 1 && this->conf1_reg.output_lock_enable)
    {
        // Locked mode, check if the fll output must be asserted or de-asserted
        int delta = this->conf1_reg.mult_factor - this->status_reg.actual_mult_factor;
        if (delta < 0)
            delta = -delta;

        if (this->locked)
        {
            if (delta > this->conf2_reg.lock_tolerance)
            {
                this->nb_stability_cycles++;
                if (this->nb_stability_cycles == this->conf2_reg.de_assert_cycles)
                {
                    this->trace.msg(vp::Trace::LEVEL_INFO, "Unlocking FLL\n");
                    this->locked = 0;
                    this->nb_stability_cycles = 0;
                }
            }
        }
        else
        {
            if (delta <= this->conf2_reg.lock_tolerance)
            {
                this->nb_stability_cycles++;
                if (this->nb_stability_cycles == this->conf2_reg.assert_cycles)
                {
                    this->trace.msg(vp::Trace::LEVEL_INFO, "Locking FLL\n");
                    this->locked = 1;
                    this->nb_stability_cycles = 0;
                }
            }
        }

        if (!this->locked)
            frequency = 0;
    }

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Setting new frequency (frequency: %d Hz)\n", frequency);
    this->fll_clock_itf.set_frequency(frequency);
}

bool Fll::status_req(int reg_offset, int size, bool is_write, uint8_t *data)
{
    if (is_write)
        return false;

    memcpy(data, &(((uint8_t *)&this->status_reg.raw)[reg_offset]), size);
    return true;
}

bool Fll::conf1_req(int reg_offset, int size, bool is_write, uint8_t *data)
{
    if (!is_write)
    {
        memcpy(data, &(((uint8_t *)&this->conf1_reg.raw)[reg_offset]), size);
    }
    else
    {
        memcpy(&(((uint8_t *)&this->conf1_reg.raw)[reg_offset]), data, size);

        this->trace.msg(vp::Trace::LEVEL_INFO, "Setting configuration 1 register (raw: 0x%x, mode: %d, "
            "lock: %d, div: %d, dco: %d, factor: %d)\n", this->conf1_reg.raw, this->conf1_reg.mode,
            this->conf1_reg.output_lock_enable, this->conf1_reg.clock_out_divider,
            this->conf1_reg.dco_input, this->conf1_reg.mult_factor);

        if (this->conf1_reg.mode == 0)
            this->dco_input = this->conf1_reg.dco_input;

        // Writing to conf1 reg, whatever the mode, is unlocking the fll
        this->locked = 0;

        this->check_state();
    }
    return true;
}

bool Fll::conf2_req(int reg_offset, int size, bool is_write, uint8_t *data)
{
    if (!is_write)
    {
        memcpy(data, &(((uint8_t *)&this->conf2_reg)[reg_offset]), size);
    }
    else
    {
        memcpy(&(((uint8_t *)&this->conf2_reg)[reg_offset]), data, size);

        this->trace.msg(vp::Trace::LEVEL_INFO, "Setting configuration 2 register (raw: 0x%x)\n",
            this->conf2_reg.raw);

        this->check_state();
    }
    return true;
}

bool Fll::integrator_req(int reg_offset, int size, bool is_write, uint8_t *data)
{
    if (!is_write)
    {
        memcpy(data, &(((uint8_t *)&this->integrator_reg.raw)[reg_offset]), size);
    }
    else
    {
        memcpy(&(((uint8_t *)&this->integrator_reg.raw)[reg_offset]), data, size);

        this->trace.msg(vp::Trace::LEVEL_INFO, "Setting integrator register (raw: 0x%x)\n",
            this->integrator_reg.raw);

        // Writing to integrator register is unlocking the FLL only in close loop
        if (this->conf1_reg.mode == 1)
            this->locked = 0;

        this->check_state();
    }
    return true;
}

void Fll::handle_req()
{
    if (this->pending.empty())
        return;

    vp::IoReq *req = this->pending.front();
    this->pending.pop();

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    this->trace.msg(vp::Trace::LEVEL_TRACE, "FLL access (offset: 0x%lx, size: 0x%lx, is_write: %d)\n",
        offset, size, is_write);

    int reg_id = offset / 4;
    int reg_offset = offset % 4;
    bool ok = false;

    if (reg_offset + size <= 4)
    {
        switch (reg_id)
        {
            case FLL_STATUS_OFFSET/4:     ok = this->status_req(reg_offset, size, is_write, data); break;
            case FLL_CONF1_OFFSET/4:      ok = this->conf1_req(reg_offset, size, is_write, data); break;
            case FLL_CONF2_OFFSET/4:      ok = this->conf2_req(reg_offset, size, is_write, data); break;
            case FLL_INTEGRATOR_OFFSET/4: ok = this->integrator_req(reg_offset, size, is_write, data); break;
        }
    }

    if (!ok)
    {
        this->trace.msg(vp::Trace::LEVEL_WARNING, "FLL invalid access (offset: 0x%lx, size: 0x%lx, is_write: %d)\n",
            offset, size, is_write);
    }

    req->set_resp_status(ok ? vp::IO_RESP_OK : vp::IO_RESP_INVALID);
    this->in.resp(req);
}

vp::IoReqStatus Fll::req(vp::Block *__this, vp::IoReq *req)
{
    Fll *_this = (Fll *)__this;

    // Answered on the next reference clock edge
    _this->pending.push(req);
    return vp::IO_REQ_GRANTED;
}

void Fll::reset(bool active)
{
    if (active)
    {
        this->status_reg.raw      = this->cfg.status_reset;
        this->conf1_reg.raw       = this->cfg.conf1_reset;
        this->conf2_reg.raw       = this->cfg.conf2_reset;
        this->integrator_reg.raw  = this->cfg.integrator_reset;
        this->nb_stability_cycles = 0;
        this->locked              = 0;
        this->dco_input           = this->conf1_reg.dco_input;
        while (!this->pending.empty())
            this->pending.pop();
    }
    else
    {
        this->check_state();
    }
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Fll(config);
}

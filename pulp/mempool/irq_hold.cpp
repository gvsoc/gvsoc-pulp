/*
 * Copyright (C) 2025 ETH Zurich and University of Bologna
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
#include <vp/itf/wire.hpp>


/**
 * @brief Rising-edge hold cell for the RedMulE done IRQ.
 *
 * Forwards only the RISING edge of its input as sync(true) and swallows the falling edge, so the
 * RedMulE 1-cycle `done` pulse produces exactly one wakeup on core 0 (via the OR + barrier_sync) and
 * no phantom pending wakeup from the falling edge (which would desync the RedMulE core from the shared
 * barrier when completions spread out). Re-triggers work because the tracked input returns low between
 * jobs, so the next rising edge re-fires.
 */
class IrqHold : public vp::Component
{
public:
    IrqHold(vp::ComponentConf &config);

    void reset(bool active);

private:
    static void sync(vp::Block *__this, bool value);

    vp::Trace trace;

    vp::WireSlave<bool> input_itf;
    vp::WireMaster<bool> output_itf;

    bool last;
};


IrqHold::IrqHold(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->input_itf.set_sync_meth(&IrqHold::sync);
    this->new_slave_port("input", &this->input_itf);

    this->new_master_port("output", &this->output_itf);

    this->last = false;
}


void IrqHold::reset(bool active)
{
    if (active)
    {
        this->last = false;
    }
}


void IrqHold::sync(vp::Block *__this, bool value)
{
    IrqHold *_this = (IrqHold *)__this;

    // Forward only the rising edge (the actual wakeup); swallow the falling edge so it cannot latch a
    // spurious pending wakeup on the core.
    if (value && !_this->last)
    {
        _this->output_itf.sync(true);
    }

    _this->last = value;
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new IrqHold(config);
}

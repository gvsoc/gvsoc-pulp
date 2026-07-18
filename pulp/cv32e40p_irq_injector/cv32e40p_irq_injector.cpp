/*
 * Copyright (C) 2026 Fondazione Chips-it
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
 * Authors: Marco Paci, Fondazione Chips-it (marco.paci@chips.it)
 */

/*
 * CV32E40P interrupt-line injector.
 *
 * Exposes the core interrupt wires to an external gv:: client: the
 * co-simulation bridge binds each line with gv::wire_bind and drives it
 * as the RTL irq_i inputs change. Every line forwards to the matching
 * IrqRiscv slave port, so mip and the wake-up logic follow the same path
 * as a platform interrupt source.
 */

#include <string>

#include <vp/vp.hpp>
#include <vp/itf/wire.hpp>
#include <gv/gvsoc.hpp>

class Cv32e40pIrqInjector : public vp::Component
{
public:
    Cv32e40pIrqInjector(vp::ComponentConf &config);

    void *external_bind(std::string comp_name, std::string itf_name,
                        void *handle) override;

private:
    /* One interrupt line: gv::Wire_binding facade over the master port. */
    class Line : public gv::Wire_binding
    {
    public:
        void update(int value) override
        {
            if (this->itf.is_bound())
            {
                this->itf.sync(value != 0);
            }
        }

        std::string name;
        vp::WireMaster<bool> itf;
    };

    /* msi, mti, mei plus the sixteen fast lines irq[31:16]. */
    static constexpr int NB_LINES = 19;
    Line lines[NB_LINES];
};

Cv32e40pIrqInjector::Cv32e40pIrqInjector(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->lines[0].name = "msi";
    this->lines[1].name = "mti";
    this->lines[2].name = "mei";
    for (int i = 3; i < NB_LINES; i++)
    {
        this->lines[i].name = "external_irq_" + std::to_string(16 + i - 3);
    }

    for (auto &line : this->lines)
    {
        this->new_master_port(line.name, &line.itf);
    }
}

void *Cv32e40pIrqInjector::external_bind(std::string comp_name,
                                         std::string itf_name, void *handle)
{
    (void)handle;

    if (comp_name != this->get_name())
    {
        return NULL;
    }

    for (auto &line : this->lines)
    {
        if (line.name == itf_name)
        {
            return static_cast<gv::Wire_binding *>(&line);
        }
    }

    return NULL;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Cv32e40pIrqInjector(config);
}

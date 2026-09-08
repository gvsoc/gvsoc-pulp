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
 * Authors: Lorenzo Zuolo, Fondazione Chips-IT (lorenzo.zuolo@chips.it)
 */

#include <vp/vp.hpp>
#include "fe/idma_fe_reg32_3d.hpp"
#include "me/idma_me_3d.hpp"
#include "be/idma_be.hpp"
#include "be/idma_be_axi.hpp"
#include "be/idma_be_tcdm.hpp"


/**
 * @brief PULP Open cluster DMA
 *
 * iDMA-based cluster DMA, replacing mchan in the PULP Open cluster. It matches the dmac_wrap that
 * pulp_cluster compiles when built with the idma bender target, and puts together:
 *   - reg32_3d front-end, exposing one register file per core plus the peripheral ports
 *   - 3D middle end, decomposing transfers along up to three dimensions
 *   - AXI and TCDM backend protocols, for the external interconnect and the local TCDM
 *
 * The hardware selects the source and destination backends from the protocol fields of the
 * configuration register. The backend here picks them from the address instead, which gives the
 * same result for the AXI and OBI protocols since the local area is exactly the TCDM. The INIT
 * protocol, which sources zeros and sinks writes, has no address-based equivalent and is not
 * modelled yet.
 */
class PulpOpenDma : public vp::Component
{
public:
    PulpOpenDma(vp::ComponentConf &config);

private:
    IDmaFeReg32_3d fe;
    IDmaMe3d me;
    IDmaBeAxi be_axi_read;
    IDmaBeAxi be_axi_write;
    IDmaBeTcdm be_tcdm_read;
    IDmaBeTcdm be_tcdm_write;
    IDmaBe be;
};



PulpOpenDma::PulpOpenDma(vp::ComponentConf &config)
    : vp::Component(config),
    fe(this, &this->me),
    me(this, &this->fe, &this->be),
    be_axi_read(this, "axi_read", &this->be), be_axi_write(this, "axi_write", &this->be),
    be_tcdm_read(this, "tcdm_read", &this->be), be_tcdm_write(this, "tcdm_write", &this->be),
    be(this, &this->me, &this->be_tcdm_read, &this->be_tcdm_write,
        &this->be_axi_read, &this->be_axi_write)
{
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new PulpOpenDma(config);
}

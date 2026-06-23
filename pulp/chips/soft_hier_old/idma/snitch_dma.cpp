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
 */

#include <vp/vp.hpp>
#include "fe/idma_fe_xdma.hpp"
#include "me/idma_me_2d.hpp"
#include "be/idma_be.hpp"
#include "be/idma_be_axi.hpp"
#include "be/idma_be_tcdm.hpp"



/**
 * @brief Snitch DMA
 *
 * This puts together:
 *   - Xdma front-end to handle xdma custom instructions from snitch core
 *   - 2D middle end to add support for 2D transfers
 *   - AXI and TCDM backend protocols to interact with external AXI interconnect and local
 *   TCDM memory
 */
class SnitchDma : public vp::Component
{
public:
    SnitchDma(vp::ComponentConf &config);

private:
    IDmaFeXdma fe;
    IDmaMe2D me;
    IDmaBeAxi be_axi_read;
    IDmaBeAxi be_axi_write;
    IDmaBeTcdm be_tcdm_read;
    IDmaBeTcdm be_tcdm_write;
    IDmaBe be;
};



SnitchDma::SnitchDma(vp::ComponentConf &config)
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
    return new SnitchDma(config);
}

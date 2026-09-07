/*
 * Copyright (C) 2026 ETH Zurich and University of Bologna
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
 * Authors: Germain Haugou (germain.haugou@gmail.com)
 */

#include <vp/vp.hpp>
#include <ips/pulp/idma_v3/reg_dma_config/reg_dma_v3_config.hpp>
#include "idma.hpp"
#include "fe/idma_fe_reg.hpp"
#include "me/idma_me_nd.hpp"
#include "be/idma_be.hpp"
#include "be/idma_axi_read.hpp"
#include "be/idma_axi_write.hpp"

/**
 * @brief Single-stream register-driven iDMA: one register port, one
 * stream, AXI read and write managers.
 */
class RegDma : public vp::Component
{
public:
    RegDma(vp::ComponentConf &config);

private:
    static IdmaBackendParams backend_params(const RegDmaV3Config &cfg)
    {
        IdmaBackendParams params;
        params.width = cfg.axi_width;
        params.num_ax_in_flight = cfg.num_ax_in_flight;
        params.buffer_depth = cfg.buffer_depth;
        params.meta_fifo_depth = cfg.meta_fifo_depth;
        return params;
    }

    RegDmaV3Config cfg;
    IdmaFeReg fe;
    IdmaMeNd me;
    IdmaBackend be;
    IdmaAxiRead axi_read;
    IdmaAxiWrite axi_write;
};



RegDma::RegDma(vp::ComponentConf &config)
:   vp::Component(config, this->cfg),
    fe(this, 1, 1, 0, this->cfg.launch_bubble),
    me(this, "me", this->cfg.req_fifo_depth, this->cfg.nb_dims, this->fe.stream(0)),
    be(this, "be", backend_params(this->cfg), &this->me),
    axi_read(this, "axi_read", &this->be, this->cfg.axi_width, this->cfg.burst_len,
        this->cfg.num_ax_in_flight),
    axi_write(this, "axi_write", &this->be, this->cfg.axi_width, this->cfg.burst_len,
        this->cfg.num_ax_in_flight, this->cfg.meta_fifo_depth, this->cfg.raw_coupling)
{
    this->be.add_read_manager(&this->axi_read);
    this->be.add_write_manager(&this->axi_write);
    this->me.set_backend(&this->be);
    this->fe.set_stream(0, &this->me, &this->be);
}



extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new RegDma(config);
}

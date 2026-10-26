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

#include <memory>
#include <vp/vp.hpp>
#include <ips/pulp/idma_v3/reg_dma_config/reg_dma_v3_config.hpp>
#include "idma.hpp"
#include "fe/idma_fe_reg.hpp"
#include "me/idma_me_nd.hpp"
#include "be/idma_be.hpp"
#include "be/idma_manager_factory.hpp"

/**
 * @brief Single-stream register-driven iDMA: one register port, one stream,
 * one read and one write manager, each AXI or OBI (the upstream back-ends
 * rw_axi, r_axi_w_obi, r_obi_w_axi and rw_obi).
 */
class RegDma : public vp::Component
{
public:
    RegDma(vp::ComponentConf &config);

private:
    static IdmaBackendParams backend_params(const RegDmaV3Config &cfg)
    {
        IdmaBackendParams params;
        params.width = cfg.data_width;
        params.num_ax_in_flight = cfg.num_ax_in_flight;
        params.buffer_depth = cfg.buffer_depth;
        params.meta_fifo_depth = cfg.meta_fifo_depth;
        params.raw_coupling = cfg.raw_coupling;
        return params;
    }

    RegDmaV3Config cfg;
    IdmaFeReg fe;
    IdmaMeNd me;
    IdmaBackend be;
    std::unique_ptr<IdmaReadManager> read_mgr;
    std::unique_ptr<IdmaWriteManager> write_mgr;
};



RegDma::RegDma(vp::ComponentConf &config)
:   vp::Component(config, this->cfg),
    fe(this, 1, 1, 0, this->cfg.launch_bubble, this->cfg.max_num_streams),
    me(this, "me", this->cfg.req_fifo_depth, this->cfg.nb_dims, this->fe.stream(0)),
    be(this, "be", backend_params(this->cfg), &this->me)
{
    // Only rw_axi has the read/write coupler
    if (this->cfg.raw_coupling
        && (this->cfg.read.protocol != IDMA_PROT_AXI || this->cfg.write.protocol != IDMA_PROT_AXI))
    {
        this->get_trace()->fatal("idma_v3: raw_coupling needs AXI on both sides\n");
    }

    this->read_mgr = idma_new_read_manager(this, &this->be, backend_params(this->cfg),
        this->cfg.read);
    this->write_mgr = idma_new_write_manager(this, &this->be, backend_params(this->cfg),
        this->cfg.write);
    this->be.add_read_manager(this->read_mgr.get());
    this->be.add_write_manager(this->write_mgr.get());

    this->me.set_backend(&this->be);
    this->fe.set_stream(0, &this->me, &this->be);
}



extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new RegDma(config);
}

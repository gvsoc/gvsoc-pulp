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

#include <memory>
#include <vp/vp.hpp>
#include <ips/pulp/idma_v3/axi_obi_dma_config/axi_obi_dma_v3_config.hpp>
#include "idma.hpp"
#include "fe/idma_fe_reg.hpp"
#include "me/idma_me_nd.hpp"
#include "be/idma_be.hpp"
#include "be/idma_axi_read.hpp"
#include "be/idma_axi_write.hpp"
#include "be/idma_obi_port_group.hpp"
#include "be/idma_obi_read.hpp"
#include "be/idma_obi_write.hpp"

/**
 * @brief Single-stream, single-direction AXI/OBI iDMA.
 *
 * One register port and one stream (idma_reg32_3d), the ND mid-end and a
 * back-end with one AXI and one OBI manager: AXI read and OBI write for an
 * AXI to OBI channel (idma_backend_r_axi_w_obi), OBI read and AXI write for
 * an OBI to AXI one (idma_backend_r_obi_w_axi). Only the managers of the
 * configured direction are built, so only their ports exist.
 */
class AxiObiDma : public vp::Component
{
public:
    AxiObiDma(vp::ComponentConf &config);

private:
    static IdmaBackendParams backend_params(const AxiObiDmaV3Config &cfg)
    {
        IdmaBackendParams params;
        params.width = cfg.axi_width;
        params.num_ax_in_flight = cfg.num_ax_in_flight;
        params.buffer_depth = cfg.buffer_depth;
        params.meta_fifo_depth = cfg.meta_fifo_depth;
        return params;
    }

    static uint64_t obi_addr_mask(const AxiObiDmaV3Config &cfg)
    {
        if (cfg.obi_addr_width <= 0 || cfg.obi_addr_width >= 64)
        {
            return ~(uint64_t)0;
        }
        return ((uint64_t)1 << cfg.obi_addr_width) - 1;
    }

    AxiObiDmaV3Config cfg;
    IdmaFeReg fe;
    IdmaMeNd me;
    IdmaBackend be;
    // Managers of the direction, the others stay null
    std::unique_ptr<IdmaAxiRead> axi_read;
    std::unique_ptr<IdmaAxiWrite> axi_write;
    std::unique_ptr<IdmaObiPortGroup> obi_ports;
    std::unique_ptr<IdmaObiRead> obi_read;
    std::unique_ptr<IdmaObiWrite> obi_write;
};



AxiObiDma::AxiObiDma(vp::ComponentConf &config)
:   vp::Component(config, this->cfg),
    fe(this, 1, 1, 0, this->cfg.launch_bubble, this->cfg.multireg_count),
    me(this, "me", this->cfg.req_fifo_depth, this->cfg.nb_dims, this->fe.stream(0)),
    be(this, "be", backend_params(this->cfg), &this->me)
{
    if (this->cfg.obi_ports_per_access * this->cfg.obi_port_width != this->cfg.axi_width)
    {
        this->get_trace()->fatal("idma_v3: the OBI access width (%ld x %ld) must equal "
            "axi_width (%ld)\n", this->cfg.obi_ports_per_access, this->cfg.obi_port_width,
            this->cfg.axi_width);
    }

    if (this->cfg.axi_to_obi)
    {
        this->axi_read = std::make_unique<IdmaAxiRead>(this, "axi_read", &this->be,
            this->cfg.axi_width, this->cfg.burst_len, this->cfg.num_ax_in_flight);
        this->obi_ports = std::make_unique<IdmaObiPortGroup>(this, "obi_write",
            this->cfg.obi_ports_per_access, this->cfg.obi_port_width, obi_addr_mask(this->cfg));
        this->obi_write = std::make_unique<IdmaObiWrite>(this, "obi_write_mgr", &this->be,
            this->obi_ports.get());
        this->be.add_read_manager(this->axi_read.get());
        this->be.add_write_manager(this->obi_write.get());
    }
    else
    {
        this->obi_ports = std::make_unique<IdmaObiPortGroup>(this, "obi_read",
            this->cfg.obi_ports_per_access, this->cfg.obi_port_width, obi_addr_mask(this->cfg));
        this->obi_read = std::make_unique<IdmaObiRead>(this, "obi_read_mgr", &this->be,
            this->obi_ports.get());
        // No read/write coupler on this back-end (idma_backend_r_obi_w_axi)
        this->axi_write = std::make_unique<IdmaAxiWrite>(this, "axi_write", &this->be,
            this->cfg.axi_width, this->cfg.burst_len, this->cfg.num_ax_in_flight,
            this->cfg.meta_fifo_depth, false);
        this->be.add_read_manager(this->obi_read.get());
        this->be.add_write_manager(this->axi_write.get());
    }

    this->me.set_backend(&this->be);
    this->fe.set_stream(0, &this->me, &this->be);
}



extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new AxiObiDma(config);
}

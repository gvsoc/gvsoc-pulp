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
#include <ips/pulp/idma_v3/cluster_dma_config/cluster_dma_v3_config.hpp>
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
 * @brief Two-stream cluster iDMA.
 *
 * Stream 0 copies out of the TCDM (OBI read manager, AXI write manager),
 * stream 1 copies into it (AXI or OBI read manager, OBI write manager). Each
 * stream has its request FIFO, mid-end and back-end; the two OBI read
 * managers share the TCDM read port group (the RTL obi_mux), the write
 * manager owns the write port group.
 */
class ClusterDma : public vp::Component
{
public:
    ClusterDma(vp::ComponentConf &config);

private:
    static IdmaBackendParams backend_params(const ClusterDmaV3Config &cfg)
    {
        IdmaBackendParams params;
        params.width = cfg.axi_width;
        params.num_ax_in_flight = cfg.num_ax_in_flight;
        params.buffer_depth = cfg.buffer_depth;
        params.meta_fifo_depth = cfg.meta_fifo_depth;
        return params;
    }

    static uint64_t obi_addr_mask(const ClusterDmaV3Config &cfg)
    {
        if (cfg.obi_addr_width <= 0 || cfg.obi_addr_width >= 64)
        {
            return ~(uint64_t)0;
        }
        return ((uint64_t)1 << cfg.obi_addr_width) - 1;
    }

    ClusterDmaV3Config cfg;
    IdmaFeReg fe;
    IdmaMeNd me0;
    IdmaMeNd me1;
    IdmaBackend be0;
    IdmaBackend be1;
    IdmaAxiWrite axi_write;
    IdmaAxiRead axi_read;
    IdmaObiPortGroup tcdm_write_ports;
    IdmaObiPortGroup tcdm_read_ports;
    IdmaObiWrite obi_write;
    IdmaObiRead obi_read_s0;
    IdmaObiRead obi_read_s1;
};



ClusterDma::ClusterDma(vp::ComponentConf &config)
:   vp::Component(config, this->cfg),
    fe(this, this->cfg.nb_reg_ports, 2, this->cfg.nb_events, this->cfg.launch_bubble),
    me0(this, "me0", this->cfg.req_fifo_depth, this->cfg.nb_dims, this->fe.stream(0)),
    me1(this, "me1", this->cfg.req_fifo_depth, this->cfg.nb_dims, this->fe.stream(1)),
    be0(this, "be0", backend_params(this->cfg), &this->me0),
    be1(this, "be1", backend_params(this->cfg), &this->me1),
    axi_write(this, "axi_write", &this->be0, this->cfg.axi_width, this->cfg.burst_len,
        this->cfg.num_ax_in_flight, this->cfg.meta_fifo_depth, false),
    axi_read(this, "axi_read", &this->be1, this->cfg.axi_width, this->cfg.burst_len,
        this->cfg.num_ax_in_flight),
    tcdm_write_ports(this, "tcdm_write", this->cfg.obi_ports_per_access,
        this->cfg.obi_port_width, obi_addr_mask(this->cfg)),
    tcdm_read_ports(this, "tcdm_read", this->cfg.obi_ports_per_access,
        this->cfg.obi_port_width, obi_addr_mask(this->cfg)),
    obi_write(this, "obi_write", &this->be1, &this->tcdm_write_ports),
    obi_read_s0(this, "obi_read_s0", &this->be0, &this->tcdm_read_ports),
    obi_read_s1(this, "obi_read_s1", &this->be1, &this->tcdm_read_ports)
{
    if (this->cfg.obi_ports_per_access * this->cfg.obi_port_width != this->cfg.axi_width)
    {
        this->get_trace()->fatal("idma_v3: the OBI access width (%ld x %ld) must equal "
            "axi_width (%ld)\n", this->cfg.obi_ports_per_access, this->cfg.obi_port_width,
            this->cfg.axi_width);
    }

    // Stream 0: OBI -> AXI; stream 1: AXI -> OBI and OBI -> OBI, the two read
    // managers of stream 1 queue in the same read FIFO
    this->be0.add_read_manager(&this->obi_read_s0);
    this->be0.add_write_manager(&this->axi_write);
    this->be1.add_read_manager(&this->axi_read);
    this->be1.add_read_manager(&this->obi_read_s1);
    this->be1.add_write_manager(&this->obi_write);

    this->me0.set_backend(&this->be0);
    this->me1.set_backend(&this->be1);
    this->fe.set_stream(0, &this->me0, &this->be0);
    this->fe.set_stream(1, &this->me1, &this->be1);
}



extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ClusterDma(config);
}

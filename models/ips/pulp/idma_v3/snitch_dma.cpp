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
#include <ips/pulp/idma_v3/snitch_dma_config/snitch_dma_v3_config.hpp>
#include "idma.hpp"
#include "fe/idma_fe_xdma.hpp"
#include "me/idma_me_nd.hpp"
#include "be/idma_be.hpp"
#include "be/idma_axi_read.hpp"
#include "be/idma_axi_write.hpp"
#include <pulp/snitch/snitch_cluster/snitch_perf_events.hpp>

/**
 * @brief Snitch xdma-driven iDMA: the offload front-end, one stream, AXI
 * read and write managers.
 */
class SnitchDma : public vp::Component
{
public:
    SnitchDma(vp::ComponentConf &config);

private:
    static IdmaBackendParams backend_params(const SnitchDmaV3Config &cfg)
    {
        IdmaBackendParams params;
        params.width = cfg.data_width;
        params.num_ax_in_flight = cfg.num_ax_in_flight;
        params.buffer_depth = cfg.buffer_depth;
        params.meta_fifo_depth = cfg.meta_fifo_depth;
        params.raw_coupling = cfg.raw_coupling;
        params.enable_compute = cfg.enable_compute;
        params.transpose_full_duplex = cfg.transpose_full_duplex;
        return params;
    }

    SnitchDmaV3Config cfg;
    IdmaFeXdma fe;
    IdmaMeNd me;
    IdmaBackend be;
    IdmaAxiRead axi_read;
    IdmaAxiWrite axi_write;

    /// Totals of the AXI handshakes, for the cluster performance counters
    static void perf_events_sync(vp::Block *__this, SnitchPerfEvents *events);
    vp::WireSlave<SnitchPerfEvents *> perf_events_itf;
};



SnitchDma::SnitchDma(vp::ComponentConf &config)
:   vp::Component(config, this->cfg),
    fe(this, &this->me, this->cfg.offload_req_latency),
    me(this, "me", this->cfg.req_fifo_depth, this->cfg.nb_dims, &this->fe),
    be(this, "be", backend_params(this->cfg), &this->me),
    axi_read(this, "axi_read", &this->be, backend_params(this->cfg), this->cfg.axi),
    axi_write(this, "axi_write", &this->be, backend_params(this->cfg), this->cfg.axi)
{
    this->be.add_read_manager(&this->axi_read);
    this->be.add_write_manager(&this->axi_write);
    this->me.set_backend(&this->be);
    this->fe.set_backend(&this->be);

    this->perf_events_itf.set_sync_meth(&SnitchDma::perf_events_sync);
    this->new_slave_port("perf_events", &this->perf_events_itf);
}



void SnitchDma::perf_events_sync(vp::Block *__this, SnitchPerfEvents *events)
{
    SnitchDma *_this = (SnitchDma *)__this;
    events->count[SNITCH_PERF_DMA_AR_DONE] = _this->axi_read.perf_ar_done;
    events->count[SNITCH_PERF_DMA_AR_BW] = _this->axi_read.perf_ar_bytes;
    events->count[SNITCH_PERF_DMA_R_DONE] = _this->axi_read.perf_r_done;
    events->count[SNITCH_PERF_DMA_AW_DONE] = _this->axi_write.perf_aw_done;
    events->count[SNITCH_PERF_DMA_AW_BW] = _this->axi_write.perf_aw_bytes;
    events->count[SNITCH_PERF_DMA_W_DONE] = _this->axi_write.perf_w_done;
    events->count[SNITCH_PERF_DMA_W_BW] = _this->axi_write.perf_w_bytes;
    events->count[SNITCH_PERF_DMA_B_DONE] = _this->axi_write.perf_b_done;
}



extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new SnitchDma(config);
}

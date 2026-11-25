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
 * Authors: Marco Bertuletti, ETH Zurich (mbertuletti@iis.ee.ethz.ch)
 */

#include <string>
#include <vector>
#include <vp/vp.hpp>
#include "fe/idma_fe_xdma.hpp"
#include "me/idma_me_2d.hpp"
#include "me/idma_me_split.hpp"
#include "me/idma_me_dist.hpp"
#include "be/idma_be.hpp"
#include "be/idma_be_axi.hpp"
#include "be/idma_be_tcdm.hpp"



/**
 * @brief Distributed iDMA
 *
 * This puts together, as the MemPool iDMA:
 *   - Xdma front-end to handle xdma custom instructions from the core
 *   - 2D middle-end to add support for 2D transfers
 *   - Split middle-end, so that a transfer touches each back-end region at most once
 *   - Distributed middle-ends, one for the cluster and one per group, giving each back-end the
 *     piece of the transfer in its region
 *   - nb_groups x nb_dmas_per_group back-ends, each with its AXI and TCDM protocols
 */
class DistributedDma : public vp::Component
{
public:
    // Shared with the front-end, middle-ends and back-ends
    IdmaConfig cfg;

    DistributedDma(vp::ComponentConf &config);

private:
    IDmaFeXdma *fe;
    IDmaMe2D *me;
    IDmaMeSplit *me_split;
    IDmaMeDist *me_cluster_dist;
    std::vector<IDmaMeDist *> me_group_dist;
    std::vector<IDmaBeAxi *> be_axi_read;
    std::vector<IDmaBeAxi *> be_axi_write;
    std::vector<IDmaBeTcdm *> be_tcdm_read;
    std::vector<IDmaBeTcdm *> be_tcdm_write;
    std::vector<IDmaBe *> be;
};



DistributedDma::DistributedDma(vp::ComponentConf &config)
    : vp::Component(config, this->cfg)
{
    int nb_groups = this->cfg.nb_groups;
    int nb_dmas_per_group = this->cfg.nb_dmas_per_group;
    int nb_be = nb_groups * nb_dmas_per_group;
    uint64_t be_width = this->cfg.be_width;

    // The stages are created from the front-end on, each next stage is set once it exists
    this->fe = new IDmaFeXdma(this, nullptr);

    this->me = new IDmaMe2D(this, this->cfg, this->fe, nullptr);
    this->fe->set_me(this->me);

    this->me_split = new IDmaMeSplit(this, this->cfg, "me_split", this->me, nullptr,
        nb_be * be_width);
    this->me->set_be(this->me_split);

    this->me_cluster_dist = new IDmaMeDist(this, this->cfg, "me_cluster_dist", this->me_split, {},
        nb_dmas_per_group * be_width);
    this->me_split->set_be(this->me_cluster_dist);

    this->me_group_dist.resize(nb_groups);
    std::vector<IdmaTransferConsumer *> me_group_itfs;
    for (int i = 0; i < nb_groups; i++)
    {
        this->me_group_dist[i] = new IDmaMeDist(this, this->cfg, "me_group_dist_" + std::to_string(i),
            this->me_cluster_dist, {}, be_width);
        me_group_itfs.push_back(this->me_group_dist[i]);
    }
    this->me_cluster_dist->set_be(me_group_itfs);

    this->be_axi_read.resize(nb_be);
    this->be_axi_write.resize(nb_be);
    this->be_tcdm_read.resize(nb_be);
    this->be_tcdm_write.resize(nb_be);
    this->be.resize(nb_be);
    for (int i = 0; i < nb_groups; i++)
    {
        std::vector<IdmaTransferConsumer *> be_itfs;
        for (int j = 0; j < nb_dmas_per_group; j++)
        {
            int idx = i * nb_dmas_per_group + j;
            std::string suffix = "_" + std::to_string(i) + "_" + std::to_string(j);
            // As the MemPool DMA: wait for the grant after a denied read burst, and push the read
            // data in issue order, since the bursts of the back-ends may complete out of order
            this->be_axi_read[idx] = new IDmaBeAxi(this, this->cfg, "axi_read" + suffix, nullptr,
                true, true);
            this->be_axi_write[idx] = new IDmaBeAxi(this, this->cfg, "axi_write" + suffix, nullptr,
                true, true);
            this->be_tcdm_read[idx] = new IDmaBeTcdm(this, this->cfg, "tcdm_read" + suffix, nullptr);
            this->be_tcdm_write[idx] = new IDmaBeTcdm(this, this->cfg, "tcdm_write" + suffix, nullptr);
            this->be[idx] = new IDmaBe(this, this->cfg, this->me_group_dist[i],
                this->be_tcdm_read[idx], this->be_tcdm_write[idx],
                this->be_axi_read[idx], this->be_axi_write[idx], "be" + suffix);

            this->be_axi_read[idx]->set_be(this->be[idx]);
            this->be_axi_write[idx]->set_be(this->be[idx]);
            this->be_tcdm_read[idx]->set_be(this->be[idx]);
            this->be_tcdm_write[idx]->set_be(this->be[idx]);

            be_itfs.push_back(this->be[idx]);
        }
        this->me_group_dist[i]->set_be(be_itfs);
    }
}



extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new DistributedDma(config);
}

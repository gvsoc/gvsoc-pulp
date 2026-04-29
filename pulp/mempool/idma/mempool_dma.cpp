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
#include "me/idma_me_split.hpp"
#include "me/idma_me_dist.hpp"
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
class MemPoolDma : public vp::Component
{
public:
    MemPoolDma(vp::ComponentConf &config);

private:
    IDmaFeXdma *fe;
    IDmaMe2D *me;
    IDmaMeSplit *me_split;
    IDmaMeDist * me_cluster_dist;
    std::vector<IDmaMeDist *> me_group_dist;
    std::vector<IDmaBeAxi *> be_axi_read;
    std::vector<IDmaBeAxi *> be_axi_write;
    std::vector<IDmaBeTcdm *> be_tcdm_read;
    std::vector<IDmaBeTcdm *> be_tcdm_write;
    std::vector<IDmaBe *> be;
};



MemPoolDma::MemPoolDma(vp::ComponentConf &config)
    : vp::Component(config)
{
    int nb_groups = get_js_config()->get_int("nb_groups");
    int nb_dmas_per_group = get_js_config()->get_int("nb_dmas_per_group");
    int nb_be = nb_groups * nb_dmas_per_group;
    int be_width = get_js_config()->get_int("be_width");

    this->fe = new IDmaFeXdma(this, nullptr);

    this->me = new IDmaMe2D(this, this->fe, nullptr);
    this->fe->set_me(this->me);

    this->me_split = new IDmaMeSplit(this, this->me, nullptr, nb_be * be_width);
    this->me->set_be(this->me_split);

    this->me_cluster_dist = new IDmaMeDist(this, "me_cluster_dist", this->me_split, {}, nb_dmas_per_group * be_width);
    this->me_split->set_be(this->me_cluster_dist);

    this->me_group_dist.resize(nb_groups);
    for (int i = 0; i < nb_groups; i++)
    {
        this->me_group_dist[i] = new IDmaMeDist(this, "me_group_dist_" + std::to_string(i), this->me_cluster_dist, {}, be_width);
    }
    std::vector<IdmaTransferConsumer *> me_dist_itfs;
    for (auto *me_group_dist: this->me_group_dist)
    {
        me_dist_itfs.push_back(me_group_dist);
    }
    this->me_cluster_dist->set_be(me_dist_itfs);

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
            this->be_axi_read[idx] = new IDmaBeAxi(this, "axi_read_" + std::to_string(i) + "_" + std::to_string(j), nullptr);
            this->be_axi_write[idx] = new IDmaBeAxi(this, "axi_write_" + std::to_string(i) + "_" + std::to_string(j), nullptr);
            this->be_tcdm_read[idx] = new IDmaBeTcdm(this, "tcdm_read_" + std::to_string(i) + "_" + std::to_string(j), nullptr);
            this->be_tcdm_write[idx] = new IDmaBeTcdm(this, "tcdm_write_" + std::to_string(i) + "_" + std::to_string(j), nullptr);
            this->be[idx] = new IDmaBe(this, "be_" + std::to_string(i) + "_" + std::to_string(j), this->me_group_dist[i], this->be_tcdm_read[idx], this->be_tcdm_write[idx], this->be_axi_read[idx], this->be_axi_write[idx]);

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
    return new MemPoolDma(config);
}

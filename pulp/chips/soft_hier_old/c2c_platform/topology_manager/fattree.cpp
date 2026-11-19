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
 * Authors: Chi Zhang, ETH Zurich (chizhang@iis.ee.ethz.ch)
 */

#include <vp/vp.hpp>
#include <pulp/chips/soft_hier_old/c2c_platform/topology_manager/fattree/fat_tree_config.hpp>
#include <vp/itf/io.hpp>
#include <queue>
#include <cmath>  // for pow()
#include "../c2c_platform.hpp"

class FatTree : public vp::Component
{
    FatTreeConfig cfg;

public:
    FatTree(vp::ComponentConf &config);

private:
    void reset(bool active);
    static vp::IoReqStatus      req(vp::Block *__this, vp::IoReq *req);
    vp::Trace                   trace;
    vp::IoSlave                 input_itf;
    int                         radix;
    int                         level;
    int                         half_radix;
    int                         endpoints;
    int                         num_top_router;
    int                         num_mid_router;
    int                         pos_idx;
    int                         pos_lvl;
};

FatTree::FatTree(vp::ComponentConf &config)
    : vp::Component(config, this->cfg)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->input_itf.set_req_meth(&FatTree::req);
    this->new_slave_port("in", &this->input_itf);

    this->radix             = this->cfg.radix;
    this->level             = this->cfg.level;
    this->half_radix        = this->cfg.half_radix;
    this->endpoints         = this->cfg.endpoints;
    this->num_top_router    = this->cfg.num_top_router;
    this->num_mid_router    = this->cfg.num_mid_router;
    this->pos_idx           = this->cfg.pos_idx;
    this->pos_lvl           = this->cfg.pos_lvl;
}

void FatTree::reset(bool active)
{
    if (active)
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Reset done\n");
    }
}

vp::IoReqStatus FatTree::req(vp::Block *__this, vp::IoReq *req)
{
    FatTree *_this = (FatTree *)__this;

    int REQ_SOUR_ID     = *(int *)req->arg_get(C2CPlatform::REQ_SOUR_ID);
    int REQ_DEST_ID     = *(int *)req->arg_get(C2CPlatform::REQ_DEST_ID);
    int REQ_IS_ACK      = *(int *)req->arg_get(C2CPlatform::REQ_IS_ACK);
    int REQ_IS_LAST     = *(int *)req->arg_get(C2CPlatform::REQ_IS_LAST);
    int REQ_SIZE        = *(int *)req->arg_get(C2CPlatform::REQ_SIZE);
    int REQ_WRITE       = *(int *)req->arg_get(C2CPlatform::REQ_WRITE);
    int REQ_PORT_ID     = *(int *)req->arg_get(C2CPlatform::REQ_PORT_ID);

    int in_port         = REQ_PORT_ID;
    int out_port        = 0;

    if(_this->pos_lvl == (_this->level - 1)){
        // At top router
        if(_this->level == 1){
            out_port = REQ_DEST_ID;
        } else {
            int down_level_group = static_cast<int>(std::pow(_this->half_radix, _this->pos_lvl - 1));
            out_port = (REQ_DEST_ID / _this->half_radix) / down_level_group;
        }
    } else {
        int this_level_group = static_cast<int>(std::pow(_this->half_radix, _this->pos_lvl));
        int port_index = in_port % _this->half_radix;
        int group_id_of_router = _this->pos_idx / this_level_group;
        int group_id_of_dest   = (REQ_DEST_ID / _this->half_radix) / this_level_group;
        if(group_id_of_router != group_id_of_dest){
            // Go Up
            out_port = port_index + _this->half_radix;
        } else {
            // Go Down
            if(_this->pos_lvl == 0){
                // At edge router
                out_port = REQ_DEST_ID % _this->half_radix;
            } else {
                // At mid router
                int down_level_group = static_cast<int>(std::pow(_this->half_radix, _this->pos_lvl - 1));
                out_port = ((REQ_DEST_ID / _this->half_radix) / down_level_group) % _this->half_radix;
            }
        }
    }

    *req->arg_get(C2CPlatform::REQ_PORT_ID) = (void *)out_port;

    return vp::IO_REQ_OK;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new FatTree(config);
}


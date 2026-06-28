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

#include <vp/vp.hpp>
#include <vp/itf/wire.hpp>
#include <cpu/iss/include/offload.hpp>
#include <vector>


class SoftHierOldOffloadDecoder : public vp::Component
{
public:
    SoftHierOldOffloadDecoder(vp::ComponentConf &config);

private:
    static void offload_sync(vp::Block *__this, IssOffloadInsn<uint32_t> *insn, int core_id);
    static void dma_grant_sync(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result);
    static void redmule_grant_sync(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result);

    int nb_cores = 1;
    int current_core = 0;
    int pending_dma_core = 0;
    int pending_redmule_core = 0;

    std::vector<vp::WireSlave<IssOffloadInsn<uint32_t> *>> offload_itf;
    std::vector<vp::WireMaster<IssOffloadInsnGrant<uint32_t> *>> offload_grant_itf;

    vp::WireMaster<IssOffloadInsn<uint32_t> *> dma_offload_itf;
    vp::WireSlave<IssOffloadInsnGrant<uint32_t> *> dma_offload_grant_itf;

    vp::WireMaster<IssOffloadInsn<uint32_t> *> redmule_offload_itf;
    vp::WireSlave<IssOffloadInsnGrant<uint32_t> *> redmule_offload_grant_itf;

    vp::Trace trace;
};


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new SoftHierOldOffloadDecoder(config);
}


SoftHierOldOffloadDecoder::SoftHierOldOffloadDecoder(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->nb_cores = this->get_js_config()->get_int("nb_cores");
    this->offload_itf.resize(this->nb_cores);
    this->offload_grant_itf.resize(this->nb_cores);

    for (int core_id = 0; core_id < this->nb_cores; core_id++)
    {
        this->offload_itf[core_id].set_sync_meth_muxed(&SoftHierOldOffloadDecoder::offload_sync, core_id);
        this->new_slave_port("offload_" + std::to_string(core_id), &this->offload_itf[core_id], this);
        this->new_master_port("offload_grant_" + std::to_string(core_id), &this->offload_grant_itf[core_id], this);
    }

    this->new_master_port("dma_offload", &this->dma_offload_itf, this);
    this->dma_offload_grant_itf.set_sync_meth(&SoftHierOldOffloadDecoder::dma_grant_sync);
    this->new_slave_port("dma_offload_grant", &this->dma_offload_grant_itf, this);

    this->new_master_port("redmule_offload", &this->redmule_offload_itf, this);
    this->redmule_offload_grant_itf.set_sync_meth(&SoftHierOldOffloadDecoder::redmule_grant_sync);
    this->new_slave_port("redmule_offload_grant", &this->redmule_offload_grant_itf, this);
}


void SoftHierOldOffloadDecoder::dma_grant_sync(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result)
{
    SoftHierOldOffloadDecoder *_this = (SoftHierOldOffloadDecoder *)__this;
    _this->offload_grant_itf[_this->pending_dma_core].sync(result);
}


void SoftHierOldOffloadDecoder::redmule_grant_sync(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result)
{
    SoftHierOldOffloadDecoder *_this = (SoftHierOldOffloadDecoder *)__this;
    _this->offload_grant_itf[_this->pending_redmule_core].sync(result);
}


void SoftHierOldOffloadDecoder::offload_sync(vp::Block *__this, IssOffloadInsn<uint32_t> *insn, int core_id)
{
    SoftHierOldOffloadDecoder *_this = (SoftHierOldOffloadDecoder *)__this;
    uint32_t opcode = insn->opcode & 0x7f;
    _this->current_core = core_id;

    switch (opcode)
    {
        case 0b0101011:
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Dispatch old SoftHier iDMA opcode\n");
            _this->pending_dma_core = core_id;
            _this->dma_offload_itf.sync(insn);
            break;

        case 0b0001010:
        case 0b0101010:
            _this->trace.msg(vp::Trace::LEVEL_TRACE, "Dispatch old SoftHier RedMule opcode\n");
            _this->pending_redmule_core = core_id;
            _this->redmule_offload_itf.sync(insn);
            break;

        default:
            _this->trace.fatal("Unexpected old SoftHier offload opcode 0x%x\n", opcode);
            break;
    }
}

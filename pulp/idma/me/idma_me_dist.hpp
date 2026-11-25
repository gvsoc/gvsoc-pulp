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

#pragma once

#include <queue>
#include <set>
#include <string>
#include <vector>
#include <vp/vp.hpp>
#include "../idma.hpp"



/**
 * @brief Distributed middle-end
 *
 * Model of idma_distributed_midend: the local address space is seen as nb_be consecutive
 * regions of region_size bytes, repeating every nb_be * region_size bytes, and next stage i
 * handles region i. Each transfer is cut into one piece per region it touches, given to the
 * matching next stage; a next stage whose region is not touched gets nothing. The regions are
 * taken on the side of the transfer in the local area (loc_base / loc_size), the source if it is
 * there, the destination otherwise.
 *
 * A transfer must not touch the same region twice, i.e. it must fit in an aligned window of
 * nb_be * region_size bytes: a split middle-end (IDmaMeSplit) placed in front ensures it.
 *
 * The next stages are independent, so a transfer can complete before an older one. Transfers
 * are still acknowledged to the previous stage in order.
 *
 * Only plain copies are supported: a broadcast or a reduction (collective_op != 0) is fatal.
 */
class IDmaMeDist : public vp::Block, public IdmaTransferConsumer, public IdmaTransferProducer
{
public:
    /**
     * @brief Construct a new distributed middle-end
     *
     * @param idma The top iDMA block.
     * @param cfg The configuration of the top iDMA block.
     * @param name The name of this block.
     * @param fe The previous stage, can be set later with set_fe.
     * @param be The next stages, one per region, can be set later with set_be.
     * @param region_size Size in bytes of the region handled by each next stage.
     */
    IDmaMeDist(vp::Component *idma, const IdmaConfig &cfg, std::string name,
        IdmaTransferProducer *fe, std::vector<IdmaTransferConsumer *> be, uint64_t region_size);

    void reset(bool active) override;

    bool can_accept_transfer() override;
    void enqueue_transfer(IdmaTransfer *transfer) override;
    void update() override;
    void ack_transfer(IdmaTransfer *transfer) override;
    void set_fe(IdmaTransferProducer *fe) { this->fe = fe; }
    void set_be(std::vector<IdmaTransferConsumer *> be);

private:
    // Retire, in order, the transfers at the head of pending_transfers which are done
    void retire_transfers();

    // Pointer to the previous stage
    IdmaTransferProducer *fe;
    // Pointers to the next stages, one per region
    std::vector<IdmaTransferConsumer *> be;
    // Trace for this block, messages will be displayed with this block's name
    vp::Trace trace;
    // Number of next stages
    int nb_be;
    // Size in bytes of the region handled by each next stage
    uint64_t region_size;
    // Top parameter giving the maximum number of transfers which can be enqueued per next stage
    uint64_t transfer_queue_size;
    // Local area, used to decide on which side of the transfer the regions are taken
    uint64_t loc_base;
    uint64_t loc_size;
    // Pieces of the enqueued transfers, per next stage, in order. A next stage which gets
    // nothing from a transfer still has an empty piece, so that the head of every queue belongs
    // to the oldest transfer
    std::vector<std::queue<IdmaTransfer *>> transfer_queue;
    // Enqueued transfers, in order
    std::queue<IdmaTransfer *> pending_transfers;
    // Transfers whose pieces are all done, but which wait for an older transfer to be retired
    std::set<IdmaTransfer *> done_transfers;
};

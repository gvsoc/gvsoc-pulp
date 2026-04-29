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

#pragma once

#include <vp/vp.hpp>
#include "../idma.hpp"



/**
 * @brief 2D middle-end
 *
 * This front-end can be used to get support for 2D transfers
 */
class IDmaMeDist : public vp::Block, public IdmaTransferConsumer, public IdmaTransferProducer
{
public:
    /**
     * @brief Construct a new IDmaFeXdma frontend
     *
     * @param idma The top iDMA block.
     * @param fe The front end.
     * @param be The back end.
     */
    IDmaMeDist(vp::Component *idma, std::string name, IdmaTransferProducer *fe, std::vector<IdmaTransferConsumer *> be, int be_region_size);

    void reset(bool active) override;

    bool can_accept_transfer() override;
    void enqueue_transfer(IdmaTransfer *transfer) override;
    void update() override;
    void ack_transfer(IdmaTransfer *transfer) override;
    void set_fe(IdmaTransferProducer *fe) { this->fe = fe; }
    void set_be(std::vector<IdmaTransferConsumer *> be);


private:
    static unsigned int clog2(int value);

    // Pointer to frontend
    IdmaTransferProducer *fe;
    // Pointer to backend
    std::vector<IdmaTransferConsumer *> be;
    // Trace for this block, messages will be displayed with this block's name
    vp::Trace trace;
    int nb_be;
    int be_region_size;
    // Top parameter giving the maximum number of transfers which can be enqueued
    int transfer_queue_size;
    int dma_region_start;
    int dma_region_end;
    // Queue of enqueued transfers. The number of transfers which can be enqueued is defined by
    // transfer_queue_size
    std::vector<std::queue<IdmaTransfer *>> transfer_queue;
};

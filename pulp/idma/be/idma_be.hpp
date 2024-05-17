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
#include "vp/itf/io.hpp"

class IdmaTransferProducer;



/**
 * @brief Interface for backend protocols
 *
 * This is the interface that any backend protocol like AXI or TCDM must implement so that
 * it can interact with the backend.
 */
class IdmaBeConsumer
{
public:
    /**
     * @brief Update notification
     *
     * This notifies the backend protocol that something has changed and that his FSM should be
     * scheduled to check if any action should be taken.
     * This is for example called when the backend becomes ready to accept incoming data.
     */
    virtual void update() = 0;

    /**
     * @brief Ask if backend protocol is ready to accept a burst
     *
     * This is called by the backend to know if a burst can be enqueued to the backend protocol.
     * This should be used to model a FIFO of outstanding bursts.
     *
     * @return True if the backend protocol is ready to accept at least one burst
     */
    virtual bool can_accept_burst() = 0;

    /**
     * @brief Ask if backend protocol is ready to accept data
     *
     * This is called by the backend to know if data to be written for the current burst can be
     * pushed.
     *
     * @return True if the backend protocol is ready to accept data
     */
    virtual bool can_accept_data() = 0;

    /**
     * @brief Enqueue a read burst
     *
     * This is called only when the backend protocol is ready to accept a burst.
     * The burst should be immediatly processed or enqueued in a FIFO.
     * Once a burst is being processed, it should call the method write_data on the backend
     * to push a chunk of data which has been read.
     *
     * @param base Base address of the burst
     * @param size Size of the burst
     */
    virtual void read_burst(uint64_t base, uint64_t size) = 0;

    /**
     * @brief Acknowledge written data
     *
     * This is called by the destination backend to the source backend to notify
     * the fact that the data pushed through the method write_data has been written and
     * can be freed.
     *
     * @param data Pointer to the data being acknowledged
     */
    virtual void write_data_ack(uint8_t *data) = 0;

    /**
     * @brief Enqueue a write burst
     *
     * This is called only when the backend protocol is ready to accept a burst.
     * The burst should be enqueued into a FIFO. Several bursts can be enqueued.
     * Calls to write_data will then follow to write chunks of data for the active burst
     * which is the first one.
     * As soon as all the data has been written, the current burst should be released and the
     * next one should be marked as the active one.
     *
     * @param base Base address of the burst
     * @param size Size of the burst
     */
    virtual void write_burst(uint64_t base, uint64_t size) = 0;

    /**
     * @brief Write data
     *
     * This is called when a chunk of data is ready to be written for the active burst.
     * Any call to this method must be acknowleged by calling the ack_data method on the backend.
     * Several calls to this method can be done before the first data is acknowleged, to proper
     * handle the router latency.
     * The provided data pointer can be kept until the data has been acknowledged.
     *
     * @param data Pointer to the data to be written
     * @param size Size of the burst
     */
    virtual void write_data(uint8_t *data, uint64_t size) = 0;

    /**
     * @brief Get the legal burst size
     *
     * Once a transfer is processed, the backend cuts the transfer into bursts whose size
     * is compatible with the 2 backend protocols involved in the transfer.
     * To do so, it will call this method on the 2 backend protocols to get a compatible burst size
     * in case a backend has some limitations like maximum size or page crossing.
     *
     * @param base Base address of the burst
     * @param size Size of the burst
     *
     * @return The legalized burst size
     */
    virtual uint64_t get_burst_size(uint64_t base, uint64_t size) = 0;
};



/**
 * @brief Backend interface
 *
 * This is the interface implemented by the backend that each backend protocol can use to
 * interact with it.
 */
class IdmaBeProducer
{
public:
    /**
     * @brief Update notification
     *
     * This notifies the backend that something has changed and that his FSM should be
     * scheduled to check if any action should be taken.
     * This can be called for example when a backend protocol becomes ready to accept incoming data.
     */
    virtual void update() = 0;

    /**
     * @brief Ask if backend if ready to accept data to be written
     *
     * A backend protocol must push data to be written to backend only if the backend is ready.
     * This function can be called to know if it is ready.
     * Only one chunk of data can then be written and this function must be called again.
     */
    virtual bool is_ready_to_accept_data() = 0;

    /**
     * @brief Write data to destination backend protocol
     *
     * The data should be a chunk of the active burst. The method is_ready_to_accept_data must
     * be called first to verify that the backend is ready to accept data.
     *
     * @param data Pointer to the data to be written
     * @param size Size of the burst
     */
    virtual void write_data(uint8_t *data, uint64_t size) = 0;

    /**
     * @brief Acknowledge write data
     *
     * The data received through the method write_data on backend protocol side must be acknowledge
     * as soon as it is written to the destination pointer and the data pointer can be released
     * by the source backend protocol.
     */
    virtual void ack_data(uint8_t *data) = 0;
};




/**
 * @brief Backend
 *
 * The backend takes care of moving data from source and destination.
 * It is connected to a local backend and a remote backend.
 * The backends are selecting based on the transfer source and destination addresses
 * which are compared to the local area given to this backend.
 */
class IDmaBe : public vp::Block, public IdmaTransferConsumer, public IdmaBeProducer
{
public:
    /**
     * @brief Construct a new backend
     *
     * @param idma The top iDMA block.
     * @param me The middle-end
     * @param loc_be The local backend, selected when an address falls into the local range.
     * @param ext_be The external backend, selected when an address does not fall into the local
     *  range.
     */
    IDmaBe(vp::Component *idma, IdmaTransferProducer *me, IdmaBeConsumer *loc_be_read,
        IdmaBeConsumer *loc_be_write, IdmaBeConsumer *ext_be_read, IdmaBeConsumer *ext_be_write);

    void reset(bool active);

    bool can_accept_transfer() override;
    void enqueue_transfer(IdmaTransfer *transfer) override;
    void update() override;
    bool is_ready_to_accept_data() override;
    void write_data(uint8_t *data, uint64_t size) override;
    void ack_data(uint8_t *data) override;

private:
    // FSM handler, called to check if any action should be taken after something was updated
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    // Returne backend protocol corresponding to the specified range
    IdmaBeConsumer *get_be_consumer(uint64_t base, uint64_t size, bool is_read);
    // Pointer to middle-end, used to interact with it
    IdmaTransferProducer *me;
    // Trace for this block, messages will be displayed with this block's name
    vp::Trace trace;
    // Block FSM event, used to trigger all checks after something has been updated
    vp::ClockEvent fsm_event;
    // Current transfer being processed. This transfer is kept active until all bursts
    // have been delegated to backend protocols.
    IdmaTransfer *current_transfer;
    // Current source address of the current transfer. This is increased everytime a burst is
    // delegated
    uint64_t current_transfer_src;
    // Current destination address of the current transfer. This is increased everytime a burst is
    // delegated
    uint64_t current_transfer_dst;
    // Current size of the current transfer. This is decreased everytime a burst is delegated
    uint64_t current_transfer_size;
    // Source backend protocol of the current transfer
    IdmaBeConsumer *current_transfer_src_be;
    // Destination backend protocol of the current transfer
    IdmaBeConsumer *current_transfer_dst_be;
    // Queue of pending transfers, whose bursts have already been sent. They need to be kept
    // since we need transfer information when bursts are back from memory
    std::queue<IdmaTransfer *> transfer_queue;
    // Backend for local area
    IdmaBeConsumer *loc_be_read;
    IdmaBeConsumer *loc_be_write;
    // Backend for external area
    IdmaBeConsumer *ext_be_read;
    IdmaBeConsumer *ext_be_write;
    // Base address of the local area
    uint64_t loc_base;
    // Size of the local area
    uint64_t loc_size;
};

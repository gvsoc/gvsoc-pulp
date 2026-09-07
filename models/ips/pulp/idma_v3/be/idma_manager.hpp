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

#pragma once

#include <cstdint>
#include "../idma.hpp"

/**
 * @file idma_manager.hpp
 * @brief The protocol manager interfaces of the back-end.
 *
 * A read manager is the RTL idma_<protocol>_read module together with its
 * meta channel (AR): it owns the protocol rules the legalizer asks for
 * (page size, bursting), issues the address phase of every split and feeds
 * the response beats into the back-end's byte-lane buffer. A write manager
 * (idma_<protocol>_write) issues the address phase and drains the buffer
 * into data beats, then reports the response (B) that completes the burst.
 *
 * The managers never talk to each other; the back-end (IdmaBackend) owns the
 * buffer and the ordering FIFOs and offers them the services they need.
 */
class IdmaReadManager
{
public:
    virtual ~IdmaReadManager() {}

    /// Protocol served (IdmaProtocol).
    virtual int protocol() = 0;

    /// True when the protocol moves one bus word per split (OBI, INIT):
    /// the legalizer then runs decoupled and the page is the bus width.
    virtual bool not_bursting() = 0;

    /// Bytes from @p addr to the next boundary a burst may not cross.
    virtual uint32_t bytes_to_page_boundary(uint64_t addr) = 0;

    /// True when the address channel can take one more split this cycle.
    virtual bool ar_ready() = 0;

    /// Issue the address phase of a split (same cycle as the legalizer emits
    /// it). Returns a token identifying the burst, echoed back with its
    /// response beats so the back-end can check they arrive in order.
    virtual void *issue_ar(const IdmaSplit &split) = 0;

    /// Called by the back-end tick after this cycle's buffer pops: a response
    /// beat held back because the buffer was full may be offered again.
    virtual void retry_held_beat() = 0;

    /// True while a split is issued or a response is outstanding.
    virtual bool busy() = 0;
};

class IdmaWriteManager
{
public:
    virtual ~IdmaWriteManager() {}

    virtual int protocol() = 0;
    virtual bool not_bursting() = 0;
    virtual uint32_t bytes_to_page_boundary(uint64_t addr) = 0;

    /// True when the address channel can take one more split this cycle.
    virtual bool aw_ready() = 0;

    /// Issue the address phase of a split.
    virtual void issue_aw(const IdmaSplit &split) = 0;

    /// Called once per back-end tick: emit at most one data beat if the
    /// buffer holds its bytes. Returns true when a beat was emitted.
    virtual bool tick(int64_t now) = 0;

    /// The first response beat of a read burst arrived (RAW coupler credit).
    virtual void on_read_first_beat() {}

    /// True while a split is issued or a response is outstanding.
    virtual bool busy() = 0;
};

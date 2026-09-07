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

#include <deque>
#include <vector>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include "../idma.hpp"



/**
 * @brief User of an OBI port group
 */
class IdmaObiClient
{
public:
    /**
     * @brief Tell if the client has a word to issue
     */
    virtual bool obi_has_word() = 0;

    /**
     * @brief Issue one word
     *
     * Called by the group when the client wins the arbitration; the client
     * must call IdmaObiPortGroup::send exactly once from here.
     */
    virtual void obi_issue_word() = 0;

    /**
     * @brief Word granted
     *
     * Called by the group once every port of the access has been granted (the
     * OBI gnt), before the completion.
     *
     * @param token The token given to send
     */
    virtual void obi_word_granted(void *token) {}

    /**
     * @brief Word completed
     *
     * Called by the group once every port of the access has answered, at the
     * cycle the data is valid (read) or written (write).
     *
     * @param token The token given to send
     */
    virtual void obi_word_done(void *token) = 0;
};



/**
 * @brief Group of narrow OBI ports driven as one wider port
 *
 * Model of the RTL mem_to_banks in front of the TCDM crossbar: an access of
 * up to nb_ports * port_width bytes is split into one request per port, all
 * issued in the same cycle, and completes once every port has answered. A
 * port denied by the crossbar holds its request and re-issues it from the
 * retry callback, as the crossbar requires; the next access is issued only
 * when every port of the current one has been granted, at most one access per
 * cycle. Several clients may share the group (the RTL obi_mux in front of the
 * read ports); they are served round-robin.
 *
 * Addresses are masked to the crossbar's local address space.
 */
class IdmaObiPortGroup : public vp::Block
{
public:
    IdmaObiPortGroup(vp::Component *idma, std::string name, int nb_ports, int port_width,
        uint64_t addr_mask);

    void reset(bool active) override;

    /**
     * @brief Register a client; clients are arbitrated in registration order
     */
    void add_client(IdmaObiClient *client);

    /**
     * @brief Notify that a client may have a word to issue
     *
     * The arbitration runs at once when the group is free (the RTL request
     * goes out in the cycle it is presented), otherwise once the current
     * access is granted.
     */
    void update();

    /**
     * @brief Issue one access
     *
     * Only callable from IdmaObiClient::obi_issue_word. The access must not
     * cross an access-width boundary.
     *
     * @param client Issuing client
     * @param token Returned in obi_word_done
     * @param addr Address of the first byte
     * @param data Data buffer (written for a read, read for a write)
     * @param size Size in bytes
     * @param is_write True for a write
     */
    void send(IdmaObiClient *client, void *token, uint64_t addr, uint8_t *data, uint64_t size,
        bool is_write);

    /**
     * @brief Bytes of one access
     */
    int access_width() { return this->nb_ports * this->port_width; }

private:
    /// One access from its grant to its completion.
    struct Access
    {
        /// Client that issued it and the token it gave.
        IdmaObiClient *client;
        void *token;
        /// Cycle its data is valid (the slowest port's latency).
        int64_t done_cycle;
    };

    /// Deferred arbitration (after a grant, or when the cycle was busy).
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    /// Pick the next client with a word, round-robin, and let it issue.
    void arbitrate();
    /// Delivers the completions whose cycle has come, in order.
    static void done_handler(vp::Block *__this, vp::ClockEvent *event);
    /// Crossbar retry of one port: re-issue its held request inline.
    static void retry_meth(vp::Block *__this, int id, vp::IoRetryChannel channel);
    /// Never expected: the ports answer inline.
    static vp::IoRespAck resp_meth(vp::Block *__this, vp::IoReq *req, int id);
    /// One port of the current access was granted.
    void port_granted(int id);
    /// Arm the completion event for the oldest pending access.
    void schedule_done();

    vp::Trace trace;
    vp::ClockEvent fsm_event;
    vp::ClockEvent done_event;
    /// Ports driven together and the width of each, in bytes.
    int nb_ports;
    int port_width;
    /// Mask applied to the addresses (local address space of the crossbar).
    uint64_t addr_mask;
    /// The io_v2 masters and one request object per port.
    std::vector<vp::IoMaster *> ports;
    std::vector<vp::IoReq *> reqs;
    /// Clients in registration order, and the next one to serve.
    std::vector<IdmaObiClient *> clients;
    int rr_next;
    /// Access being granted: ports still waiting for their grant.
    bool issuing;
    int nb_pending_grants;
    /// Per port: request held after a deny; request part of the access.
    std::vector<bool> port_denied;
    std::vector<bool> port_pending;
    /// The access being granted.
    Access current;
    /// Cycle after which a new access may be issued (one per cycle).
    int64_t next_issue_cycle;
    /// Granted accesses waiting for their completion cycle, in order.
    std::deque<Access> pending_done;
};

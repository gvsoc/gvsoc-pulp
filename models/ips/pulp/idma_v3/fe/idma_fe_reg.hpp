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

#include <vector>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/itf/wire.hpp>
#include <vp/register.hpp>
#include <vp/signal.hpp>
#include "../idma.hpp"

class IdmaBackend;

/**
 * @brief Register front-end (idma_reg32_3d): N register ports feeding M
 * streams.
 *
 * Register map (32-bit accesses, one copy of the transfer registers per port):
 *
 *   0x000              CONF: bits 11:10 enable_nd (0 1D, 1 2D, 2 3D), 14:12
 *                      source protocol, 17:15 destination protocol, bit 0
 *                      decouple_rw, bit 1 decouple_aw
 *   0x004 + 4 s        STATUS_s: bits 7:0 back-end busy, bit 8 mid-end busy
 *   0x004 + 4 (M + s)  NEXT_ID_s: a read launches the transfer on stream s
 *                      and returns its id
 *   0x004 + 4 (2M + s) DONE_ID_s: completion counter of stream s
 *   0x0D0 / 0x0D8      destination / source address
 *   0x0E0              length
 *   0x0E8 / 0x0F0 / 0x0F8  dst stride 2 / src stride 2 / reps 2
 *   0x100 / 0x108 / 0x110  dst stride 3 / src stride 3 / reps 3
 *
 * Ids start at 2 and skip 0 and 1 on wrap; DONE_ID counts completions the
 * same way and is updated one cycle after the completion (registered
 * counter), together with the completion event pulse shared by the
 * completions of that cycle. A launch is denied while the stream's request
 * FIFO is full and retried, round-robin between the ports, once a slot is
 * free. While the enable wire is bound and low every access is refused with
 * a warning (the RTL block is clock gated and would hang the requester).
 */
class IdmaFeReg : public vp::Block
{
public:
    /// One stream: its id counters and its side of the mid-end interface.
    class Stream : public vp::Block, public IdmaFeSource
    {
        friend class IdmaFeReg;
    public:
        Stream(IdmaFeReg *top, int id);

        // IdmaFeSource, forwarded to the front-end
        void me_ready() override;
        void complete_nd(IdmaNdReq *req) override;

    private:
        IdmaFeReg *top;
        int id;
        /// Mid-end and back-end of the stream (set_stream()).
        IdmaFeSink *me = nullptr;
        IdmaBackend *be = nullptr;
        /// Id of the next launch (reset 2) and DONE_ID (reset 1).
        vp::Register<uint32_t> next_id;
        vp::Register<uint32_t> done_id;
        /// Completions of the current cycle, applied to done_id next cycle.
        int pending_done = 0;
        /// GUI signals: a transfer is in flight, id of the last launch.
        vp::Signal<bool> trace_busy;
        vp::Signal<uint32_t> trace_id;
    };

    /// @param top           Owning component; the ports are created on it.
    /// @param nb_ports      Register ports (input_<n>), each a register file.
    /// @param nb_streams    Streams (id counters, STATUS / NEXT_ID / DONE_ID).
    /// @param nb_events     Completion event outputs (event_<n>).
    /// @param launch_bubble Extra cycles of a launch from idle (clock gate).
    IdmaFeReg(vp::Component *top, int nb_ports, int nb_streams, int nb_events, int launch_bubble);

    /// Hardware reset; the block starts gated when the enable wire is bound.
    void reset(bool active) override;

    /// Attach the mid-end and back-end of a stream (once, at construction).
    void set_stream(int stream, IdmaFeSink *me, IdmaBackend *be);
    /// The stream's side of the mid-end interface, to hand to its mid-end.
    IdmaFeSource *stream(int stream) { return this->streams[stream]; }

    /// True while any stream has a transfer queued or in flight.
    bool is_busy();

private:
    /// One register access port with its own copy of the transfer registers.
    struct RegPort : public vp::Block
    {
        RegPort(IdmaFeReg *top, int id);

        int id;
        /// The io_v2 slave (input_<id>).
        vp::IoSlave itf;
        /// The transfer registers (see the class description).
        vp::Register<uint32_t> conf;
        vp::Register<uint32_t> dst;
        vp::Register<uint32_t> src;
        vp::Register<uint32_t> length;
        vp::Register<uint32_t> dst_stride_2;
        vp::Register<uint32_t> src_stride_2;
        vp::Register<uint32_t> reps_2;
        vp::Register<uint32_t> dst_stride_3;
        vp::Register<uint32_t> src_stride_3;
        vp::Register<uint32_t> reps_3;
        /// A launch was denied on this port and waits for that stream.
        bool launch_pending = false;
        int pending_stream = 0;
    };

    /// Register access on one port (decode, see the class description).
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req, int port);
    /// Clock gate wire.
    static void enable_sync(vp::Block *__this, bool value);
    /// Applies the pending completions to DONE_ID and pulses the events.
    static void done_handler(vp::Block *__this, vp::ClockEvent *event);
    /// A NEXT_ID read: launch the transfer of a port on a stream, or deny it.
    vp::IoReqStatus launch(RegPort *port, int stream, uint32_t *rdata);
    /// A stream's request FIFO has a free slot: retry the denied launches.
    void stream_ready(int stream);
    /// A stream completed an ND transfer.
    void stream_done(Stream *stream, IdmaNdReq *req);
    /// Recompute and drive the busy wire.
    void update_busy();

    vp::Trace trace;
    std::vector<RegPort *> ports;
    std::vector<Stream *> streams;
    /// Completion events: one per core (event_<n>) and the FC one.
    std::vector<vp::WireMaster<bool> *> event_itf;
    vp::WireMaster<bool> fc_event_itf;
    /// Busy wire (cluster power model) and clock gate input.
    vp::WireMaster<bool> busy_itf;
    vp::WireSlave<bool> enable_itf;
    /// Clock gate state (accesses are refused while false).
    bool enabled = true;
    /// DONE_ID update and event pulse, one cycle after the completions.
    vp::ClockEvent done_event;
    /// Last value driven on the busy wire.
    bool busy = false;
    /// Extra cycles of a launch from idle (datapath clock gate); 0 disables.
    int launch_bubble;
    /// Round-robin position among the ports waiting for a stream.
    int rr_port = 0;
};

/*
 * Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and
 *                    University of Bologna
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

#include <string>
#include <climits>
#include <cstring>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include "floonoc_v2.hpp"
#include "floonoc_network_interface_v2.hpp"

NetworkQueueV2::NetworkQueueV2(NetworkInterfaceV2 &ni, std::string name, uint64_t width, int nw)
: vp::Block(&ni, name), ni(ni), width(width), nw(nw)
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);
}

void NetworkQueueV2::reset(bool active)
{
    if (active)
    {
        while (this->queue.size() > 0)
        {
            // Queued flits are exclusively ours — recycle them. The external
            // bursts/beats they reference are dropped, as before.
            this->ni.flit_allocator->free(this->queue.front());
            this->queue.pop();
        }
        this->stalled = false;
    }
}

void NetworkQueueV2::check()
{
    if (!this->stalled && this->queue.size() > 0)
    {
        this->send_router_req();
    }
}

void NetworkQueueV2::unstall()
{
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Unstalling queue\n");
    this->stalled = false;
    this->ni.fsm_event.enqueue();
}

void NetworkQueueV2::handle_req(vp::IoReq *req, bool wide)
{
    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received %s burst from initiator (burst: %p, offset: 0x%lx, size: 0x%lx, is_write: %d, op: %d)\n",
                     wide ? "wide" : "narrow", req, req->get_addr(), req->get_size(), req->get_is_write(), req->get_opcode());

    // wide here is the EXTERNAL burst's wide flag (which port it came in on),
    // independent of which internal NetworkQueueV2 (req/rsp/wide) carries the
    // router_reqs. The destination NI uses router_req->wide to pick which
    // target (wide_output_itf vs narrow_output_itf) to forward to, so it must
    // match the input port, not the carrier network.
    //
    // enqueue_router_req frames wormhole packets by destination run: the write
    // data (W beats) to one mesh position stay contiguous (matching the RTL
    // chimney, where the id-less AXI W beats must not interleave), while the
    // address header and any entry-boundary-crossing fragments become their own
    // single-flit packets. Reads (AR) are single-flit and never lock.
    this->enqueue_router_req(req, true, wide, true);
    if (req->get_is_write())
    {
        this->enqueue_router_req(req, false, wide, true);
    }
}

void NetworkQueueV2::handle_rsp(FloonocReqV2 *req, bool is_address)
{
    this->enqueue_router_rsp(req, is_address);
}


void NetworkQueueV2::enqueue_router_req(vp::IoReq *req, bool is_address, bool wide, bool is_req)
{
    uint64_t burst_base = req->get_addr();
    uint64_t burst_size = req->get_size();
    uint8_t *burst_data = req->get_data();
    // Previous flit's destination, for dest-run packet framing below. INT_MIN
    // sentinel so the first flit always opens a run.
    int prev_dest_x = INT_MIN, prev_dest_y = INT_MIN;

    while(burst_size > 0)
    {
        // Address phases (read AR and write AW) are metadata-only headers —
        // one FloonocReqV2 per burst. Data and response phases are paced at
        // the carrier queue's beat width. Beat-based downstreams may answer
        // a forwarded read with a multi-beat resp() stream; the NI's
        // wide_response / narrow_response path is built to absorb that
        // (each beat forwards as a partial response chunk; the FloonocReqV2
        // is only released on the last beat).
        uint64_t size = is_address ? burst_size : std::min(this->width, burst_size);
        FloonocReqV2 *router_req = this->ni.flit_allocator->alloc();

        router_req->src_x = this->ni.x;
        router_req->src_y = this->ni.y;
        router_req->is_rsp = false;
        router_req->burst = req;
        router_req->is_address = is_address;
        router_req->wide = wide;
        router_req->set_size(size);
        // Address headers (AR/AW) carry no data. Read bursts are data-less
        // anyway (io_v2 beat protocol: the data comes back inside the
        // response flits); W data flits point into the incoming write beat's
        // buffer — the NI owns the beat (granted write beats are
        // consumer-freed) and keeps it unfreed, hence the buffer valid, until
        // the beat's B flit returns.
        router_req->set_data(is_address ? NULL : burst_data);
        router_req->set_addr(burst_base);
        router_req->set_is_write(req->get_is_write());
        router_req->set_opcode(req->get_opcode());
        router_req->set_second_data(req->get_second_data());
        // Snapshot the external burst metadata the source-side B accounting
        // needs into the flit's own (otherwise unused) fields: a write beat is
        // consumed and freed by the destination target, so the B flit — which
        // copies these — is all that comes back to account the ack from.
        router_req->burst_id = req->burst_id;
        router_req->initiator = req->initiator;

        if (wide)
        {
            req->get_is_write() ? this->ni.wide_write_pending_burst_nb_req++ :
                this->ni.wide_read_pending_burst_nb_req++;
        }
        else
        {
            req->get_is_write() ? this->ni.narrow_write_pending_burst_nb_req++ :
                this->ni.narrow_read_pending_burst_nb_req++;
        }

        EntryV2 *entry = this->ni.get_entry(burst_base, size);

        if (entry == NULL)
        {
            this->trace.msg(vp::Trace::LEVEL_ERROR, "No entry found for base 0x%lx\n", burst_base);
            return;
        }
        uint64_t entry_end = entry->base + entry->size;
        uint64_t max_size = entry_end - burst_base;
        size = std::min(max_size, size);

        this->trace.msg(vp::Trace::LEVEL_TRACE,
            "Enqueue request to router (req: %p, base: 0x%lx, size: 0x%lx, "
            "destination: (%d, %d))\n",
            router_req, burst_base, size, entry->x, entry->y);

        router_req->set_size(size);
        router_req->set_addr(burst_base - entry->remove_offset);
        router_req->initiator_addr = burst_base;
        router_req->dest_x = entry->x;
        router_req->dest_y = entry->y;

        // Beat encapsulation: a W flit that covers its WHOLE beat carries
        // beat ownership — the destination hands the beat itself to the
        // target (no wrapper) and the beat is consumed and freed there. A
        // split beat (a slice smaller than the beat: oversized big-packet
        // form one-beat writes, which are legal at any size, or the entry
        // clamp above) keeps the legacy scheme — its buffer must outlive
        // every flit, so the beat stays unfreed until its B flit returns to
        // the source.
        router_req->owns_beat = !is_address && req->get_opcode() == vp::WRITE
            && size == req->get_size();

        // Wormhole packet framing by DESTINATION run: a packet may only span
        // flits going to the same mesh position, because it reserves a router
        // output along one route until its tail flit. A burst that crosses a
        // memory-map entry boundary (e.g. a 256KB transfer spilling across
        // 64KB per-node regions) fragments into flits with different dests —
        // each same-dest run is its own packet. Framing this way (rather than
        // one packet per burst) is what stops the head flit of one run from
        // leaking a lock on an output the tail flit — bound elsewhere — never
        // releases. is_first opens a run when the dest changes; is_last closes
        // it when the burst ends or the next flit would cross into a new entry.
        bool dest_changed = (entry->x != prev_dest_x || entry->y != prev_dest_y);
        bool reached_entry_end = (burst_base + size >= entry_end);
        bool burst_ends = (burst_size <= size);
        router_req->is_first = dest_changed;
        router_req->is_last = burst_ends || reached_entry_end;
        prev_dest_x = entry->x;
        prev_dest_y = entry->y;

        this->trace.msg(vp::Trace::LEVEL_DEBUG,
            "NOC_V2_INJECT req=%p addr=0x%lx bytes=%lu src=(%d,%d) dst=(%d,%d) rsp=0 write=%d nw=%d\n",
            router_req, burst_base, size, this->ni.x, this->ni.y,
            router_req->dest_x, router_req->dest_y, req->get_is_write(), this->nw);
        this->queue.push(router_req);

        burst_base += size;
        if (burst_data != NULL)
        {
            burst_data += size;
        }
        burst_size -= size;
    }
    this->ni.fsm_event.enqueue();
}

void NetworkQueueV2::enqueue_router_rsp(FloonocReqV2 *req, bool is_address)
{
    // Allocate a fresh FloonocReqV2 for the response traversal, copying the
    // metadata the router needs from the incoming request. The caller frees
    // the original after we return — mirrors the v1 enqueue_router_req
    // response-path branch.
    FloonocReqV2 *router_req = this->ni.flit_allocator->alloc();

    // Response flits are single-flit packets, mirroring the RTL chimney: R
    // beats set hdr.last=1 on every beat ("no reason to do wormhole routing for
    // R bursts" — floo_axi_chimney) and the B ack is a lone flit. Read data
    // therefore never holds a router output across the holes the (slow) target
    // leaves between beats; only writes (AW+W) are wormhole packets.
    router_req->is_first = true;
    router_req->is_last  = true;
    router_req->src_x = req->src_x;
    router_req->src_y = req->src_y;
    router_req->is_rsp = true;
    router_req->burst = req->burst;
    router_req->is_address = is_address;
    router_req->wide = req->wide;
    router_req->dest_x = req->dest_x;
    router_req->dest_y = req->dest_y;
    router_req->initiator_addr = req->initiator_addr;
    // Carry the external write-burst metadata through to the source side:
    // only the write-B path of an owns_beat flit consumes these there (the
    // beat it belongs to was consumed and freed by the destination target).
    // Stale for R flits, whose source-side accounting uses the still-alive
    // read burst instead.
    router_req->burst_id = req->burst_id;
    router_req->initiator = req->initiator;
    router_req->owns_beat = req->owns_beat;
    router_req->set_size(req->get_size());
    // A read-data response flit carries its data slice BY VALUE across the
    // mesh (like the RTL chimney's R flits): the incoming req's data points
    // into our own request flit's payload, which is recycled per beat, so a
    // queued response flit cannot alias it. Ack flits (is_address) carry none.
    if (!req->get_is_write() && !is_address && req->get_size() > 0
        && req->get_data() != NULL)
    {
        router_req->set_payload(req->get_data(), req->get_size());
    }
    else
    {
        router_req->set_data(req->get_data());
    }
    router_req->set_addr(req->get_addr());
    router_req->set_is_write(req->get_is_write());
    router_req->set_opcode(req->get_opcode());
    router_req->set_second_data(req->get_second_data());
    router_req->set_resp_status(req->get_resp_status());

    this->trace.msg(vp::Trace::LEVEL_DEBUG,
        "NOC_V2_INJECT req=%p addr=0x%lx bytes=%lu src=(%d,%d) dst=(%d,%d) rsp=1 write=%d nw=%d\n",
        router_req, router_req->initiator_addr, router_req->get_size(), this->ni.x, this->ni.y,
        router_req->dest_x, router_req->dest_y, req->get_is_write(), this->nw);
    this->queue.push(router_req);
    this->ni.fsm_event.enqueue();
}

void NetworkQueueV2::send_router_req()
{
    FloonocReqV2 *req = this->queue.front();
    this->queue.pop();

    // Keyed on the flit's own copies of is_write/wide: req->burst may only be
    // dereferenced on the request path (a write beat is consumed and freed by
    // the destination target, so it is stale on the B response path).
    if (!req->is_rsp)
    {
        int *nb_req;
        if (req->wide)
        {
            nb_req = req->get_is_write() ? &this->ni.wide_write_pending_burst_nb_req :
                &this->ni.wide_read_pending_burst_nb_req;
        }
        else
        {
            nb_req = req->get_is_write() ? &this->ni.narrow_write_pending_burst_nb_req :
                &this->ni.narrow_read_pending_burst_nb_req;
        }
        *nb_req = *nb_req - 1;
        if (*nb_req == 0)
        {
            this->ni.fsm_event.enqueue();
        }
    }

    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Injecting flit (req: %p, base: 0x%lx, size: 0x%lx, is_write: %d, op: %d, is_rsp: %d)\n",
                    req, req->get_addr(), req->get_size(), req->get_is_write(), req->get_opcode(), req->is_rsp);

    this->stalled = this->ni.link_out[this->nw].req(req);
    if (this->stalled)
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Stalling network interface (position: (%d, %d))\n", this->ni.x, this->ni.y);
    }

    if (this->queue.size() > 0)
    {
        this->ni.fsm_event.enqueue();
    }
}

NetworkInterfaceV2::NetworkInterfaceV2(vp::ComponentConf &config)
    : vp::Component(config),
      wide_output_itf(&NetworkInterfaceV2::wide_retry, &NetworkInterfaceV2::wide_response),
      narrow_output_itf(&NetworkInterfaceV2::narrow_retry, &NetworkInterfaceV2::narrow_response),
      wide_input_itf(&NetworkInterfaceV2::wide_req, &NetworkInterfaceV2::wide_response_retry),
      narrow_input_itf(&NetworkInterfaceV2::narrow_req, &NetworkInterfaceV2::narrow_response_retry),
      link_out{{
        FloonocLinkMaster(NW_REQ, &NetworkInterfaceV2::link_unstall),
        FloonocLinkMaster(NW_RSP, &NetworkInterfaceV2::link_unstall),
        FloonocLinkMaster(NW_WIDE, &NetworkInterfaceV2::link_unstall)
      }},
      link_in{{
        FloonocLinkSlave(NW_REQ, &NetworkInterfaceV2::link_req),
        FloonocLinkSlave(NW_RSP, &NetworkInterfaceV2::link_req),
        FloonocLinkSlave(NW_WIDE, &NetworkInterfaceV2::link_req)
      }},
      fsm_event(this, &NetworkInterfaceV2::fsm_handler),
      signal_narrow_req(*this, "narrow_req", 64),
      signal_wide_req(*this, "wide_req", 64),
      req_queue(*this, "narrow", get_js_config()->get_uint("narrow_width"), NW_REQ),
      rsp_queue(*this, "rsp", get_js_config()->get_uint("narrow_width"), NW_RSP),
      wide_queue(*this, "wide", get_js_config()->get_uint("wide_width"), NW_WIDE),
      response_queue(this, "response_queue", &this->fsm_event)
{
    traces.new_trace("trace", &trace, vp::DEBUG);

    this->x = get_js_config()->get_int("x");
    this->y = get_js_config()->get_int("y");
    this->narrow_width = get_js_config()->get_uint("narrow_width");
    this->wide_width = get_js_config()->get_uint("wide_width");
    this->rsp_allocator[0] = vp::IoReqAllocator::get(this->narrow_width);
    this->rsp_allocator[1] = vp::IoReqAllocator::get(this->wide_width);
    this->ack_allocator = vp::IoReqAllocator::get(0);
    this->fwd_wr_allocator = vp::IoReqAllocator::get(0);
    this->flit_allocator = FloonocReqV2Allocator::get();
    this->ni_outstanding_reqs = get_js_config()->get_int("ni_outstanding_reqs");
    this->max_burst_size = get_js_config()->get_uint("max_burst_size");

    this->new_master_port("wide_output", &this->wide_output_itf);
    this->new_master_port("narrow_output", &this->narrow_output_itf);
    this->new_slave_port("narrow_input", &this->narrow_input_itf);
    this->new_slave_port("wide_input", &this->wide_input_itf);

    static const char *nw_names[NW_NB] = {"req", "rsp", "wide"};
    for (int i = 0; i < NW_NB; i++)
    {
        this->new_master_port(std::string(nw_names[i]) + "_link_out", &this->link_out[i]);
        this->new_slave_port(std::string(nw_names[i]) + "_link_in", &this->link_in[i]);
    }

    // Every NI receives the full memory map so it can translate addresses to
    // mesh positions on its own.
    js::Config *mappings = get_js_config()->get("mappings");
    if (mappings != NULL)
    {
        this->entries.reserve(mappings->get_childs().size());
        for (auto& mapping: mappings->get_childs())
        {
            js::Config *config = mapping.second;

            uint64_t base = config->get_uint("base");
            uint64_t size = config->get_uint("size");
            uint64_t remove_offset = config->get_uint("remove_offset");
            int map_x = config->get_int("x");
            int map_y = config->get_int("y");

            EntryV2 entry;
            entry.base = base;
            entry.size = size;
            entry.x = map_x;
            entry.y = map_y;
            entry.remove_offset = remove_offset;
            this->entries.push_back(entry);
        }
    }

    // AXI-style burst legality: a burst may not cross a max_burst_size boundary
    // (the 4KB rule), so it always targets a single mesh position and every
    // wormhole packet is single-destination. For that to hold, no two targets
    // may share a max_burst_size-aligned page. We enforce it the simple way:
    // require every target to be aligned to and a multiple of max_burst_size, so
    // each occupies whole pages and can never share one with another. That is
    // how real AXI slave regions are laid out anyway. Config error, so always
    // active (not just in asserts builds).
    if (this->max_burst_size > 0)
    {
        for (EntryV2 &e : this->entries)
        {
            if (e.size == 0)
            {
                continue;
            }
            if (e.base % this->max_burst_size != 0 || e.size % this->max_burst_size != 0)
            {
                this->trace.fatal("Target (%d,%d) [0x%lx..0x%lx] is not aligned to and a "
                    "multiple of max_burst_size=0x%lx; targets must not share a page so a "
                    "burst always lands in one target\n", e.x, e.y, e.base,
                    e.base + e.size - 1, this->max_burst_size);
            }
        }
    }
}

EntryV2 *NetworkInterfaceV2::get_entry(uint64_t base, uint64_t size)
{
    for (int i=0; i<this->entries.size(); i++)
    {
        EntryV2 *entry = &this->entries[i];
        if (entry->size > 0 && base >= entry->base && base < entry->base + entry->size)
        {
            return entry;
        }
    }
    return NULL;
}

vp::IoRespAck NetworkInterfaceV2::wide_response(vp::Block *__this, vp::IoReq *req)
{
    NetworkInterfaceV2 *_this = (NetworkInterfaceV2 *)__this;
    _this->handle_response(_this->unwrap_response(req));
    return vp::IO_RESP_ACCEPTED;
}

// Recover our own FloonocReqV2 from a downstream response.
//
// Read path: a beat target answers with DISTINCT per-beat objects
// (initiator-owned convention) carrying our request as req->initiator; we fold
// this beat's per-beat fields (addr/size/framing/status) onto our own request
// — reused per beat, exactly as the legacy same-object beat-stream model
// expects — copy its allocator-provided payload into our flit payload (the
// beat is recycled the moment we free it), and return the beat to its pool.
//
// Write path: the target consumed and freed the write object we forwarded
// (make_target_req — the encapsulated external beat, or a split-path pool
// wrapper) and answers with the distinct data-less burst ack, carrying the
// flit as req->initiator (copied from the beat). Only the status is folded —
// the flit's own addr/size drive the B accounting, and its framing was
// forced to a self-contained single beat at forward time — and the ack is
// freed (the initiator frees the ack).
//
// An atomic round-trips our own object (req == initiator), so there is
// nothing to fold or free.
FloonocReqV2 *NetworkInterfaceV2::unwrap_response(vp::IoReq *req)
{
    FloonocReqV2 *self_req = (FloonocReqV2 *)req->initiator;
    if (req != self_req)
    {
        if (req->get_opcode() == vp::WRITE)
        {
            self_req->set_resp_status(req->get_resp_status());
            req->free();
        }
        else
        {
            self_req->set_addr(req->get_addr());
            self_req->set_size(req->get_size());
            if (!req->get_is_write() && req->get_size() > 0)
            {
                self_req->set_payload(req->get_data(), req->get_size());
            }
            self_req->is_first = req->is_first;
            self_req->is_last  = req->is_last;
            self_req->set_resp_status(req->get_resp_status());
            req->free();
        }
    }
    return self_req;
}

void NetworkInterfaceV2::wide_retry(vp::Block *block, vp::IoRetryChannel)
{
    static_cast<NetworkInterfaceV2 *>(block)->retry_target(true);
}

void NetworkInterfaceV2::retry_target(bool wide)
{
    auto &pending = this->target_pending[wide];
    while (!pending.empty())
    {
        auto held = pending.front();
        if (this->send_to_target(held.first, wide)) return;
        pending.pop_front();
        this->link_in[held.second].unstall();
    }
    this->fsm_event.enqueue();
}

bool NetworkInterfaceV2::deliver_response(bool wide, vp::IoReq *req, int nw)
{
    auto &pending = this->response_pending[wide];
    vp::IoSlave &port = wide ? this->wide_input_itf : this->narrow_input_itf;
    if (!pending.empty() || port.resp(req) == vp::IO_RESP_DENIED)
    {
        pending.emplace_back(req, nw);
        return true; // Stop this mesh link until the consumer accepts the beat.
    }
    return false;
}

void NetworkInterfaceV2::retry_response(bool wide)
{
    auto &pending = this->response_pending[wide];
    vp::IoSlave &port = wide ? this->wide_input_itf : this->narrow_input_itf;
    while (!pending.empty())
    {
        auto held = pending.front();
        if (port.resp(held.first) == vp::IO_RESP_DENIED) return;
        pending.pop_front();
        this->link_in[held.second].unstall();
    }
}

void NetworkInterfaceV2::wide_response_retry(vp::Block *block, vp::IoRetryChannel)
{
    static_cast<NetworkInterfaceV2 *>(block)->retry_response(true);
}

void NetworkInterfaceV2::narrow_response_retry(vp::Block *block, vp::IoRetryChannel)
{
    static_cast<NetworkInterfaceV2 *>(block)->retry_response(false);
}

vp::IoRespAck NetworkInterfaceV2::narrow_response(vp::Block *__this, vp::IoReq *req)
{
    NetworkInterfaceV2 *_this = (NetworkInterfaceV2 *)__this;
    _this->handle_response(_this->unwrap_response(req));
    return vp::IO_RESP_ACCEPTED;
}

void NetworkInterfaceV2::narrow_retry(vp::Block *block, vp::IoRetryChannel)
{
    static_cast<NetworkInterfaceV2 *>(block)->retry_target(false);
}

void NetworkInterfaceV2::reset(bool active)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Resetting network interface\n");
    if (active)
    {
        this->wide_read_pending_burst = NULL;
        this->wide_write_pending_burst = NULL;
        this->wide_read_pending_burst_nb_req = 0;
        this->wide_write_pending_burst_nb_req = 0;
        this->narrow_read_pending_burst = NULL;
        this->narrow_write_pending_burst = NULL;
        this->narrow_read_pending_burst_nb_req = 0;
        this->narrow_write_pending_burst_nb_req = 0;
        this->nb_pending_bursts[0] = 0;
        this->nb_pending_bursts[1] = 0;
        this->owes_retry_wide_input = false;
        this->owes_retry_narrow_input = false;
        // A held (denied) forwarded write object is allocator-backed either
        // way — the encapsulated external beat (ours since GRANT under the
        // consumer-frees contract) or a split-path pool wrapper — so return
        // it to its pool. A held read/atomic flit is ours too — recycle it.
        // The other in-flight flits (in router queues or pending responses)
        // are dropped without freeing, and the write beats they encapsulate
        // or reference with them — consistent with reads, where the external
        // master likewise never gets its in-flight burst answered across a
        // reset.
        for (auto &pending : this->target_pending)
        {
            for (auto held : pending)
            {
                if (held.first->initiator != held.first) held.first->free();
                else this->flit_allocator->free((FloonocReqV2 *)held.first);
            }
            pending.clear();
        }
        for (auto &pending : this->response_pending)
        {
            for (auto held : pending)
                if (held.first->allocator) held.first->free();
            pending.clear();
        }
        this->wr_bursts[0].clear();
        this->wr_bursts[1].clear();
    }
}

int NetworkInterfaceV2::get_req_nw(bool is_wide, bool is_write)
{
    if (is_wide)
    {
        return is_write ? NetworkInterfaceV2::NW_WIDE : NetworkInterfaceV2::NW_REQ;
    }
    else
    {
        return NetworkInterfaceV2::NW_REQ;
    }
}

int NetworkInterfaceV2::get_rsp_nw(bool is_wide, bool is_write)
{
    return is_wide ? NetworkInterfaceV2::NW_WIDE : NetworkInterfaceV2::NW_RSP;
}

vp::IoReqStatus NetworkInterfaceV2::narrow_req(vp::Block *__this, vp::IoReq *req)
{
    NetworkInterfaceV2 *_this = (NetworkInterfaceV2 *)__this;
    _this->signal_narrow_req = req->get_addr();
    return _this->handle_req(req, /*wide=*/false);
}

vp::IoReqStatus NetworkInterfaceV2::wide_req(vp::Block *__this, vp::IoReq *req)
{
    NetworkInterfaceV2 *_this = (NetworkInterfaceV2 *)__this;
    _this->signal_wide_req = req->get_addr();
    return _this->handle_req(req, /*wide=*/true);
}

vp::IoReqStatus NetworkInterfaceV2::handle_req(vp::IoReq *req, bool wide)
{
    this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received request from target (req: %p, base: 0x%lx, size: 0x%lx, wide: %d)\n",
        req, req->get_addr(), req->get_size(), wide);

    // Reject an unmapped or overflowing range before allocating a flit or
    // consuming an outstanding slot. An accepted request must always complete.
    uint64_t addr = req->get_addr(), size = req->get_size();
    bool mapped = size > 0 && addr <= UINT64_MAX - size;
    uint64_t remaining = size, cursor = addr;
    while (mapped && remaining > 0)
    {
        EntryV2 *entry = this->get_entry(cursor, remaining);
        if (entry == nullptr)
        {
            mapped = false;
            break;
        }
        uint64_t chunk = std::min(remaining, entry->size - (cursor - entry->base));
        cursor += chunk;
        remaining -= chunk;
    }
    if (!mapped)
    {
        req->set_resp_status(vp::IO_RESP_INVALID);
        return vp::IO_REQ_DONE;
    }

    // AXI burst legality (asserts builds only): a burst must fit in max_burst_size
    // and must not cross a max_burst_size boundary, so it lands in a single page
    // and hence a single target — keeping every wormhole packet single-dest.
    if (this->max_burst_size > 0)
    {
        uint64_t base = req->get_addr(), size = req->get_size();
        this->traces.assert(size <= this->max_burst_size,
            "Input burst size 0x%lx exceeds max_burst_size 0x%lx (addr 0x%lx)",
            size, this->max_burst_size, base);
        this->traces.assert(size == 0 || (base % this->max_burst_size) + size <= this->max_burst_size,
            "Input burst [0x%lx..0x%lx] crosses a max_burst_size=0x%lx boundary (AXI 4KB rule)",
            base, base + size - 1, this->max_burst_size);
    }

    // Use the v2 IoReq remaining_size field to track how many bytes still need
    // to be returned through the mesh before we can ack the burst.
    req->remaining_size = req->get_size();
    // We do NOT clobber req->initiator here: v2 masters such as the iDMA
    // backend use that field for their own bookkeeping (BurstInfo*) and
    // expect it to survive the round-trip through the mesh. The NI instead
    // picks the response port from the inner FloonocReqV2's `wide` flag.

    vp::IoReq **queue;
    if (wide)
    {
        queue = req->get_is_write() ? &this->wide_write_pending_burst :
                &this->wide_read_pending_burst;
    }
    else
    {
        queue = req->get_is_write() ? &this->narrow_write_pending_burst :
                &this->narrow_read_pending_burst;
    }

    // Admission is bounded only by the number of outstanding bursts, matching
    // the RTL chimney's MaxTxns semantics. We deliberately do NOT serialize on
    // the previous burst finishing its fragmentation into router flits: that
    // coupling made admission stall whenever the downstream router was
    // back-pressured (a hotspot), starving the sources closest to the jam in
    // periodic bubbles the RTL does not have. The per-burst pointer below is
    // kept only as an aggregate drain-wakeup helper (see fsm_handler).
    if (this->nb_pending_bursts[wide] >= this->ni_outstanding_reqs)
    {
        // v2 deny: do not queue. Remember that the master is owed a retry()
        // when capacity returns.
        if (wide)
        {
            this->owes_retry_wide_input = true;
        }
        else
        {
            this->owes_retry_narrow_input = true;
        }
        return vp::IO_REQ_DENIED;
    }
    else
    {
        this->nb_pending_bursts[wide]++;

        // Per-burst write acknowledgement (io_v2 write-ack contract): the mesh
        // machinery below still treats every accepted beat as its own
        // mini-burst (its own AW+W flits and its own B flit back, so the
        // router traffic — and the calibration — is unchanged), but the
        // endpoint acknowledges once per BURST: a beat that fits one W flit
        // travels the mesh encapsulated in it and is consumed and freed by
        // the destination target (a split beat stays alive until its B flit
        // returns, as before), and a single data-less pool ack answers the
        // whole burst once every B flit is back. Atomics keep the classic
        // round-trip (keyed on opcode == WRITE, not get_is_write()).
        if (req->get_opcode() == vp::WRITE)
        {
            this->traces.assert(req->allocator != nullptr,
                "write beat is not allocator-backed (req: %p) — unported master", req);
            if (!(req->is_first && req->is_last && req->burst_id == -1))
            {
                this->traces.assert(req->burst_id >= 0,
                    "multi-beat write beat without a burst_id (req: %p)", req);
                auto &bursts = this->wr_bursts[wide];
                auto it = bursts.find(req->burst_id);
                if (req->is_first)
                {
                    this->traces.assert(it == bursts.end(),
                        "write burst_id %ld reopened while still tracked",
                        (long)req->burst_id);
                    it = bursts.emplace(req->burst_id, WrTrack{}).first;
                    it->second.initiator = req->initiator;
                    it->second.base = req->get_addr();
                }
                else
                {
                    this->traces.assert(it != bursts.end(),
                        "write-burst continuation without an open burst (burst_id: %ld)",
                        (long)req->burst_id);
                    this->traces.assert(it->second.initiator == req->initiator,
                        "write beats of one burst must carry the same initiator (req: %p)",
                        req);
                }
                it->second.issued_bytes += req->get_size();
                it->second.seen_last |= req->is_last;
            }
        }

        *queue = req;
        if (!req->get_is_write() || !wide)
        {
            this->req_queue.handle_req(req, wide);
        }
        else
        {
            this->wide_queue.handle_req(req, wide);
        }
        this->fsm_event.enqueue();
        return vp::IO_REQ_GRANTED;
    }
}

// Build the object forwarded to the local target for one mesh request flit.
//
// The is_first/is_last on the flit are the mesh wormhole packet framing (set
// by enqueue_router_req/rsp). They must NOT leak into the io_v2 beat protocol
// used towards the local target: each forwarded flit is presented as a
// self-contained single-beat submission. We also force them on the flit
// itself — its mesh framing has been consumed by the time it reaches us, and
// handle_response relies on is_last to release it.
//
// Write data flits come in two flavours, decided at enqueue time (owns_beat):
//
// A W flit covering its WHOLE beat hands the ENCAPSULATED EXTERNAL BEAT
// itself to the target: the beat travelled the mesh inside the flit
// (ownership moved to the flit at GRANT on the source side), and the target
// consumes and frees it under the write-ack contract — no wrapper object.
// The beat is rewritten as a self-contained single-beat submission at the
// mesh-translated address (legal: we own it). This is the common case: on an
// IoV2Beat-signature'd binding an incoming beat is capped at the port width,
// which is also the carrier queue's slice width.
//
// A flit of a SPLIT beat (an oversized big-packet-form one-beat write, or
// an entry-clamped slice) is wrapped in a DISTINCT data-less
// pool beat instead: the external beat's buffer must stay valid for the
// beat's other flits, so the beat itself must not be handed out (the target
// would free it); it stays unfreed until its B flit returns to the source.
//
// Either way the submitted object back-references the flit through
// initiator, so the burst ack (or the object itself on DONE/DENIED) leads
// back to the flit. The flit's own burst_id/initiator keep carrying the
// external burst metadata for the B return and must not be touched here.
//
// Reads and atomics round-trip, so the flit itself is forwarded.
vp::IoReq *NetworkInterfaceV2::make_target_req(FloonocReqV2 *req)
{
    req->prepare();
    req->is_first = true;
    req->is_last = true;

    if (req->get_opcode() == vp::WRITE)
    {
        if (req->owns_beat)
        {
            vp::IoReq *beat = req->burst;
            this->traces.assert(beat != NULL && req->get_size() == beat->get_size()
                && req->get_data() == beat->get_data(),
                "owns_beat W flit does not cover its whole beat (flit: %p, beat: %p)",
                req, beat);
            beat->set_addr(req->get_addr());
            beat->is_first = true;
            beat->is_last = true;
            beat->burst_id = -1;
            beat->initiator = req;
            return beat;
        }

        vp::IoReq *beat = this->fwd_wr_allocator->alloc();
        beat->prepare();
        beat->set_addr(req->get_addr());
        beat->set_size(req->get_size());
        beat->set_opcode(vp::WRITE);
        beat->set_data(req->get_data());
        beat->is_first = true;
        beat->is_last = true;
        beat->burst_id = -1;
        beat->initiator = req;
        return beat;
    }

    // Initiator-owned request convention: a beat target answers an async
    // read with DISTINCT response beat objects (not this request reused).
    // Point initiator at ourselves so each response beat carries a back-ref
    // to this FloonocReqV2; the response callbacks recover it via
    // req->initiator instead of assuming the response IS this object.
    req->initiator = req;
    return req;
}

// Send (or re-send, from a retry) a request built by make_target_req to the
// local target port and handle the inline outcomes. Returns true when the
// target denied it — the caller then keeps `to_send` and re-sends it on the
// target's retry().
bool NetworkInterfaceV2::send_to_target(vp::IoReq *to_send, bool wide)
{
    vp::IoMaster *target = wide ? &this->wide_output_itf : &this->narrow_output_itf;

    to_send->prepare();
    vp::IoReqStatus result = target->req(to_send);

    if (result == vp::IO_REQ_DENIED)
    {
        return true;
    }

    if (result == vp::IO_REQ_DONE)
    {
        FloonocReqV2 *flit = (FloonocReqV2 *)to_send->initiator;
        int64_t latency;
        if (flit != to_send)
        {
            // Forwarded write object completed inline (last-beat DONE is the
            // burst ack; ownership stayed with us): fold the status and the
            // full timing annotation onto the flit and return the object —
            // encapsulated external beat or split-path wrapper — to its pool.
            flit->set_resp_status(to_send->get_resp_status());
            latency = to_send->get_full_latency();
            to_send->free();
            flit->set_latency(latency);
        }
        else
        {
            latency = flit->get_latency();
        }
        if (latency > 0)
        {
            this->response_queue.push_delayed(flit, latency);
        }
        else
        {
            this->handle_response(flit);
        }
    }
    // GRANTED: the response arrives via wide_response / narrow_response. For
    // a write beat, ownership (buffer included) moved on to the target, which
    // consumes and frees the external beat; the burst ack comes back as a
    // distinct data-less resp() leading back to the flit.

    return false;
}

bool NetworkInterfaceV2::link_req(vp::Block *__this, FloonocReqV2 *req, int nw)
{
    NetworkInterfaceV2 *_this = (NetworkInterfaceV2 *)__this;
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "NOC_V2_EJECT req=%p at=(%d,%d)\n",
        req, _this->x, _this->y);

    if (req->is_rsp)
    {
        // Response path: a reply has come back from the destination NI to us
        // (the source NI). Account it on the corresponding burst and reply to
        // the master when the burst is complete.
        _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received response from router (req: %p)\n", req);

        bool wide = req->wide;

        // The inner FloonocReqV2's `wide` flag tells us which external slave
        // port the burst entered through; we use that to dispatch the
        // response rather than burst->initiator (which belongs to the
        // master, e.g. the iDMA's BurstInfo*). Branches are keyed on the
        // flit's own is_write/opcode copies: for the B flit of an owns_beat
        // write the beat behind req->burst was consumed and freed by the
        // destination target, so it must not be dereferenced.
        bool response_stalled = false;

        if (req->get_is_write() && req->get_opcode() != vp::WRITE)
        {
            // Atomics keep the classic round-trip: hand the master its own
            // object back with the completion status (the response data was
            // written in place, the W flit pointed into its buffer).
            vp::IoReq *burst = req->burst;
            _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received atomic response (burst: %p)\n",
                burst);
            _this->nb_pending_bursts[wide]--;

            burst->set_resp_status(req->get_resp_status());
            response_stalled = _this->deliver_response(wide, burst, nw);
        }
        else if (req->get_is_write())
        {
            // Per-burst write acknowledgement: this B flit completes ONE
            // incoming write beat (the mesh treated it as its own mini-burst).
            // The B still fires where the old per-beat resp() used to, so
            // the ack timing is unchanged.
            _this->trace.msg(vp::Trace::LEVEL_DEBUG,
                "Received write beat response (flit: %p, burst_id: %ld)\n",
                req, (long)req->burst_id);
            _this->nb_pending_bursts[wide]--;

            bool error = req->get_resp_status() == vp::IO_RESP_INVALID;

            // Resolve the ack accounting fields. owns_beat: the beat was
            // consumed and freed at the destination — everything came back
            // in the flit (size, external burst_id/initiator snapshotted at
            // enqueue, pre-translation address in initiator_addr). Split
            // beat: the beat is still ours and alive (its buffer backed all
            // the W flits until now), and this single B — sent once every
            // slice's wrapper was consumed — is exactly where it is freed,
            // like the pre-encapsulation model did. burst_id == -1 is a
            // reliable lone-beat marker either way: admission asserts that
            // any multi-beat write carries burst_id >= 0.
            uint64_t beat_size;
            int64_t burst_id;
            void *initiator;
            uint64_t base;
            if (req->owns_beat)
            {
                beat_size = req->get_size();
                burst_id = req->burst_id;
                initiator = req->initiator;
                base = req->initiator_addr;
            }
            else
            {
                vp::IoReq *burst = req->burst;
                beat_size = burst->get_size();
                burst_id = burst->burst_id;
                initiator = burst->initiator;
                base = burst->get_addr();
                burst->free();
            }

            if (burst_id == -1)
            {
                // Lone single-beat burst without an id: bypasses the tracking
                // map and completes on its own B.
                vp::IoReq *ack = _this->ack_allocator->alloc();
                ack->prepare();
                ack->set_addr(base);
                ack->set_size(beat_size);
                ack->set_opcode(vp::WRITE);
                ack->set_data(NULL);
                ack->is_first = true;
                ack->is_last = true;
                ack->burst_id = -1;
                ack->initiator = initiator;
                ack->set_resp_status(error ? vp::IO_RESP_INVALID : vp::IO_RESP_OK);
                response_stalled = _this->deliver_response(wide, ack, nw);
            }
            else
            {
                auto it = _this->wr_bursts[wide].find(burst_id);
                _this->traces.assert(it != _this->wr_bursts[wide].end(),
                    "write beat response for untracked burst (burst_id: %ld)",
                    (long)burst_id);
                WrTrack &track = it->second;
                track.acked_bytes += beat_size;
                track.error |= error;

                // Order-robust close: B flits may return out of order, so the
                // burst completes when the is_last beat has been submitted AND
                // every issued byte has been acked.
                if (track.seen_last && track.acked_bytes == track.issued_bytes)
                {
                    vp::IoReq *ack = _this->ack_allocator->alloc();
                    ack->prepare();
                    ack->set_addr(track.base);
                    ack->set_size(track.issued_bytes);
                    ack->set_opcode(vp::WRITE);
                    ack->set_data(NULL);
                    ack->is_first = true;
                    ack->is_last = true;
                    ack->burst_id = it->first;
                    ack->initiator = track.initiator;
                    ack->set_resp_status(track.error ? vp::IO_RESP_INVALID : vp::IO_RESP_OK);
                    _this->wr_bursts[wide].erase(it);
                    response_stalled = _this->deliver_response(wide, ack, nw);
                }
            }
        }
        else
        {
            // Read response flit: forward it upstream as one distinct
            // allocator-backed beat carrying the flit's payload (the burst
            // request is data-less and never round-tripped as a read beat;
            // the external master copies each beat out, frees it, and frees
            // its own burst request on the last one — initiator-owned
            // convention). The read burst stays master-owned and alive, so
            // dereferencing it here is fine.
            vp::IoReq *burst = req->burst;
            _this->trace.msg(vp::Trace::LEVEL_TRACE,
                "Reducing remaining size of burst (burst: %p, size: %d, req: %p, size %d)\n",
                burst, (int)burst->remaining_size, req, (int)req->get_size());

            uint64_t total = burst->get_size();
            uint64_t offset = total - burst->remaining_size;
            burst->remaining_size -= req->get_size();

            vp::IoReq *beat = _this->rsp_allocator[wide]->alloc();
            beat->prepare();
            beat->set_addr(burst->get_addr() + offset);
            beat->set_size(req->get_size());
            beat->set_is_write(false);
            if (req->get_size() > 0)
            {
                memcpy(beat->get_data(), req->get_data(), req->get_size());
            }
            beat->is_first = offset == 0;
            beat->is_last = burst->remaining_size == 0;
            beat->burst_id = burst->burst_id;
            beat->initiator = burst->initiator;
            beat->set_resp_status(req->get_resp_status());

            if (burst->remaining_size == 0)
            {
                _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Finished burst (burst: %p)\n", burst);
                _this->nb_pending_bursts[wide]--;
            }
            response_stalled = _this->deliver_response(wide, beat, nw);
        }

        _this->fsm_event.enqueue();

        _this->flit_allocator->free(req);
        return response_stalled;
    }
    else
    {
        // Request path: we are the destination NI. Forward to the local target
        // via our external master port.
        _this->trace.msg(vp::Trace::LEVEL_DEBUG,
            "Received request from router (req: %p, base: 0x%lx, size: 0x%lx, isaddr: (%d), "
            "position: (%d, %d)) origin Ni: (%d, %d)\n",
            req, req->get_addr(), req->get_size(), (int)req->is_address, _this->x,
            _this->y, req->src_x, req->src_y);

        if ((req->get_is_write() && !req->is_address) || !req->get_is_write())
        {
            bool wide = req->wide;
            _this->trace.msg(vp::Trace::LEVEL_DEBUG,
                "Sending request to target (req: %p, base: 0x%lx, size: 0x%lx)\n",
                req, req->get_addr(), req->get_size());

            vp::IoReq *to_send = _this->make_target_req(req);

            auto &pending = _this->target_pending[wide];
            if (!pending.empty() || _this->send_to_target(to_send, wide))
            {
                // AR and W arrive on different physical networks. Retain
                // both if one target stalls; never overwrite a denied AR.
                pending.emplace_back(to_send, nw);
                return true;
            }
            return false;
        }
        else
        {
            // Address-only phase of a split write — no actual transfer to do.
            _this->flit_allocator->free(req);
        }
    }

    return false;
}

void NetworkInterfaceV2::link_unstall(vp::Block *__this, int nw)
{
    NetworkInterfaceV2 *_this = (NetworkInterfaceV2 *)__this;
    NetworkQueueV2 *queues[NW_NB] = {&_this->req_queue, &_this->rsp_queue, &_this->wide_queue};
    queues[nw]->unstall();
}


void NetworkInterfaceV2::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    NetworkInterfaceV2 *_this = (NetworkInterfaceV2 *)__this;

    _this->req_queue.check();
    _this->rsp_queue.check();
    _this->wide_queue.check();

    if (!_this->response_queue.empty())
    {
        _this->handle_response((FloonocReqV2 *)_this->response_queue.pop());
    }

    if (_this->wide_read_pending_burst && _this->wide_read_pending_burst_nb_req == 0)
    {
        _this->wide_read_pending_burst = NULL;
        _this->fsm_event.enqueue();
    }

    if (_this->wide_write_pending_burst && _this->wide_write_pending_burst_nb_req == 0)
    {
        _this->wide_write_pending_burst = NULL;
        _this->fsm_event.enqueue();
    }

    if (_this->narrow_read_pending_burst && _this->narrow_read_pending_burst_nb_req == 0)
    {
        _this->narrow_read_pending_burst = NULL;
        _this->fsm_event.enqueue();
    }

    if (_this->narrow_write_pending_burst && _this->narrow_write_pending_burst_nb_req == 0)
    {
        _this->narrow_write_pending_burst = NULL;
        _this->fsm_event.enqueue();
    }

    // v2 deny/retry: if a master was previously denied and we now have capacity
    // (an outstanding burst completed), call retry() once. The master will
    // re-issue when it sees the retry callback. Mirrors the admission gate
    // above: retry as soon as nb_pending_bursts drops below the limit, without
    // waiting for any in-flight fragmentation to drain.
    if (_this->owes_retry_wide_input &&
        _this->nb_pending_bursts[1] < _this->ni_outstanding_reqs)
    {
        _this->owes_retry_wide_input = false;
        _this->wide_input_itf.retry();
    }
    if (_this->owes_retry_narrow_input &&
        _this->nb_pending_bursts[0] < _this->ni_outstanding_reqs)
    {
        _this->owes_retry_narrow_input = false;
        _this->narrow_input_itf.retry();
    }

    _this->response_queue.trigger_next();
}


void NetworkInterfaceV2::handle_response(FloonocReqV2 *req)
{
    if (!req->get_is_write())
    {
        // Read response: forward the data back through the rsp/wide network
        // to the originating NI. handle_rsp allocates a *fresh* FloonocReqV2
        // for the return trip and copies the relevant fields, so the
        // incoming req survives even if we still need it for further beats.
        //
        // io_v2 slaves may answer in three forms — sync DONE, async
        // big-packet (single resp with is_first==is_last==true) or beat
        // stream (N resps reusing the same IoReq, with is_first/is_last
        // mutated per beat and cumulative sizes equal to the request size).
        // For each beat we ship a partial response of req->get_size() bytes;
        // the source NI accumulates ``remaining_size`` and replies to the
        // upstream master when it reaches zero. The FloonocReqV2 is only
        // released once the downstream slave signals is_last — until then
        // it must stay alive for the next beat to mutate.
        req->dest_x = req->src_x;
        req->dest_y = req->src_y;
        if (req->wide)
        {
            this->wide_queue.handle_rsp(req, false);
        }
        else
        {
            this->rsp_queue.handle_rsp(req, false);
        }
    }
    else if (req->get_opcode() != vp::WRITE)
    {
        // Atomic response: atomics keep the classic round-trip on the
        // master's own (still-alive) object — account the data phase and
        // emit the single ack through the mesh once complete.
        vp::IoReq *burst = req->burst;

        this->trace.msg(vp::Trace::LEVEL_TRACE,
            "Reducing remaining size of burst (burst: %p, size: %d, req: %p, size %d)\n",
            burst, (int)burst->remaining_size, req, (int)req->get_size());
        burst->remaining_size -= req->get_size();

        if (burst->remaining_size == 0)
        {
            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Finished burst (burst: %p)\n", burst);
            req->dest_x = req->src_x;
            req->dest_y = req->src_y;
            this->rsp_queue.handle_rsp(req, true);
        }
    }
    else if (req->owns_beat)
    {
        // Write ack for an encapsulated beat: the beat was consumed and
        // freed by the target, so nothing may be dereferenced through
        // req->burst. The flit covered the whole beat, so this ack completes
        // the mini-burst — send the B flit back to the source NI immediately.
        req->dest_x = req->src_x;
        req->dest_y = req->src_y;
        this->rsp_queue.handle_rsp(req, true);
    }
    else
    {
        // Write ack for one wrapped flit of a SPLIT beat: the beat is alive
        // (its buffer backs the beat's other flits; the source frees it on
        // the B) — account this slice on it and emit the single B flit once
        // every slice's wrapper has been consumed.
        vp::IoReq *burst = req->burst;

        this->trace.msg(vp::Trace::LEVEL_TRACE,
            "Reducing remaining size of burst (burst: %p, size: %d, req: %p, size %d)\n",
            burst, (int)burst->remaining_size, req, (int)req->get_size());
        burst->remaining_size -= req->get_size();

        if (burst->remaining_size == 0)
        {
            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Finished burst (burst: %p)\n", burst);
            req->dest_x = req->src_x;
            req->dest_y = req->src_y;
            this->rsp_queue.handle_rsp(req, true);
        }
    }
    // Keep the FloonocReqV2 alive across the beats of a multi-beat resp().
    // Only release it on the last beat — sync DONE and async big-packet
    // both deliver is_last=true on their single resp, so they release
    // immediately as before.
    if (req->is_last)
    {
        this->flit_allocator->free(req);
    }
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new NetworkInterfaceV2(config);
}

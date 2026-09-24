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

#pragma once

#include <vector>
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>

/**
 * Shared definitions of the v2 FlooNoC model.
 *
 * The network is assembled by the Python generator
 * (pulp/floonoc_v2/floonoc_v2.py): routers and network interfaces are
 * standalone components bound together with 'floonoc_link' ports (see
 * floonoc_link_v2.hpp). This header only carries what travels or is shared
 * between them.
 *
 * Nodes are identified by an integer node ID, carried in every flit like the
 * dst_id field of the RTL flit header. How a router turns it into an output
 * port depends on its routing algorithm (see RouterV2):
 * - XY routing (2D mesh): the ID packs the mesh position, see
 *   floonoc_xy_node_id(). Negative IDs are the FlooNocV2Direction values
 *   (-4..-1) of targets reached by leaving the mesh in one direction.
 * - ID table: the ID is an opaque index into the router's routing table.
 */

// Pack a mesh position into an XY-routing node ID. Must match
// xy_node_id() in floonoc_v2.py.
static inline int floonoc_xy_node_id(int x, int y)
{
    return (y << 16) | x;
}

static inline int floonoc_xy_node_x(int node_id)
{
    return node_id & 0xffff;
}

static inline int floonoc_xy_node_y(int node_id)
{
    return node_id >> 16;
}

/**
 * Subclass of v2 vp::IoReq used to carry per-mesh metadata that the v1 model
 * stored in the IoReq arg-stack. v2 has no arg stack, so we attach the data as
 * regular fields on a subclass that the NI allocates and the routers downcast.
 */
class FloonocReqV2 : public vp::IoReq
{
public:
    // Node ID of the destination (see the node ID note at the top).
    int dest_id;
    // Node ID of the source NI. On the request path the destination NI uses
    // it to route the response back; on the response path it is the node the
    // response is coming back to.
    int src_id;
    // True if the request is travelling on the response path (back to the
    // source NI), false on the request path (towards the target).
    bool is_rsp;
    // Pointer back to the external IoReq (from the master) that this internal
    // request belongs to. For a write beat that fits in one W flit (the
    // common, IoV2Beat-signature'd case) the beat is ENCAPSULATED
    // (owns_beat): ownership travels with the flit, and the destination NI
    // hands the beat itself to the local target, which consumes and frees it
    // (write-ack contract) — on the B return path the pointer is then STALE
    // and must not be dereferenced (the ack accounting rides in the flit's
    // own fields: burst_id, initiator, initiator_addr, size). A beat SPLIT
    // across several W flits (oversized beats do reach the NI: a big-packet
    // form write is a legal one-beat burst of any size — e.g. from a
    // collapse adapter, or a beat master with runtime-sized chunks like the
    // traffic generator — and the entry clamp can split when max_burst_size
    // is 0) keeps the legacy scheme instead: the beat stays alive — nobody
    // frees it before its B — its buffer backs all the W flits, and the
    // source NI frees it when its single B flit returns.
    vp::IoReq *burst;
    // True on a W flit that covers its whole beat: the flit carries beat
    // ownership (see `burst` above). Decided at enqueue time on the source
    // side, copied onto the B flit for the return-path accounting.
    bool owns_beat;
    // True if the request travels on the wide network, false for narrow.
    bool wide;
    // True if this is the AR/AW (address) phase of a split request, false if it
    // is the data phase.
    bool is_address;
    // Pre-translation address, used for VCD traces in the routers.
    uint64_t initiator_addr;
    // Payload storage for read-response flits. Read burst requests are
    // data-less (io_v2 beat protocol) and the target's response beats are
    // pooled objects recycled as soon as they are consumed, so a response
    // flit must carry its data slice across the mesh by value — like the RTL
    // chimney's R flits. Write flits keep pointing into the incoming write
    // beat's buffer and leave this empty: the beat travels inside its W flit
    // (ownership moved to the flit at GRANT) and stays unfreed until the
    // destination target consumes it, which keeps the buffer valid exactly
    // as long as the flit references it.
    std::vector<uint8_t> payload;

    // Point this flit's data at its own payload, filled from `data`.
    void set_payload(uint8_t *data, uint64_t size)
    {
        this->payload.assign(data, data + size);
        this->set_data(this->payload.data());
    }
};


/**
 * Pooled allocator for FloonocReqV2 mesh flits, mirroring vp::IoReqAllocator
 * (intrusive freelist through IoReq::next, single-threaded by design). Flits
 * are allocated by the source-side NI queues and released by whichever NI
 * terminates them (destination NI for request flits, source NI for response
 * flits), so the pool is shared across all NIs.
 *
 * Unlike vp::IoReqAllocator, alloc() DOES reinitialize the recycled flit:
 * flits carry per-traversal mesh metadata (framing, is_rsp, burst, wide) and
 * a stale value on any of them is a routing or lifetime bug, so everything is
 * reset to the default-constructed values. The payload vector keeps its
 * capacity across recycles, which is the point: at steady state a
 * read-response flit's by-value data copy costs no allocation.
 *
 * Flits deliberately keep vp::IoReq::allocator == nullptr: they never cross
 * an io_v2 consumer-frees boundary themselves (the write path hands the
 * encapsulated external beat to the target, not the flit), so a misdirected
 * req->free() fails loudly instead of corrupting an engine pool.
 */
class FloonocReqV2Allocator
{
public:
    // Shared pool, created on first use. NIs fetch it once at construction.
    static FloonocReqV2Allocator *get()
    {
        static FloonocReqV2Allocator allocator;
        return &allocator;
    }

    FloonocReqV2 *alloc()
    {
        FloonocReqV2 *req = this->first_free;
        if (req != NULL)
        {
            this->first_free = (FloonocReqV2 *)req->next;
        }
        else
        {
            req = new FloonocReqV2();
        }
        req->prepare();
        req->is_first = true;
        req->is_last = true;
        req->burst_id = -1;
        req->initiator = NULL;
        req->set_data(NULL);
        req->set_second_data(NULL);
        req->burst = NULL;
        req->owns_beat = false;
        req->is_rsp = false;
        req->is_address = false;
        req->wide = false;
        req->initiator_addr = 0;
        req->dest_id = req->src_id = 0;
        return req;
    }

    void free(FloonocReqV2 *req)
    {
        req->next = this->first_free;
        this->first_free = req;
    }

    // Releases the recycled flits at process teardown so they don't show up
    // as leaks.
    ~FloonocReqV2Allocator()
    {
        while (this->first_free != NULL)
        {
            FloonocReqV2 *req = this->first_free;
            this->first_free = (FloonocReqV2 *)req->next;
            delete req;
        }
    }

private:
    FloonocReqV2Allocator() {}

    FloonocReqV2 *first_free = nullptr;
};


/**
 * Memory-map entry: range -> destination node.
 */
class EntryV2
{
public:
    uint64_t base;
    uint64_t size;
    int node_id;
    uint64_t remove_offset;
};

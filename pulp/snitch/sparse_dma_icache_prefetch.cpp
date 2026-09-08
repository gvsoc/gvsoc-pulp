// SPDX-License-Identifier: Apache-2.0
// Instruction-aware L0 prefetch policy from snitch_icache_l0.sv: next line,
// first backward branch, or JAL target. One speculative refill at a time.
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <algorithm>
#include <unordered_map>

class SparseDmaIcachePrefetch : public vp::Component
{
    vp::IoSlave input;
    vp::IoMaster output;
    vp::ClockEvent event;
    std::unordered_map<vp::IoReq *, vp::IoReq *> demands;
    vp::IoReq prefetch;
    uint8_t data[32];
    bool pending = false, wanted = false;
    uint32_t next_address = 0;
    bool fallthrough_wanted = false;
    uint32_t fallthrough_address = 0;
    int64_t ready = -1, wanted_cycle = 0;

    void predict(vp::IoReq *req)
    {
        if (req->get_size() != 32 || req->get_is_write()) return;
        uint32_t address = req->get_addr();
        uint32_t target = address + 32;
        auto bytes = req->get_data();
        for (unsigned offset=0; offset<32; offset+=4)
        {
            uint32_t insn = uint32_t(bytes[offset]) | uint32_t(bytes[offset+1])<<8
                | uint32_t(bytes[offset+2])<<16 | uint32_t(bytes[offset+3])<<24;
            int32_t displacement;
            if ((insn & 0x7f) == 0x6f)
            {
                uint32_t immediate = ((insn>>31)<<20) | (insn & 0xff000)
                    | (((insn>>20)&1)<<11) | (((insn>>21)&0x3ff)<<1);
                displacement = int32_t(immediate << 11) >> 11;
            }
            else if ((insn & 0x7f) == 0x63 && (insn >> 31))
            {
                uint32_t immediate = ((insn>>31)<<12) | (((insn>>7)&1)<<11)
                    | (((insn>>25)&0x3f)<<5) | (((insn>>8)&15)<<1);
                displacement = int32_t(immediate << 19) >> 19;
            }
            else continue;
            target = address + offset + displacement;
            break;
        }
        next_address = target & ~uint32_t(31);
        // The ISS fetches a whole packet, whereas RTL revisits prediction at
        // every instruction PC. Cover the prediction after passing the branch
        // too, so a loop exit can prefetch its following packet during a call.
        // Refills remain serialized; this is a packet-level approximation.
        fallthrough_address = address + 32;
        fallthrough_wanted = fallthrough_address != next_address;
        wanted = true;
        wanted_cycle = clock.get_cycles() + std::max<uint64_t>(1, req->get_full_latency());
        event.enqueue(std::max<int64_t>(1, wanted_cycle-clock.get_cycles()));
    }
    static vp::IoReqStatus request(vp::Block *block, vp::IoReq *req)
    {
        auto self = static_cast<SparseDmaIcachePrefetch *>(block);
        if (req->is_debug()) return self->output.req_forward(req);
        auto child = new vp::IoReq;
        child->prepare();
        child->set_addr(req->get_addr());
        child->set_size(req->get_size());
        child->set_is_write(req->get_is_write());
        child->set_data(req->get_data());
        self->demands.emplace(child, req);
        auto status = self->output.req(child);
        if (status == vp::IO_REQ_OK)
        {
            req->inc_latency(child->get_full_latency());
            self->predict(req);
            self->demands.erase(child);
            delete child;
        }
        else if (status == vp::IO_REQ_INVALID)
        {
            self->demands.erase(child);
            delete child;
        }
        return status == vp::IO_REQ_DENIED ? vp::IO_REQ_PENDING : status;
    }
    static void response(vp::Block *block, vp::IoReq *req)
    {
        auto self = static_cast<SparseDmaIcachePrefetch *>(block);
        if (req == &self->prefetch)
        {
            self->pending = false;
            if (self->wanted) self->event.enqueue();
            return;
        }
        auto parent = self->demands.at(req);
        parent->inc_latency(req->get_full_latency());
        self->predict(parent);
        self->demands.erase(req);
        delete req;
        parent->get_resp_port()->resp(parent);
    }
    static void step(vp::Block *block, vp::ClockEvent *)
    {
        auto self = static_cast<SparseDmaIcachePrefetch *>(block);
        if (self->pending && self->ready >= 0)
        {
            if (self->clock.get_cycles() < self->ready)
            {
                self->event.enqueue(self->ready-self->clock.get_cycles());
                return;
            }
            self->pending = false;
            self->ready = -1;
        }
        if (self->pending || !self->wanted) return;
        if (self->wanted_cycle > self->clock.get_cycles())
        {
            self->event.enqueue(self->wanted_cycle-self->clock.get_cycles());
            return;
        }
        self->prefetch.prepare();
        self->prefetch.set_addr(self->next_address);
        self->prefetch.set_size(32);
        self->prefetch.set_is_write(false);
        self->prefetch.set_data(self->data);
        self->wanted = self->fallthrough_wanted;
        self->next_address = self->fallthrough_address;
        self->fallthrough_wanted = false;
        self->pending = true;
        auto status = self->output.req(&self->prefetch);
        if (status == vp::IO_REQ_OK)
        {
            self->ready = self->clock.get_cycles() + std::max<uint64_t>(1, self->prefetch.get_full_latency());
            self->event.enqueue(self->ready-self->clock.get_cycles());
        }
        else if (status == vp::IO_REQ_INVALID) self->pending = false;
    }
public:
    SparseDmaIcachePrefetch(vp::ComponentConf &config) : vp::Component(config), event(this, step)
    {
        input.set_req_meth(request);
        new_slave_port("input", &input);
        output.set_resp_meth(response);
        new_master_port("output", &output);
    }
};
extern "C" vp::Component *gv_new(vp::ComponentConf &conf) { return new SparseDmaIcachePrefetch(conf); }

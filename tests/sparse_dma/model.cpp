// SPDX-License-Identifier: Apache-2.0
// Exercise the real XDMA frontend and backends with bounded queues and delayed
// AXI/index responses. The checker computes expected bytes independently.
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <cpu/iss/include/offload.hpp>
#include <algorithm>
#include <cstring>
#include <deque>
#include <vector>

class SparseDmaTest : public vp::Component
{
    struct Case { unsigned config, width, reps, size, src_stride, dst_stride; };
    std::vector<Case> cases = {{4,0,13,256,256,256}, {4,1,13,256,256,256},
        {4,2,13,256,256,256}, {4,3,13,256,256,256},
        {0,0,1,300,0,0}, {2,0,7,63,128,80}, {4,1,0,256,256,256},
        {4,1,13,0,256,256}, {0,0,1,0,0,0}, {2,0,0,32,128,80},
        {6,1,13,256,256,256}};
    std::vector<uint8_t> src = std::vector<uint8_t>(65536);
    std::vector<uint8_t> dst = std::vector<uint8_t>(131072, 0xcc);
    vp::WireMaster<IssOffloadInsn<uint32_t> *> offload;
    vp::WireSlave<IssOffloadInsnGrant<uint32_t> *> grant;
    vp::IoSlave axi, tcdm, index;
    vp::ClockEvent event;
    struct Response { int64_t cycle; vp::IoReq *req; bool denied; };
    std::deque<Response> responses;
    unsigned test = 0, op = 0, index_reads = 0, stalls = 0;
    bool blocked = false;
    static unsigned row(unsigned i) { return (i * 19 + (i % 3)) % 97; }
    static uint64_t bias(unsigned n)
    {
        const uint64_t values[] = {128, 32768, uint64_t(1)<<31, uint64_t(1)<<36};
        return n < 4 ? values[n] : 0;
    }
    uint32_t source(unsigned n) { return 0x80000000U + (n == 4 ? 4090 : 0); }
    uint32_t dest(unsigned n) { return 0x100000U + n * 4096; }
    uint32_t indices(unsigned n) { return 0x118000U + n * 256; }
    static vp::IoReqStatus memory(vp::Block *block, vp::IoReq *req, int port)
    {
        auto self = static_cast<SparseDmaTest *>(block);
        uint64_t addr = req->get_addr();
        uint8_t *data = nullptr;
        unsigned salt = 0;
        if (port == 0)
        {
            // Sparse high-address windows distinguish unsigned extraction
            // and full 64-bit address generation from truncation/sign extension.
            for (unsigned n=0; n<4; ++n)
            {
                uint64_t base = 0x80000000ULL + bias(n)*256;
                if (addr >= base && addr + req->get_size() <= base + 32768)
                {
                    addr -= bias(n)*256;
                    salt = (n+1)*29;
                    break;
                }
            }
        }
        if (port == 0 && addr >= 0x80000000 && addr + req->get_size() <= 0x80010000)
            data = self->src.data() + addr - 0x80000000;
        else if (port == 1 && addr + req->get_size() <= self->dst.size())
            data = self->dst.data() + addr;
        else if (port == 2 && addr >= 0x100000 && addr + req->get_size() <= 0x120000)
            data = self->dst.data() + addr - 0x100000;
        else return vp::IO_REQ_INVALID;
        if (req->get_is_write()) memcpy(data, req->get_data(), req->get_size());
        else memcpy(req->get_data(), data, req->get_size());
        if (salt && !req->get_is_write())
            for (uint64_t i=0; i<req->get_size(); ++i) req->get_data()[i] ^= salt;
        if (port == 2) self->index_reads++;
        if (port != 1)
        {
            bool denied = port == 2 && self->index_reads % 3 == 0;
            self->responses.push_back({self->clock.get_cycles() + 7, req, denied});
            return denied ? vp::IO_REQ_DENIED : vp::IO_REQ_PENDING;
        }
        req->inc_latency(2);
        return vp::IO_REQ_OK;
    }
    static void granted(vp::Block *block, IssOffloadInsnGrant<uint32_t> *)
    { static_cast<SparseDmaTest *>(block)->blocked = false; }
    uint32_t insn(unsigned function, uint32_t a=0, uint32_t b=0)
    {
        IssOffloadInsn<uint32_t> req = {};
        req.opcode = (function << 25) | ((b & 31) << 20) | 0x2b;
        req.arg_a = a; req.arg_b = b;
        offload.sync(&req);
        if (!req.granted) { blocked = true; stalls++; }
        return req.result;
    }
    void check()
    {
        for (unsigned n=0; n<cases.size(); ++n)
        {
            const auto &c = cases[n];
            unsigned reps = (c.config & 6) ? c.reps : 1;
            for (unsigned i=0; i<4096; ++i)
            {
                uint8_t expected = 0xcc;
                for (unsigned r=0; r<reps; ++r)
                {
                    unsigned offset = (c.config & 6) ? r*c.dst_stride : 0;
                    if (i >= offset && i < offset+c.size)
                    {
                        unsigned s = source(n)-0x80000000;
                        if (c.config & 4) s += row(r)*c.src_stride;
                        else if (c.config & 2) s += r*c.src_stride;
                        expected = src[s+i-offset];
                        if ((c.config & 4) && n < 4) expected ^= (n+1)*29;
                    }
                }
                if (dst[n*4096+i] != expected)
                {
                    printf("FAIL case=%u byte=%u actual=%u expected=%u\n", n,i,dst[n*4096+i],expected);
                    this->time.get_engine()->quit(1); return;
                }
            }
        }
        // Four widths plus the gather-and-2D-flags precedence case.
        if (index_reads != 30 || stalls == 0)
        {
            printf("FAIL index_reads=%u stalls=%u\n",index_reads,stalls);
            this->time.get_engine()->quit(1); return;
        }
        printf("SPARSE_DMA_TEST_SUCCESS cases=%zu packed_index_reads=%u queue_stalls=%u\n",
            cases.size(),index_reads,stalls);
        this->time.get_engine()->quit(0);
    }
    static void step(vp::Block *block, vp::ClockEvent *)
    {
        auto self = static_cast<SparseDmaTest *>(block);
        auto now = self->clock.get_cycles();
        while (!self->responses.empty() && self->responses.front().cycle <= now)
        {
            auto pending = self->responses.front();
            auto req = pending.req;
            self->responses.pop_front();
            if (pending.denied) req->get_resp_port()->grant(req);
            req->get_resp_port()->resp(req);
        }
        if (now > 100000) { printf("FAIL timeout\n"); self->time.get_engine()->quit(1); return; }
        if (!self->blocked)
        {
            if (self->test == self->cases.size())
            {
                if (self->insn(4,0,2) == 0) { self->check(); return; }
            }
            else
            {
                const auto &c = self->cases[self->test];
                switch (self->op++)
                {
                    case 0: self->insn(0,self->source(self->test)); break;
                    case 1: self->insn(1,self->dest(self->test)); break;
                    case 2: self->insn(8,self->indices(self->test),c.width); break;
                    case 3: self->insn(6,c.src_stride,c.dst_stride); break;
                    case 4: self->insn(7,c.reps); break;
                    case 5: self->insn(2,c.size,c.config); self->op=0; self->test++; break;
                }
            }
        }
        self->event.enqueue();
    }
public:
    SparseDmaTest(vp::ComponentConf &config) : vp::Component(config), event(this,step)
    {
        new_master_port("offload", &offload);
        grant.set_sync_meth(granted); new_slave_port("grant", &grant);
        axi.set_req_meth_muxed(memory,0); new_slave_port("axi", &axi);
        tcdm.set_req_meth_muxed(memory,1); new_slave_port("tcdm", &tcdm);
        index.set_req_meth_muxed(memory,2); new_slave_port("index", &index);
    }
    void reset(bool active) override
    {
        if (!active)
        {
            for (unsigned i=0; i<src.size(); ++i) src[i] = (i*17 + (i>>8)*31 + 7) & 255;
            for (unsigned n=0;n<cases.size();++n)
                if (cases[n].config & 4)
                for (unsigned i=0;i<13;++i)
                    for (unsigned b=0;b<(1U<<cases[n].width);++b)
                        dst[indices(n)-0x100000+(i<<cases[n].width)+b] =
                            ((bias(n)+row(i)) >> (8*b)) & 255;
            event.enqueue();
        }
    }
};
extern "C" vp::Component *gv_new(vp::ComponentConf &conf) { return new SparseDmaTest(conf); }

// SPDX-License-Identifier: Apache-2.0
// Drive the real private frontend/backends, checking bytes, collective payloads,
// legacy status conventions, packed index reads, and asynchronous responses.
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <cpu/iss/include/offload.hpp>
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

class SoftHierOldDmaTest : public vp::Component
{
    struct Case {
        unsigned config, width, reps, size, src_stride, dst_stride, collective;
        uint64_t src, dst, bias;
    };
    struct Response { vp::IoReq *req; unsigned port; bool grant_only; };
    std::vector<Case> cases;
    std::vector<uint8_t> local = std::vector<uint8_t>(0x20000, 0xcc);
    std::vector<uint8_t> external = std::vector<uint8_t>(0x20000, 0xcc);
    std::vector<unsigned> written;
    std::multimap<int64_t, Response> responses;
    vp::WireMaster<IssOffloadInsn<uint32_t> *> offload;
    vp::WireSlave<IssOffloadInsnGrant<uint32_t> *> grant;
    vp::IoSlave axi, tcdm, index;
    vp::ClockEvent event;
    std::string mode;
    unsigned test = 0, op = 0, issued = 0, index_reads = 0, axi_reads = 0;
    unsigned stalls = 0, grants = 0, index_pending = 0, denied_indices = 0;
    uint64_t traffic_hash = 1469598103934665603ULL;
    bool blocked = false, failed = false, gather = false, async_index = false;

    static unsigned row(unsigned i) { return (i*19 + i%3) % 97; }
    static uint8_t pattern(uint64_t address)
    {
        address ^= address >> 30;
        address *= 0xbf58476d1ce4e5b9ULL;
        address ^= address >> 27;
        address *= 0x94d049bb133111ebULL;
        return uint8_t(address ^ (address >> 31));
    }
    uint32_t indices(unsigned n) { return 0x11c000 + n*128; }
    bool indexed(const Case &c) { return gather && (c.config & 4); }
    unsigned reps(const Case &c) { return (c.config & 6) ? c.reps : 1; }
    uint64_t source(const Case &c, unsigned r)
    {
        if (indexed(c)) return c.src + (c.bias + row(r))*c.src_stride;
        return c.src + ((c.config & 2) ? r*c.src_stride : 0);
    }
    void hash(uint64_t value)
    {
        for (unsigned i=0; i<8; ++i) {
            traffic_hash ^= uint8_t(value >> (8*i));
            traffic_hash *= 1099511628211ULL;
        }
    }
    void fail(const char *message)
    {
        printf("SOFT_HIER_OLD_DMA_FAIL mode=%s cycle=%lld %s\n",
               mode.c_str(), clock.get_cycles(), message);
        failed = true;
        time.get_engine()->quit(1);
    }
    int matching_case(uint64_t address, unsigned size, bool write)
    {
        for (unsigned n=0; n<cases.size(); ++n) {
            const auto &c = cases[n];
            for (unsigned r=0; r<reps(c); ++r) {
                uint64_t base = write ? c.dst + ((c.config & 6) ? r*c.dst_stride : 0)
                                      : source(c,r);
                if (address >= base && address+size <= base+c.size) return n;
            }
        }
        return -1;
    }
    static vp::IoReqStatus memory(vp::Block *block, vp::IoReq *req, int port)
    {
        auto self = static_cast<SoftHierOldDmaTest *>(block);
        uint64_t addr = req->get_addr();
        unsigned size = req->get_size();
        bool write = req->get_is_write();
        uint64_t absolute = port == 1 ? addr + 0x100000 : addr;
        int n = port == 2 ? -1 : self->matching_case(absolute,size,write);
        if (port != 2 && n < 0) {
            self->fail("unexpected memory range"); return vp::IO_REQ_INVALID;
        }
        self->hash(self->clock.get_cycles()); self->hash(port);
        self->hash(absolute); self->hash(size); self->hash(write);
        if (port == 0) {
            auto payload = req->get_payload();
            const auto &c = self->cases[n];
            unsigned expected_type = c.collective;
            unsigned expected_row = 0x11 + n;
            unsigned expected_col = 0x51 + n;
            // New gathers are ordinary memory transactions, independent of
            // a previously programmed collective mask.
            if (self->indexed(c)) expected_row = expected_col = 0;
            if (payload[0] != expected_type || payload[1] != expected_row || payload[2] != expected_col) {
                printf("payload case=%d actual=%u/%u/%u expected=%u/%u/%u\n", n,
                    payload[0],payload[1],payload[2],expected_type,expected_row,expected_col);
                self->fail("collective metadata changed");
            }
            self->hash(payload[0]); self->hash(payload[1]); self->hash(payload[2]);
        }
        if (write) {
            uint8_t *dest = nullptr;
            if (port == 1 && addr+size <= self->local.size()) dest = self->local.data()+addr;
            if (port == 0 && addr >= 0x90000000 && addr+size <= 0x90020000)
                dest = self->external.data()+addr-0x90000000;
            if (!dest) return vp::IO_REQ_INVALID;
            memcpy(dest,req->get_data(),size);
            self->written[n] += size;
        } else if (port == 0) {
            for (unsigned i=0; i<size; ++i) req->get_data()[i] = pattern(addr+i);
        } else {
            uint64_t offset = port == 1 ? addr : addr-0x100000;
            if (offset+size > self->local.size()) return vp::IO_REQ_INVALID;
            memcpy(req->get_data(),self->local.data()+offset,size);
        }
        if (port == 2) {
            if (write || size != 8 || (addr & 7) || self->index_pending)
                self->fail("invalid or overlapping index read");
            ++self->index_reads;
            if (!self->async_index) { req->inc_latency(2); return vp::IO_REQ_OK; }
            ++self->index_pending;
            bool denied = self->index_reads%3 == 0;
            if (denied) {
                ++self->denied_indices;
                self->responses.emplace(self->clock.get_cycles()+3,Response{req,2,true});
            }
            self->responses.emplace(self->clock.get_cycles()+7,Response{req,2,false});
            return denied ? vp::IO_REQ_DENIED : vp::IO_REQ_PENDING;
        }
        if (port == 0) {
            // Alternating read delays exercise the original response reorder buffer.
            unsigned delay = write ? 7 : (++self->axi_reads%2 ? 23 : 5);
            self->responses.emplace(self->clock.get_cycles()+delay,Response{req,0,false});
            return vp::IO_REQ_PENDING;
        }
        // The private TCDM backend assumes DMA writes pass immediately.
        return vp::IO_REQ_OK;
    }
    uint32_t insn(unsigned fn, uint32_t a=0, uint32_t b=0, int immediate=-1)
    {
        IssOffloadInsn<uint32_t> req = {};
        req.opcode = (fn<<25) | (((immediate < 0 ? b : immediate)&31)<<20) | 0x2b;
        req.arg_a = a; req.arg_b = b;
        offload.sync(&req);
        if (!req.granted) { blocked = true; ++stalls; }
        return req.result;
    }
    static void granted(vp::Block *block, IssOffloadInsnGrant<uint32_t> *req)
    {
        auto self = static_cast<SoftHierOldDmaTest *>(block);
        if (!self->blocked || req->result != self->issued-1)
            self->fail("legacy grant ID changed");
        self->blocked = false; ++self->grants;
    }
    void check_completion()
    {
        unsigned completed = insn(4,0,0);
        if (completed > issued) { fail("completion count exceeds issued copies"); return; }
        for (unsigned n=0; n<completed; ++n)
            if (written[n] != reps(cases[n])*cases[n].size) {
                fail("completion reported before an older copy finished"); return;
            }
    }
    void check()
    {
        for (unsigned n=0; n<cases.size(); ++n) {
            const auto &c = cases[n];
            const auto &mem = c.dst >= 0x90000000 ? external : local;
            unsigned offset = c.dst - (c.dst >= 0x90000000 ? 0x90000000 : 0x100000);
            for (unsigned i=0; i<4096; ++i) {
                uint8_t expected = 0xcc;
                for (unsigned r=0; r<reps(c); ++r) {
                    unsigned d = (c.config & 6) ? r*c.dst_stride : 0;
                    if (i >= d && i < d+c.size) expected = pattern(source(c,r)+i-d);
                }
                if (mem[offset+i] != expected) {
                    printf("case=%u byte=%u got=%u expected=%u\n",n,i,mem[offset+i],expected);
                    fail("data or destination padding mismatch"); return;
                }
            }
        }
        unsigned expected_reads = 0;
        for (const auto &c : cases)
            if (indexed(c) && c.size) expected_reads += (c.reps*(1U<<c.width)+7)/8;
        if (index_reads != expected_reads || (gather && !stalls) || grants != stalls || !responses.empty()) {
            fail("index traffic, stalls, or response accounting mismatch"); return;
        }
        if (insn(4,0,0) != cases.size() || insn(4,0,1) != cases.size()+1 || insn(4,0,3) != 0) {
            fail("legacy final status convention changed"); return;
        }
        printf("SOFT_HIER_OLD_DMA_SUCCESS mode=%s cases=%zu cycles=%lld index_reads=%u "
               "denied_indices=%u stalls=%u grants=%u traffic=%016llx\n", mode.c_str(),
               cases.size(),clock.get_cycles(),index_reads,denied_indices,stalls,grants,
               (unsigned long long)traffic_hash);
        time.get_engine()->quit(0);
    }
    static void step(vp::Block *block, vp::ClockEvent *)
    {
        auto self = static_cast<SoftHierOldDmaTest *>(block);
        auto now = self->clock.get_cycles();
        while (!self->responses.empty() && self->responses.begin()->first <= now) {
            auto response = self->responses.begin()->second;
            self->responses.erase(self->responses.begin());
            if (response.grant_only) response.req->get_resp_port()->grant(response.req);
            else {
                if (response.port == 2) --self->index_pending;
                response.req->get_resp_port()->resp(response.req);
            }
        }
        if (self->failed) return;
        if (now > 100000) { self->fail("timeout"); return; }
        self->check_completion();
        if (self->failed) return;
        if (!self->blocked) {
            if (self->test == self->cases.size()) {
                if (self->insn(4,0,2) == 0) { self->check(); return; }
            } else {
                unsigned n = self->test;
                const auto &c = self->cases[n];
                // Legacy source changes and collective metadata are checked
                // with the existing software convention of waiting for idle.
                // Gathers remain queued, including empty descriptors.
                if (self->op == 0 && n > 0
                    && (!self->indexed(c) || !self->indexed(self->cases[n-1]))
                    && self->insn(4,0,2)) {
                    self->event.enqueue(); return;
                }
                switch (self->op++) {
                    case 0: self->insn(0,uint32_t(c.src),c.src>>32); break;
                    case 1: self->insn(1,uint32_t(c.dst),c.dst>>32); break;
                    case 2: self->insn(8,self->indices(n),c.width); break;
                    case 3: self->insn(6,c.src_stride,c.dst_stride); break;
                    case 4: self->insn(7,c.reps); break;
                    case 5: {
                        uint32_t mask = ((0x51+n)<<16) | (0x11+n);
                        if (self->insn(5,0,mask) != mask) self->fail("dmmask return value changed");
                        break;
                    }
                    case 6: {
                        unsigned result = c.collective ? self->insn(3,c.size,4,c.collective)
                                                       : self->insn(2,c.size,c.config);
                        if (result != self->issued++) self->fail("legacy allocation ID changed");
                        self->op=0; ++self->test;
                        break;
                    }
                }
            }
        }
        self->event.enqueue();
    }
public:
    SoftHierOldDmaTest(vp::ComponentConf &config) : vp::Component(config), event(this,step)
    {
        mode = get_js_config()->get("mode")->get_str();
        gather = mode.find("gather") == 0; async_index = mode == "gather-async";
        if (!gather) {
            cases = {{0,0,1,300,0,0,0}, {2,0,7,63,128,80,0},
                     {0,0,1,256,0,0,0}, {0,0,1,192,0,0,0}, {0,0,1,256,0,0,0},
                     {0,0,1,1024,0,0,1}, {0,0,1,1024,0,0,4},
                     {0,0,1,1024,0,0,7}, {0,0,1,1024,0,0,31}};
        } else {
            cases = {{4,0,13,256,256,256,0}, {4,1,0,256,256,256,0},
                     {4,1,13,256,256,272,0}, {4,2,13,63,128,80,0},
                     {4,3,13,256,512,256,0}, {4,1,13,0,256,256,0},
                     {6,1,13,256,256,256,0}, {0,0,1,300,0,0,0},
                     {2,0,7,63,128,80,0}, {0,0,1,256,0,0,4},
                     {4,3,1,128,256,128,0}, {4,0,19,16,16,32,0}};
        }
        const uint64_t biases[] = {128,32768,uint64_t(1)<<31,uint64_t(1)<<36};
        for (unsigned n=0; n<cases.size(); ++n) {
            auto &c = cases[n];
            c.src = 0x80000000ULL + n*65536;
            c.dst = 0x100000 + n*4096;
            c.bias = indexed(c) ? biases[c.width] : 0;
        }
        if (!gather) {
            cases[0].src += 4090;
            cases[2].src = 0x118000; cases[2].dst = 0x90002000;
            cases[3].src = 0x118200;
            cases[4].dst = 0x90004000;
        } else cases[7].src += 4090;
        written.resize(cases.size());
        new_master_port("offload",&offload);
        grant.set_sync_meth(granted); new_slave_port("grant",&grant);
        axi.set_req_meth_muxed(memory,0); new_slave_port("axi",&axi);
        tcdm.set_req_meth_muxed(memory,1); new_slave_port("tcdm",&tcdm);
        index.set_req_meth_muxed(memory,2); new_slave_port("index",&index);
    }
    void reset(bool active) override
    {
        if (!active) {
            for (unsigned i=0x18000; i<0x1c000; ++i) local[i] = pattern(0x100000+i);
            for (unsigned n=0; n<cases.size(); ++n) {
                const auto &c = cases[n];
                if (!indexed(c)) continue;
                for (unsigned i=0; i<c.reps; ++i)
                    for (unsigned b=0; b<(1U<<c.width); ++b)
                        local[indices(n)-0x100000+(i<<c.width)+b] =
                            uint8_t((c.bias+row(i))>>(8*b));
            }
            event.enqueue();
        }
    }
};
extern "C" vp::Component *gv_new(vp::ComponentConf &conf) { return new SoftHierOldDmaTest(conf); }

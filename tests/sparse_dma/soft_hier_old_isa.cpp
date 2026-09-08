// SPDX-License-Identifier: Apache-2.0
// A small RV32 program runs on the actual private Snitch core. The memory
// checker validates DMIDX decoding, DMCPYI gather, status values, and a later
// DMCPY collective using the DMMASK value programmed before the gather.
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <cstring>
#include <vector>

class SoftHierOldDmaIsaTest : public vp::Component
{
    vp::IoSlave fetch, data, axi, tcdm, index;
    vp::ClockEvent watchdog;
    std::vector<uint8_t> local = std::vector<uint8_t>(4096, 0xcc);
    std::vector<uint8_t> program;
    unsigned checks = 0, index_reads = 0;
    static uint8_t pattern(uint64_t addr) { return uint8_t(addr ^ (addr >> 4)); }
    void emit(uint32_t word)
    {
        for (unsigned b=0; b<4; ++b) program.push_back(uint8_t(word >> (8*b)));
    }
    void constant(unsigned reg, uint32_t value)
    {
        emit(((value+0x800) & 0xfffff000) | (reg<<7) | 0x37); // lui
        emit(((value & 0xfff)<<20) | (reg<<15) | (reg<<7) | 0x13); // addi
    }
    void xdma(unsigned fn, unsigned rd, unsigned rs1, unsigned rs2)
    { emit((fn<<25) | (rs2<<20) | (rs1<<15) | (rd<<7) | 0x2b); }
    void wait_idle()
    {
        xdma(4,14,0,2); // dmstati x14, 2
        emit(0xfe071ee3); // bne x14, x0, -4
    }
    void store(unsigned reg, unsigned offset)
    { emit((reg<<20) | (16<<15) | (2<<12) | (offset<<7) | 0x23); }
    void fail(const char *reason)
    {
        printf("SOFT_HIER_OLD_DMA_ISA_FAIL %s\n", reason);
        time.get_engine()->quit(1);
    }
    static void timeout(vp::Block *block, vp::ClockEvent *)
    { static_cast<SoftHierOldDmaIsaTest *>(block)->fail("CPU program timed out"); }
    void check()
    {
        const unsigned selected[] = {3,1,7};
        for (unsigned i=0; i<local.size(); ++i) {
            uint8_t expected = 0xcc;
            if (i < 48) expected = pattern(0x80000000 + selected[i/16]*128 + i%16);
            if (i >= 0x200 && i < 0x220) expected = pattern(0x80001000 + i-0x200);
            if (i >= 0x400 && i < 0x406)
                expected = ((i-0x400)&1) ? 0 : selected[(i-0x400)/2];
            if (local[i] != expected) { fail("gather, collective, or padding bytes"); return; }
        }
        if (index_reads != 1) { fail("packed index count"); return; }
        printf("SOFT_HIER_OLD_DMA_ISA_SUCCESS copies=2 index_reads=1 checks=%u cycles=%lld\n",
            checks, clock.get_cycles());
        time.get_engine()->quit(0);
    }
    static vp::IoReqStatus memory(vp::Block *block, vp::IoReq *req, int port)
    {
        auto self = static_cast<SoftHierOldDmaIsaTest *>(block);
        uint64_t addr = req->get_addr(), size = req->get_size();
        bool write = req->get_is_write();
        if (port == 0 && !write && addr >= 0x20000000
            && addr+size <= 0x20000000+self->program.size()) {
            memcpy(req->get_data(), self->program.data()+addr-0x20000000, size);
            return vp::IO_REQ_OK;
        }
        if (port == 1 && write && size == 4) {
            const uint32_t expected[] = {0x78125634,0,1,1,2,3,0x55};
            uint32_t value = 0;
            for (unsigned b=0; b<4; ++b) value |= uint32_t(req->get_data()[b]) << (8*b);
            if (self->checks >= 7 || addr != 0x40000000+self->checks*4
                || value != expected[self->checks]) self->fail("instruction return value");
            else if (++self->checks == 7) self->check();
            return vp::IO_REQ_OK;
        }
        if (port == 2 && !write) {
            auto payload = req->get_payload();
            bool collective = addr >= 0x80001000 && addr+size <= 0x80001020;
            if (payload[0] != (collective ? 4 : 0)
                || payload[1] != (collective ? 0x34 : 0)
                || payload[2] != (collective ? 0x12 : 0))
                self->fail("legacy collective type or saved mask");
            for (unsigned i=0; i<size; ++i) req->get_data()[i] = pattern(addr+i);
            req->inc_latency(7);
            return vp::IO_REQ_OK;
        }
        if (port == 4) {
            if (addr != 0x100400 || size != 8 || write) self->fail("DMIDX arguments");
            ++self->index_reads;
            addr -= 0x100000;
            req->inc_latency(2);
        }
        if ((port == 3 || port == 4) && addr+size <= self->local.size()) {
            auto bytes = self->local.data()+addr;
            if (write) memcpy(bytes, req->get_data(), size);
            else memcpy(req->get_data(), bytes, size);
            return vp::IO_REQ_OK;
        }
        self->fail("unexpected CPU or DMA memory request");
        return vp::IO_REQ_INVALID;
    }
public:
    SoftHierOldDmaIsaTest(vp::ComponentConf &conf) : vp::Component(conf), watchdog(this,timeout)
    {
        constant(5,0x80000000); constant(6,0x100000); constant(7,0x100400);
        constant(8,128); constant(9,16); constant(10,3);
        constant(12,0x78125634); constant(15,16); constant(16,0x40000000);
        xdma(5,11,0,12); // dmmask: retain its return value across the gather
        xdma(0,0,5,0); xdma(1,0,6,0); xdma(6,0,8,9); xdma(7,0,10,0);
        xdma(8,0,7,1); // dmidx: packed unsigned 16-bit indices
        xdma(2,13,15,4); // dmcpyi: gather flag
        wait_idle(); store(11,0); store(13,4);
        xdma(4,14,0,0); store(14,8);
        constant(5,0x80001000); constant(6,0x100200); constant(15,32);
        constant(4,4);
        xdma(0,0,5,0); xdma(1,0,6,0);
        xdma(3,13,15,4); // original collective type 4, with no second dmmask
        wait_idle(); store(13,12);
        xdma(4,14,0,0); store(14,16);
        xdma(4,14,0,1); store(14,20);
        constant(14,0x55); store(14,24);
        emit(0x0000006f); // stop fetching new code while the host checks the result
        for (unsigned i=0; i<16; ++i) emit(0x00000013); // prefetch padding
        const unsigned selected[] = {3,1,7};
        for (unsigned i=0; i<3; ++i) {
            local[0x400+2*i] = selected[i]; local[0x401+2*i] = 0;
        }
        vp::IoSlave *ports[] = {&fetch,&data,&axi,&tcdm,&index};
        const char *names[] = {"fetch","data","axi","tcdm","index"};
        for (unsigned i=0; i<5; ++i) {
            ports[i]->set_req_meth_muxed(memory,i); new_slave_port(names[i],ports[i]);
        }
    }
    void reset(bool active) override { if (!active) watchdog.enqueue(10000); }
};
extern "C" vp::Component *gv_new(vp::ComponentConf &conf) { return new SoftHierOldDmaIsaTest(conf); }

// SPDX-License-Identifier: Apache-2.0
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <array>
#include <vector>
#include <map>
#include <unordered_map>
#include <cstring>

class BridgeTest : public vp::Component
{
public:
    BridgeTest(vp::ComponentConf &config) : vp::Component(config), event(this, &tick)
    {
        traces.new_trace("trace", &trace, vp::DEBUG);
        for (int i=0; i<3; ++i)
        {
            out[i].set_resp_meth(&response);
            new_master_port("out_" + std::to_string(i), &out[i]);
        }
        memory.set_req_meth(&access);
        new_slave_port("memory", &memory);
        probe_done.set_sync_meth(&probe);
        new_slave_port("probe_done", &probe_done);
    }
    void reset(bool active) override { if (!active) event.enqueue(); }
private:
    static constexpr uint64_t base = 0x180000000ULL;
    struct Job { vp::IoReq req; std::vector<uint8_t> data; uint64_t offset; bool invalid; };
    static uint8_t value(uint64_t offset) { return (offset * 37 + (offset >> 8) + 13) & 255; }
    void issue(unsigned port, uint64_t offset, unsigned size, bool write, bool invalid=false)
    {
        auto *job = new Job{};
        job->offset=offset; job->invalid=invalid; job->data.resize(size,0);
        if (write) for (unsigned i=0; i<size; ++i) job->data[i]=value(offset+i);
        job->req.init(); job->req.set_addr(base+offset); job->req.set_size(size);
        job->req.set_data(job->data.data()); job->req.set_is_write(write);
        jobs.emplace(&job->req,job);
        if (out[port].req(&job->req) != vp::IO_REQ_PENDING)
            trace.fatal("Expected asynchronous legacy bridge completion\n");
    }
    static void response(vp::Block *block, vp::IoReq *req)
    {
        auto *self=static_cast<BridgeTest *>(block);
        auto it=self->jobs.find(req);
        if (it==self->jobs.end()) self->trace.fatal("Duplicate/unknown response\n");
        Job *job=it->second;
        if ((req->status==vp::IO_REQ_INVALID) != job->invalid)
            self->trace.fatal("Wrong response status\n");
        if (!job->invalid && !req->get_is_write())
            for (unsigned i=0;i<job->data.size();++i)
                if (job->data[i]!=value(job->offset+i))
                    self->trace.fatal("Read mismatch at 0x%lx: %x != %x\n",
                        job->offset+i,job->data[i],value(job->offset+i));
        self->jobs.erase(it); delete job; self->completed++;
        self->event.enqueue();
    }
    static vp::IoReqStatus access(vp::Block *block, vp::IoReq *req)
    {
        auto *self=static_cast<BridgeTest *>(block);
        if (req->get_addr()+req->get_size()>self->data.size()) return vp::IO_REQ_INVALID;
        bool denied=(self->accesses++ % 3)==0;
        // Deliberately finish neighbouring beats out of order, and exercise
        // v1's queued DENIED/grant semantics (no master resubmission).
        int delay=20+(3-((req->get_addr()/128)%4))*7;
        self->pending.emplace(self->clock.get_cycles()+delay,std::make_pair(req,denied));
        self->event.enqueue();
        return denied ? vp::IO_REQ_DENIED : vp::IO_REQ_PENDING;
    }
    static void probe(vp::Block *block,bool done)
    {
        auto *self=static_cast<BridgeTest *>(block);
        self->native_done=done; self->event.enqueue();
    }
    static void tick(vp::Block *block,vp::ClockEvent *)
    {
        auto *self=static_cast<BridgeTest *>(block);
        if (self->clock.get_cycles()>200000) self->trace.fatal("Bridge regression timed out\n");
        while (!self->pending.empty() && self->pending.begin()->first<=self->clock.get_cycles())
        {
            auto item=self->pending.begin()->second;
            self->pending.erase(self->pending.begin());
            vp::IoReq *req=item.first;
            if (req->get_is_write()) std::memcpy(self->data.data()+req->get_addr(),req->get_data(),req->get_size());
            else std::memcpy(req->get_data(),self->data.data()+req->get_addr(),req->get_size());
            req->status=vp::IO_REQ_OK;
            if (item.second) req->get_resp_port()->grant(req);
            req->get_resp_port()->resp(req);
        }
        if (self->jobs.empty())
        {
            switch (self->stage++)
            {
                case 0:
                    self->issue(0,0xff0,65536,true);
                    self->issue(1,0x14000,4096,true);
                    self->issue(2,0x16000,4096,true);
                    break;
                case 1:
                    self->issue(0,0xff0,65536,false);
                    self->issue(1,0x14000,4096,false);
                    self->issue(2,0x16000,4096,false);
                    break;
                case 2:
                    for (unsigned i=0;i<48;++i)
                        self->issue(i%3,0x14000+(i%16)*128,128,(i%2)==0);
                    self->issue(0,0x30000,128,false,true);
                    self->issue(1,0x14000,0,false);
                    break;
                default:
                    if (self->native_done)
                    {
                        printf("SOFTHIER_NOC_V2_PASS jobs=%u target_beats=%u high_address=1 fanin=1 large_unaligned=1 mixed_stalls=1 invalid=1 response_retry=1\n",self->completed,self->accesses);
                        self->time.get_engine()->quit(0);
                        return;
                    }
            }
        }
        self->event.enqueue();
    }
    vp::IoMaster out[3];
    vp::IoSlave memory;
    vp::WireSlave<bool> probe_done;
    vp::ClockEvent event;
    vp::Trace trace;
    std::array<uint8_t,0x20000> data{};
    std::unordered_map<vp::IoReq *,Job *> jobs;
    std::multimap<int64_t,std::pair<vp::IoReq *,bool>> pending;
    unsigned stage=0, completed=0, accesses=0;
    bool native_done=false;
};
extern "C" vp::Component *gv_new(vp::ComponentConf &config) { return new BridgeTest(config); }

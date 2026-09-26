// SPDX-License-Identifier: Apache-2.0
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <array>
#include <vector>
#include <map>
#include <unordered_map>
#include <cstring>

// Independent integer/FP16-exact goldens, with real v1 target denial and
// intentionally reordered completions. Every source shares the same mesh.
class CollectiveProbe : public vp::Component
{
public:
    CollectiveProbe(vp::ComponentConf &config) : vp::Component(config), event(this, &tick)
    {
        traces.new_trace("trace", &trace, vp::DEBUG);
        for (int n = 0; n < 16; ++n)
        {
            out[n].set_resp_meth(&response);
            out[n].set_grant_meth(&grant);
            new_master_port("out_" + std::to_string(n), &out[n]);
            mem[n].set_req_meth_muxed(&access, n);
            new_slave_port("mem_" + std::to_string(n), &mem[n]);
            data[n].resize(0x10000, 0xa5);
        }
    }
    void reset(bool active) override { if (!active) event.enqueue(); }
private:
    struct Job
    {
        vp::IoReq req;
        std::vector<uint8_t> buffer, expected;
        std::array<uint64_t, 16> visits{};
        int root, type, row, col;
        uint64_t offset, size;
        bool error, bad_input, subnormal;
    };
    struct Pending { vp::IoReq *req; int node; bool denied; };
    static void grant(vp::Block *, vp::IoReq *) {}
    bool selected(Job *j, int n)
    {
        return ((n % 4) & j->row) == ((j->root % 4) & j->row) &&
               ((n / 4) & j->col) == ((j->root / 4) & j->col);
    }
    static uint16_t half_integer(int value)
    {
        uint16_t sign = value < 0 ? 0x8000 : 0;
        unsigned magnitude = value < 0 ? -value : value;
        if (!magnitude) return sign;
        unsigned exponent = 0;
        while ((1u << (exponent + 1)) <= magnitude) ++exponent;
        return sign | ((exponent + 15) << 10) | ((magnitude - (1u << exponent)) << (10 - exponent));
    }
    uint16_t value(Job *j, int n, uint64_t word)
    {
        if (j->subnormal) return 1;
        switch (j->type)
        {
            case 2: case 5: return uint16_t(60000 + n * 17 + word % 7);
            case 3: case 6: return uint16_t(-250 - n * 2 + word % 7);
            case 4: return half_integer(n + 1 + word % 4);
            case 7: return half_integer(-n - 1 - int(word % 4));
        }
        return 0;
    }
    void issue(int root, int type, int row, int col, uint64_t offset, unsigned size,
               bool error=false, bool bad_input=false, bool subnormal=false)
    {
        auto *j = new Job{};
        j->root=root; j->type=type; j->row=row; j->col=col; j->offset=offset;
        j->size=size; j->error=error; j->bad_input=bad_input; j->subnormal=subnormal;
        // Nonzero destination poison catches accidental seed/double counting.
        j->buffer.resize(size, 0x5a); j->expected.resize(size);
        if (type == 1)
        {
            for (unsigned i=0; i<size; ++i) j->buffer[i] = (i * 37 + root * 13) & 255;
            j->expected = j->buffer;
            for (auto &m : data) std::memset(m.data()+offset, 0xa5, size);
        }
        else
        {
            for (unsigned i=0; i+1<size; i+=2)
            {
                unsigned sum=0, maxu=0, count=0;
                int maxi=-32768, exact=0, maxf=-10000;
                for (int n=0; n<16; ++n)
                {
                    uint16_t v=value(j,n,i/2);
                    std::memcpy(data[n].data()+offset+i,&v,2);
                    if (!selected(j,n)) continue;
                    ++count; sum+=v; maxu=std::max(maxu,unsigned(v));
                    maxi=std::max(maxi,int(int16_t(v)));
                    exact+=n+1+(i/2)%4; maxf=std::max(maxf,-n-1-int((i/2)%4));
                }
                uint16_t want = subnormal ? count : type<=3 ? uint16_t(sum) :
                    type==4 ? half_integer(exact) : type==5 ? maxu :
                    type==6 ? uint16_t(maxi) : half_integer(maxf);
                std::memcpy(j->expected.data()+i,&want,2);
            }
        }
        j->req.init(); j->req.set_addr(0x30000000ULL + root*0x10000 + offset);
        j->req.set_size(size); j->req.set_is_write(type==1);
        j->req.set_data(j->buffer.data());
        j->req.get_payload()[0]=type;
        j->req.get_payload()[1]=row;
        j->req.get_payload()[2]=col;
        jobs.emplace(&j->req,j);
        ++issued;
        auto status=out[root].req(&j->req);
        if (status==vp::IO_REQ_OK || status==vp::IO_REQ_INVALID)
        {
            j->req.status=status;
            response(this,&j->req);
        }
    }
    static void response(vp::Block *block, vp::IoReq *req)
    {
        auto *self=static_cast<CollectiveProbe *>(block);
        auto it=self->jobs.find(req);
        if (it==self->jobs.end()) self->trace.fatal("Duplicate completion\n");
        auto *j=it->second;
        if ((req->status==vp::IO_REQ_INVALID)!=j->error)
            self->trace.fatal("Collective status mismatch type=%d stage=%d\n",j->type,self->stage);
        if (!j->bad_input)
        {
            for (int n=0;n<16;++n)
            {
                uint64_t expected=self->selected(j,n)?j->size:0;
                if (j->visits[n]!=expected)
                    self->trace.fatal("Participant/completion mismatch node=%d bytes=%lu expected=%lu type=%d\n",
                        n,j->visits[n],expected,j->type);
                if (j->type==1)
                    for (unsigned i=0;i<j->size;++i)
                    {
                        uint8_t want=self->selected(j,n)?j->expected[i]:0xa5;
                        if (self->data[n][j->offset+i]!=want)
                            self->trace.fatal("Broadcast mismatch node=%d byte=%u\n",n,i);
                    }
            }
            if (j->type!=1 && !j->error && j->buffer!=j->expected)
            {
                for (unsigned i=0;i<j->size;++i)
                    if (j->buffer[i]!=j->expected[i])
                        self->trace.fatal("Reduction mismatch type=%d byte=%u got=%u expected=%u stage=%d\n",
                            j->type,i,j->buffer[i],j->expected[i],self->stage);
            }
        }
        self->jobs.erase(it); delete j; ++self->completed;
        self->event.enqueue();
    }
    static vp::IoReqStatus access(vp::Block *block, vp::IoReq *req, int node)
    {
        auto *self=static_cast<CollectiveProbe *>(block);
        if (req->get_addr()+req->get_size()>0x10000) return vp::IO_REQ_INVALID;
        bool denied=(++self->accesses % 3)==0;
        uint64_t delay=2+(node*7+req->get_addr()/128*3)%19;
        if (node==15) delay+=43;
        self->pending.emplace(self->clock.get_cycles()+delay,Pending{req,node,denied});
        self->event.enqueue();
        return denied?vp::IO_REQ_DENIED:vp::IO_REQ_PENDING;
    }
    static void tick(vp::Block *block, vp::ClockEvent *)
    {
        auto *self=static_cast<CollectiveProbe *>(block);
        if (self->clock.get_cycles()>2000000)
            self->trace.fatal("Collective timeout stage=%d pending_jobs=%zu\n",self->stage,self->jobs.size());
        while (!self->pending.empty() && self->pending.begin()->first<=self->clock.get_cycles())
        {
            auto p=self->pending.begin()->second;
            self->pending.erase(self->pending.begin());
            auto *req=p.req;
            uint64_t offset=req->get_addr(), size=req->get_size();
            for (auto item:self->jobs)
            {
                auto *j=item.second;
                if (offset>=j->offset && offset+size<=j->offset+j->size) j->visits[p.node]+=size;
            }
            if (req->get_is_write()) std::memcpy(self->data[p.node].data()+offset,req->get_data(),size);
            else std::memcpy(req->get_data(),self->data[p.node].data()+offset,size);
            req->status=(p.node==15 && offset==0x7f00)?vp::IO_REQ_INVALID:vp::IO_REQ_OK;
            if (p.denied) req->get_resp_port()->grant(req);
            req->get_resp_port()->resp(req);
        }
        if (self->jobs.empty())
        {
            int stage=self->stage++;
            if (stage==0) self->issue(5,1,0,3,0xff7,8257);
            else if (stage==1) self->issue(10,1,3,0,0x3001,4103);
            else if (stage==2) self->issue(9,1,0,0,0x5000,512);
            else if (stage==3) self->issue(10,1,1,1,0x5800,256);
            else if (stage==4) self->issue(15,1,3,3,0x5900,256);
            else if (stage<11) self->issue(5,stage-3,0,0,0x6000,512);
            else if (stage==11) self->issue(5,4,0,0,0x6000,128,false,false,true);
            else if (stage==12) self->issue(5,2,0,0,0x7f00,128,true);
            else if (stage==13)
                for (int n=0;n<16;++n) self->issue(n,n%7+1,0,0,0x8000+n*0x800,2048);
            else if (stage==14) self->issue(0,2,0,0,0x4001,1,true,true);
            else
            {
                printf("SOFTHIER_COLLECTIVE_PASS jobs=%u target_beats=%u cycles=%ld masks=1 broadcast=1 all_reductions=1 subnormal=1 concurrent=1 stalls=1 errors=1\n",
                    self->completed,self->accesses,self->clock.get_cycles());
                self->time.get_engine()->quit(0);
                return;
            }
        }
        self->event.enqueue();
    }
    vp::Trace trace;
    vp::ClockEvent event;
    vp::IoMaster out[16];
    vp::IoSlave mem[16];
    std::array<std::vector<uint8_t>,16> data;
    std::unordered_map<vp::IoReq *,Job *> jobs;
    std::multimap<int64_t,Pending> pending;
    unsigned stage=0, issued=0, completed=0, accesses=0;
};

extern "C" vp::Component *gv_new(vp::ComponentConf &config) { return new CollectiveProbe(config); }

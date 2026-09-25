// SPDX-License-Identifier: Apache-2.0
#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/itf/wire.hpp>

class ResponseProbe : public vp::Component
{
public:
    ResponseProbe(vp::ComponentConf &config) : vp::Component(config),
        out(&retry,&response), event(this,&tick)
    {
        traces.new_trace("trace",&trace,vp::DEBUG);
        new_master_port("output",&out); new_master_port("done",&done);
    }
    void reset(bool active) override { if (!active) event.enqueue(); }
private:
    void send()
    {
        waiting=out.req(request)==vp::IO_REQ_DENIED;
    }
    static void retry(vp::Block *block,vp::IoRetryChannel)
    {
        auto *self=static_cast<ResponseProbe *>(block);
        if (self->waiting) self->send();
    }
    static vp::IoRespAck response(vp::Block *block,vp::IoReq *beat)
    {
        auto *self=static_cast<ResponseProbe *>(block);
        if (!self->accept)
        {
            self->blocked=true;
            self->event.enqueue(11);
            return vp::IO_RESP_DENIED;
        }
        if (beat->get_resp_status()!=vp::IO_RESP_OK) self->trace.fatal("Probe response error\n");
        for (unsigned i=0;i<beat->get_size();++i)
            if (beat->get_data()[i]!=0) self->trace.fatal("Probe read data mismatch\n");
        self->bytes+=beat->get_size();
        bool last=beat->is_last;
        beat->free();
        if (last)
        {
            if (self->bytes!=4096 || !self->blocked) self->trace.fatal("Probe coverage missing\n");
            self->request->free();
            self->done.sync(true);
        }
        return vp::IO_RESP_ACCEPTED;
    }
    static void tick(vp::Block *block,vp::ClockEvent *)
    {
        auto *self=static_cast<ResponseProbe *>(block);
        if (self->blocked)
        {
            self->accept=true;
            self->out.resp_retry();
        }
        else
        {
            auto *req=vp::IoReqAllocator::get(0)->alloc();
            req->prepare(); req->set_size(4096);
            req->set_addr(0x180000000ULL + 0x18000);
            req->set_data(nullptr);req->set_is_write(false);
            req->is_first=req->is_last=true;req->burst_id=7;req->initiator=self;
            self->request=req;self->send();
        }
    }
    vp::IoMaster out;
    vp::WireMaster<bool> done;
    vp::ClockEvent event;
    vp::Trace trace;
    vp::IoReq *request=nullptr;
    bool waiting=false, blocked=false, accept=false;
    unsigned bytes=0;
};
extern "C" vp::Component *gv_new(vp::ComponentConf &config) { return new ResponseProbe(config); }

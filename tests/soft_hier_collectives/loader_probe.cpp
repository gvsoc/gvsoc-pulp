// SPDX-License-Identifier: Apache-2.0
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>

class LoaderProbe : public vp::Component
{
public:
    LoaderProbe(vp::ComponentConf &config) : vp::Component(config), event(this, &tick)
    {
        traces.new_trace("trace", &trace, vp::DEBUG);
        input.set_req_meth(&request); done.set_sync_meth(&finished);
        new_slave_port("input", &input); new_slave_port("done", &done);
    }
private:
    static vp::IoReqStatus request(vp::Block *block, vp::IoReq *req)
    {
        auto *self=static_cast<LoaderProbe *>(block);
        if (self->pending) self->trace.fatal("Loader reused a pending request\n");
        for (int i=0;i<req->get_payload_size();++i)
            if (req->get_payload()[i]) self->trace.fatal("Loader leaked optional payload byte %d\n",i);
        self->pending=req; self->event.enqueue(11);
        return vp::IO_REQ_PENDING;
    }
    static void tick(vp::Block *block, vp::ClockEvent *)
    {
        // Force reuse of event-handler stack storage before the deferred copy.
        volatile uint8_t overwrite[65536];
        for (unsigned i=0;i<sizeof(overwrite);++i) overwrite[i]=0xa5;
        auto *self=static_cast<LoaderProbe *>(block);
        auto *req=self->pending;
        for (uint64_t i=0;i<req->get_size();++i)
        {
            uint64_t offset=req->get_addr()+i-0x1000;
            uint8_t expected=offset<16?offset:0;
            if (req->get_data()[i]!=expected) self->trace.fatal("Async loader data mismatch at %lu\n",offset);
        }
        self->bytes+=req->get_size(); ++self->chunks;
        // Reused requests must clear payload even after a target writes it.
        req->get_payload()[0]=4;
        self->pending=nullptr;
        req->status=vp::IO_REQ_OK;
        req->get_resp_port()->resp(req);
    }
    static void finished(vp::Block *block, bool value)
    {
        if (!value) return;
        auto *self=static_cast<LoaderProbe *>(block);
        if (self->pending || self->bytes!=0x20000 || self->chunks!=3)
            self->trace.fatal("Premature loader completion\n");
        printf("SOFTHIER_LOADER_PASS bytes=%lu chunks=%u async=1 metadata=1 zero_fill=1\n",self->bytes,self->chunks);
        self->time.get_engine()->quit(0);
    }
    vp::Trace trace;
    vp::ClockEvent event;
    vp::IoSlave input;
    vp::WireSlave<bool> done;
    vp::IoReq *pending=nullptr;
    uint64_t bytes=0;
    unsigned chunks=0;
};
extern "C" vp::Component *gv_new(vp::ComponentConf &config) { return new LoaderProbe(config); }

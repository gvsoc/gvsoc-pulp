// SPDX-License-Identifier: Apache-2.0
// RTL testharness fake_uart at 0xa0000000: byte stores go to the host console.
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <cstdio>

class SparseDmaUart : public vp::Component
{
    vp::IoSlave input;
    static vp::IoReqStatus req(vp::Block *, vp::IoReq *request)
    {
        if (!request->get_is_write()) return vp::IO_REQ_INVALID;
        for (uint64_t i=0; i<request->get_size(); ++i) std::putchar(request->get_data()[i]);
        std::fflush(stdout);
        return vp::IO_REQ_OK;
    }
public:
    SparseDmaUart(vp::ComponentConf &config) : vp::Component(config)
    { input.set_req_meth(req); new_slave_port("input", &input); }
};
extern "C" vp::Component *gv_new(vp::ComponentConf &conf) { return new SparseDmaUart(conf); }

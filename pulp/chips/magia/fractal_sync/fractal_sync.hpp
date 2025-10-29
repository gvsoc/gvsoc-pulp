#pragma once

#include <stdint.h>

template<typename T>
//class SlvPortInput
class PortReq
{
public:
    bool sync;
    T aggr;
    T id_req;
};

template<typename T>
//class SlvPortOutput
class PortResp
{
public:
    bool wake;
    T lvl;
    T id_rsp;
    bool error;
};

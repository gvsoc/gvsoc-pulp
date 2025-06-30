#pragma once

#include <stdint.h>


//MST port in RTL
// template<typename T>
// class MstPortOutput
// {
// public:
//     bool sync;
//     T aggr;
//     T id_req;
// };

// template<typename T>
// class MstPortInput
// {
// public:
//     bool wake;
//     T lvl;
//     T id_rsp;
//     bool error;
// };

//SLV port in RTL
// template<typename T>
// class SlvPortInput
// {
// public:
//     bool sync;
//     T aggr;
//     T id_req;
// };

// template<typename T>
// class SlvPortOutput
// {
// public:
//     bool wake;
//     T lvl;
//     T id_rsp;
//     bool error;
// };

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

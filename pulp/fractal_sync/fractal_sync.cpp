#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <stdio.h>
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <stdint.h>

#include "fractal_sync.hpp"
#include <cmath>

enum fractalsync_input_directions {
    NORD,
    SUD,
    EAST,
    WEST
};

enum fractalsync_output_directions {
    EAST_WEST, //horizontal = 0
    NORD_SUD   //vertical = 1
};

enum fractalsync_state {
    IDLE,
    SLAVE_NORD_REQ,
    SLAVE_SUD_REQ,
    SLAVE_EAST_REQ,
    SLAVE_WEST_REQ,
    NORD_SUD_END_SYNCRO,
    NORD_SUD_UP_SYNCRO,
    EAST_WEST_END_SYNCRO,
    EAST_WEST_UP_SYNCRO
};


class FractalSync : public vp::Component
{

public:
    FractalSync(vp::ComponentConf &config);

protected:    
    static void master_input_method(vp::Block *__this, PortResp<uint32_t> *req, int id);
    static void slave_input_method(vp::Block *__this, PortReq<uint32_t> *req, int id);

    vp::WireSlave<PortResp<uint32_t> *> master_ew_input_port;
    vp::WireMaster<PortReq<uint32_t> *> master_ew_output_port;


    vp::WireSlave<PortResp<uint32_t> *> master_ns_input_port;
    vp::WireMaster<PortReq<uint32_t> *> master_ns_output_port;

    vp::WireSlave<PortReq<uint32_t> *> slave_nord_input_port;
    vp::WireMaster<PortResp<uint32_t> *> slave_nord_output_port;

    vp::WireSlave<PortReq<uint32_t> *> slave_sud_input_port;
    vp::WireMaster<PortResp<uint32_t> *> slave_sud_output_port;

    vp::WireSlave<PortReq<uint32_t> *> slave_east_input_port;
    vp::WireMaster<PortResp<uint32_t> *> slave_east_output_port;

    vp::WireSlave<PortReq<uint32_t> *> slave_west_input_port;
    vp::WireMaster<PortResp<uint32_t> *> slave_west_output_port;

    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);

    vp::ClockEvent *fsm_event;
    vp::reg_32 state;

    int syncro_val_nord_sud[128]; //FIXME please make these parametric... I think based on the current level
    int syncro_val_east_west[128]; //FIXME please make these parametric... I think based on the current level

    uint32_t level; //internal level set when fractal sync is instantiated in one hot coding

    const char* directions[4];

    uint32_t nord_current_aggr[128]; //level sent by the fsync request at nord port in one hot coding
    uint32_t nord_current_id_req; //id sent by the fsync request at nord port in one hot coding

    uint32_t sud_current_aggr[128]; //level sent by the fsync request at sud port in one hot coding
    uint32_t sud_current_id_req; //id sent by the fsync request at sud port in one hot coding

    uint32_t east_current_aggr[128]; //level sent by the fsync request at east port in one hot coding
    uint32_t east_current_id_req; //id sent by the fsync request at east port in one hot coding

    uint32_t west_current_aggr[128]; //level sent by the fsync request at west port in one hot coding
    uint32_t west_current_id_req; //id sent by the fsync request at west port in one hot coding

    uint32_t nord_sud_current_aggr; //temp register used only for sending the request to the upper fsync
    uint32_t nord_sud_current_id_req; //temp register used only for sending the request to the upper fsync

    uint32_t east_west_current_aggr; //temp register used only for sending the request to the upper fsync
    uint32_t east_west_current_id_req; //temp register used only for sending the request to the upper fsync

    vp::Trace trace;

};

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new FractalSync(config);
}

FractalSync::FractalSync(vp::ComponentConf &config)
    : vp::Component(config)
{
    //Initialize interface
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->master_ew_input_port.set_sync_meth_muxed(&FractalSync::master_input_method,fractalsync_output_directions::EAST_WEST); //id=0 means horizontal (east-west)
    this->new_slave_port("master_ew_input_port", &this->master_ew_input_port, this);
    this->new_master_port("master_ew_output_port", &this->master_ew_output_port, this);

    this->master_ns_input_port.set_sync_meth_muxed(&FractalSync::master_input_method,fractalsync_output_directions::NORD_SUD); //id=1 means vertical (nord-sud)
    this->new_slave_port("master_ns_input_port", &this->master_ns_input_port, this);
    this->new_master_port("master_ns_output_port", &this->master_ns_output_port, this);

    this->slave_nord_input_port.set_sync_meth_muxed(&FractalSync::slave_input_method, fractalsync_input_directions::NORD);
    this->new_slave_port("slave_n_input_port", &this->slave_nord_input_port, this);
    this->new_master_port("slave_n_output_port", &this->slave_nord_output_port, this);

    this->slave_sud_input_port.set_sync_meth_muxed(&FractalSync::slave_input_method, fractalsync_input_directions::SUD);
    this->new_slave_port("slave_s_input_port", &this->slave_sud_input_port, this);
    this->new_master_port("slave_s_output_port", &this->slave_sud_output_port, this);

    this->slave_east_input_port.set_sync_meth_muxed(&FractalSync::slave_input_method, fractalsync_input_directions::EAST);
    this->new_slave_port("slave_e_input_port", &this->slave_east_input_port, this);
    this->new_master_port("slave_e_output_port", &this->slave_east_output_port, this);

    this->slave_west_input_port.set_sync_meth_muxed(&FractalSync::slave_input_method, fractalsync_input_directions::WEST);
    this->new_slave_port("slave_w_input_port", &this->slave_west_input_port, this);
    this->new_master_port("slave_w_output_port", &this->slave_west_output_port, this);

    //Initialize FSM
    this->state.set(IDLE);

    memset(this->syncro_val_nord_sud,0x0,128*sizeof(int));
    memset(this->syncro_val_east_west,0x0,128*sizeof(int));

    this->fsm_event = this->event_new(&FractalSync::fsm_handler);


    this->level   = 1 << get_js_config()->get("level")->get_int(); //in one hot coding

    this->directions[fractalsync_input_directions::NORD] = "NORD";
    this->directions[fractalsync_input_directions::SUD] = "SUD";
    this->directions[fractalsync_input_directions::EAST] = "EAST";
    this->directions[fractalsync_input_directions::WEST] = "WEST";

    memset(this->nord_current_aggr,0xFFFFFFFF,128*sizeof(uint32_t));
    this->nord_current_id_req = 0xFFFFFFFF;

    memset(this->sud_current_aggr,0xFFFFFFFF,128*sizeof(uint32_t));
    this->sud_current_id_req = 0xFFFFFFFF;

    memset(this->east_current_aggr,0xFFFFFFFF,128*sizeof(uint32_t));
    this->east_current_id_req = 0xFFFFFFFF;

    memset(this->west_current_aggr,0xFFFFFFFF,128*sizeof(uint32_t));
    this->west_current_id_req = 0xFFFFFFFF;

    this->east_west_current_aggr=0xFFFFFFFF;
    this->east_west_current_id_req=0xFFFFFFFF;

    this->nord_sud_current_aggr=0xFFFFFFFF;
    this->nord_sud_current_id_req=0xFFFFFFFF;

    this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] Instantiated\n");
}

void FractalSync::fsm_handler(vp::Block *__this, vp::ClockEvent *event) {
    FractalSync *_this = (FractalSync *)__this;

    uint32_t msb_pos;

    switch (_this->state.get()) {
        case IDLE:
        {    
            //_this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] In IDLE\n");
            break;
        }
        case SLAVE_NORD_REQ:
        {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] processed request-id=%d aggr=0x%08x from NORD port\n",_this->nord_current_id_req,_this->nord_current_aggr[_this->nord_current_id_req]);
            msb_pos = (sizeof(_this->nord_current_aggr[_this->nord_current_id_req])*8)-1 - __builtin_clz(_this->nord_current_aggr[_this->nord_current_id_req]); //then get the position of the msbit of the request
            if (msb_pos == (uint32_t)(log2(_this->level))) { //and check if target syncro ends here at this fractal
                _this->syncro_val_nord_sud[_this->nord_current_id_req]++; //vertical i.e., nord-sud
                if (_this->syncro_val_nord_sud[_this->nord_current_id_req]==2) { //if both the ports have been syncronized
                    _this->state.set(NORD_SUD_END_SYNCRO);
                    _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
                }
                else {
                    _this->state.set(IDLE); //syncro is not completed, so wait for request from next port
                    _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
                }
            }
            else if ((_this->nord_current_aggr[_this->nord_current_id_req]&_this->level)!=0) {
                _this->syncro_val_nord_sud[_this->nord_current_id_req]++; //vertical i.e., nord-sud
                if (_this->syncro_val_nord_sud[_this->nord_current_id_req]==2) { //if both the ports have been syncronized
                    _this->nord_sud_current_aggr=_this->nord_current_aggr[_this->nord_current_id_req];
                    _this->nord_sud_current_id_req=_this->nord_current_id_req;
                    _this->state.set(NORD_SUD_UP_SYNCRO);
                    _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
                }
                else {
                    _this->state.set(IDLE); //syncro is not completed, so wait for request from next port
                    _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
                }

            }
            else { //not for this fractal so propagate to the next
                _this->nord_sud_current_aggr=_this->nord_current_aggr[_this->nord_current_id_req];
                _this->nord_sud_current_id_req=_this->nord_current_id_req;
                _this->state.set(NORD_SUD_UP_SYNCRO);
                _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
            }
            break;
        }
        case SLAVE_SUD_REQ:
        {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] processed request-id=%d aggr=0x%08x from SUD port\n",_this->sud_current_id_req,_this->sud_current_aggr[_this->sud_current_id_req]);
            msb_pos = (sizeof(_this->sud_current_aggr[_this->sud_current_id_req])*8)-1 - __builtin_clz(_this->sud_current_aggr[_this->sud_current_id_req]); //then get the position of the msbit of the request
            if (msb_pos == (uint32_t)(log2(_this->level))) { //and check if syncro ends here at this fractal
                _this->syncro_val_nord_sud[_this->sud_current_id_req]++; //vertical i.e., nord-sud
                if (_this->syncro_val_nord_sud[_this->sud_current_id_req]==2) { //if both the ports have been syncronized
                    _this->state.set(NORD_SUD_END_SYNCRO);
                    _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
                }
                else {
                    _this->state.set(IDLE); //syncro is not completed, so wait for request from next port
                    _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
                }   
            }
            else if ((_this->sud_current_aggr[_this->sud_current_id_req]&_this->level)!=0) {
                _this->syncro_val_nord_sud[_this->sud_current_id_req]++; //vertical i.e., nord-sud
                if (_this->syncro_val_nord_sud[_this->sud_current_id_req]==2) { //if both the ports have been syncronized
                    _this->nord_sud_current_aggr=_this->sud_current_aggr[_this->sud_current_id_req];
                    _this->nord_sud_current_id_req=_this->sud_current_id_req;
                    _this->state.set(NORD_SUD_UP_SYNCRO);
                    _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
                }
                else {
                    _this->state.set(IDLE); //syncro is not completed, so wait for request from next port
                    _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
                }
            }
            else { //not for this fractal so propagate to the next
                _this->nord_sud_current_aggr=_this->sud_current_aggr[_this->sud_current_id_req];
                _this->nord_sud_current_id_req=_this->sud_current_id_req;
                _this->state.set(NORD_SUD_UP_SYNCRO);
                _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
            }
            break;
        }
        case SLAVE_EAST_REQ:
        {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] processed request-id=%d aggr=0x%08x from EAST port\n",_this->east_current_id_req,_this->east_current_aggr[_this->east_current_id_req]);
            msb_pos = (sizeof(_this->east_current_aggr[_this->east_current_id_req])*8)-1 - __builtin_clz(_this->east_current_aggr[_this->east_current_id_req]); //then get the position of the msbit of the request
            if (msb_pos == (uint32_t)(log2(_this->level))) { //and check if syncro ends here at this fractal
                _this->syncro_val_east_west[_this->east_current_id_req]++; //horizontal i.e., east-west
                if (_this->syncro_val_east_west[_this->east_current_id_req]==2) { //if both the ports have been syncronized
                    _this->state.set(EAST_WEST_END_SYNCRO);
                    _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
                }
                else {
                    _this->state.set(IDLE); //syncro is not completed, so wait for request from next port
                    _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
                }
            }
            else if ((_this->east_current_aggr[_this->east_current_id_req]&_this->level)!=0) {
                _this->syncro_val_east_west[_this->east_current_id_req]++; //horizontal i.e., east-west
                if (_this->syncro_val_east_west[_this->east_current_id_req]==2) { //if both the ports have been syncronized
                    _this->east_west_current_aggr=_this->east_current_aggr[_this->east_current_id_req];
                    _this->east_west_current_id_req=_this->east_current_id_req;
                    _this->state.set(EAST_WEST_UP_SYNCRO);
                    _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
                }
                else {
                    _this->state.set(IDLE); //syncro is not completed, so wait for request from next port
                    _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
                }
            }
            else { //not for this fractal so propagate to the next
                _this->east_west_current_aggr=_this->east_current_aggr[_this->east_current_id_req];
                _this->east_west_current_id_req=_this->east_current_id_req;
                _this->state.set(EAST_WEST_UP_SYNCRO);
                _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
            }
            break;
        }
        case SLAVE_WEST_REQ:
        {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] processed request-id=%d aggr=0x%08x from WEST port\n",_this->west_current_id_req,_this->west_current_aggr[_this->west_current_id_req]);
            msb_pos = (sizeof(_this->west_current_aggr[_this->west_current_id_req])*8)-1 - __builtin_clz(_this->west_current_aggr[_this->west_current_id_req]); //then get the position of the msbit of the request
            if (msb_pos == (uint32_t)(log2(_this->level))) { //and check if syncro ends here at this fractal
                _this->syncro_val_east_west[_this->west_current_id_req]++; //horizontal i.e., east-west
                if (_this->syncro_val_east_west[_this->west_current_id_req]==2) { //if both the ports have been syncronized
                    _this->state.set(EAST_WEST_END_SYNCRO);
                    _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
                }
                else {
                    _this->state.set(IDLE); //syncro is not completed, so wait for request from next port
                    _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
                }
            }
            else if ((_this->east_current_aggr[_this->west_current_id_req]&_this->level)!=0) {
                _this->syncro_val_east_west[_this->west_current_id_req]++; //horizontal i.e., east-west
                if (_this->syncro_val_east_west[_this->west_current_id_req]==2) { //if both the ports have been syncronized
                     _this->east_west_current_aggr=_this->west_current_aggr[_this->west_current_id_req];
                    _this->east_west_current_id_req=_this->west_current_id_req;
                    _this->state.set(EAST_WEST_UP_SYNCRO);
                    _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
                }
                else {
                    _this->state.set(IDLE); //syncro is not completed, so wait for request from next port
                    _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
                }
            }
            else { //not for this fractal so propagate to the next
                _this->east_west_current_aggr=_this->west_current_aggr[_this->west_current_id_req];
                _this->east_west_current_id_req=_this->west_current_id_req;
                _this->state.set(EAST_WEST_UP_SYNCRO);
                _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
            }
            break;
        }
        case NORD_SUD_UP_SYNCRO:
        {
            PortReq<uint32_t> OutReq = {
                .sync=true,
                .aggr=_this->nord_sud_current_aggr, //here the aggregate associated with the level coming from nord port shoud be the same of the one coming from the sud port
                .id_req=_this->nord_sud_current_id_req
            };
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] sending EAST-WEST req for level up [id=%d]\n",_this->nord_sud_current_id_req);
            _this->master_ew_output_port.sync(&OutReq); 
            _this->syncro_val_nord_sud[_this->nord_sud_current_id_req]=0;
            _this->state.set(IDLE); //syncro is not completed, so wait for request from next port
            _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
            break;
        }
        case NORD_SUD_END_SYNCRO:
        {
            uint32_t id_rsp_to_propagate;
            if (_this->syncro_val_nord_sud[_this->nord_current_id_req]==2)
                id_rsp_to_propagate=_this->nord_current_id_req;
            else if (_this->syncro_val_nord_sud[_this->sud_current_id_req]==2)
                id_rsp_to_propagate=_this->sud_current_id_req;
            else
                _this->trace.fatal("[FractalSync] INVALID FractalSync NORD_SUD_END_SYNCRO: %d\n", _this->state);
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] NORD-SUD level syncro completed - ENDING - [id-nord=%d,id-sud=%d]\n",id_rsp_to_propagate,id_rsp_to_propagate);
            _this->syncro_val_nord_sud[id_rsp_to_propagate]=0; //reset syncro val. Here nord and sud id req should be the same! 0 horizontal 1 vertical
            _this->state.set(IDLE); //go to idle when syncro is completed
            PortResp<uint32_t> nord_resp = {
                    .wake=true,
                    .lvl=_this->nord_current_aggr[id_rsp_to_propagate],
                    .id_rsp=id_rsp_to_propagate, //vertical i.e., nord-sud
                    .error=false
            };
            PortResp<uint32_t> sud_resp = {
                    .wake=true,
                    .lvl=_this->sud_current_aggr[id_rsp_to_propagate],
                    .id_rsp=id_rsp_to_propagate, //vertical i.e., nord-sud
                    .error=false
            };
            //broadcast response
            _this->slave_nord_output_port.sync(&nord_resp);
            _this->slave_sud_output_port.sync(&sud_resp);
            _this->nord_current_aggr[id_rsp_to_propagate]=0xFFFFFFFF;
            _this->sud_current_aggr[id_rsp_to_propagate]=0xFFFFFFFF;
            _this->state.set(IDLE);
            _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
            break;
        }
        case EAST_WEST_UP_SYNCRO:
        {
            PortReq<uint32_t> OutReq = {
                .sync=true,
                .aggr=_this->east_west_current_aggr, //here the aggregate associated with the level coming from east port shoud be the same of the one coming from the west port
                .id_req=_this->east_west_current_id_req //here... I think that 0 is enought... bho
            };
            
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] sending NORD-SUD req for level up [id=%d]\n",_this->east_west_current_id_req);
            _this->master_ns_output_port.sync(&OutReq);
            _this->syncro_val_east_west[_this->east_west_current_id_req]=0;
            _this->state.set(IDLE); //syncro is not completed, so wait for request from next port
            _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
            break;
        }
        case EAST_WEST_END_SYNCRO:
        {
            uint32_t id_rsp_to_propagate;
            if (_this->syncro_val_east_west[_this->east_current_id_req]==2)
                id_rsp_to_propagate=_this->east_current_id_req;
            else if (_this->syncro_val_east_west[_this->west_current_id_req]==2)
                id_rsp_to_propagate=_this->west_current_id_req;
            else
                _this->trace.fatal("[FractalSync] INVALID FractalSync EAST_WEST_END_SYNCRO: %d\n", _this->state);
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] EAST-WEST level syncro completed - ENDING - [id-east=%d,id-west=%d]\n",id_rsp_to_propagate,id_rsp_to_propagate);
            _this->syncro_val_east_west[id_rsp_to_propagate]=0; //reset syncro val. Here east and west id req should be the same! 0 horizontal 1 vertical
            _this->state.set(IDLE); //go to idle when syncro is completed
            PortResp<uint32_t> east_resp = {
                    .wake=true,
                    .lvl=_this->east_current_aggr[id_rsp_to_propagate],
                    .id_rsp=id_rsp_to_propagate, //horizontal i.e., east-west
                    .error=false
            };
            PortResp<uint32_t> west_resp = {
                    .wake=true,
                    .lvl=_this->west_current_aggr[id_rsp_to_propagate],
                    .id_rsp=id_rsp_to_propagate, //horizontal i.e., east-west
                    .error=false
            };
            //broadcast response
            _this->slave_west_output_port.sync(&west_resp);
            _this->slave_east_output_port.sync(&east_resp);
            _this->west_current_aggr[id_rsp_to_propagate]=0xFFFFFFFF;
            _this->east_current_aggr[id_rsp_to_propagate]=0xFFFFFFFF;
            _this->state.set(IDLE);
            _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
            break;
        }
        default:
            _this->trace.fatal("[FractalSync] INVALID FractalSync Status: %d\n", _this->state);
    }
}

void FractalSync::slave_input_method(vp::Block *__this, PortReq<uint32_t> *req, int id) {

    FractalSync *_this = (FractalSync *)__this;

    uint32_t msb_pos= (sizeof(req->aggr)*8)-1 - __builtin_clz(req->aggr); //get the position of the msbit of the request

    if (req->sync) {
        if ((req->aggr&_this->level)!=0) { //first check if the aggr has a bit set at the level postion of this fractal
            //_this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] received request from %s - Target is current fractal (aggr is 0x%08x)\n",_this->directions[id],req->aggr);
            switch (id) {
                case fractalsync_input_directions::NORD: //NORD
                    _this->nord_current_aggr[req->id_req]=req->aggr;
                    _this->nord_current_id_req=req->id_req;
                    _this->state.set(fractalsync_state::SLAVE_NORD_REQ);
                    break;
                case fractalsync_input_directions::SUD: //SUD
                    _this->sud_current_aggr[req->id_req]=req->aggr;
                    _this->sud_current_id_req=req->id_req;
                    _this->state.set(fractalsync_state::SLAVE_SUD_REQ);
                    break;
                case fractalsync_input_directions::EAST: //EAST
                    _this->east_current_aggr[req->id_req]=req->aggr;
                    _this->east_current_id_req=req->id_req;
                    _this->state.set(fractalsync_state::SLAVE_EAST_REQ);
                    break;
                case fractalsync_input_directions::WEST: //WEST
                    _this->west_current_aggr[req->id_req]=req->aggr;
                    _this->west_current_id_req=req->id_req;
                    _this->state.set(fractalsync_state::SLAVE_WEST_REQ);
                    break;
                default:
                   _this->trace.fatal("[FractalSync] wrong direction\n");
                   break;
            }
            _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
        }
        else if (msb_pos > (uint32_t)(log2(_this->level))) { //then, if not, check if the position of the msbit is higher than the current level 
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] received request from %s - Target is next level fractal (aggr is 0x%08x - req_id is %d)\n",_this->directions[id],req->aggr,req->id_req);
            PortReq<uint32_t> OutReq = {
                .sync=true,
                .aggr=req->aggr,
                .id_req=req->id_req
            };
            switch (id) { //keep track from which port we received the request so that when we get the response we know on which port the message has te be routed
                case fractalsync_input_directions::NORD: //NORD
                    _this->nord_current_aggr[req->id_req]=req->aggr;
                    break;
                case fractalsync_input_directions::SUD: //SUD
                    _this->sud_current_aggr[req->id_req]=req->aggr;
                    break;
                case fractalsync_input_directions::EAST: //EAST
                    _this->east_current_aggr[req->id_req]=req->aggr;
                    break;
                case fractalsync_input_directions::WEST: //WEST
                    _this->west_current_aggr[req->id_req]=req->aggr;
                    break;
                default:
                   _this->trace.fatal("[FractalSync] wrong direction\n");
                   break;
            }
            //if (req->id_req==fractalsync_output_directions::EAST_WEST)
            if ((id == fractalsync_input_directions::EAST) || (id == fractalsync_input_directions::WEST))
                _this->master_ns_output_port.sync(&OutReq);
            //else if (req->id_req==fractalsync_output_directions::NORD_SUD)
            else if ((id == fractalsync_input_directions::NORD) || (id == fractalsync_input_directions::SUD))
                _this->master_ew_output_port.sync(&OutReq);
            else
                _this->trace.fatal("[FractalSync] wrong direction\n");
        }
        else {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] received request from %s - ERROR IN AGGR\n",_this->directions[id]);
            PortResp<uint32_t> resp = {
                .wake=false,
                .lvl=0x0,
                .id_rsp=0x0,
                .error=true
            };
            switch (id) {
                case fractalsync_input_directions::NORD: //NORD
                    _this->slave_nord_output_port.sync(&resp);
                    break;
                case fractalsync_input_directions::SUD: //SUD
                    _this->slave_sud_output_port.sync(&resp);
                    break;
                case fractalsync_input_directions::EAST: //EAST
                    _this->slave_east_output_port.sync(&resp);
                    break;
                case fractalsync_input_directions::WEST: //WEST
                    _this->slave_west_output_port.sync(&resp);
                    break;
                default:
                   _this->trace.fatal("[FractalSync] wrong direction\n");
                   break;
            }
        }
    }
    else { //error case
        _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] received request from %s - ERROR IN SYNC\n",_this->directions[id]);
        PortResp<uint32_t> resp = {
            .wake=false,
            .lvl=0x0,
            .id_rsp=0x0,
            .error=true
        };
        switch (id) {
            case fractalsync_input_directions::NORD: //NORD
                _this->slave_nord_output_port.sync(&resp);
                break;
            case fractalsync_input_directions::SUD: //SUD
                _this->slave_sud_output_port.sync(&resp);
                break;
            case fractalsync_input_directions::EAST: //EAST
                _this->slave_east_output_port.sync(&resp);
                break;
            case fractalsync_input_directions::WEST: //WEST
                _this->slave_west_output_port.sync(&resp);
                break;
            default:
                _this->trace.fatal("[FractalSync] wrong direction\n");
                break;
        }
    }
}

void FractalSync::master_input_method(vp::Block *__this, PortResp<uint32_t> *req, int id) {
    
    FractalSync *_this = (FractalSync *)__this;
    if (id==fractalsync_output_directions::EAST_WEST) {
        PortResp<uint32_t> resp = {
                .wake=true,
                .lvl=_this->level,
                //.id_rsp=0x0,
                .id_rsp=req->id_rsp,
                .error=false
        };
        if ((_this->nord_current_aggr[req->id_rsp]!=0xFFFFFFFF) && (_this->sud_current_aggr[req->id_rsp]==0xFFFFFFFF)) {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] Received EAST-WEST response from above level. Sending a NORD request [id=%d]\n",req->id_rsp);
            _this->slave_nord_output_port.sync(&resp);
            _this->nord_current_aggr[req->id_rsp]=0xFFFFFFFF;
        }
        else if ((_this->nord_current_aggr[req->id_rsp]==0xFFFFFFFF) && (_this->sud_current_aggr[req->id_rsp]!=0xFFFFFFFF)) {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] Received EAST-WEST response from above level. Sending a SUD request [id=%d]\n",req->id_rsp);
            _this->slave_sud_output_port.sync(&resp);
            _this->sud_current_aggr[req->id_rsp]=0xFFFFFFFF;
        }
        else if ((_this->nord_current_aggr[req->id_rsp]!=0xFFFFFFFF) && (_this->sud_current_aggr[req->id_rsp]!=0xFFFFFFFF)) {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] Received EAST-WEST response from above level. Sending a NORD-SUD request [id=%d]\n",req->id_rsp);
            _this->slave_nord_output_port.sync(&resp);
            _this->slave_sud_output_port.sync(&resp);
            _this->nord_current_aggr[req->id_rsp]=0xFFFFFFFF;
            _this->sud_current_aggr[req->id_rsp]=0xFFFFFFFF;
        }
    }
    else if (id==fractalsync_output_directions::NORD_SUD) {
        PortResp<uint32_t> resp = {
                .wake=true,
                .lvl=_this->level,
                //.id_rsp=0x0,
                .id_rsp=req->id_rsp,
                .error=false
        };
        //broadcast response
        if ((_this->east_current_aggr[req->id_rsp]!=0xFFFFFFFF) && (_this->west_current_aggr[req->id_rsp]==0xFFFFFFFF)) {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] Received NORD-SUD response from above level. Sending a EAST request [id=%d]\n",req->id_rsp);
            _this->slave_east_output_port.sync(&resp);
            _this->east_current_aggr[req->id_rsp]=0xFFFFFFFF;
        }
        else if ((_this->east_current_aggr[req->id_rsp]==0xFFFFFFFF) && (_this->west_current_aggr[req->id_rsp]!=0xFFFFFFFF)) {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] Received NORD-SUD response from above level. Sending a WEST request [id=%d]\n",req->id_rsp);
            _this->slave_west_output_port.sync(&resp);
            _this->west_current_aggr[req->id_rsp]=0xFFFFFFFF;
        }
        else if ((_this->east_current_aggr[req->id_rsp]!=0xFFFFFFFF) && (_this->west_current_aggr[req->id_rsp]!=0xFFFFFFFF)) {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] Received NORD-SUD response from above level. Sending a EAST-WEST request [id=%d]\n",req->id_rsp);
            _this->slave_west_output_port.sync(&resp);
            _this->slave_east_output_port.sync(&resp);
            _this->east_current_aggr[req->id_rsp]=0xFFFFFFFF;
            _this->west_current_aggr[req->id_rsp]=0xFFFFFFFF;
        }        
    }

}

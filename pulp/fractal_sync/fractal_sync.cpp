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

enum fractalsync_state {
    IDLE,
    SLAVE_REQ,
    WAIT_SYNCRO
};


class FractalSync : public vp::Component
{

public:
    FractalSync(vp::ComponentConf &config);

protected:

    //static void master_input_method(vp::Block *__this, MstPortInput<uint32_t> *req);
    
    static void master_input_method(vp::Block *__this, PortResp<uint32_t> *req);
    static void slave_input_method(vp::Block *__this, PortReq<uint32_t> *req);

    //vp::WireSlave<MstPortInput<uint32_t> *> master_n_input_port;
    //vp::WireMaster<MstPortOutput<uint32_t> *> master_n_output_port;

    vp::WireSlave<PortResp<uint32_t> *> master_n_input_port;
    vp::WireMaster<PortReq<uint32_t> *> master_n_output_port;


    // vp::WireSlave<MstPortInput<uint32_t> *> master_s_input_port;
    // vp::WireMaster<MstPortOutput<uint32_t> *> master_s_output_port;

    // vp::WireSlave<SlvPortInput<uint32_t> *> slave_nord_input_port;
    // vp::WireMaster<SlvPortOutput<uint32_t> *> slave_nord_output_port;

    // vp::WireSlave<SlvPortInput<uint32_t> *> slave_sud_input_port;
    // vp::WireMaster<SlvPortOutput<uint32_t> *> slave_sud_output_port;

    vp::WireSlave<PortReq<uint32_t> *> slave_east_input_port;
    vp::WireMaster<PortResp<uint32_t> *> slave_east_output_port;

    vp::WireSlave<PortReq<uint32_t> *> slave_west_input_port;
    vp::WireMaster<PortResp<uint32_t> *> slave_west_output_port;

    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);

    vp::ClockEvent *fsm_event;
    vp::reg_32 state;

    int syncro_val;

    int level; //internal level set when fractal sync is instantiated
    uint32_t current_level; //level sent by the fsync request
    uint32_t current_id_req; //id sent by the fsync request

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


    this->master_n_input_port.set_sync_meth(&FractalSync::master_input_method);
    this->new_slave_port("master_n_input_port", &this->master_n_input_port, this);
    this->new_master_port("master_n_output_port", &this->master_n_output_port, this);

    // this->master_s_output_port.set_sync_meth(&FractalSync::master_output_method);
    // this->new_slave_port("master_s_input_port", &this->master_s_input_port, this);
    // this->new_master_port("master_s_output_port", &this->master_s_output_port, this);

    // this->slave_nord_input_port.set_sync_meth(&FractalSync::slave_input_method);
    // this->new_slave_port("slave_n_input_port", &this->slave_nord_input_port, this);
    // this->new_master_port("slave_n_output_port", &this->slave_nord_output_port, this);

    // this->slave_sud_input_port.set_sync_meth(&FractalSync::slave_input_method);
    // this->new_slave_port("slave_s_input_port", &this->slave_sud_input_port, this);
    // this->new_master_port("slave_s_output_port", &this->slave_sud_output_port, this);

    this->slave_east_input_port.set_sync_meth(&FractalSync::slave_input_method);
    this->new_slave_port("slave_e_input_port", &this->slave_east_input_port, this);
    this->new_master_port("slave_e_output_port", &this->slave_east_output_port, this);

    this->slave_west_input_port.set_sync_meth(&FractalSync::slave_input_method);
    this->new_slave_port("slave_w_input_port", &this->slave_west_input_port, this);
    this->new_master_port("slave_w_output_port", &this->slave_west_output_port, this);

    //Initialize FSM
    this->state.set(IDLE);

    this->syncro_val=0;

    this->fsm_event = this->event_new(&FractalSync::fsm_handler);


    this->level   = get_js_config()->get("level")->get_int(); //>=1
    this->current_level = 0; //uninitialized
    this->current_id_req = 0;

    this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] Instantiated\n");
}

void FractalSync::fsm_handler(vp::Block *__this, vp::ClockEvent *event) {
    FractalSync *_this = (FractalSync *)__this;

    switch (_this->state.get()) {
        case IDLE:
            break;
        case SLAVE_REQ:
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] processed request from SLAVE\n");
            _this->syncro_val++;
            _this->state.set(WAIT_SYNCRO);
            _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
            break;      
        case WAIT_SYNCRO: //here I think we should check if the level is the same for the two requests
            if (_this->syncro_val==2) {
                if (_this->current_level==_this->level) {
                    _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] level syncro completed - ENDING\n");
                    _this->syncro_val=0; //reset syncro val
                    _this->state.set(IDLE); //go to idle when syncro is completed
                    PortResp<uint32_t> resp = {
                            .wake=true,
                            .lvl=0x0,
                            .id_rsp=0x0,
                            .error=false
                    };
                    //broadcast response
                    _this->slave_west_output_port.sync(&resp);
                    _this->slave_east_output_port.sync(&resp);
                }
                else {
                    _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] level syncro completed - LEVEL UP\n");
                    _this->syncro_val=0; //reset syncro val
                    _this->state.set(IDLE); //go to idle when syncro is completed
                    //MstPortOutput<uint32_t> req = {
                    PortReq<uint32_t> req = {
                        .sync=true,
                        .aggr=_this->current_level,
                        .id_req=_this->current_id_req
                    };
                    _this->master_n_output_port.sync(&req);
                }
            }
            else {
                _this->state.set(IDLE); //go to idle until sincronization is completed 
                _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
            }
            break;
        default:
            _this->trace.fatal("[FractalSync] INVALID FractalSync Status: %d\n", _this->state);
    }
}

void FractalSync::slave_input_method(vp::Block *__this, PortReq<uint32_t> *req){

    FractalSync *_this = (FractalSync *)__this;
    PortResp<uint32_t> resp;


    _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] received request from SLAVE\n");
    if ((req->sync) && (req->aggr!=0)) { //check if the sync signal is true and the required level (aggr) is !=0
        if (_this->current_level==0) { // OK case. Opportunistic set. The firt tile that arrives here sets the value
            _this->current_level=req->aggr;
            _this->current_id_req=0x1; //TODO: fix this... Need to understand what it is used for
        }
        else {
            if (req->aggr!=_this->current_level) { //ERROR CASE
                resp = {
                    .wake=false,
                    .lvl=0x0,
                    .id_rsp=0x0,
                    .error=true
                };
                //Broadcast error!
                _this->slave_west_output_port.sync(&resp);
                _this->slave_east_output_port.sync(&resp);
            }
        }
        _this->state.set(SLAVE_REQ);
        _this->event_enqueue(_this->fsm_event, 1); //trigger fsm
    }
    else {
        resp = {
            .wake=false,
            .lvl=0x0,
            .id_rsp=0x0,
            .error=true
        };
        //Broadcast error!
        _this->slave_west_output_port.sync(&resp);
        _this->slave_east_output_port.sync(&resp);
    }
}

//void FractalSync::master_input_method(vp::Block *__this, MstPortInput<uint32_t> *req) {

void FractalSync::master_input_method(vp::Block *__this, PortResp<uint32_t> *req) {
    
    FractalSync *_this = (FractalSync *)__this;
    _this->trace.msg(vp::Trace::LEVEL_TRACE,"[FractalSync] Received response from above level\n");
    PortResp<uint32_t> resp = {
            .wake=true,
            .lvl=0x0,
            .id_rsp=0x0,
            .error=false
    };
    //broadcast response
    _this->slave_west_output_port.sync(&resp);
    _this->slave_east_output_port.sync(&resp);
}

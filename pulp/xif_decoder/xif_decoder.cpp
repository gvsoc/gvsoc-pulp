#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <stdio.h>
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <stdint.h>

#include <cpu/iss/include/offload.hpp>
#include "../fractal_sync/fractal_sync.hpp"

enum fractal_directions {
    EAST_WEST, //horizontal = 0
    NORD_SUD   //vertical = 1
};

/*****************************************************
*                   Class Definition                 *
*****************************************************/


class XifDecoder : public vp::Component
{

public:
    XifDecoder(vp::ComponentConf &config);

protected:
    static void offload_sync_m(vp::Block *__this, IssOffloadInsn<uint32_t> *insn);
    vp::WireSlave<IssOffloadInsn<uint32_t> *> offload_itf_m;
    vp::WireMaster<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf_m;

    //static void offload_sync_s1(vp::Block *__this, IssOffloadInsn<uint32_t> *insn);
    vp::WireMaster<IssOffloadInsn<uint32_t> *> offload_itf_s1;
    static void grant_sync_s1(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result);
    vp::WireSlave<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf_s1;

    //static void offload_sync_s2(vp::Block *__this, IssOffloadInsn<uint32_t> *insn);
    vp::WireMaster<IssOffloadInsn<uint32_t> *> offload_itf_s2;
    static void grant_sync_s2(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result);
    vp::WireSlave<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf_s2;


    static void fractal_output_method(vp::Block *__this, PortResp<uint32_t> *req, int id);

    vp::WireMaster<PortReq<uint32_t> *> fractal_ew_input_port;
    vp::WireSlave<PortResp<uint32_t> *> fractal_ew_output_port;

    vp::WireMaster<PortReq<uint32_t> *> fractal_ns_input_port;
    vp::WireSlave<PortResp<uint32_t> *> fractal_ns_output_port;


    //static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);

    vp::Trace trace;
    //vp::ClockEvent *idma_event;
    //vp::ClockEvent *redmule_event;
    //IssOffloadInsn<uint32_t> *current_Insn;
    
};

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new XifDecoder(config);
}

XifDecoder::XifDecoder(vp::ComponentConf &config)
    : vp::Component(config)
{
    //Initialize interface
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->offload_itf_m.set_sync_meth(&XifDecoder::offload_sync_m);
    this->new_slave_port("offload_m", &this->offload_itf_m, this);
    this->new_master_port("offload_grant_m", &this->offload_grant_itf_m, this);

    //this->offload_itf_s1.set_sync_meth(&XifDecoder::offload_sync_s1);
    this->new_master_port("offload_s1", &this->offload_itf_s1, this);
    this->offload_grant_itf_s1.set_sync_meth(&XifDecoder::grant_sync_s1);
    this->new_slave_port("offload_grant_s1", &this->offload_grant_itf_s1, this);

    //this->offload_itf_s2.set_sync_meth(&XifDecoder::offload_sync_s2);
    this->new_master_port("offload_s2", &this->offload_itf_s2, this);
    this->offload_grant_itf_s2.set_sync_meth(&XifDecoder::grant_sync_s2);
    this->new_slave_port("offload_grant_s2", &this->offload_grant_itf_s2, this);

    this->fractal_ew_output_port.set_sync_meth_muxed(&XifDecoder::fractal_output_method,fractal_directions::EAST_WEST);
    this->fractal_ns_output_port.set_sync_meth_muxed(&XifDecoder::fractal_output_method,fractal_directions::NORD_SUD);
    this->new_slave_port("fractal_ew_output_port", &this->fractal_ew_output_port, this);
    this->new_slave_port("fractal_ns_output_port", &this->fractal_ns_output_port, this);

    this->new_master_port("fractal_ew_input_port", &this->fractal_ew_input_port, this);
    this->new_master_port("fractal_ns_input_port", &this->fractal_ns_input_port, this);


    this->trace.msg(vp::Trace::LEVEL_TRACE,"[XifDecoder] Instantiated\n");

    //this->offload_stalled=false;

    //this->idma_event = this->event_new(&XifDecoder::idma_handler);
    //this->redmule_event = this->event_new(&XifDecoder::idma_handler);
    //this->current_Insn = NULL;
}

void XifDecoder::grant_sync_s1(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result){

    XifDecoder *_this = (XifDecoder *)__this;

    _this->trace.msg(vp::Trace::LEVEL_TRACE,"[XifDecoder] received GRANT from iDMA\n");

    _this->offload_grant_itf_m.sync(result);
}

void XifDecoder::grant_sync_s2(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result){

    XifDecoder *_this = (XifDecoder *)__this;

    _this->trace.msg(vp::Trace::LEVEL_TRACE,"[XifDecoder] received GRANT from RedMule\n");

    _this->offload_grant_itf_m.sync(result);
}

void XifDecoder::fractal_output_method(vp::Block *__this, PortResp<uint32_t> *req, int id) {
    XifDecoder *_this = (XifDecoder *)__this;

    if ((req->wake) && (!req->error)) {
        if (id==fractal_directions::EAST_WEST)
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[XifDecoder] received wake response from EAST-WEST Fractal\n");
        else if (id==fractal_directions::NORD_SUD)
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[XifDecoder] received wake response from NORD-SUD Fractal\n");
        else
            _this->trace.fatal("[XifDecoder] wrong direction\n");
        IssOffloadInsnGrant<uint32_t> offload_grant = {
            .result=0x1
        };
        _this->offload_grant_itf_m.sync(&offload_grant);
    }
    else if (req->error){
        _this->trace.fatal("[XifDecoder] received error response from Fractal\n");
    }
}


void XifDecoder::offload_sync_m(vp::Block *__this, IssOffloadInsn<uint32_t> *insn)
{
    XifDecoder *_this = (XifDecoder *)__this;
    uint32_t opc = insn->opcode & 0x7F;

    switch (opc) //here in RTL the mapping is: port 0 Redmule, port 1 iDMA, port 2, Fractal
    {
        case 0b0101011: //these are all the opcodes associated with the IDMA
        {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[XifDecoder] received opcode for IDMA\n");
            _this->offload_itf_s1.sync(insn);
            //_this->current_Insn=insn;
            //_this->event_enqueue(_this->fsm_event, 1);
            break;
        }
        case 0b0001011: //these are all the opcodes associated with the RedMule
        case 0b1101011:
        {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[XifDecoder] received opcode for RedMule\n");
            _this->offload_itf_s2.sync(insn);
            //_this->current_Insn=insn;
            //_this->event_enqueue(_this->fsm_event, 1);
            break;
        }
        case 0b1011011: //this is fractal sync case please update the opcode
        {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[XifDecoder] received opcode for FractalSync (id=%d - aggr=%d)\n",insn->arg_b,insn->arg_a);
            insn->granted = false; //immeditaly stall the core
            PortReq<uint32_t> req = {
                .sync=true,
                .aggr=insn->arg_a,
                .id_req=insn->arg_b
            };
            switch (insn->arg_b)
            {
            case fractal_directions::EAST_WEST:
                _this->fractal_ew_input_port.sync(&req);
                break;
            case fractal_directions::NORD_SUD:
                _this->fractal_ns_input_port.sync(&req);
                break;
            default:
                _this->trace.fatal("[XifDecoder] wrong direction\n");
                break;
            }        
        }
    }
}


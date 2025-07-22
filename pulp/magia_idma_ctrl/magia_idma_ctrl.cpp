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

/*****************************************************
*                   Class Definition                 *
*****************************************************/


class Magia_iDMA_Ctrl : public vp::Component
{

public:
    Magia_iDMA_Ctrl(vp::ComponentConf &config);

protected:
    static void offload_sync_m(vp::Block *__this, IssOffloadInsn<uint32_t> *insn);
    vp::WireSlave<IssOffloadInsn<uint32_t> *> offload_itf_m;
    vp::WireMaster<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf_m;

    vp::WireMaster<IssOffloadInsn<uint32_t> *> offload_itf_idma0;
    static void grant_sync_idma0(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result);
    vp::WireSlave<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf_idma0;

    vp::WireMaster<IssOffloadInsn<uint32_t> *> offload_itf_idma1;
    static void grant_sync_idma1(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result);
    vp::WireSlave<IssOffloadInsnGrant<uint32_t> *> offload_grant_itf_idma1;


    vp::WireMaster<bool> dma0_done;
    vp::WireMaster<bool> dma1_done;


    vp::Trace trace;
};

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Magia_iDMA_Ctrl(config);
}

Magia_iDMA_Ctrl::Magia_iDMA_Ctrl(vp::ComponentConf &config)
    : vp::Component(config)
{
    //Initialize interface
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->offload_itf_m.set_sync_meth(&Magia_iDMA_Ctrl::offload_sync_m);
    this->new_slave_port("offload_m", &this->offload_itf_m, this);
    this->new_master_port("offload_grant_m", &this->offload_grant_itf_m, this);

    this->new_master_port("offload_idma0_axi2obi", &this->offload_itf_idma0, this);
    //this->offload_grant_itf_idma0.set_sync_meth(&Magia_iDMA_Ctrl::grant_sync_idma0);
    this->new_slave_port("offload_grant_idma0_axi2obi", &this->offload_grant_itf_idma0, this);

    this->new_master_port("offload_idma1_obi2axi", &this->offload_itf_idma1, this);
    //this->offload_grant_itf_idma1.set_sync_meth(&Magia_iDMA_Ctrl::grant_sync_idma1);
    this->new_slave_port("offload_grant_idma1_obi2axi", &this->offload_grant_itf_idma1, this);

    this->new_master_port("idma0_done_irq", &this->dma0_done, this);
    this->new_master_port("idma1_done_irq", &this->dma1_done, this);


    this->trace.msg(vp::Trace::LEVEL_TRACE,"[Magia iDMA Ctrl] Instantiated\n");

}

// void Magia_iDMA_Ctrl::grant_sync_idma0(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result){

//     Magia_iDMA_Ctrl *_this = (Magia_iDMA_Ctrl *)__this;

//     _this->trace.msg(vp::Trace::LEVEL_TRACE,"[XifDecoder] received GRANT from iDMA0 AXI2OBI\n");

//     _this->offload_grant_itf_m.sync(result);
// }

// void Magia_iDMA_Ctrl::grant_sync_idma1(vp::Block *__this, IssOffloadInsnGrant<uint32_t> *result){

//     Magia_iDMA_Ctrl *_this = (Magia_iDMA_Ctrl *)__this;

//     _this->trace.msg(vp::Trace::LEVEL_TRACE,"[XifDecoder] received GRANT from iDMA1 OBI2AXI\n");

//     _this->offload_grant_itf_m.sync(result);
// }


void Magia_iDMA_Ctrl::offload_sync_m(vp::Block *__this, IssOffloadInsn<uint32_t> *insn)
{
    Magia_iDMA_Ctrl *_this = (Magia_iDMA_Ctrl *)__this;
    uint32_t opc = insn->opcode & 0x7F;
    uint32_t func3 = (insn->opcode >> 12) & 0x7;
    bool dir = (insn->opcode >> 25) & 0x1;

    switch (opc)
    {
        case 0b1011011: //these are all the opcodes associated with the IDMA
        {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,"[Magia iDMA Ctrl] received opcode dmcnf for IDMA - DIR: %d\n",dir);
            insn->granted = true; //immeditaly grant back the core
            //_this->offload_itf_s1.sync(insn);
            //_this->current_Insn=insn;
            //_this->event_enqueue(_this->fsm_event, 1);
            break;
        }
        case 0b1111011:
        {
            if (func3==0b111) {
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[Magia iDMA Ctrl] received opcode dmstr for IDMA - DIR: %d\n",dir);
                insn->granted = true; //immeditaly grant back the core
                if(!dir)
                    _this->dma0_done.sync(true);
                else
                    _this->dma1_done.sync(true);
            }
            else {
                _this->trace.msg(vp::Trace::LEVEL_TRACE,"[Magia iDMA Ctrl] received opcode dm1d2d3d for IDMA - DIR: %d\n",dir);
                insn->granted = true; //immeditaly grant back the core
            }
            //_this->offload_itf_s2.sync(insn);
            //_this->current_Insn=insn;
            //_this->event_enqueue(_this->fsm_event, 1);
            break;
        }
        default:
            _this->trace.fatal("[Magia iDMA Ctrl] Received wrong opcode\n");
            break;
    }
}


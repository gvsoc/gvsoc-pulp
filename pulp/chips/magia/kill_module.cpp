#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <stdio.h>

class KillModule : public vp::Component
{

public:

  KillModule(vp::ComponentConf &config);

  static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

private:

  vp::Trace     trace;
  vp::IoSlave in;

  uint64_t kill_base_address;
  uint64_t kill_addr_size;
  int nb_cores_to_wait;
  int nb_recv_kill_reqs;

};

KillModule::KillModule(vp::ComponentConf &config)
: vp::Component(config)
{
  this->traces.new_trace("trace", &trace, vp::DEBUG);
  this->in.set_req_meth(&KillModule::req);
  this->new_slave_port("input", &this->in, this);

  this->kill_base_address = get_js_config()->get("kill_addr_base")->get_int();
  this->kill_addr_size = get_js_config()->get("kill_addr_size")->get_int();
  this->nb_cores_to_wait = get_js_config()->get("nb_cores_to_wait")->get_int();

  this->nb_recv_kill_reqs=0;
}

vp::IoReqStatus KillModule::req(vp::Block *__this, vp::IoReq *req)
{
    KillModule *_this = (KillModule *)__this;

    uint64_t offset = req->get_addr();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    if ((!is_write) || (size>4))
      return vp::IO_REQ_INVALID;
    else {

      if ((offset>=_this->kill_base_address) && (offset<=(_this->kill_base_address)+_this->kill_addr_size)) {
          _this->nb_recv_kill_reqs++;
          _this->trace.msg(vp::Trace::LEVEL_TRACE, "Received kill request at address 0x%08lx. Current kill count is %d. Number of cores to wait is %d\n",offset,_this->nb_recv_kill_reqs,_this->nb_cores_to_wait);
      }

      if (_this->nb_recv_kill_reqs==_this->nb_cores_to_wait) {
        _this->time.get_engine()->quit(0);
      }
      return vp::IO_REQ_OK;
    }
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
  return new KillModule(config);
}

// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// io_v2 port of the ``stdout_v3`` peripheral. Functionally identical to
// stdout_v3_impl.cpp: each byte written into the per-(cluster, core)
// channel is buffered and flushed to host stdout on newline or buffer
// overflow.
//
// Differences vs v1:
//   - Slave port is ``vp::IoSlave`` from ``vp/itf/io_v2.hpp``; the
//     request callback is attached via the in-class initializer.
//   - Sync replies use ``IO_REQ_DONE``. Bad cluster/core IDs are
//     reported as ``IO_REQ_DONE`` + ``IO_RESP_INVALID`` on the response
//     status sideband (v2's split of v1's ``IO_REQ_INVALID``).

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <stdio.h>
#include <string.h>
#include <vector>

#define MAX_PUTC_LENGTH 1024

class Stdout : public vp::Component
{

public:

  Stdout(vp::ComponentConf &config);

  static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

private:

  vp::Trace     trace;
  vp::IoSlave in{&Stdout::req};

  int nb_cluster;
  int nb_core;
  int user_set_core_id;
  int user_set_cluster_id;

  std::vector <char *> putc_buffer;
  int *putc_buffer_pos;

};

Stdout::Stdout(vp::ComponentConf &config)
: vp::Component(config)
{
  traces.new_trace("trace", &trace, vp::DEBUG);
  new_slave_port("input", &in);

  nb_cluster = get_js_config()->get_child_int("max_cluster");
  nb_core = get_js_config()->get_child_int("max_core_per_cluster");
  user_set_core_id = get_js_config()->get_child_int("user_set_core_id");
  user_set_cluster_id = get_js_config()->get_child_int("user_set_cluster_id");

  putc_buffer_pos = new int[nb_cluster*nb_core];
  for (int j=0; j<nb_cluster; j++) {
    for (int i=0; i<nb_core; i++) {
      putc_buffer.push_back(new char[MAX_PUTC_LENGTH]);
      putc_buffer_pos[j*nb_core+i] = 0;
    }
  }
}

vp::IoReqStatus Stdout::req(vp::Block *__this, vp::IoReq *req)
{
  Stdout *_this = (Stdout *)__this;

  uint64_t offset = req->get_addr();
  uint8_t *data = req->get_data();
  uint64_t size = req->get_size();

  int core_id;
  int cluster_id;

  if (_this->user_set_core_id != 0xDEADBEEF)
    core_id = _this->user_set_core_id;
  else
    core_id = (offset >> 3) & 0xf;


  if (_this->user_set_cluster_id != 0xDEADBEEF)
    cluster_id = _this->user_set_cluster_id;
  else
    cluster_id = (offset >> 7) & 0x3f;

  _this->trace.msg("Stdout access (offset: 0x%x, size: 0x%x, is_write: %d, core_id: %d, cluster_id: %d)\n", offset, size, req->get_is_write(), core_id, cluster_id);

  if (core_id >= _this->nb_core || cluster_id >= _this->nb_cluster)
  {
    _this->trace.warning("Accessing invalid stdout channel (coreId: %d, clusterId: %d)\n", core_id, cluster_id);
    req->set_resp_status(vp::IO_RESP_INVALID);
    return vp::IO_REQ_DONE;
  }

  _this->putc_buffer[cluster_id*_this->nb_core+core_id][_this->putc_buffer_pos[cluster_id*_this->nb_core+core_id]++] = *data;
  if (*data == '\n' || _this->putc_buffer_pos[cluster_id*_this->nb_core+core_id] == MAX_PUTC_LENGTH - 1) {
    _this->putc_buffer[cluster_id*_this->nb_core+core_id][_this->putc_buffer_pos[cluster_id*_this->nb_core+core_id]] = 0;
    fwrite((void *)_this->putc_buffer[cluster_id*_this->nb_core+core_id], 1, _this->putc_buffer_pos[cluster_id*_this->nb_core+core_id], stdout);
    _this->putc_buffer_pos[cluster_id*_this->nb_core+core_id] = 0;
  }

  return vp::IO_REQ_DONE;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
  return new Stdout(config);
}

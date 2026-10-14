// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

// Control block of the cluster instruction cache. See cluster_icache_ctrl.py
// for the register map.

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/itf/wire.hpp>
#include <ips/pulp/cluster/cluster_icache_ctrl/cluster_icache_ctrl_config.hpp>

#define CL_ICACHE_ENABLE_OFFSET       0x00
#define CL_ICACHE_FLUSH_OFFSET        0x04
#define CL_ICACHE_L0_FLUSH_OFFSET     0x08
#define CL_ICACHE_SEL_FLUSH_OFFSET    0x0c
#define CL_ICACHE_SPECIAL_CORE_OFFSET 0x18
#define CL_ICACHE_PREFETCH_OFFSET     0x1c

class ClusterIcacheCtrl : public vp::Component
{
public:
    ClusterIcacheCtrl(vp::ComponentConf &config);

    ClusterIcacheCtrlConfig cfg;

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    void reset(bool active) override;

    vp::Trace trace;

    vp::IoSlave in{&ClusterIcacheCtrl::req};

    vp::WireMaster<bool>     enable_itf;
    vp::WireMaster<bool>     flush_itf;
    vp::WireMaster<bool>     l0_flush_itf;
    vp::WireMaster<bool>     flush_line_itf;
    vp::WireMaster<uint32_t> flush_line_addr_itf;

    uint32_t enable;
    uint32_t special_core_cache;
    uint32_t prefetch;
};

ClusterIcacheCtrl::ClusterIcacheCtrl(vp::ComponentConf &config)
    : vp::Component(config, this->cfg)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->new_slave_port("input", &this->in);

    this->new_master_port("enable", &this->enable_itf);
    this->new_master_port("flush", &this->flush_itf);
    this->new_master_port("l0_flush", &this->l0_flush_itf);
    this->new_master_port("flush_line", &this->flush_line_itf);
    this->new_master_port("flush_line_addr", &this->flush_line_addr_itf);

    this->enable = 0;
    this->special_core_cache = 0;
    this->prefetch = 0;
}

void ClusterIcacheCtrl::reset(bool active)
{
    if (active)
    {
        this->enable = 0;
        this->special_core_cache = 0;
        this->prefetch = 0;
    }
}

vp::IoReqStatus ClusterIcacheCtrl::req(vp::Block *__this, vp::IoReq *req)
{
    ClusterIcacheCtrl *_this = (ClusterIcacheCtrl *)__this;

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    _this->trace.msg(vp::Trace::LEVEL_TRACE,
        "Cluster icache ctrl access (offset: 0x%lx, size: 0x%lx, is_write: %d)\n",
        offset, size, is_write);

    if (size != 4)
    {
        req->set_resp_status(vp::IO_RESP_INVALID);
        return vp::IO_REQ_DONE;
    }

    uint32_t value = is_write ? *(uint32_t *)data : 0;

    switch (offset)
    {
        case CL_ICACHE_ENABLE_OFFSET:
            if (is_write)
            {
                // The register is a per-bank mask; the model has one knob, so
                // any bank enabled enables the hierarchy.
                _this->enable = value;
                _this->trace.msg(vp::Trace::LEVEL_INFO,
                    "Setting cluster icache enable (mask: 0x%x)\n", value);
                if (_this->enable_itf.is_bound())
                {
                    _this->enable_itf.sync(value != 0);
                }
            }
            else
            {
                *(uint32_t *)data = _this->enable;
            }
            break;

        case CL_ICACHE_FLUSH_OFFSET:
            if (is_write)
            {
                _this->trace.msg(vp::Trace::LEVEL_INFO, "Flushing cluster icache\n");
                if (_this->flush_itf.is_bound())
                {
                    _this->flush_itf.sync(true);
                }
            }
            else
            {
                *(uint32_t *)data = 0;
            }
            break;

        case CL_ICACHE_L0_FLUSH_OFFSET:
            if (is_write)
            {
                _this->trace.msg(vp::Trace::LEVEL_INFO, "Flushing cluster L0 icaches\n");
                if (_this->l0_flush_itf.is_bound())
                {
                    _this->l0_flush_itf.sync(true);
                }
            }
            else
            {
                *(uint32_t *)data = 0;
            }
            break;

        case CL_ICACHE_SEL_FLUSH_OFFSET:
            if (is_write)
            {
                _this->trace.msg(vp::Trace::LEVEL_INFO,
                    "Flushing cluster icache line (addr: 0x%x)\n", value);
                if (_this->flush_line_addr_itf.is_bound())
                {
                    _this->flush_line_addr_itf.sync(value);
                }
                if (_this->flush_line_itf.is_bound())
                {
                    _this->flush_line_itf.sync(true);
                }
            }
            else
            {
                *(uint32_t *)data = 0;
            }
            break;

        // Configuration bits of the real hierarchy that this model does not
        // distinguish: stored so software reads back what it wrote.
        case CL_ICACHE_SPECIAL_CORE_OFFSET:
            if (is_write) { _this->special_core_cache = value; }
            else          { *(uint32_t *)data = _this->special_core_cache; }
            break;

        case CL_ICACHE_PREFETCH_OFFSET:
            if (is_write) { _this->prefetch = value; }
            else          { *(uint32_t *)data = _this->prefetch; }
            break;

        default:
            if (!is_write)
            {
                *(uint32_t *)data = 0;
            }
            break;
    }

    return vp::IO_REQ_DONE;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ClusterIcacheCtrl(config);
}

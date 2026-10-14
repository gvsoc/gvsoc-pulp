// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// GAP9 cluster control unit (EOC / fetch-enable / per-core boot address /
// debug halt). Ported from the legacy `pulp.cluster.cluster_ctrl_v2_impl`
// io-protocol model to io_v2: the only change is the slave port, which now
// replies inline with IO_RESP_OK / IO_RESP_INVALID and IO_REQ_DONE. The
// bootaddr wire is uint64_t so it binds directly to the iss_v2 core i_ENTRY.

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/itf/wire.hpp>
#include <stdio.h>
#include <string.h>

#include "archi/cluster_ctrl_v2.h"

class Core_cluster_ctrl
{
public:
    vp::WireMaster<uint64_t> bootaddr_itf;
    vp::WireMaster<bool> fetchen_itf;
    vp::WireMaster<bool> halt_itf;
    vp::WireSlave<bool> halt_status_itf;
    uint32_t bootaddr;
};

class cluster_ctrl : public vp::Component
{

public:
    cluster_ctrl(vp::ComponentConf &config);

    void reset(bool active);

    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

private:
    static void halt_status_sync(vp::Block *__this, bool status, int id);
    static void reset_sync(vp::Block *__this, bool active);
    void fetch_en_req(bool is_write, uint32_t *data);
    void cluster_cfg_req(bool is_write, uint32_t *data);
    void dbg_halt_status_req(bool is_write, uint32_t *data);
    void dbg_halt_mask_req(bool is_write, uint32_t *data);
    void bootaddr_req(int core, bool is_write, uint32_t *data);
    void check_dbg_halt();

    vp::Trace trace;
    vp::IoSlave in{&cluster_ctrl::req};
    // Island reset driven by the PMU cluster ICU (asserted while the cluster is
    // powered off). When bound, the cluster boots held off until the Maestro
    // cluster-on sequence releases it; left unbound it has no effect.
    vp::WireSlave<bool> reset_itf;
    // HCI arbitration control towards the L1 crossbar. The CLUSTER_CFG
    // register (offset 0x18) carries
    // | fregfile_dis(12) | hwpe_en(11) | hci_ctrl(10:0) | with hci_ctrl =
    // {arb_policy[10:9], hwpe_prio[8], low_prio_max_stall[7:0]}.
    vp::WireMaster<uint32_t> hci_ctrl_itf;
    // Automatic clock-gating enable (CLUSTER_CLK_GATE register), towards the
    // cluster composite power model.
    vp::WireMaster<bool> clock_gating_en_itf;
    bool powered;
    int nb_core;
    Core_cluster_ctrl *cores;

    uint32_t dbg_halt_mask;
    uint32_t dbg_halt_status;
    uint32_t dbg_halt_status_sync;
    uint32_t cluster_cfg;
};

cluster_ctrl::cluster_ctrl(vp::ComponentConf &config)
    : vp::Component(config)
{
    nb_core = get_js_config()->get_child_int("nb_core");

    cores = new Core_cluster_ctrl[nb_core];

    traces.new_trace("trace", &trace, vp::DEBUG);

    new_slave_port("input", &in);

    this->reset_itf.set_sync_meth(&cluster_ctrl::reset_sync);
    new_slave_port("pmu_reset", &this->reset_itf);

    new_master_port("hci_ctrl", &this->hci_ctrl_itf);

    new_master_port("clock_gating_en", &this->clock_gating_en_itf);

    for (int i = 0; i < nb_core; i++)
    {
        cores[i].bootaddr = 0x57575757;

        new_master_port("bootaddr_" + std::to_string(i), &cores[i].bootaddr_itf);
        new_master_port("fetchen_" + std::to_string(i), &cores[i].fetchen_itf);
        new_master_port("halt_" + std::to_string(i), &cores[i].halt_itf);

        cores[i].halt_status_itf.set_sync_meth_muxed(&cluster_ctrl::halt_status_sync, i);
        new_slave_port("core_halt_" + std::to_string(i), &cores[i].halt_status_itf);
    }
}

vp::IoReqStatus cluster_ctrl::req(vp::Block *__this, vp::IoReq *req)
{
    cluster_ctrl *_this = (cluster_ctrl *)__this;

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    _this->trace.msg("cluster_ctrl access (offset: 0x%x, size: 0x%x, is_write: %d)\n", offset, size, is_write);

    if (size != 4)
    {
        _this->trace.force_warning("Only 32 bits accesses are allowed\n");
        req->set_resp_status(vp::IO_RESP_INVALID);
        return vp::IO_REQ_DONE;
    }

    bool valid = true;

    if (offset == ARCHI_CLUSTER_CTRL_FETCH_EN)
    {
        _this->fetch_en_req(is_write, (uint32_t *)data);
    }
    else if (offset >= ARCHI_CLUSTER_CTRL_BOOTADDR(0) && offset < ARCHI_CLUSTER_CTRL_BOOTADDR(_this->nb_core))
    {
        _this->bootaddr_req(ARCHI_CLUSTER_CTRL_BOOTADDR_COREID(offset), is_write, (uint32_t *)data);
    }
    else if (offset == ARCHI_CLUSTER_CTRL_EVENT)
    {
    }
    else if (offset == ARCHI_CLUSTER_CTRL_CLUSTER_CFG)
    {
        _this->cluster_cfg_req(is_write, (uint32_t *)data);
    }
    else if (offset == ARCHI_CLUSTER_CTRL_CLUSTER_CLK_GATE)
    {
        // Forward the automatic clock-gating enable to the cluster composite
        // (switches its background power between full and clock-gated).
        if (is_write && _this->clock_gating_en_itf.is_bound())
        {
            _this->clock_gating_en_itf.sync((*(uint32_t *)data) & 1);
        }
    }
    else if (offset == ARCHI_CLUSTER_CTRL_DBG_STATUS)
    {
        _this->dbg_halt_status_req(is_write, (uint32_t *)data);
    }
    else if (offset == ARCHI_CLUSTER_CTRL_DBG_HALT_MASK)
    {
        _this->dbg_halt_mask_req(is_write, (uint32_t *)data);
    }
    else
    {
        _this->trace.force_warning("Invalid access\n");
        valid = false;
    }

    req->set_resp_status(valid ? vp::IO_RESP_OK : vp::IO_RESP_INVALID);
    return vp::IO_REQ_DONE;
}

void cluster_ctrl::cluster_cfg_req(bool is_write, uint32_t *data)
{
    if (is_write)
    {
        this->trace.msg("Writing cluster cfg (value: 0x%x)\n", *data);
        // Only bits 12:0 are implemented (fregfile_dis | hwpe_en | hci_ctrl),
        // as in the RTL read-back.
        this->cluster_cfg = *data & 0x1fff;
        // Forward the hci_ctrl field to the L1 crossbar. hwpe_en (clock
        // gating) and fregfile_dis are stored for read-back only.
        if (this->hci_ctrl_itf.is_bound())
        {
            this->hci_ctrl_itf.sync(*data & 0x7ff);
        }
    }
    else
    {
        *data = this->cluster_cfg;
    }
}

void cluster_ctrl::dbg_halt_status_req(bool is_write, uint32_t *data)
{
    if (is_write)
    {
        this->trace.msg("Writing dbg status (dbg_status: 0x%x)\n", *data);

        // When writing to the halt status register, each bit set to 1 indicates
        // a core to resume
        this->dbg_halt_status &= ~(*data);

        this->check_dbg_halt();
    }
    else
    {
        *data = this->dbg_halt_status;
    }
}

void cluster_ctrl::dbg_halt_mask_req(bool is_write, uint32_t *data)
{
    if (is_write)
    {
        this->trace.msg("Writing dbg halt mask (dbg_halt_mask: 0x%x)\n", *data);
        this->dbg_halt_mask = *data;

        this->check_dbg_halt();
    }
    else
    {
        *data = this->dbg_halt_mask;
    }
}

void cluster_ctrl::check_dbg_halt()
{
    uint32_t trig_mask = this->dbg_halt_status & this->dbg_halt_mask;

    if (trig_mask && trig_mask != this->dbg_halt_mask)
    {
        trace.msg("Propagating cross-trigger halt signal (halt_status: 0x%x, new_halt_status: 0x%x)\n",
            this->dbg_halt_status & this->dbg_halt_mask, this->dbg_halt_mask);

        this->dbg_halt_status |= this->dbg_halt_mask;
    }

    if (this->dbg_halt_status != this->dbg_halt_status_sync)
    {
        uint32_t dbg_halt_status_sync = this->dbg_halt_status_sync;
        this->dbg_halt_status_sync = this->dbg_halt_status;

        for (int i = 0; i < nb_core; i++)
        {
            int current_status = (this->dbg_halt_status >> i) & 1;
            int sync_status = (dbg_halt_status_sync >> i) & 1;

            if (current_status != sync_status)
            {
                trace.msg("Synchronizing core halt (core: %d, halt: %d)\n", i, current_status);

                this->dbg_halt_status_sync |= current_status << i;
                cores[i].halt_itf.sync(current_status);
            }
        }
    }
}

void cluster_ctrl::fetch_en_req(bool is_write, uint32_t *data)
{
    if (!this->powered)
    {
        // The island is held in reset by the PMU; a fetch-enable has no effect
        // until the cluster-on sequence powers it. Quietly ignore (software
        // will observe the cluster never started) rather than aborting.
        this->trace.msg(vp::Trace::LEVEL_WARNING,
            "Ignoring cluster fetch-enable while powered off (program the PMU cluster-on sequence first)\n");
        return;
    }

    for (int i = 0; i < nb_core; i++)
    {
        cores[i].fetchen_itf.sync(((*data) >> i) & 1);
    }
}

void cluster_ctrl::reset_sync(vp::Block *__this, bool active)
{
    cluster_ctrl *_this = (cluster_ctrl *)__this;

    _this->trace.msg("Cluster island reset from PMU (active: %d)\n", active);

    _this->powered = !active;

    // While the island is held in reset, force all PEs out of fetch so a
    // power-down stops the cluster even if it was running.
    if (active)
    {
        for (int i = 0; i < _this->nb_core; i++)
        {
            _this->cores[i].fetchen_itf.sync(0);
        }
    }
}

void cluster_ctrl::bootaddr_req(int core, bool is_write, uint32_t *data)
{
    if (is_write)
    {
        trace.msg("Setting boot address (core: %d, addr: 0x%x)\n", core, *data);

        cores[core].bootaddr_itf.sync((uint64_t)*data);
        cores[core].bootaddr = *data;
    }
    else
    {
        *data = cores[core].bootaddr;
    }
}

void cluster_ctrl::halt_status_sync(vp::Block *__this, bool status, int id)
{
    cluster_ctrl *_this = (cluster_ctrl *)__this;

    _this->trace.msg("Received new core halt status (core: %d, halt: %d)\n", id, status);
    _this->dbg_halt_status = (_this->dbg_halt_status & ~(1 << id)) | (status << id);
    _this->dbg_halt_status_sync = (_this->dbg_halt_status & ~(1 << id)) | (status << id);

    if (status)
        _this->check_dbg_halt();
}

void cluster_ctrl::reset(bool active)
{
    if (active)
    {
        this->dbg_halt_mask = 0;
        this->dbg_halt_status = 0;
        this->dbg_halt_status_sync = 0;
        this->cluster_cfg = 0;
        // If the PMU island reset is wired, the cluster boots powered off and
        // stays held until the cluster-on sequence releases it. Standalone
        // (reset unbound), the cluster is always powered.
        this->powered = !this->reset_itf.is_bound();
    }
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new cluster_ctrl(config);
}

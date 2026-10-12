/*
 * Copyright (C) 2025 Fondazione Chips-IT
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <stdio.h>
#include <cstring>
#include <stdint.h>
#include <string>
#include <vector>

/*****************************************************
*                   Class Definition                 *
*****************************************************/

class ClusterRegs : public vp::Component
{

public:
    ClusterRegs(vp::ComponentConf &config);
    static void spatz_fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    static void pulp_fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    static void pulp_start_deassert_handler(vp::Block *__this, vp::ClockEvent *event);

protected:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    vp::IoSlave         input_itf;

    /* Spatz registers — offsets [0x00, 0x18] */
    vp::reg_32 spatz_clock_en_reg;
    vp::reg_32 spatz_ready_reg;
    vp::reg_32 spatz_start_irq_reg;
    vp::reg_32 spatz_taskbin_reg;
    vp::reg_32 spatz_data_reg;
    vp::reg_32 spatz_return_reg;
    vp::reg_32 spatz_done_reg;

    vp::WireMaster<bool> spatz_clock_en;
    vp::WireMaster<bool> spatz_start_irq;
    vp::WireMaster<bool> spatz_done_irq;

    /* PULP cluster registers — offsets [0x40, 0x5C]
     *   0x40: PULP_CLK_EN           — broadcast enable: write 1 starts ALL cores, write 0 stops all
     *   0x44: PULP_BINARY           — entry point, written by CV32 before enabling clock
     *   0x48: PULP_NB_CORES_TO_WAIT — set by CV32 in pulp_run_task (popcount of core_mask)
     *   0x4C: PULP_DONE             — each PULP hart writes 1 after task returns; when all done → DONE IRQ
     *   0x50: PULP_TASKBIN          — task function address; all cores read this via MMIO
     *   0x54: PULP_DATA             — context pointer passed to the task function
     *   0x58: PULP_START            — CV32 writes one-hot core_mask → per-core IRQ edge pulse (1 cyc);
     *                                 PULP cores write 0 BEFORE task (ack); register clears when all ACKs
     *                                 received → unblocks CV32 while(PULP_START!=0) in pulp_run_task
     *   0x5C: PULP_READY            — each PULP hart writes 1 when booted; CV32 reads until nb_pulp_cores ready
     */
    vp::reg_32 pulp_clock_en_reg;
    vp::reg_32 pulp_binary_reg;
    vp::reg_32 pulp_nb_cores_to_wait_reg;
    vp::reg_32 pulp_done_reg;
    vp::reg_32 pulp_taskbin_reg;
    vp::reg_32 pulp_data_reg;
    vp::reg_32 pulp_start_reg;
    vp::reg_32 pulp_ready_reg;

    vp::WireMaster<bool>              pulp_clock_en;  /* broadcast: single port, all cores via GVSoC fan-out */
    std::vector<vp::WireMaster<bool>> pulp_start_irq; /* per-core one-hot IRQ */
    vp::WireMaster<bool>              pulp_done_irq;
    vp::WireMaster<uint64_t>          pulp_entry;

    vp::ClockEvent *spatz_fsm_eu_event;
    vp::ClockEvent *pulp_fsm_eu_event;
    vp::ClockEvent *pulp_start_deassert_event;

    int nb_pulp_cores;
    int nb_recv_ack_reqs;   /* counts PULP_START=0 writes (ACK before task) */
    int nb_recv_end_reqs;   /* counts PULP_DONE=1 writes (completion after task) */
    int nb_recv_ready_reqs;

    vp::Trace trace;
};

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ClusterRegs(config);
}

ClusterRegs::ClusterRegs(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->input_itf.set_req_meth(&ClusterRegs::req);
    this->new_slave_port("input", &this->input_itf);

    this->spatz_clock_en_reg.set(0x00000000);
    this->spatz_ready_reg.set(0x00000000);
    this->spatz_start_irq_reg.set(0x00000000);
    this->spatz_taskbin_reg.set(0x00000000);
    this->spatz_data_reg.set(0x00000000);
    this->spatz_return_reg.set(0x00000000);
    this->spatz_done_reg.set(0x00000000);

    this->pulp_clock_en_reg.set(0x00000000);
    this->pulp_binary_reg.set(0x00000000);
    this->pulp_nb_cores_to_wait_reg.set(0x00000000);
    this->pulp_done_reg.set(0x00000000);
    this->pulp_taskbin_reg.set(0x00000000);
    this->pulp_data_reg.set(0x00000000);
    this->pulp_start_reg.set(0x00000000);
    this->pulp_ready_reg.set(0x00000000);

    this->nb_pulp_cores      = get_js_config()->get("nb_pulp_cores")->get_int();
    this->nb_recv_ack_reqs   = 0;
    this->nb_recv_end_reqs   = 0;
    this->nb_recv_ready_reqs = 0;

    this->new_master_port("spatz_clock_en",  &this->spatz_clock_en,  this);
    this->new_master_port("spatz_start_irq", &this->spatz_start_irq, this);
    this->new_master_port("spatz_done_irq",  &this->spatz_done_irq,  this);

    /* Single broadcast clock-enable port — GVSoC fan-out via linked list */
    this->new_master_port("pulp_clock_en", &this->pulp_clock_en, this);

    /* Per-core start-IRQ ports (one-hot) — resize before taking addresses */
    this->pulp_start_irq.resize(this->nb_pulp_cores);
    for (int i = 0; i < this->nb_pulp_cores; i++) {
        this->new_master_port("pulp_start_irq_" + std::to_string(i),
                              &this->pulp_start_irq[i], this);
    }

    this->new_master_port("pulp_done_irq", &this->pulp_done_irq, this);
    this->new_master_port("pulp_entry",    &this->pulp_entry,    this);

    this->spatz_fsm_eu_event       = this->event_new(&ClusterRegs::spatz_fsm_handler);
    this->pulp_fsm_eu_event        = this->event_new(&ClusterRegs::pulp_fsm_handler);
    this->pulp_start_deassert_event = this->event_new(&ClusterRegs::pulp_start_deassert_handler);

    this->trace.msg(vp::Trace::LEVEL_TRACE, "[Magia Cluster regs] Instantiated\n");
}

void ClusterRegs::spatz_fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    ClusterRegs *_this = (ClusterRegs *)__this;
    _this->spatz_done_reg.set(0x00);
    _this->spatz_done_irq.sync(false);
    _this->trace.msg("[Spatz Regs] Done reg reset\n");
}

void ClusterRegs::pulp_fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    ClusterRegs *_this = (ClusterRegs *)__this;
    _this->pulp_done_reg.set(0x00);
    _this->pulp_done_irq.sync(false);
    _this->trace.msg("[PULP Regs] Done IRQ deasserted\n");
}

void ClusterRegs::pulp_start_deassert_handler(vp::Block *__this, vp::ClockEvent *event)
{
    ClusterRegs *_this = (ClusterRegs *)__this;
    for (int i = 0; i < _this->nb_pulp_cores; i++) {
        _this->pulp_start_irq[i].sync(false);
    }
    _this->trace.msg("[PULP Regs] Start IRQ auto-deasserted (edge pulse done)\n");
}

vp::IoReqStatus ClusterRegs::req(vp::Block *__this, vp::IoReq *req)
{
    ClusterRegs *_this = (ClusterRegs *)__this;

    uint64_t offset   = req->get_addr();
    uint8_t *data     = req->get_data();
    uint64_t size     = req->get_size();
    bool     is_write = req->get_is_write();

    if (size != 4) {
        _this->trace.fatal("[Cluster Regs] Only 32-bit accesses supported (addr=0x%08lx size=%lu)\n", offset, size);
    }

    /* ------------------------------------------------------------------ */
    /* Spatz registers                                                      */
    /* ------------------------------------------------------------------ */

    if (offset == 0x00) { /* SPATZ_CLK_EN */
        if (is_write) {
            uint32_t val; memcpy(&val, data, 4);
            _this->spatz_clock_en_reg.set(val);
            if (val == 0x01) {
                _this->spatz_clock_en.sync(true);
                _this->trace.msg("[Spatz Regs][0x00] Clock enabled\n");
            } else if (val == 0x00) {
                _this->spatz_clock_en.sync(false);
                _this->trace.msg("[Spatz Regs][0x00] Clock disabled\n");
            } else {
                _this->trace.fatal("[Spatz Regs][0x00] Unsupported clock enable value\n");
            }
        } else {
            uint32_t val = _this->spatz_clock_en_reg.get();
            memcpy(data, &val, 4);
            _this->trace.msg("[Spatz Regs][0x00] Read clock enable (0x%08x)\n", val);
        }
    }
    else if (offset == 0x04) { /* SPATZ_READY */
        if (is_write) {
            uint32_t val; memcpy(&val, data, 4);
            _this->spatz_ready_reg.set(val);
            _this->trace.msg("[Spatz Regs][0x04] Write ready (0x%08x)\n", val);
        } else {
            uint32_t val = _this->spatz_ready_reg.get();
            memcpy(data, &val, 4);
            _this->trace.msg("[Spatz Regs][0x04] Read ready (0x%08x)\n", val);
        }
    }
    else if (offset == 0x08) { /* SPATZ_START */
        if (is_write) {
            uint32_t val; memcpy(&val, data, 4);
            _this->spatz_start_irq_reg.set(val);
            if (val == 0x01) {
                _this->spatz_start_irq.sync(true);
                _this->trace.msg("[Spatz Regs][0x08] Start IRQ asserted\n");
            } else if (val == 0x00) {
                _this->spatz_start_irq.sync(false);
                _this->trace.msg("[Spatz Regs][0x08] Start IRQ deasserted\n");
            } else {
                _this->trace.fatal("[Spatz Regs][0x08] Unsupported start value\n");
            }
        } else {
            uint32_t val = _this->spatz_start_irq_reg.get();
            memcpy(data, &val, 4);
            _this->trace.msg("[Spatz Regs][0x08] Read start (0x%08x)\n", val);
        }
    }
    else if (offset == 0x0C) { /* SPATZ_TASKBIN */
        if (is_write) {
            uint32_t val; memcpy(&val, data, 4);
            _this->spatz_taskbin_reg.set(val);
            _this->trace.msg("[Spatz Regs][0x0C] Write taskbin (0x%08x)\n", val);
        } else {
            uint32_t val = _this->spatz_taskbin_reg.get();
            memcpy(data, &val, 4);
            _this->trace.msg("[Spatz Regs][0x0C] Read taskbin (0x%08x)\n", val);
        }
    }
    else if (offset == 0x10) { /* SPATZ_DATA */
        if (is_write) {
            uint32_t val; memcpy(&val, data, 4);
            _this->spatz_data_reg.set(val);
            _this->trace.msg("[Spatz Regs][0x10] Write data (0x%08x)\n", val);
        } else {
            uint32_t val = _this->spatz_data_reg.get();
            memcpy(data, &val, 4);
            _this->trace.msg("[Spatz Regs][0x10] Read data (0x%08x)\n", val);
        }
    }
    else if (offset == 0x14) { /* SPATZ_RETURN */
        if (is_write) {
            uint32_t val; memcpy(&val, data, 4);
            _this->spatz_return_reg.set(val);
            _this->trace.msg("[Spatz Regs][0x14] Write return (0x%08x)\n", val);
        } else {
            uint32_t val = _this->spatz_return_reg.get();
            memcpy(data, &val, 4);
            _this->trace.msg("[Spatz Regs][0x14] Read return (0x%08x)\n", val);
        }
    }
    else if (offset == 0x18) { /* SPATZ_DONE */
        if (is_write) {
            uint32_t val; memcpy(&val, data, 4);
            _this->spatz_done_reg.set(val);
            _this->spatz_done_irq.sync(true);
            _this->event_enqueue(_this->spatz_fsm_eu_event, 1);
            _this->trace.msg("[Spatz Regs][0x18] Done signalled\n");
        } else {
            _this->trace.fatal("[Spatz Regs][0x18] Done register is write-only\n");
        }
    }

    /* ------------------------------------------------------------------ */
    /* PULP cluster registers                                               */
    /* ------------------------------------------------------------------ */

    else if (offset == 0x40) { /* PULP_CLK_EN — broadcast: 1=enable all, 0=disable all */
        if (is_write) {
            uint32_t val; memcpy(&val, data, 4);
            _this->pulp_clock_en_reg.set(val);
            /* Reset READY counter so CV32 can re-poll after each init */
            _this->nb_recv_ready_reqs = 0;
            _this->pulp_ready_reg.set(0x00000000);
            bool en = (val != 0);
            _this->pulp_clock_en.sync(en);
            _this->trace.msg("[PULP Regs][0x40] Clock %s (broadcast)\n", en ? "enabled" : "disabled");
        } else {
            uint32_t val = _this->pulp_clock_en_reg.get();
            memcpy(data, &val, 4);
            _this->trace.msg("[PULP Regs][0x40] Read clock enable (0x%08x)\n", val);
        }
    }
    else if (offset == 0x44) { /* PULP_BINARY — entry point written by CV32 before enabling clock */
        if (is_write) {
            uint32_t val; memcpy(&val, data, 4);
            _this->pulp_binary_reg.set(val);
            _this->pulp_entry.sync((uint64_t)val);
            _this->trace.msg("[PULP Regs][0x44] Binary entry point set (0x%08x)\n", val);
        } else {
            uint32_t val = _this->pulp_binary_reg.get();
            memcpy(data, &val, 4);
            _this->trace.msg("[PULP Regs][0x44] Read binary entry point (0x%08x)\n", val);
        }
    }
    else if (offset == 0x48) { /* PULP_NB_CORES_TO_WAIT — programmed by CV32 firmware */
        if (is_write) {
            uint32_t val; memcpy(&val, data, 4);
            _this->pulp_nb_cores_to_wait_reg.set(val);
            _this->trace.msg("[PULP Regs][0x48] Nb cores to wait set (%u)\n", val);
        } else {
            uint32_t val = _this->pulp_nb_cores_to_wait_reg.get();
            memcpy(data, &val, 4);
            _this->trace.msg("[PULP Regs][0x48] Read nb cores to wait (%u)\n", val);
        }
    }
    else if (offset == 0x4C) { /* PULP_DONE — each hart writes 1 after task returns; fires DONE IRQ when all done */
        if (is_write) {
            _this->nb_recv_end_reqs++;
            uint32_t nb_to_wait = _this->pulp_nb_cores_to_wait_reg.get();
            _this->trace.msg("[PULP Regs][0x4C] Done write %d/%u\n", _this->nb_recv_end_reqs, nb_to_wait);
            if (nb_to_wait > 0 && _this->nb_recv_end_reqs == (int)nb_to_wait) {
                _this->nb_recv_end_reqs = 0;
                _this->pulp_done_irq.sync(true);
                _this->event_enqueue(_this->pulp_fsm_eu_event, 1);
                _this->trace.msg("[PULP Regs][0x4C] All done — DONE IRQ fired\n");
            }
        } else {
            _this->trace.fatal("[PULP Regs][0x4C] Done register is write-only\n");
        }
    }
    else if (offset == 0x50) { /* PULP_TASKBIN — task function address, read by all cores via MMIO */
        if (is_write) {
            uint32_t val; memcpy(&val, data, 4);
            _this->pulp_taskbin_reg.set(val);
            _this->trace.msg("[PULP Regs][0x50] Taskbin set (0x%08x)\n", val);
        } else {
            uint32_t val = _this->pulp_taskbin_reg.get();
            memcpy(data, &val, 4);
            _this->trace.msg("[PULP Regs][0x50] Read taskbin (0x%08x)\n", val);
        }
    }
    else if (offset == 0x54) { /* PULP_DATA — context pointer passed to the task */
        if (is_write) {
            uint32_t val; memcpy(&val, data, 4);
            _this->pulp_data_reg.set(val);
            _this->trace.msg("[PULP Regs][0x54] Data set (0x%08x)\n", val);
        } else {
            uint32_t val = _this->pulp_data_reg.get();
            memcpy(data, &val, 4);
            _this->trace.msg("[PULP Regs][0x54] Read data (0x%08x)\n", val);
        }
    }
    else if (offset == 0x58) { /* PULP_START — CV32: one-hot core_mask → per-core IRQ edge (1 cyc);
                                 *               PULP: write 0 BEFORE task (ack); register clears when
                                 *               all nb_cores_to_wait ACKs received → unblocks CV32 poll */
        if (is_write) {
            uint32_t val; memcpy(&val, data, 4);
            if (val != 0) {
                /* CV32 dispatch: fire edge IRQ to each selected core, reset counters */
                _this->pulp_start_reg.set(val);
                _this->nb_recv_ack_reqs = 0;
                _this->nb_recv_end_reqs = 0;
                for (int i = 0; i < _this->nb_pulp_cores; i++) {
                    if ((val >> i) & 0x1) {
                        _this->pulp_start_irq[i].sync(true);
                        _this->trace.msg("[PULP Regs][0x58] Start IRQ → core %d\n", i);
                    }
                }
                _this->event_enqueue(_this->pulp_start_deassert_event, 1);
            } else {
                /* PULP core ACK (before task): count; when all done → clear register only */
                _this->nb_recv_ack_reqs++;
                uint32_t nb_to_wait = _this->pulp_nb_cores_to_wait_reg.get();
                _this->trace.msg("[PULP Regs][0x58] ACK %d/%u\n", _this->nb_recv_ack_reqs, nb_to_wait);
                if (nb_to_wait > 0 && _this->nb_recv_ack_reqs == (int)nb_to_wait) {
                    _this->pulp_start_reg.set(0x00000000);
                    _this->trace.msg("[PULP Regs][0x58] All ACKs received — PULP_START cleared\n");
                }
            }
        } else {
            uint32_t val = _this->pulp_start_reg.get();
            memcpy(data, &val, 4);
            _this->trace.msg("[PULP Regs][0x58] Read start (0x%08x)\n", val);
        }
    }
    else if (offset == 0x5C) { /* PULP_READY — each hart writes 1 when booted; CV32 polls until nb_pulp_cores ready */
        if (is_write) {
            _this->nb_recv_ready_reqs++;
            _this->trace.msg("[PULP Regs][0x5C] Ready write %d/%d\n", _this->nb_recv_ready_reqs, _this->nb_pulp_cores);
            if (_this->nb_recv_ready_reqs == _this->nb_pulp_cores) {
                _this->pulp_ready_reg.set(0x00000001);
                _this->trace.msg("[PULP Regs][0x5C] All cores ready\n");
            }
        } else {
            uint32_t val = _this->pulp_ready_reg.get();
            memcpy(data, &val, 4);
            _this->trace.msg("[PULP Regs][0x5C] Read ready (0x%08x)\n", val);
        }
    }

    return vp::IO_REQ_OK;
}

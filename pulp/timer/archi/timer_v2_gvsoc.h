
/* THIS FILE HAS BEEN GENERATED, DO NOT MODIFY IT.
 */

/*
 * Copyright (C) 2020 GreenWaves Technologies
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

#ifndef __ARCHI_TIMER_V2_GVSOC__
#define __ARCHI_TIMER_V2_GVSOC__

#if !defined(LANGUAGE_ASSEMBLY) && !defined(__ASSEMBLER__)

#include <stdint.h>

#endif




//
// REGISTERS STRUCTS
//

#ifdef __GVSOC__

class vp_timer_v2_cfg_lo : public vp::Register<uint32_t>
{
public:
    inline void enable_set(uint32_t value) { this->set_field(value, TIMER_V2_CFG_LO_ENABLE_BIT, TIMER_V2_CFG_LO_ENABLE_WIDTH); }
    inline uint32_t enable_get() { return this->get_field(TIMER_V2_CFG_LO_ENABLE_BIT, TIMER_V2_CFG_LO_ENABLE_WIDTH); }
    inline void reset_set(uint32_t value) { this->set_field(value, TIMER_V2_CFG_LO_RESET_BIT, TIMER_V2_CFG_LO_RESET_WIDTH); }
    inline uint32_t reset_get() { return this->get_field(TIMER_V2_CFG_LO_RESET_BIT, TIMER_V2_CFG_LO_RESET_WIDTH); }
    inline void irqen_set(uint32_t value) { this->set_field(value, TIMER_V2_CFG_LO_IRQEN_BIT, TIMER_V2_CFG_LO_IRQEN_WIDTH); }
    inline uint32_t irqen_get() { return this->get_field(TIMER_V2_CFG_LO_IRQEN_BIT, TIMER_V2_CFG_LO_IRQEN_WIDTH); }
    inline void iem_set(uint32_t value) { this->set_field(value, TIMER_V2_CFG_LO_IEM_BIT, TIMER_V2_CFG_LO_IEM_WIDTH); }
    inline uint32_t iem_get() { return this->get_field(TIMER_V2_CFG_LO_IEM_BIT, TIMER_V2_CFG_LO_IEM_WIDTH); }
    inline void mode_set(uint32_t value) { this->set_field(value, TIMER_V2_CFG_LO_MODE_BIT, TIMER_V2_CFG_LO_MODE_WIDTH); }
    inline uint32_t mode_get() { return this->get_field(TIMER_V2_CFG_LO_MODE_BIT, TIMER_V2_CFG_LO_MODE_WIDTH); }
    inline void one_s_set(uint32_t value) { this->set_field(value, TIMER_V2_CFG_LO_ONE_S_BIT, TIMER_V2_CFG_LO_ONE_S_WIDTH); }
    inline uint32_t one_s_get() { return this->get_field(TIMER_V2_CFG_LO_ONE_S_BIT, TIMER_V2_CFG_LO_ONE_S_WIDTH); }
    inline void pen_set(uint32_t value) { this->set_field(value, TIMER_V2_CFG_LO_PEN_BIT, TIMER_V2_CFG_LO_PEN_WIDTH); }
    inline uint32_t pen_get() { return this->get_field(TIMER_V2_CFG_LO_PEN_BIT, TIMER_V2_CFG_LO_PEN_WIDTH); }
    inline void ccfg_set(uint32_t value) { this->set_field(value, TIMER_V2_CFG_LO_CCFG_BIT, TIMER_V2_CFG_LO_CCFG_WIDTH); }
    inline uint32_t ccfg_get() { return this->get_field(TIMER_V2_CFG_LO_CCFG_BIT, TIMER_V2_CFG_LO_CCFG_WIDTH); }
    inline void pval_set(uint32_t value) { this->set_field(value, TIMER_V2_CFG_LO_PVAL_BIT, TIMER_V2_CFG_LO_PVAL_WIDTH); }
    inline uint32_t pval_get() { return this->get_field(TIMER_V2_CFG_LO_PVAL_BIT, TIMER_V2_CFG_LO_PVAL_WIDTH); }
    inline void casc_set(uint32_t value) { this->set_field(value, TIMER_V2_CFG_LO_CASC_BIT, TIMER_V2_CFG_LO_CASC_WIDTH); }
    inline uint32_t casc_get() { return this->get_field(TIMER_V2_CFG_LO_CASC_BIT, TIMER_V2_CFG_LO_CASC_WIDTH); }
    vp_timer_v2_cfg_lo(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "CFG_LO";
        this->offset = 0x0;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0x8000ffff;
        this->reset_val = 0x0;
        this->regfields.push_back(new vp::regfield("ENABLE", 0, 1));
        this->regfields.push_back(new vp::regfield("RESET", 1, 1));
        this->regfields.push_back(new vp::regfield("IRQEN", 2, 1));
        this->regfields.push_back(new vp::regfield("IEM", 3, 1));
        this->regfields.push_back(new vp::regfield("MODE", 4, 1));
        this->regfields.push_back(new vp::regfield("ONE_S", 5, 1));
        this->regfields.push_back(new vp::regfield("PEN", 6, 1));
        this->regfields.push_back(new vp::regfield("CCFG", 7, 1));
        this->regfields.push_back(new vp::regfield("PVAL", 8, 8));
        this->regfields.push_back(new vp::regfield("CASC", 31, 1));
    }
};

class vp_timer_v2_cfg_hi : public vp::Register<uint32_t>
{
public:
    inline void enable_set(uint32_t value) { this->set_field(value, TIMER_V2_CFG_HI_ENABLE_BIT, TIMER_V2_CFG_HI_ENABLE_WIDTH); }
    inline uint32_t enable_get() { return this->get_field(TIMER_V2_CFG_HI_ENABLE_BIT, TIMER_V2_CFG_HI_ENABLE_WIDTH); }
    inline void reset_set(uint32_t value) { this->set_field(value, TIMER_V2_CFG_HI_RESET_BIT, TIMER_V2_CFG_HI_RESET_WIDTH); }
    inline uint32_t reset_get() { return this->get_field(TIMER_V2_CFG_HI_RESET_BIT, TIMER_V2_CFG_HI_RESET_WIDTH); }
    inline void irqen_set(uint32_t value) { this->set_field(value, TIMER_V2_CFG_HI_IRQEN_BIT, TIMER_V2_CFG_HI_IRQEN_WIDTH); }
    inline uint32_t irqen_get() { return this->get_field(TIMER_V2_CFG_HI_IRQEN_BIT, TIMER_V2_CFG_HI_IRQEN_WIDTH); }
    inline void iem_set(uint32_t value) { this->set_field(value, TIMER_V2_CFG_HI_IEM_BIT, TIMER_V2_CFG_HI_IEM_WIDTH); }
    inline uint32_t iem_get() { return this->get_field(TIMER_V2_CFG_HI_IEM_BIT, TIMER_V2_CFG_HI_IEM_WIDTH); }
    inline void mode_set(uint32_t value) { this->set_field(value, TIMER_V2_CFG_HI_MODE_BIT, TIMER_V2_CFG_HI_MODE_WIDTH); }
    inline uint32_t mode_get() { return this->get_field(TIMER_V2_CFG_HI_MODE_BIT, TIMER_V2_CFG_HI_MODE_WIDTH); }
    inline void one_s_set(uint32_t value) { this->set_field(value, TIMER_V2_CFG_HI_ONE_S_BIT, TIMER_V2_CFG_HI_ONE_S_WIDTH); }
    inline uint32_t one_s_get() { return this->get_field(TIMER_V2_CFG_HI_ONE_S_BIT, TIMER_V2_CFG_HI_ONE_S_WIDTH); }
    inline void pen_set(uint32_t value) { this->set_field(value, TIMER_V2_CFG_HI_PEN_BIT, TIMER_V2_CFG_HI_PEN_WIDTH); }
    inline uint32_t pen_get() { return this->get_field(TIMER_V2_CFG_HI_PEN_BIT, TIMER_V2_CFG_HI_PEN_WIDTH); }
    inline void clkcfg_set(uint32_t value) { this->set_field(value, TIMER_V2_CFG_HI_CLKCFG_BIT, TIMER_V2_CFG_HI_CLKCFG_WIDTH); }
    inline uint32_t clkcfg_get() { return this->get_field(TIMER_V2_CFG_HI_CLKCFG_BIT, TIMER_V2_CFG_HI_CLKCFG_WIDTH); }
    vp_timer_v2_cfg_hi(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "CFG_HI";
        this->offset = 0x4;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xff;
        this->reset_val = 0x0;
        this->regfields.push_back(new vp::regfield("ENABLE", 0, 1));
        this->regfields.push_back(new vp::regfield("RESET", 1, 1));
        this->regfields.push_back(new vp::regfield("IRQEN", 2, 1));
        this->regfields.push_back(new vp::regfield("IEM", 3, 1));
        this->regfields.push_back(new vp::regfield("MODE", 4, 1));
        this->regfields.push_back(new vp::regfield("ONE_S", 5, 1));
        this->regfields.push_back(new vp::regfield("PEN", 6, 1));
        this->regfields.push_back(new vp::regfield("CLKCFG", 7, 1));
    }
};

class vp_timer_v2_cnt_lo : public vp::Register<uint32_t>
{
public:
    inline void cnt_lo_set(uint32_t value) { this->set_field(value, TIMER_V2_CNT_LO_CNT_LO_BIT, TIMER_V2_CNT_LO_CNT_LO_WIDTH); }
    inline uint32_t cnt_lo_get() { return this->get_field(TIMER_V2_CNT_LO_CNT_LO_BIT, TIMER_V2_CNT_LO_CNT_LO_WIDTH); }
    vp_timer_v2_cnt_lo(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "CNT_LO";
        this->offset = 0x8;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
        this->regfields.push_back(new vp::regfield("CNT_LO", 0, 32));
    }
};

class vp_timer_v2_cnt_hi : public vp::Register<uint32_t>
{
public:
    inline void cnt_hi_set(uint32_t value) { this->set_field(value, TIMER_V2_CNT_HI_CNT_HI_BIT, TIMER_V2_CNT_HI_CNT_HI_WIDTH); }
    inline uint32_t cnt_hi_get() { return this->get_field(TIMER_V2_CNT_HI_CNT_HI_BIT, TIMER_V2_CNT_HI_CNT_HI_WIDTH); }
    vp_timer_v2_cnt_hi(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "CNT_HI";
        this->offset = 0xc;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
        this->regfields.push_back(new vp::regfield("CNT_HI", 0, 32));
    }
};

class vp_timer_v2_cmp_lo : public vp::Register<uint32_t>
{
public:
    inline void cmp_lo_set(uint32_t value) { this->set_field(value, TIMER_V2_CMP_LO_CMP_LO_BIT, TIMER_V2_CMP_LO_CMP_LO_WIDTH); }
    inline uint32_t cmp_lo_get() { return this->get_field(TIMER_V2_CMP_LO_CMP_LO_BIT, TIMER_V2_CMP_LO_CMP_LO_WIDTH); }
    vp_timer_v2_cmp_lo(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "CMP_LO";
        this->offset = 0x10;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
        this->regfields.push_back(new vp::regfield("CMP_LO", 0, 32));
    }
};

class vp_timer_v2_cmp_hi : public vp::Register<uint32_t>
{
public:
    inline void cmp_hi_set(uint32_t value) { this->set_field(value, TIMER_V2_CMP_HI_CMP_HI_BIT, TIMER_V2_CMP_HI_CMP_HI_WIDTH); }
    inline uint32_t cmp_hi_get() { return this->get_field(TIMER_V2_CMP_HI_CMP_HI_BIT, TIMER_V2_CMP_HI_CMP_HI_WIDTH); }
    vp_timer_v2_cmp_hi(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "CMP_HI";
        this->offset = 0x14;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
        this->regfields.push_back(new vp::regfield("CMP_HI", 0, 32));
    }
};

class vp_timer_v2_start_lo : public vp::Register<uint32_t>
{
public:
    inline void strt_lo_set(uint32_t value) { this->set_field(value, TIMER_V2_START_LO_STRT_LO_BIT, TIMER_V2_START_LO_STRT_LO_WIDTH); }
    inline uint32_t strt_lo_get() { return this->get_field(TIMER_V2_START_LO_STRT_LO_BIT, TIMER_V2_START_LO_STRT_LO_WIDTH); }
    vp_timer_v2_start_lo(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "START_LO";
        this->offset = 0x18;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0x1;
        this->reset_val = 0x0;
        this->regfields.push_back(new vp::regfield("STRT_LO", 0, 1));
    }
};

class vp_timer_v2_start_hi : public vp::Register<uint32_t>
{
public:
    inline void strt_hi_set(uint32_t value) { this->set_field(value, TIMER_V2_START_HI_STRT_HI_BIT, TIMER_V2_START_HI_STRT_HI_WIDTH); }
    inline uint32_t strt_hi_get() { return this->get_field(TIMER_V2_START_HI_STRT_HI_BIT, TIMER_V2_START_HI_STRT_HI_WIDTH); }
    vp_timer_v2_start_hi(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "START_HI";
        this->offset = 0x1c;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0x1;
        this->reset_val = 0x0;
        this->regfields.push_back(new vp::regfield("STRT_HI", 0, 1));
    }
};

class vp_timer_v2_reset_lo : public vp::Register<uint32_t>
{
public:
    inline void rst_lo_set(uint32_t value) { this->set_field(value, TIMER_V2_RESET_LO_RST_LO_BIT, TIMER_V2_RESET_LO_RST_LO_WIDTH); }
    inline uint32_t rst_lo_get() { return this->get_field(TIMER_V2_RESET_LO_RST_LO_BIT, TIMER_V2_RESET_LO_RST_LO_WIDTH); }
    vp_timer_v2_reset_lo(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "RESET_LO";
        this->offset = 0x20;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0x1;
        this->reset_val = 0x0;
        this->regfields.push_back(new vp::regfield("RST_LO", 0, 1));
    }
};

class vp_timer_v2_reset_hi : public vp::Register<uint32_t>
{
public:
    inline void rst_hi_set(uint32_t value) { this->set_field(value, TIMER_V2_RESET_HI_RST_HI_BIT, TIMER_V2_RESET_HI_RST_HI_WIDTH); }
    inline uint32_t rst_hi_get() { return this->get_field(TIMER_V2_RESET_HI_RST_HI_BIT, TIMER_V2_RESET_HI_RST_HI_WIDTH); }
    vp_timer_v2_reset_hi(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "RESET_HI";
        this->offset = 0x24;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0x1;
        this->reset_val = 0x0;
        this->regfields.push_back(new vp::regfield("RST_HI", 0, 1));
    }
};


class vp_regmap_timer_v2 : public vp::regmap
{
public:
    vp_timer_v2_cfg_lo cfg_lo;
    vp_timer_v2_cfg_hi cfg_hi;
    vp_timer_v2_cnt_lo cnt_lo;
    vp_timer_v2_cnt_hi cnt_hi;
    vp_timer_v2_cmp_lo cmp_lo;
    vp_timer_v2_cmp_hi cmp_hi;
    vp_timer_v2_start_lo start_lo;
    vp_timer_v2_start_hi start_hi;
    vp_timer_v2_reset_lo reset_lo;
    vp_timer_v2_reset_hi reset_hi;
    vp_regmap_timer_v2(vp::Block &top, std::string name): vp::regmap(top, name),
        cfg_lo(*this, "cfg_lo"),
        cfg_hi(*this, "cfg_hi"),
        cnt_lo(*this, "cnt_lo"),
        cnt_hi(*this, "cnt_hi"),
        cmp_lo(*this, "cmp_lo"),
        cmp_hi(*this, "cmp_hi"),
        start_lo(*this, "start_lo"),
        start_hi(*this, "start_hi"),
        reset_lo(*this, "reset_lo"),
        reset_hi(*this, "reset_hi")
    {
        this->registers_new.push_back(&this->cfg_lo);
        this->registers_new.push_back(&this->cfg_hi);
        this->registers_new.push_back(&this->cnt_lo);
        this->registers_new.push_back(&this->cnt_hi);
        this->registers_new.push_back(&this->cmp_lo);
        this->registers_new.push_back(&this->cmp_hi);
        this->registers_new.push_back(&this->start_lo);
        this->registers_new.push_back(&this->start_hi);
        this->registers_new.push_back(&this->reset_lo);
        this->registers_new.push_back(&this->reset_hi);
    }
};

#endif

#endif


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

#ifndef __ARCHI_SSR_GVSOC__
#define __ARCHI_SSR_GVSOC__

#if !defined(LANGUAGE_ASSEMBLY) && !defined(__ASSEMBLER__)

#include <stdint.h>

#endif




//
// REGISTERS STRUCTS
//

#ifdef __GVSOC__

class vp_ssr_status : public vp::Register<uint32_t>
{
public:
    vp_ssr_status(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "STATUS";
        this->offset = 0x0;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
    }
};

class vp_ssr_repeat : public vp::Register<uint32_t>
{
public:
    vp_ssr_repeat(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "REPEAT";
        this->offset = 0x4;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
    }
};

class vp_ssr_bounds_0 : public vp::Register<uint32_t>
{
public:
    vp_ssr_bounds_0(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "BOUNDS_0";
        this->offset = 0x8;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
    }
};

class vp_ssr_bounds_1 : public vp::Register<uint32_t>
{
public:
    vp_ssr_bounds_1(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "BOUNDS_1";
        this->offset = 0xc;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
    }
};

class vp_ssr_bounds_2 : public vp::Register<uint32_t>
{
public:
    vp_ssr_bounds_2(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "BOUNDS_2";
        this->offset = 0x10;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
    }
};

class vp_ssr_bounds_3 : public vp::Register<uint32_t>
{
public:
    vp_ssr_bounds_3(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "BOUNDS_3";
        this->offset = 0x14;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
    }
};

class vp_ssr_strides_0 : public vp::Register<uint32_t>
{
public:
    vp_ssr_strides_0(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "STRIDES_0";
        this->offset = 0x18;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
    }
};

class vp_ssr_strides_1 : public vp::Register<uint32_t>
{
public:
    vp_ssr_strides_1(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "STRIDES_1";
        this->offset = 0x1c;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
    }
};

class vp_ssr_strides_2 : public vp::Register<uint32_t>
{
public:
    vp_ssr_strides_2(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "STRIDES_2";
        this->offset = 0x20;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
    }
};

class vp_ssr_strides_3 : public vp::Register<uint32_t>
{
public:
    vp_ssr_strides_3(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "STRIDES_3";
        this->offset = 0x24;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
    }
};

class vp_ssr_rptr_0 : public vp::Register<uint32_t>
{
public:
    vp_ssr_rptr_0(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "RPTR_0";
        this->offset = 0x60;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
    }
};

class vp_ssr_rptr_1 : public vp::Register<uint32_t>
{
public:
    vp_ssr_rptr_1(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "RPTR_1";
        this->offset = 0x64;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
    }
};

class vp_ssr_rptr_2 : public vp::Register<uint32_t>
{
public:
    vp_ssr_rptr_2(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "RPTR_2";
        this->offset = 0x68;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
    }
};

class vp_ssr_rptr_3 : public vp::Register<uint32_t>
{
public:
    vp_ssr_rptr_3(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "RPTR_3";
        this->offset = 0x6c;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
    }
};

class vp_ssr_wptr_0 : public vp::Register<uint32_t>
{
public:
    vp_ssr_wptr_0(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "WPTR_0";
        this->offset = 0x70;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
    }
};

class vp_ssr_wptr_1 : public vp::Register<uint32_t>
{
public:
    vp_ssr_wptr_1(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "WPTR_1";
        this->offset = 0x74;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
    }
};

class vp_ssr_wptr_2 : public vp::Register<uint32_t>
{
public:
    vp_ssr_wptr_2(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "WPTR_2";
        this->offset = 0x78;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
    }
};

class vp_ssr_wptr_3 : public vp::Register<uint32_t>
{
public:
    vp_ssr_wptr_3(vp::Block &top, std::string name) : vp::Register<uint32_t>(top, name, 32, true, 0)
    {
        this->name = "WPTR_3";
        this->offset = 0x7c;
        this->width = 32;
        this->do_reset = 1;
        this->write_mask = 0xffffffff;
        this->reset_val = 0x0;
    }
};


class vp_regmap_ssr : public vp::regmap
{
public:
    vp_ssr_status status;
    vp_ssr_repeat repeat;
    vp_ssr_bounds_0 bounds_0;
    vp_ssr_bounds_1 bounds_1;
    vp_ssr_bounds_2 bounds_2;
    vp_ssr_bounds_3 bounds_3;
    vp_ssr_strides_0 strides_0;
    vp_ssr_strides_1 strides_1;
    vp_ssr_strides_2 strides_2;
    vp_ssr_strides_3 strides_3;
    vp_ssr_rptr_0 rptr_0;
    vp_ssr_rptr_1 rptr_1;
    vp_ssr_rptr_2 rptr_2;
    vp_ssr_rptr_3 rptr_3;
    vp_ssr_wptr_0 wptr_0;
    vp_ssr_wptr_1 wptr_1;
    vp_ssr_wptr_2 wptr_2;
    vp_ssr_wptr_3 wptr_3;
    vp_regmap_ssr(vp::Block &top, std::string name): vp::regmap(top, name),
        status(*this, "status"),
        repeat(*this, "repeat"),
        bounds_0(*this, "bounds_0"),
        bounds_1(*this, "bounds_1"),
        bounds_2(*this, "bounds_2"),
        bounds_3(*this, "bounds_3"),
        strides_0(*this, "strides_0"),
        strides_1(*this, "strides_1"),
        strides_2(*this, "strides_2"),
        strides_3(*this, "strides_3"),
        rptr_0(*this, "rptr_0"),
        rptr_1(*this, "rptr_1"),
        rptr_2(*this, "rptr_2"),
        rptr_3(*this, "rptr_3"),
        wptr_0(*this, "wptr_0"),
        wptr_1(*this, "wptr_1"),
        wptr_2(*this, "wptr_2"),
        wptr_3(*this, "wptr_3")
    {
        this->registers_new.push_back(&this->status);
        this->registers_new.push_back(&this->repeat);
        this->registers_new.push_back(&this->bounds_0);
        this->registers_new.push_back(&this->bounds_1);
        this->registers_new.push_back(&this->bounds_2);
        this->registers_new.push_back(&this->bounds_3);
        this->registers_new.push_back(&this->strides_0);
        this->registers_new.push_back(&this->strides_1);
        this->registers_new.push_back(&this->strides_2);
        this->registers_new.push_back(&this->strides_3);
        this->registers_new.push_back(&this->rptr_0);
        this->registers_new.push_back(&this->rptr_1);
        this->registers_new.push_back(&this->rptr_2);
        this->registers_new.push_back(&this->rptr_3);
        this->registers_new.push_back(&this->wptr_0);
        this->registers_new.push_back(&this->wptr_1);
        this->registers_new.push_back(&this->wptr_2);
        this->registers_new.push_back(&this->wptr_3);
    }
};

#endif

#endif

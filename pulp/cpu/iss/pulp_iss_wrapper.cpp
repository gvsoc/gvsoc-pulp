/*
 * Copyright (C) 2020 ETH Zurich
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
 * Authors: Germain Haugou, GreenWaves Technologies (germain.haugou@greenwaves-technologies.com)
 */


#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include "pulp_iss_wrapper.hpp"
#include <cpu/iss/include/iss_core.hpp>

pulp_iss_wrapper::pulp_iss_wrapper(vp::ComponentConf &config)
: IssWrapper(config)
{
    this->iss.csr.declare_pcer(CSR_PCER_CYCLES, "cycles", "Count the number of cycles the core was running");
    this->iss.csr.declare_pcer(CSR_PCER_INSTR, "instr", "Count the number of instructions executed");
    this->iss.csr.declare_pcer(CSR_PCER_LD_STALL, "ld_stall", "Number of load use hazards");
    this->iss.csr.declare_pcer(CSR_PCER_JMP_STALL, "jmp_stall", "Number of jump register hazards");
    this->iss.csr.declare_pcer(CSR_PCER_IMISS, "imiss", "Cycles waiting for instruction fetches. i.e. the number of instructions wasted due to non-ideal caches");
    this->iss.csr.declare_pcer(CSR_PCER_LD, "ld", "Number of memory loads executed. Misaligned accesses are counted twice");
    this->iss.csr.declare_pcer(CSR_PCER_ST, "st", "Number of memory stores executed. Misaligned accesses are counted twice");
    this->iss.csr.declare_pcer(CSR_PCER_JUMP, "jump", "Number of jump instructions seen, i.e. j, jr, jal, jalr");
    this->iss.csr.declare_pcer(CSR_PCER_BRANCH, "branch", "Number of branch instructions seen, i.e. bf, bnf");
    this->iss.csr.declare_pcer(CSR_PCER_TAKEN_BRANCH, "taken_branch", "Number of taken branch instructions seen, i.e. bf, bnf");
    this->iss.csr.declare_pcer(CSR_PCER_RVC, "rvc", "Number of compressed instructions");
    this->iss.csr.declare_pcer(CSR_PCER_ELW, "elw", "Cycles wasted due to ELW instruction");
    
#if defined(CONFIG_GVSOC_ISS_EXTERNAL_PCCR)
    this->iss.csr.declare_pcer(CSR_PCER_LD_EXT, "ld_ext", "Number of memory loads to EXT executed. Misaligned accesses are counted twice. Every non-TCDM access is considered external");
    this->iss.csr.declare_pcer(CSR_PCER_ST_EXT, "st_ext", "Number of memory stores to EXT executed. Misaligned accesses are counted twice. Every non-TCDM access is considered external");
    this->iss.csr.declare_pcer(CSR_PCER_LD_EXT_CYC, "ld_ext_cycles", "Cycles used for memory loads to EXT. Every non-TCDM access is considered external");
    this->iss.csr.declare_pcer(CSR_PCER_ST_EXT_CYC, "st_ext_cycles", "Cycles used for memory stores to EXT. Every non-TCDM access is considered external");
    this->iss.csr.declare_pcer(CSR_PCER_TCDM_CONT, "tcdm_cont", "Cycles wasted due to TCDM/log-interconnect contention");
#endif
    this->iss.csr.declare_pcer(CSR_PCER_MISALIGNED, "misaligned", "Cycles wasted due to misaligned accesses");
    this->iss.csr.declare_pcer(CSR_PCER_INSN_CONT, "insn_cont", "Cycles wasted due to instruction contentions on shared units");

    traces.new_trace_event("pcer_cycles", &this->iss.timing.pcer_trace_event[0], 1);
    traces.new_trace_event("pcer_instr", &this->iss.timing.pcer_trace_event[1], 1);
    traces.new_trace_event("pcer_ld_stall", &this->iss.timing.pcer_trace_event[2], 1);
    traces.new_trace_event("pcer_jmp_stall", &this->iss.timing.pcer_trace_event[3], 1);
    traces.new_trace_event("pcer_imiss", &this->iss.timing.pcer_trace_event[4], 1);
    traces.new_trace_event("pcer_ld", &this->iss.timing.pcer_trace_event[5], 1);
    traces.new_trace_event("pcer_st", &this->iss.timing.pcer_trace_event[6], 1);
    traces.new_trace_event("pcer_jump", &this->iss.timing.pcer_trace_event[7], 1);
    traces.new_trace_event("pcer_branch", &this->iss.timing.pcer_trace_event[8], 1);
    traces.new_trace_event("pcer_taken_branch", &this->iss.timing.pcer_trace_event[9], 1);
    traces.new_trace_event("pcer_rvc", &this->iss.timing.pcer_trace_event[10], 1);
    traces.new_trace_event("elw", &this->iss.timing.pcer_trace_event[11], 1);
#if defined(CONFIG_GVSOC_ISS_EXTERNAL_PCCR)
    traces.new_trace_event("pcer_ld_ext", &this->iss.timing.pcer_trace_event[12], 1);
    traces.new_trace_event("pcer_st_ext", &this->iss.timing.pcer_trace_event[13], 1);
    traces.new_trace_event("pcer_ld_ext_cycles", &this->iss.timing.pcer_trace_event[14], 1);
    traces.new_trace_event("pcer_st_ext_cycles", &this->iss.timing.pcer_trace_event[15], 1);
    traces.new_trace_event("pcer_tcdm_cont", &this->iss.timing.pcer_trace_event[16], 1);
#endif

    traces.new_trace_event("pcer_misaligned", &this->iss.timing.pcer_trace_event[29], 1);
    traces.new_trace_event("pcer_insn_cont", &this->iss.timing.pcer_trace_event[30], 1);

    iss_decoder_item_t *isa = iss_isa_get(&this->iss);
    int core_id = this->iss.csr.mhartid;
    bool is_fc = core_id == 9;

    int fpu_base=0, fpu_div=1, int64=2;

    if (!__iss_isa_set.initialized)
    {
        __iss_isa_set.initialized = true;

        if (is_fc)
        {
            iss_resource_declare(&this->iss, fpu_base, 1);
            iss_resource_declare(&this->iss, fpu_div, 1);
        }
        else
        {
            iss_resource_declare(&this->iss, fpu_base, 4);
            iss_resource_declare(&this->iss, fpu_div, 1);
        }

        iss_resource_attach_from_tag(&this->iss, "fadd", fpu_base, 1, 1);
        iss_resource_attach_from_tag(&this->iss, "fmadd", fpu_base, 1, 1);
        iss_resource_attach_from_tag(&this->iss, "fmul", fpu_base, 1, 1);
        iss_resource_attach_from_tag(&this->iss, "fconv", fpu_base, 1, 1);
        iss_resource_attach_from_tag(&this->iss, "fother", fpu_base, 1, 1);
        iss_resource_attach_from_tag(&this->iss, "sfmadd", fpu_base, 1, 1);
        iss_resource_attach_from_tag(&this->iss, "sfadd", fpu_base, 1, 1);
        iss_resource_attach_from_tag(&this->iss, "sfmul", fpu_base, 1, 1);
        iss_resource_attach_from_tag(&this->iss, "sfconv", fpu_base, 1, 1);
        iss_resource_attach_from_tag(&this->iss, "sfother", fpu_base, 1, 1);
        iss_resource_attach_from_tag(&this->iss, "fdiv", fpu_div, 14, 14);
        iss_resource_attach_from_tag(&this->iss, "sfdiv", fpu_div, 10, 10);

        for (iss_decoder_item_t *insn: *this->iss.decode.get_insns_from_tag("mul"))
        {
            insn->u.insn.latency = 2;
        }
        for (iss_decoder_item_t *insn: *this->iss.decode.get_insns_from_tag("mulh"))
        {
            insn->u.insn.latency = 5;
        }

        // TODO there should be a latency for jump using the register set
        // in previous cycle, and report the stalls as jump stalls

    }

    if (is_fc)
    {
        iss_resource_assign_instance(&this->iss, fpu_base, 0);
        iss_resource_assign_instance(&this->iss, fpu_div, 0);
    }
    else
    {
        iss_resource_assign_instance(&this->iss, fpu_base, core_id % 4);
        iss_resource_assign_instance(&this->iss, fpu_div, 0);
    }
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
  return new pulp_iss_wrapper(config);
}

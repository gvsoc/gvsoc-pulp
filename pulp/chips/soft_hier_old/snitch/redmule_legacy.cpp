#include <cpu/iss/include/iss.hpp>
#include <cpu/iss/include/isa_lib/int.h>
#include <cpu/iss/include/isa_lib/macros.h>
#include <cpu/iss/include/offload.hpp>
#include <pulp/chips/soft_hier_old/snitch/redmule_legacy.hpp>

iss_reg_t mcnfig_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    IssOffloadInsn<iss_reg_t> offload_insn = {
        .opcode=insn->opcode,
        .arg_a=REG_GET(0),
        .arg_b=REG_GET(1),
    };
    iss->exec.offload_insn(&offload_insn);

    if (!offload_insn.granted)
    {
        iss->exec.stall_reg = REG_OUT(0);
        iss->exec.insn_stall();
    }

    return iss_insn_next(iss, insn, pc);
}

iss_reg_t marith_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    IssOffloadInsn<iss_reg_t> offload_insn = {
        .opcode=insn->opcode,
        .arg_a=REG_GET(0),
        .arg_b=REG_GET(1),
        .arg_c=REG_GET(2),
        .arg_d=UIM_GET(0),
    };
    iss->exec.offload_insn(&offload_insn);

    if (!offload_insn.granted)
    {
        iss->exec.stall_reg = REG_OUT(0);
        iss->exec.insn_stall();
    }

    return iss_insn_next(iss, insn, pc);
}

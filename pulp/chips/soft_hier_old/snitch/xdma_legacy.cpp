#include <cpu/iss/include/iss.hpp>
#include <cpu/iss/include/isa_lib/int.h>
#include <cpu/iss/include/isa_lib/macros.h>
#include <cpu/iss/include/offload.hpp>
#include <pulp/chips/soft_hier_old/snitch/xdma_legacy.hpp>

iss_reg_t dmmask_exec(Iss *iss, iss_insn_t *insn, iss_reg_t pc)
{
    IssOffloadInsn<iss_reg_t> offload_insn = {
        .opcode=insn->opcode,
        .arg_b=REG_GET(1),
    };
    iss->exec.offload_insn(&offload_insn);
    REG_SET(0, offload_insn.result);
    return iss_insn_next(iss, insn, pc);
}

/*
 * Copyright (C) 2024 ETH Zurich and University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <cpu/iss/include/offload.hpp>


#define DMSRC_FUNCT7    0b0000000
#define DMDST_FUNCT7    0b0000001
#define DMCPYI_FUNCT7   0b0000010
#define DMCPYC_FUNCT7   0b0000011
#define DMSTATI_FUNCT7  0b0000100
#define DMMASK_FUNCT7   0b0000101
#define DMSTR_FUNCT7    0b0000110
#define DMREP_FUNCT7    0b0000111

enum FlexDmaMode {
    FLEX_DMA_MODE_1D       = 0,
    FLEX_DMA_MODE_2D       = 1,
    FLEX_DMA_MODE_BCAST    = 2,
    FLEX_DMA_MODE_REDUCE   = 3,
};

// CSR map (word offsets)
#define REG_SRC_LO       0x00
#define REG_SRC_HI       0x04
#define REG_DST_LO       0x08
#define REG_DST_HI       0x0C
#define REG_SIZE         0x10
#define REG_SRC_STRIDE   0x14
#define REG_DST_STRIDE   0x18
#define REG_REPS         0x1C
#define REG_MASK         0x20
#define REG_REDUCT_FMT   0x24
#define REG_MODE         0x28
#define REG_LAUNCH       0x2C
#define REG_STATUS       0x30


class FlexMemPoolDmaCtrl : public vp::Component
{
public:
    FlexMemPoolDmaCtrl(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

    void emit(uint32_t funct7, uint32_t imm5, uint32_t arg_a, uint32_t arg_b,
              uint32_t *result_out);
    uint32_t launch();
    uint32_t status();

    vp::Trace trace;
    vp::IoSlave input_itf;
    vp::WireMaster<IssOffloadInsn<uint32_t>*> dma_offload_itf;

    // Latched CSRs
    uint32_t src_lo      = 0;
    uint32_t src_hi      = 0;
    uint32_t dst_lo      = 0;
    uint32_t dst_hi      = 0;
    uint32_t size        = 0;
    uint32_t src_stride  = 0;
    uint32_t dst_stride  = 0;
    uint32_t reps        = 0;
    uint32_t mask        = 0;
    uint32_t reduct_fmt  = 0;
    uint32_t mode        = 0;
};


FlexMemPoolDmaCtrl::FlexMemPoolDmaCtrl(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->input_itf.set_req_meth(&FlexMemPoolDmaCtrl::req);
    this->new_slave_port("input", &this->input_itf);
    this->new_master_port("dma_offload", &this->dma_offload_itf);
}


void FlexMemPoolDmaCtrl::emit(uint32_t funct7, uint32_t imm5,
                              uint32_t arg_a, uint32_t arg_b, uint32_t *result_out)
{
    IssOffloadInsn<uint32_t> insn;
    insn.opcode = ((uint64_t)funct7 << 25) | ((uint64_t)(imm5 & 0x1f) << 20);
    insn.arg_a  = arg_a;
    insn.arg_b  = arg_b;
    insn.granted = true;
    this->dma_offload_itf.sync(&insn);
    if (result_out) *result_out = insn.result;
}


uint32_t FlexMemPoolDmaCtrl::launch()
{
    uint32_t tx_id = 0;

    // Program src/dst on every launch (they are always meaningful)
    this->emit(DMSRC_FUNCT7, 0, this->src_lo, this->src_hi, nullptr);
    this->emit(DMDST_FUNCT7, 0, this->dst_lo, this->dst_hi, nullptr);

    switch (this->mode)
    {
        case FLEX_DMA_MODE_1D:
        {
            // dmcpyi: size in arg_a, config in arg_b
            this->emit(DMCPYI_FUNCT7, 0, this->size, 0, &tx_id);
            break;
        }

        case FLEX_DMA_MODE_2D:
        {
            // dmstr: src_stride in arg_a, dst_stride in arg_b
            this->emit(DMSTR_FUNCT7, 0, this->src_stride, this->dst_stride, nullptr);
            // dmrep: reps in arg_a
            this->emit(DMREP_FUNCT7, 0, this->reps, 0, nullptr);
            // dmcpyi with 2D config (0b10)
            this->emit(DMCPYI_FUNCT7, 0, this->size, 0b10, &tx_id);
            break;
        }

        case FLEX_DMA_MODE_BCAST:
        {
            // dmmask: masks packed in arg_b (col_mask<<16 | row_mask)
            this->emit(DMMASK_FUNCT7, 0, 0, this->mask, nullptr);
            // Re-issue src/dst after mask (frontend keeps them sticky but harmless)
            // dmcpyc: collective_type=1 (broadcast) in imm5, size in arg_a
            this->emit(DMCPYC_FUNCT7, 0b00001, this->size, 0, &tx_id);
            break;
        }

        case FLEX_DMA_MODE_REDUCE:
        {
            this->emit(DMMASK_FUNCT7, 0, 0, this->mask, nullptr);
            // reduction collective_type: 0=REDADD_UINT16 -> imm=2, 1=REDADD_INT16 -> 3,
            // 2=REDADD_FP16 -> 4, 3=REDMAX_UINT16 -> 5, 4=REDMAX_INT16 -> 6, 5=REDMAX_FP16 -> 7
            uint32_t imm = (this->reduct_fmt + 2) & 0x1f;
            this->emit(DMCPYC_FUNCT7, imm, this->size, 0, &tx_id);
            break;
        }

        default:
            this->trace.msg("FlexMemPoolDmaCtrl: unknown mode %d\n", this->mode);
            break;
    }

    return tx_id;
}


uint32_t FlexMemPoolDmaCtrl::status()
{
    // dmstati arg_b=2 -> "any in-flight transfer"
    uint32_t busy = 0;
    IssOffloadInsn<uint32_t> insn;
    insn.opcode  = ((uint64_t)DMSTATI_FUNCT7 << 25);
    insn.arg_a   = 0;
    insn.arg_b   = 2;
    insn.granted = true;
    this->dma_offload_itf.sync(&insn);
    busy = insn.result;
    return busy;
}


vp::IoReqStatus FlexMemPoolDmaCtrl::req(vp::Block *__this, vp::IoReq *req)
{
    FlexMemPoolDmaCtrl *_this = (FlexMemPoolDmaCtrl *)__this;

    uint64_t offset  = req->get_addr();
    uint8_t *data    = req->get_data();
    uint64_t size    = req->get_size();
    bool is_write    = req->get_is_write();

    if (size != 4)
    {
        _this->trace.msg("Invalid DMA CSR access (offset: 0x%x, size: %d)\n",
            (unsigned)offset, (int)size);
        return vp::IO_REQ_INVALID;
    }

    if (is_write)
    {
        uint32_t value = *(uint32_t *)data;
        _this->trace.msg("FlexDMA CSR write (offset: 0x%x, data: 0x%x)\n",
            (unsigned)offset, value);

        switch (offset)
        {
            case REG_SRC_LO:     _this->src_lo     = value; break;
            case REG_SRC_HI:     _this->src_hi     = value; break;
            case REG_DST_LO:     _this->dst_lo     = value; break;
            case REG_DST_HI:     _this->dst_hi     = value; break;
            case REG_SIZE:       _this->size       = value; break;
            case REG_SRC_STRIDE: _this->src_stride = value; break;
            case REG_DST_STRIDE: _this->dst_stride = value; break;
            case REG_REPS:       _this->reps       = value; break;
            case REG_MASK:       _this->mask       = value; break;
            case REG_REDUCT_FMT: _this->reduct_fmt = value; break;
            case REG_MODE:       _this->mode       = value; break;
            default:
                _this->trace.msg("FlexDMA CSR write to unmapped offset 0x%x\n",
                    (unsigned)offset);
                break;
        }
    }
    else
    {
        uint32_t out = 0;
        switch (offset)
        {
            case REG_LAUNCH: out = _this->launch(); break;
            case REG_STATUS: out = _this->status(); break;
            default:
                _this->trace.msg("FlexDMA CSR read from unmapped offset 0x%x\n",
                    (unsigned)offset);
                break;
        }
        *(uint32_t *)data = out;
    }

    return vp::IO_REQ_OK;
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new FlexMemPoolDmaCtrl(config);
}

// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

#include "riscv_ir.h"
#include "riscv_elf.h"
#include "riscv_instr.h"
#include "riscv_mem.h"

#include <stdio.h>
#include <stdlib.h>

#define EMIT_IR(...) ir_builder_push(builder, (ir_instr){__VA_ARGS__})

typedef void (*rv_handler)(ir_builder *builder, uint32_t instr, uint64_t pc);

static void emit_invalid_instruction(ir_builder *builder, uint32_t instr, uint64_t pc) {
    fprintf(stderr, "riscv-jit: unknown instruction 0x%08x at pc 0x%016lx\n", instr, (unsigned long)pc);

    EMIT_IR(IR_OP_UD2, IR_NO_REGISTER, IR_NO_REGISTER, IR_NO_REGISTER, IR_NO_IMMEDIATE, pc);
}

static bool lookup_funct3_opcode(const ir_opcode ops[RV_FUNCT3_COUNT], uint32_t funct3, ir_opcode *out_op) {
    if (funct3 >= RV_FUNCT3_COUNT || ops[funct3] == IR_OP_NOP) {
        return false;
    }

    *out_op = ops[funct3];
    return true;
}

static void emit_funct7_opcode(ir_builder *builder, uint32_t instr, uint32_t f7, ir_opcode base_op, ir_opcode alt_op, uint32_t rd, uint32_t rs1, uint32_t rs2, uint64_t pc) {
    if (f7 == RV_F7_BASE) {
        EMIT_IR(base_op, rd, rs1, rs2, IR_NO_IMMEDIATE, pc);
    } else if (f7 == RV_F7_ALT) {
        EMIT_IR(alt_op, rd, rs1, rs2, IR_NO_IMMEDIATE, pc);
    } else {
        emit_invalid_instruction(builder, instr, pc);
    }
}

static void emit_branch_to_pc(ir_builder *builder, ir_opcode op, uint32_t rs1, uint32_t rs2, uint64_t pc, int32_t offset) {
    EMIT_IR(op, IR_NO_REGISTER, rs1, rs2, pc + offset, pc);
}

static void handle_rv32_store(ir_builder *builder, uint32_t instr, uint64_t pc) {
    static const ir_opcode store_ops[RV_FUNCT3_COUNT] = {
        [RV_F3_SB] = IR_OP_STORE8,
        [RV_F3_SH] = IR_OP_STORE16,
        [RV_F3_SW] = IR_OP_STORE32,
        [RV_F3_SD] = IR_OP_STORE64,
    };

    ir_opcode op;
    if (lookup_funct3_opcode(store_ops, RV32_FUNCT3(instr), &op)) {
        EMIT_IR(op, IR_NO_REGISTER, RV32_RS1(instr), RV32_RS2(instr), RV32_IMM_S(instr), pc);
    } else {
        emit_invalid_instruction(builder, instr, pc);
    }
}

static void handle_rv32_load(ir_builder *builder, uint32_t instr, uint64_t pc) {
    static const ir_opcode load_ops[RV_FUNCT3_COUNT] = {
        [RV_F3_LB]  = IR_OP_LOAD8,
        [RV_F3_LH]  = IR_OP_LOAD16,
        [RV_F3_LW]  = IR_OP_LOAD32,
        [RV_F3_LD]  = IR_OP_LOAD64,
        [RV_F3_LBU] = IR_OP_LOAD8_U,
        [RV_F3_LHU] = IR_OP_LOAD16_U,
        [RV_F3_LWU] = IR_OP_LOAD32_U,
    };

    ir_opcode op;
    if (lookup_funct3_opcode(load_ops, RV32_FUNCT3(instr), &op)) {
        EMIT_IR(op, RV32_RD(instr), RV32_RS1(instr), IR_NO_REGISTER, RV32_IMM_I(instr), pc);
    } else {
        emit_invalid_instruction(builder, instr, pc);
    }
}

static void handle_rv32_op_imm(ir_builder *builder, uint32_t instr, uint64_t pc) {
    static const ir_opcode imm_ops[RV_FUNCT3_COUNT] = {
        [RV_F3_ADD_SUB] = IR_OP_ADDI,
        [RV_F3_SLT]     = IR_OP_SLTI,
        [RV_F3_SLTU]    = IR_OP_SLTIU,
        [RV_F3_XOR]     = IR_OP_XORI,
        [RV_F3_OR]      = IR_OP_ORI,
        [RV_F3_AND]     = IR_OP_ANDI,
    };

    uint32_t f3  = RV32_FUNCT3(instr);
    uint32_t rd  = RV32_RD(instr);
    uint32_t rs1 = RV32_RS1(instr);

    if (f3 == RV_F3_SLL) {
        EMIT_IR(IR_OP_SLLI, rd, rs1, IR_NO_REGISTER, RV64_SHAMT(instr), pc);
        return;
    }

    if (f3 == RV_F3_SRL_SRA) {
        uint32_t f6 = RV64_FUNCT6(instr);
        if (f6 == RV64_F6_SRL) {
            EMIT_IR(IR_OP_SRLI, rd, rs1, IR_NO_REGISTER, RV64_SHAMT(instr), pc);
        } else if (f6 == RV64_F6_SRA) {
            EMIT_IR(IR_OP_SRAI, rd, rs1, IR_NO_REGISTER, RV64_SHAMT(instr), pc);
        } else {
            emit_invalid_instruction(builder, instr, pc);
        }
        return;
    }

    ir_opcode op;
    if (lookup_funct3_opcode(imm_ops, f3, &op)) {
        EMIT_IR(op, rd, rs1, IR_NO_REGISTER, RV32_IMM_I(instr), pc);
    } else {
        emit_invalid_instruction(builder, instr, pc);
    }
}

static void handle_rv32_op_imm_32(ir_builder *builder, uint32_t instr, uint64_t pc) {
    uint32_t f3  = RV32_FUNCT3(instr);
    uint32_t f7  = RV32_FUNCT7(instr);
    uint32_t rd  = RV32_RD(instr);
    uint32_t rs1 = RV32_RS1(instr);

    if (f3 == RV_F3_ADD_SUB) {
        EMIT_IR(IR_OP_ADDI_EXT32, rd, rs1, IR_NO_REGISTER, RV32_IMM_I(instr), pc);
    } else if (f3 == RV_F3_SLL && f7 == RV_F7_BASE) {
        EMIT_IR(IR_OP_SLLI_EXT32, rd, rs1, IR_NO_REGISTER, RV32_SHAMT(instr), pc);
    } else if (f3 == RV_F3_SRL_SRA && f7 == RV_F7_BASE) {
        EMIT_IR(IR_OP_SRLI_EXT32, rd, rs1, IR_NO_REGISTER, RV32_SHAMT(instr), pc);
    } else if (f3 == RV_F3_SRL_SRA && f7 == RV_F7_ALT) {
        EMIT_IR(IR_OP_SRAI_EXT32, rd, rs1, IR_NO_REGISTER, RV32_SHAMT(instr), pc);
    } else {
        emit_invalid_instruction(builder, instr, pc);
    }
}

static void handle_rv32_op_muldiv(ir_builder *builder, uint32_t instr, uint64_t pc) {
    static const ir_opcode muldiv_ops[RV_FUNCT3_COUNT] = {
        [RV_F3_MUL]    = IR_OP_MUL,
        [RV_F3_MULH]   = IR_OP_MULH,
        [RV_F3_MULHSU] = IR_OP_MULHSU,
        [RV_F3_MULHU]  = IR_OP_MULHU,
        [RV_F3_DIV]    = IR_OP_DIV,
        [RV_F3_DIVU]   = IR_OP_DIVU,
        [RV_F3_REM]    = IR_OP_REM,
        [RV_F3_REMU]   = IR_OP_REMU,
    };

    ir_opcode op;
    if (lookup_funct3_opcode(muldiv_ops, RV32_FUNCT3(instr), &op)) {
        EMIT_IR(op, RV32_RD(instr), RV32_RS1(instr), RV32_RS2(instr), IR_NO_IMMEDIATE, pc);
    } else {
        emit_invalid_instruction(builder, instr, pc);
    }
}

static void handle_rv32_op_32_muldiv(ir_builder *builder, uint32_t instr, uint64_t pc) {
    static const ir_opcode muldiv_ops[RV_FUNCT3_COUNT] = {
        [RV_F3_MUL]  = IR_OP_MULW,
        [RV_F3_DIV]  = IR_OP_DIVW,
        [RV_F3_DIVU] = IR_OP_DIVUW,
        [RV_F3_REM]  = IR_OP_REMW,
        [RV_F3_REMU] = IR_OP_REMUW,
    };

    ir_opcode op;
    if (lookup_funct3_opcode(muldiv_ops, RV32_FUNCT3(instr), &op)) {
        EMIT_IR(op, RV32_RD(instr), RV32_RS1(instr), RV32_RS2(instr), IR_NO_IMMEDIATE, pc);
    } else {
        emit_invalid_instruction(builder, instr, pc);
    }
}

static void handle_rv32_op(ir_builder *builder, uint32_t instr, uint64_t pc) {
    uint32_t f3  = RV32_FUNCT3(instr);
    uint32_t f7  = RV32_FUNCT7(instr);
    uint32_t rd  = RV32_RD(instr);
    uint32_t rs1 = RV32_RS1(instr);
    uint32_t rs2 = RV32_RS2(instr);

    if (f7 == RV_F7_MULDIV) {
        handle_rv32_op_muldiv(builder, instr, pc);
    } else if (f3 == RV_F3_ADD_SUB) {
        emit_funct7_opcode(builder, instr, f7, IR_OP_ADD, IR_OP_SUB, rd, rs1, rs2, pc);
    } else if (f3 == RV_F3_SRL_SRA) {
        emit_funct7_opcode(builder, instr, f7, IR_OP_SRL, IR_OP_SRA, rd, rs1, rs2, pc);
    } else if (f7 != RV_F7_BASE) {
        emit_invalid_instruction(builder, instr, pc);
    } else if (f3 == RV_F3_SLL) {
        EMIT_IR(IR_OP_SLL, rd, rs1, rs2, IR_NO_IMMEDIATE, pc);
    } else if (f3 == RV_F3_SLT) {
        EMIT_IR(IR_OP_SLT, rd, rs1, rs2, IR_NO_IMMEDIATE, pc);
    } else if (f3 == RV_F3_SLTU) {
        EMIT_IR(IR_OP_SLTU, rd, rs1, rs2, IR_NO_IMMEDIATE, pc);
    } else if (f3 == RV_F3_XOR) {
        EMIT_IR(IR_OP_XOR, rd, rs1, rs2, IR_NO_IMMEDIATE, pc);
    } else if (f3 == RV_F3_OR) {
        EMIT_IR(IR_OP_OR, rd, rs1, rs2, IR_NO_IMMEDIATE, pc);
    } else if (f3 == RV_F3_AND) {
        EMIT_IR(IR_OP_AND, rd, rs1, rs2, IR_NO_IMMEDIATE, pc);
    } else {
        emit_invalid_instruction(builder, instr, pc);
    }
}

static void handle_rv32_op_32(ir_builder *builder, uint32_t instr, uint64_t pc) {
    uint32_t f3  = RV32_FUNCT3(instr);
    uint32_t f7  = RV32_FUNCT7(instr);
    uint32_t rd  = RV32_RD(instr);
    uint32_t rs1 = RV32_RS1(instr);
    uint32_t rs2 = RV32_RS2(instr);

    if (f7 == RV_F7_MULDIV) {
        handle_rv32_op_32_muldiv(builder, instr, pc);
    } else if (f3 == RV_F3_ADD_SUB) {
        emit_funct7_opcode(builder, instr, f7, IR_OP_ADD_EXT32, IR_OP_SUB_EXT32, rd, rs1, rs2, pc);
    } else if (f3 == RV_F3_SLL && f7 == RV_F7_BASE) {
        EMIT_IR(IR_OP_SLL_EXT32, rd, rs1, rs2, IR_NO_IMMEDIATE, pc);
    } else if (f3 == RV_F3_SRL_SRA) {
        emit_funct7_opcode(builder, instr, f7, IR_OP_SRL_EXT32, IR_OP_SRA_EXT32, rd, rs1, rs2, pc);
    } else {
        emit_invalid_instruction(builder, instr, pc);
    }
}

static void handle_rv32_branch(ir_builder *builder, uint32_t instr, uint64_t pc) {
    static const ir_opcode branch_ops[RV_FUNCT3_COUNT] = {
        [RV_F3_BEQ]  = IR_OP_BEQ,
        [RV_F3_BNE]  = IR_OP_BNE,
        [RV_F3_BLT]  = IR_OP_BLT,
        [RV_F3_BGE]  = IR_OP_BGE,
        [RV_F3_BLTU] = IR_OP_BLTU,
        [RV_F3_BGEU] = IR_OP_BGEU,
    };

    ir_opcode op;
    if (lookup_funct3_opcode(branch_ops, RV32_FUNCT3(instr), &op)) {
        emit_branch_to_pc(builder, op, RV32_RS1(instr), RV32_RS2(instr), pc, RV32_IMM_B(instr));
    } else {
        emit_invalid_instruction(builder, instr, pc);
    }
}

static void handle_rv32_jal(ir_builder *builder, uint32_t instr, uint64_t pc) {
    EMIT_IR(IR_OP_LI, RV32_RD(instr), IR_NO_REGISTER, IR_NO_REGISTER, pc + RV_INSTR_BYTES, pc);
    EMIT_IR(IR_OP_JMP, IR_NO_REGISTER, IR_NO_REGISTER, IR_NO_REGISTER, pc + RV32_IMM_J(instr), pc);
}

static void handle_rv32_jalr(ir_builder *builder, uint32_t instr, uint64_t pc) {
    EMIT_IR(IR_OP_JALR, RV32_RD(instr), RV32_RS1(instr), IR_NO_REGISTER, RV32_IMM_I(instr), pc);
}

static void handle_rv32_misc_mem(ir_builder *builder, uint32_t instr, uint64_t pc) {
    (void)instr;
    EMIT_IR(IR_OP_NOP, IR_NO_REGISTER, IR_NO_REGISTER, IR_NO_REGISTER, IR_NO_IMMEDIATE, pc);
}

static void handle_rv32_system(ir_builder *builder, uint32_t instr, uint64_t pc) {
    if (RV32_FUNCT3(instr) != RV_F3_PRIV) {
        emit_invalid_instruction(builder, instr, pc);
        return;
    }

    int32_t imm = RV32_IMM_I(instr);
    if (imm == RV_PRIV_ECALL) {
        EMIT_IR(IR_OP_ECALL, IR_NO_REGISTER, IR_NO_REGISTER, IR_NO_REGISTER, IR_NO_IMMEDIATE, pc);
    } else if (imm == RV_PRIV_EBREAK) {
        EMIT_IR(IR_OP_EBREAK, IR_NO_REGISTER, IR_NO_REGISTER, IR_NO_REGISTER, IR_NO_IMMEDIATE, pc);
    } else {
        emit_invalid_instruction(builder, instr, pc);
    }
}

static void handle_rv32_lui(ir_builder *builder, uint32_t instr, uint64_t pc) {
    EMIT_IR(IR_OP_LI, RV32_RD(instr), IR_NO_REGISTER, IR_NO_REGISTER, RV32_IMM_U(instr), pc);
}

static void handle_rv32_auipc(ir_builder *builder, uint32_t instr, uint64_t pc) {
    EMIT_IR(IR_OP_LI, RV32_RD(instr), IR_NO_REGISTER, IR_NO_REGISTER, pc + RV32_IMM_U(instr), pc);
}

static void handle_rvc_op00_addi4spn(ir_builder *builder, uint32_t instr, uint64_t pc) {
    int32_t imm = RVC_IMM_CIW_ADDI4SPN(instr);
    if (imm != 0 && RVC_RD_PR(instr) != RV_REG_ZERO) {
        EMIT_IR(IR_OP_ADDI, RVC_RD_PR(instr), RV_REG_SP, IR_NO_REGISTER, imm, pc);
    }
}

static void handle_rvc_op00_lw(ir_builder *builder, uint32_t instr, uint64_t pc) {
    EMIT_IR(IR_OP_LOAD32, RVC_RD_PR(instr), RVC_RS1_PR(instr), IR_NO_REGISTER, RVC_IMM_CL_LW(instr), pc);
}

static void handle_rvc_op00_sw(ir_builder *builder, uint32_t instr, uint64_t pc) {
    EMIT_IR(IR_OP_STORE32, IR_NO_REGISTER, RVC_RS1_PR(instr), RVC_RS2_PR(instr), RVC_IMM_CL_LW(instr), pc);
}

static void handle_rvc_op00_ld(ir_builder *builder, uint32_t instr, uint64_t pc) {
    EMIT_IR(IR_OP_LOAD64, RVC_RD_PR(instr), RVC_RS1_PR(instr), IR_NO_REGISTER, RVC_IMM_CL_LD(instr), pc);
}

static void handle_rvc_op00_sd(ir_builder *builder, uint32_t instr, uint64_t pc) {
    EMIT_IR(IR_OP_STORE64, IR_NO_REGISTER, RVC_RS1_PR(instr), RVC_RS2_PR(instr), RVC_IMM_CS_SD(instr), pc);
}

static void handle_rvc_op01_addi_addiw(ir_builder *builder, uint32_t instr, uint64_t pc) {
    uint32_t rd = RVC_RD(instr);
    if (rd == RV_REG_ZERO) {
        return;
    }

    int32_t imm = RVC_IMM_CI(instr);
    if (RVC_FUNCT3(instr) == RVC_F3_ADDIW) {
        EMIT_IR(IR_OP_ADDI_EXT32, rd, rd, IR_NO_REGISTER, imm, pc);
    } else {
        EMIT_IR(IR_OP_ADDI, rd, rd, IR_NO_REGISTER, imm, pc);
    }
}

static uint32_t rvc_shift_amount(uint32_t instr) {
    return (RVC_BIT12(instr) << RV32_SHAMT_BITS) | RV_FIELD(instr, RVC_RS2_SHIFT, RV32_SHAMT_BITS);
}

static void handle_rvc_op01_misc_alu(ir_builder *builder, uint32_t instr, uint64_t pc) {
    uint32_t funct2 = RVC_FUNCT2_MISC(instr);
    uint32_t rs1_rd = RVC_RS1_PR(instr);

    if (funct2 == RVC_F2_SRLI || funct2 == RVC_F2_SRAI) {
        uint32_t shamt = rvc_shift_amount(instr);
        if (shamt != 0) {
            EMIT_IR(funct2 == RVC_F2_SRLI ? IR_OP_SRLI : IR_OP_SRAI, rs1_rd, rs1_rd, IR_NO_REGISTER, shamt, pc);
        }
        return;
    }

    if (funct2 == RVC_F2_ANDI) {
        EMIT_IR(IR_OP_ANDI, rs1_rd, rs1_rd, IR_NO_REGISTER, RVC_IMM_CI(instr), pc);
        return;
    }

    // funct2 == RVC_F2_ALU
    uint32_t funct2_alu = RVC_FUNCT2_ALU(instr);
    uint32_t rs2        = RVC_RS2_PR(instr);

    if (!RVC_BIT12(instr)) {
        switch (funct2_alu) {
        case RVC_F2_SUB:
            EMIT_IR(IR_OP_SUB, rs1_rd, rs1_rd, rs2, IR_NO_IMMEDIATE, pc);
            return;
        case RVC_F2_XOR:
            EMIT_IR(IR_OP_XOR, rs1_rd, rs1_rd, rs2, IR_NO_IMMEDIATE, pc);
            return;
        case RVC_F2_OR:
            EMIT_IR(IR_OP_OR, rs1_rd, rs1_rd, rs2, IR_NO_IMMEDIATE, pc);
            return;
        case RVC_F2_AND:
            EMIT_IR(IR_OP_AND, rs1_rd, rs1_rd, rs2, IR_NO_IMMEDIATE, pc);
            return;
        }
    } else {
        // the two low encodings are SUBW / ADDW and the two high encodings are
        // reserved
        if (funct2_alu == RVC_F2_SUBW) {
            EMIT_IR(IR_OP_SUB_EXT32, rs1_rd, rs1_rd, rs2, IR_NO_IMMEDIATE, pc);
            return;
        }
        if (funct2_alu == RVC_F2_ADDW) {
            EMIT_IR(IR_OP_ADD_EXT32, rs1_rd, rs1_rd, rs2, IR_NO_IMMEDIATE, pc);
            return;
        }
    }

    emit_invalid_instruction(builder, instr, pc);
}

static void handle_rvc_op01_lui_addi16sp(ir_builder *builder, uint32_t instr, uint64_t pc) {
    uint32_t rd = RVC_RD(instr);

    if (rd == RV_REG_SP) {
        EMIT_IR(IR_OP_ADDI, RV_REG_SP, RV_REG_SP, IR_NO_REGISTER, RVC_IMM_ADDI16SP(instr), pc);
    } else if (rd != RV_REG_ZERO) {
        int32_t imm = (int32_t)((uint32_t)RVC_IMM_CI(instr) << RVC_BIT12_SHIFT);
        EMIT_IR(IR_OP_LI, rd, IR_NO_REGISTER, IR_NO_REGISTER, imm, pc);
    }
}

static void handle_rvc_op01_li(ir_builder *builder, uint32_t instr, uint64_t pc) {
    if (RVC_RD(instr) != RV_REG_ZERO) {
        EMIT_IR(IR_OP_LI, RVC_RD(instr), IR_NO_REGISTER, IR_NO_REGISTER, RVC_IMM_CI(instr), pc);
    }
}

static void handle_rvc_op01_j(ir_builder *builder, uint32_t instr, uint64_t pc) {
    EMIT_IR(IR_OP_JMP, IR_NO_REGISTER, IR_NO_REGISTER, IR_NO_REGISTER, pc + RVC_IMM_CJ(instr), pc);
}

static void handle_rvc_op01_beqz(ir_builder *builder, uint32_t instr, uint64_t pc) {
    EMIT_IR(IR_OP_BEQZ, IR_NO_REGISTER, RVC_RS1_PR(instr), RV_REG_ZERO, pc + RVC_IMM_CB(instr), pc);
}

static void handle_rvc_op01_bnez(ir_builder *builder, uint32_t instr, uint64_t pc) {
    EMIT_IR(IR_OP_BNE, IR_NO_REGISTER, RVC_RS1_PR(instr), RV_REG_ZERO, pc + RVC_IMM_CB(instr), pc);
}

static void handle_rvc_op10_ldsp(ir_builder *builder, uint32_t instr, uint64_t pc) {
    if (RVC_RD(instr) != RV_REG_ZERO) {
        EMIT_IR(IR_OP_LOAD64, RVC_RD(instr), RV_REG_SP, IR_NO_REGISTER, RVC_IMM_CI_LDSP(instr), pc);
    }
}

static void handle_rvc_op10_lwsp(ir_builder *builder, uint32_t instr, uint64_t pc) {
    if (RVC_RD(instr) != RV_REG_ZERO) {
        EMIT_IR(IR_OP_LOAD32, RVC_RD(instr), RV_REG_SP, IR_NO_REGISTER, RVC_IMM_CI_LWSP(instr), pc);
    }
}

static void handle_rvc_op10_swsp(ir_builder *builder, uint32_t instr, uint64_t pc) {
    EMIT_IR(IR_OP_STORE32, IR_NO_REGISTER, RV_REG_SP, RVC_RS2(instr), RVC_IMM_CSS_SWSP(instr), pc);
}

static void handle_rvc_op10_sdsp(ir_builder *builder, uint32_t instr, uint64_t pc) {
    EMIT_IR(IR_OP_STORE64, IR_NO_REGISTER, RV_REG_SP, RVC_RS2(instr), RVC_IMM_CSS_SDSP(instr), pc);
}

static void handle_rvc_op10_jr_mv_jalr_add(ir_builder *builder, uint32_t instr, uint64_t pc) {
    if (!RVC_BIT12(instr)) {
        if (RVC_RS2(instr) == RV_REG_ZERO) {
            EMIT_IR(IR_OP_JMP_REG, IR_NO_REGISTER, RVC_RS1(instr), IR_NO_REGISTER, IR_NO_IMMEDIATE, pc);
        } else if (RVC_RD(instr) != RV_REG_ZERO) {
            EMIT_IR(IR_OP_MOV, RVC_RD(instr), IR_NO_REGISTER, RVC_RS2(instr), IR_NO_IMMEDIATE, pc);
        }
    } else if (RVC_RD(instr) != RV_REG_ZERO && RVC_RS2(instr) != RV_REG_ZERO) {
        EMIT_IR(IR_OP_ADD, RVC_RD(instr), RVC_RD(instr), RVC_RS2(instr), IR_NO_IMMEDIATE, pc);
    }
}

static void handle_rvc_op10_slli(ir_builder *builder, uint32_t instr, uint64_t pc) {
    uint32_t shamt = RVC_IMM_CI(instr) & RV_MASK(RV64_SHAMT_BITS);

    if (RVC_RD(instr) != RV_REG_ZERO && shamt != 0) {
        EMIT_IR(IR_OP_SLLI, RVC_RD(instr), RVC_RD(instr), IR_NO_REGISTER, shamt, pc);
    }
}

static const rv_handler rv32_dispatch_table[RV_OPCODE_COUNT] = {
    [RV_OP_LOAD]      = handle_rv32_load,
    [RV_OP_STORE]     = handle_rv32_store,
    [RV_OP_SYSTEM]    = handle_rv32_system,
    [RV_OP_OP_IMM]    = handle_rv32_op_imm,
    [RV_OP_LUI]       = handle_rv32_lui,
    [RV_OP_AUIPC]     = handle_rv32_auipc,
    [RV_OP_OP]        = handle_rv32_op,
    [RV_OP_OP_IMM_32] = handle_rv32_op_imm_32,
    [RV_OP_OP_32]     = handle_rv32_op_32,
    [RV_OP_BRANCH]    = handle_rv32_branch,
    [RV_OP_JAL]       = handle_rv32_jal,
    [RV_OP_JALR]      = handle_rv32_jalr,
    [RV_OP_MISC_MEM]  = handle_rv32_misc_mem,
};

static const rv_handler rvc_dispatch_table[RVC_OPCODE_COUNT][RV_FUNCT3_COUNT] = {
    [RVC_OP_00] = {
        [RVC_F3_ADDI4SPN] = handle_rvc_op00_addi4spn,
        [RVC_F3_LW]       = handle_rvc_op00_lw,
        [RVC_F3_LD]       = handle_rvc_op00_ld,
        [RVC_F3_SW]       = handle_rvc_op00_sw,
        [RVC_F3_SD]       = handle_rvc_op00_sd,
    },
    [RVC_OP_01] = {
        [RVC_F3_ADDI]         = handle_rvc_op01_addi_addiw,
        [RVC_F3_ADDIW]        = handle_rvc_op01_addi_addiw,
        [RVC_F3_MISC_ALU]     = handle_rvc_op01_misc_alu,
        [RVC_F3_LUI_ADDI16SP] = handle_rvc_op01_lui_addi16sp,
        [RVC_F3_LI]           = handle_rvc_op01_li,
        [RVC_F3_J]            = handle_rvc_op01_j,
        [RVC_F3_BEQZ]         = handle_rvc_op01_beqz,
        [RVC_F3_BNEZ]         = handle_rvc_op01_bnez,
    },
    [RVC_OP_10] = {
        [RVC_F3_LWSP]           = handle_rvc_op10_lwsp,
        [RVC_F3_LDSP]           = handle_rvc_op10_ldsp,
        [RVC_F3_SWSP]           = handle_rvc_op10_swsp,
        [RVC_F3_SDSP]           = handle_rvc_op10_sdsp,
        [RVC_F3_JR_MV_JALR_ADD] = handle_rvc_op10_jr_mv_jalr_add,
        [RVC_F3_SLLI]           = handle_rvc_op10_slli,
    },
};

static void translate_rvc(ir_builder *builder, uint32_t instr, uint64_t pc) {
    uint32_t rvc_instr = instr & RV_MASK(16);

    if (rvc_instr == RVC_INSTR_NOP) {
        return;
    }

    if (rvc_instr == RVC_INSTR_EBREAK) {
        EMIT_IR(IR_OP_EBREAK, IR_NO_REGISTER, IR_NO_REGISTER, IR_NO_REGISTER, IR_NO_IMMEDIATE, pc);
        return;
    }

    uint32_t op = RVC_OP(rvc_instr);
    uint32_t f3 = RVC_FUNCT3(rvc_instr);

    rv_handler handler = rvc_dispatch_table[op][f3];
    if (handler) {
        handler(builder, rvc_instr, pc);
    } else {
        emit_invalid_instruction(builder, instr, pc);
    }
}

static void translate_rv32(ir_builder *builder, uint32_t instr, uint64_t pc) {
    uint32_t   opcode  = RV32_OPCODE(instr);
    rv_handler handler = (opcode < RISCV_ARRAY_COUNT(rv32_dispatch_table)) ? rv32_dispatch_table[opcode] : 0;

    if (handler) {
        handler(builder, instr, pc);
    } else {
        emit_invalid_instruction(builder, instr, pc);
    }
}

typedef struct {
    uint64_t addr;
    bool     data;
} rv_map_sym;

static int rv_map_sym_cmp(const void *a, const void *b) {
    uint64_t x = ((const rv_map_sym *)a)->addr;
    uint64_t y = ((const rv_map_sym *)b)->addr;
    return (x > y) - (x < y);
}

// for context, riscv places all inline data inside .text delimited by "$x"
// (code) and "$d" (data) mapping symbols, therefore disassembling the data ranges
// yields bogus instructions
//
// returns 0 when there is no symbol info, in which case everything is treated
// as code
static uint8_t *build_text_data_map(const riscv_elf *elf) {
    if (!elf->symtab || !elf->strtab || elf->text_size == 0) {
        return 0;
    }

    uint64_t         text_end  = elf->text_addr + elf->text_size;
    size_t           sym_count = elf->symtab_size / sizeof(Elf64_Sym);
    const Elf64_Sym *syms      = (const Elf64_Sym *)elf->symtab;

    rv_map_sym *maps = malloc(sym_count * sizeof(rv_map_sym));
    if (!maps) {
        return 0;
    }

    size_t map_count = 0;
    for (size_t i = 0; i < sym_count; i++) {
        const char *name = elf->strtab + syms[i].name;
        if (name[0] != '$' || (name[1] != 'x' && name[1] != 'd')) {
            continue;
        }
        if (syms[i].value < elf->text_addr || syms[i].value >= text_end) {
            continue;
        }

        maps[map_count].addr = syms[i].value;
        maps[map_count].data = (name[1] == 'd');
        map_count++;
    }

    if (map_count == 0) {
        free(maps);
        return 0;
    }

    qsort(maps, map_count, sizeof(rv_map_sym), rv_map_sym_cmp);

    uint8_t *data_map = calloc(elf->text_size, 1);
    if (!data_map) {
        free(maps);
        return 0;
    }

    // each "$d" symbol starts a data range that runs until the next mapping
    // symbol (of either kind) or the end of .text for the final one
    for (size_t i = 0; i < map_count; i++) {
        if (!maps[i].data) {
            continue;
        }

        uint64_t start = maps[i].addr;
        uint64_t end   = (i + 1 < map_count) ? maps[i + 1].addr : text_end;
        for (uint64_t a = start; a < end; a++) {
            data_map[a - elf->text_addr] = 1;
        }
    }

    free(maps);
    return data_map;
}

void ir_translate(const riscv_elf *elf, ir_builder *builder) {
    uint64_t pc       = elf->text_addr;
    uint64_t text_end = elf->text_addr + elf->text_size;
    uint8_t *data_map = build_text_data_map(elf);

    while (pc < text_end) {
        if (data_map && data_map[pc - elf->text_addr]) {
            pc += RVC_INSTR_BYTES;
            continue;
        }

        EMIT_IR(IR_OP_FUEL_CHECK, IR_NO_REGISTER, IR_NO_REGISTER, IR_NO_REGISTER, IR_NO_IMMEDIATE, pc);

        size_t   text_offset = (size_t)(pc - elf->text_addr);
        uint32_t instr       = riscv_mem_read_u16(elf->text_data + text_offset);

        if (!IS_16BIT(instr) && pc + RV_INSTR_BYTES <= text_end) {
            instr |= (uint32_t)riscv_mem_read_u16(elf->text_data + text_offset + RVC_INSTR_BYTES) << 16;
        }

        if (IS_16BIT(instr)) {
            translate_rvc(builder, instr, pc);
            pc += RVC_INSTR_BYTES;
        } else {
            translate_rv32(builder, instr, pc);
            pc += RV_INSTR_BYTES;
        }
    }

    free(data_map);
}

void ir_builder_init(ir_builder *builder, size_t init_cap) {
    builder->instrs = malloc(init_cap * sizeof(ir_instr));
    builder->count  = 0;
    builder->cap    = init_cap;
}

void ir_builder_push(ir_builder *builder, ir_instr instr) {
    if (builder->count < builder->cap) {
        builder->instrs[builder->count++] = instr;
    }
}

void ir_builder_free(ir_builder *builder) {
    if (builder->instrs) {
        free(builder->instrs);
    }
}

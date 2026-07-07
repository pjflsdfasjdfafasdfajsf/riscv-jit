// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

#ifndef RISCV_INSTR_H
#define RISCV_INSTR_H

#include "riscv_types.h"

enum rv_opcode {
    RV_OP_LOAD      = 0x03,
    RV_OP_OP_IMM    = 0x13,
    RV_OP_AUIPC     = 0x17,
    RV_OP_OP_IMM_32 = 0x1B,
    RV_OP_STORE     = 0x23,
    RV_OP_OP        = 0x33,
    RV_OP_LUI       = 0x37,
    RV_OP_OP_32     = 0x3B,
    RV_OP_BRANCH    = 0x63,
    RV_OP_JALR     = 0x67,
    RV_OP_JAL      = 0x6F,
    RV_OP_SYSTEM   = 0x73,
    RV_OP_MISC_MEM = 0x0F,
};

enum rv_funct3_system {
    RV_F3_PRIV = 0x0
};

enum rv_system_priv {
    RV_PRIV_ECALL  = 0x000,
    RV_PRIV_EBREAK = 0x001
};

enum rv_funct3_branch {
    RV_F3_BEQ  = 0x0,
    RV_F3_BNE  = 0x1,
    RV_F3_BLT  = 0x4,
    RV_F3_BGE  = 0x5,
    RV_F3_BLTU = 0x6,
    RV_F3_BGEU = 0x7
};

enum rv_funct3_load {
    RV_F3_LB  = 0x0,
    RV_F3_LH  = 0x1,
    RV_F3_LW  = 0x2,
    RV_F3_LD  = 0x3,
    RV_F3_LBU = 0x4,
    RV_F3_LHU = 0x5,
    RV_F3_LWU = 0x6
};

enum rv_funct3_store {
    RV_F3_SB = 0x0,
    RV_F3_SH = 0x1,
    RV_F3_SW = 0x2,
    RV_F3_SD = 0x3
};

enum rv_funct3_op {
    RV_F3_ADD_SUB = 0x0,
    RV_F3_SLL     = 0x1,
    RV_F3_SLT     = 0x2,
    RV_F3_SLTU    = 0x3,
    RV_F3_XOR     = 0x4,
    RV_F3_SRL_SRA = 0x5,
    RV_F3_OR      = 0x6,
    RV_F3_AND     = 0x7
};

enum rv_funct7_op {
    RV_F7_BASE = 0x00,
    RV_F7_ALT  = 0x20
};

// M extension (multiply/divide) shares the OP / OP-32 opcodes and is
// selected by funct7 == 0x01
enum rv_funct7_muldiv {
    RV_F7_MULDIV = 0x01
};

enum rv_funct3_muldiv {
    RV_F3_MUL    = 0x0,
    RV_F3_MULH   = 0x1,
    RV_F3_MULHSU = 0x2,
    RV_F3_MULHU  = 0x3,
    RV_F3_DIV    = 0x4,
    RV_F3_DIVU   = 0x5,
    RV_F3_REM    = 0x6,
    RV_F3_REMU   = 0x7
};

enum rv64_funct6_shift {
    RV64_F6_SRL = 0x00,
    RV64_F6_SRA = 0x10
};

enum rvc_opcode {
    RVC_OP_00 = 0x0,
    RVC_OP_01 = 0x1,
    RVC_OP_10 = 0x2
};

enum rvc_funct3_op00 {
    RVC_F3_ADDI4SPN = 0x0,
    RVC_F3_LW       = 0x2,
    RVC_F3_LD       = 0x3,
    RVC_F3_SW       = 0x6,
    RVC_F3_SD       = 0x7
};

enum rvc_funct3_op01 {
    RVC_F3_ADDI         = 0x0,
    RVC_F3_ADDIW        = 0x1,
    RVC_F3_LI           = 0x2,
    RVC_F3_LUI_ADDI16SP = 0x3,
    RVC_F3_MISC_ALU     = 0x4,
    RVC_F3_J            = 0x5,
    RVC_F3_BEQZ         = 0x6,
    RVC_F3_BNEZ         = 0x7
};

enum rvc_funct2_misc {
    RVC_F2_SRLI = 0x0,
    RVC_F2_SRAI = 0x1,
    RVC_F2_ANDI = 0x2,
    RVC_F2_ALU  = 0x3
};

enum rvc_funct2_alu {
    RVC_F2_SUB  = 0x0,
    RVC_F2_XOR  = 0x1,
    RVC_F2_OR   = 0x2,
    RVC_F2_AND  = 0x3,
    RVC_F2_SUBW = 0x0, // when bit 12 is 1
    RVC_F2_ADDW = 0x1  // when bit 12 is 1
};

enum rvc_funct3_op10 {
    RVC_F3_SLLI           = 0x0,
    RVC_F3_LWSP           = 0x2,
    RVC_F3_LDSP           = 0x3,
    RVC_F3_JR_MV_JALR_ADD = 0x4,
    RVC_F3_SWSP           = 0x6,
    RVC_F3_SDSP           = 0x7
};

enum rvc_inst_fixed {
    RVC_INSTR_NOP    = 0x0001,
    RVC_INSTR_EBREAK = 0x9002
};

// -- shared instruction layout constants --

enum rv_register_index {
    RV_REG_ZERO = 0,
    RV_REG_RA   = 1,
    RV_REG_SP   = 2,
};

enum rv_instruction_layout {
    RV_INSTR_BYTES  = 4,
    RVC_INSTR_BYTES = 2,

    RV_OPCODE_COUNT = 128,
    RV_FUNCT3_COUNT = 8,

    RV_REGISTER_COUNT      = 32,
    RV_REGISTER_INDEX_BITS = 5,

    RV32_OPCODE_SHIFT = 0,
    RV32_OPCODE_BITS  = 7,
    RV32_RD_SHIFT     = 7,
    RV32_FUNCT3_SHIFT = 12,
    RV32_RS1_SHIFT    = 15,
    RV32_RS2_SHIFT    = 20,
    RV32_FUNCT7_SHIFT = 25,
    RV64_FUNCT6_SHIFT = 26,

    RV32_SHAMT_BITS = 5,
    RV64_SHAMT_BITS = 6,

    RVC_OPCODE_BITS       = 2,
    RVC_OPCODE_COUNT      = 4,
    RVC_OPCODE_32BIT_MARK = 0x3,
    RVC_FUNCT3_SHIFT      = 13,
    RVC_FUNCT2_MISC_SHIFT = 10,
    RVC_FUNCT2_ALU_SHIFT  = 5,
    RVC_BIT12_SHIFT       = 12,
    RVC_RS1_SHIFT         = 7,
    RVC_RS2_SHIFT         = 2,
    RVC_PRIME_REG_BASE    = 8,
};

#define RV_MASK(width)        ((uint32_t)(((uint64_t)1 << (width)) - 1u))
#define RV_FIELD(value, lsb, width) (((uint32_t)(value) >> (lsb)) & RV_MASK(width))
#define RV_BIT(value, bit)    RV_FIELD(value, bit, 1)
#define RV_SIGN(value, bit, mask) (RV_BIT(value, bit) ? (uint32_t)(mask) : 0u)

// -- 32 bit instructions --

#define RV32_OPCODE(instr) RV_FIELD(instr, RV32_OPCODE_SHIFT, RV32_OPCODE_BITS)
#define RV32_RD(instr)     RV_FIELD(instr, RV32_RD_SHIFT, RV_REGISTER_INDEX_BITS)
#define RV32_FUNCT3(instr) RV_FIELD(instr, RV32_FUNCT3_SHIFT, 3)
#define RV32_RS1(instr)    RV_FIELD(instr, RV32_RS1_SHIFT, RV_REGISTER_INDEX_BITS)
#define RV32_RS2(instr)    RV_FIELD(instr, RV32_RS2_SHIFT, RV_REGISTER_INDEX_BITS)
#define RV32_FUNCT7(instr) RV_FIELD(instr, RV32_FUNCT7_SHIFT, RV32_OPCODE_BITS)

#define RV32_SHAMT(instr)  RV_FIELD(instr, RV32_RS2_SHIFT, RV32_SHAMT_BITS)
#define RV64_SHAMT(instr)  RV_FIELD(instr, RV32_RS2_SHIFT, RV64_SHAMT_BITS)
#define RV64_FUNCT6(instr) RV_FIELD(instr, RV64_FUNCT6_SHIFT, RV64_SHAMT_BITS)

#define RV32_IMM_I(instr) \
    (int32_t)(RV_SIGN(instr, 31, 0xFFFFF000u) | RV_FIELD(instr, RV32_RS2_SHIFT, 12))

#define RV32_IMM_S(instr) \
    (int32_t)(RV_SIGN(instr, 31, 0xFFFFF000u) | (RV_FIELD(instr, RV32_FUNCT7_SHIFT, 7) << 5) | RV32_RD(instr))

#define RV32_IMM_B(instr) \
    (int32_t)(RV_SIGN(instr, 31, 0xFFFFF000u) | (RV_BIT(instr, 7) << 11) | (RV_FIELD(instr, 25, 6) << 5) | (RV_FIELD(instr, 8, 4) << 1))

#define RV32_IMM_U(instr) (int32_t)((instr) & 0xFFFFF000u)

#define RV32_IMM_J(instr) \
    (int32_t)(RV_SIGN(instr, 31, 0xFFF00000u) | (RV_FIELD(instr, 12, 8) << 12) | (RV_BIT(instr, 20) << 11) | (RV_FIELD(instr, 21, 10) << 1))

// -- 16 bit instructions --

#define IS_16BIT(instr) (RV_FIELD(instr, 0, RVC_OPCODE_BITS) != RVC_OPCODE_32BIT_MARK)

#define RVC_OP(instr)          RV_FIELD(instr, 0, RVC_OPCODE_BITS)
#define RVC_FUNCT3(instr)      RV_FIELD(instr, RVC_FUNCT3_SHIFT, 3)
#define RVC_FUNCT2_MISC(instr) RV_FIELD(instr, RVC_FUNCT2_MISC_SHIFT, 2)
#define RVC_FUNCT2_ALU(instr)  RV_FIELD(instr, RVC_FUNCT2_ALU_SHIFT, 2)
#define RVC_BIT12(instr)       RV_BIT(instr, RVC_BIT12_SHIFT)

// standard registers (x0 - x31)
#define RVC_RS1(instr) RV_FIELD(instr, RVC_RS1_SHIFT, RV_REGISTER_INDEX_BITS)
#define RVC_RS2(instr) RV_FIELD(instr, RVC_RS2_SHIFT, RV_REGISTER_INDEX_BITS)
#define RVC_RD(instr)  RV_FIELD(instr, RVC_RS1_SHIFT, RV_REGISTER_INDEX_BITS)

// prime registers (x8 - x15)
#define RVC_RS1_PR(instr) (RVC_PRIME_REG_BASE + RV_FIELD(instr, RVC_RS1_SHIFT, 3))
#define RVC_RS2_PR(instr) (RVC_PRIME_REG_BASE + RV_FIELD(instr, RVC_RS2_SHIFT, 3))
#define RVC_RD_PR(instr)  (RVC_PRIME_REG_BASE + RV_FIELD(instr, RVC_RS2_SHIFT, 3))

#define RVC_IMM_CI(instr) \
    (int32_t)(RV_SIGN(instr, RVC_BIT12_SHIFT, 0xFFFFFFE0u) | RV_FIELD(instr, RVC_RS2_SHIFT, 5))

#define RVC_IMM_CJ(instr) \
    (int32_t)(RV_SIGN(instr, RVC_BIT12_SHIFT, 0xFFFFF800u) | (RV_BIT(instr, 8) << 10) | (RV_FIELD(instr, 9, 2) << 8) | (RV_BIT(instr, 6) << 7) | (RV_BIT(instr, 7) << 6) | (RV_BIT(instr, 2) << 5) | (RV_BIT(instr, 11) << 4) | (RV_FIELD(instr, 3, 3) << 1))

#define RVC_IMM_CB(instr) \
    (int32_t)(RV_SIGN(instr, RVC_BIT12_SHIFT, 0xFFFFFF00u) | (RV_FIELD(instr, 5, 2) << 6) | (RV_BIT(instr, 2) << 5) | (RV_FIELD(instr, 10, 2) << 3) | (RV_FIELD(instr, 3, 2) << 1))

#define RVC_IMM_CL_LW(instr) \
    ((RV_BIT(instr, 5) << 6) | (RV_FIELD(instr, 10, 3) << 3) | (RV_BIT(instr, 6) << 2))

#define RVC_IMM_CL_LD(instr) \
    ((RV_FIELD(instr, 10, 3) << 3) | (RV_FIELD(instr, 5, 2) << 6))

#define RVC_IMM_CS_SD(instr) RVC_IMM_CL_LD(instr)

#define RVC_IMM_CI_LWSP(instr) \
    ((RV_FIELD(instr, 2, 2) << 6) | (RV_BIT(instr, 12) << 5) | (RV_FIELD(instr, 4, 3) << 2))

#define RVC_IMM_CI_LDSP(instr) \
    ((RV_BIT(instr, 12) << 5) | (RV_FIELD(instr, 5, 2) << 3) | (RV_FIELD(instr, 2, 3) << 6))

#define RVC_IMM_CSS_SWSP(instr) \
    ((RV_FIELD(instr, 7, 2) << 6) | (RV_FIELD(instr, 9, 4) << 2))

#define RVC_IMM_CSS_SDSP(instr) \
    ((RV_FIELD(instr, 10, 3) << 3) | (RV_FIELD(instr, 7, 3) << 6))

#define RVC_IMM_CIW_ADDI4SPN(instr) \
    ((RV_FIELD(instr, 7, 4) << 6) | (RV_FIELD(instr, 11, 2) << 4) | (RV_BIT(instr, 5) << 3) | (RV_BIT(instr, 6) << 2))

#define RVC_IMM_ADDI16SP(instr) \
    (int32_t)(RV_SIGN(instr, RVC_BIT12_SHIFT, 0xFFFFFE00u) | (RV_FIELD(instr, 3, 2) << 7) | (RV_BIT(instr, 5) << 6) | (RV_BIT(instr, 2) << 5) | (RV_BIT(instr, 6) << 4))

#endif // RISCV_INSTR_H

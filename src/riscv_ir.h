// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

#ifndef RISCV_IR_H
#define RISCV_IR_H

#include "riscv_elf.h"
#include "riscv_types.h"

typedef enum {
    // no operation
    IR_OP_NOP,
    // decrements fuel counter and halts if zero
    IR_OP_FUEL_CHECK,
    // rd = rs2
    IR_OP_MOV,
    // rd = imm
    IR_OP_LI,
    // rd = rs1 + rs2
    IR_OP_ADD,
    // rd = rs1 + imm
    IR_OP_ADDI,
    // rd = sign_extend32(rs1 + rs2)
    IR_OP_ADD_EXT32,
    // rd = sign_extend32(rs1 + imm)
    IR_OP_ADDI_EXT32,
    // rd = rs1 << imm
    IR_OP_SLLI,
    // rd = rs1 >> imm (logical)
    IR_OP_SRLI,
    // rd = rs1 & rs2
    IR_OP_AND,
    // rd = sign_extend32(mem32[rs1 + imm])
    IR_OP_LOAD32,
    // rd = mem64[rs1 + imm]
    IR_OP_LOAD64,
    // mem32[rs1 + imm] = rs2
    IR_OP_STORE32,
    // mem64[rs1 + imm] = rs2
    IR_OP_STORE64,
    // if (rs1 == 0) jump to imm
    IR_OP_BEQZ,
    // pc = rs1
    IR_OP_JMP_REG,
    // rd = rs1 - rs2
    IR_OP_SUB,
    // rd = sign_extend32(rs1 - rs2)
    IR_OP_SUB_EXT32,
    // rd = rs1 ^ rs2
    IR_OP_XOR,
    // rd = rs1 ^ imm
    IR_OP_XORI,
    // rd = rs1 | rs2
    IR_OP_OR,
    // rd = rs1 | imm
    IR_OP_ORI,
    // rd = rs1 & imm
    IR_OP_ANDI,
    // rd = rs1 << rs2
    IR_OP_SLL,
    // rd = rs1 >> rs2 (logical)
    IR_OP_SRL,
    // rd = rs1 >> rs2 (arithmetic)
    IR_OP_SRA,
    // rd = rs1 >> imm (arithmetic)
    IR_OP_SRAI,
    // rd = (rs1 < rs2) (signed)
    IR_OP_SLT,
    // rd = (rs1 < imm) (signed)
    IR_OP_SLTI,
    // rd = (rs1 < rs2) (unsigned)
    IR_OP_SLTU,
    // rd = (rs1 < imm) (unsigned)
    IR_OP_SLTIU,
    // rd = sign_extend32(rs1 << rs2)
    IR_OP_SLL_EXT32,
    // rd = sign_extend32(rs1 >> rs2) (logical)
    IR_OP_SRL_EXT32,
    // rd = sign_extend32(rs1 >> rs2) (arithmetic)
    IR_OP_SRA_EXT32,
    // rd = sign_extend32(rs1 << imm)
    IR_OP_SLLI_EXT32,
    // rd = sign_extend32(rs1 >> imm) (logical)
    IR_OP_SRLI_EXT32,
    // rd = sign_extend32(rs1 >> imm) (arithmetic)
    IR_OP_SRAI_EXT32,
    // rd = sign_extend8(mem8[rs1 + imm])
    IR_OP_LOAD8,
    // rd = zero_extend8(mem8[rs1 + imm])
    IR_OP_LOAD8_U,
    // rd = sign_extend16(mem16[rs1 + imm])
    IR_OP_LOAD16,
    // rd = zero_extend16(mem16[rs1 + imm])
    IR_OP_LOAD16_U,
    // rd = zero_extend32(mem32[rs1 + imm])
    IR_OP_LOAD32_U,
    // mem8[rs1 + imm] = rs2
    IR_OP_STORE8,
    // mem16[rs1 + imm] = rs2
    IR_OP_STORE16,
    // if (rs1 == rs2) jump to imm
    IR_OP_BEQ,
    // if (rs1 != rs2) jump to imm
    IR_OP_BNE,
    // if (rs1 < rs2) jump to imm (signed)
    IR_OP_BLT,
    // if (rs1 >= rs2) jump to imm (signed)
    IR_OP_BGE,
    // if (rs1 < rs2) jump to imm (unsigned)
    IR_OP_BLTU,
    // if (rs1 >= rs2) jump to imm (unsigned)
    IR_OP_BGEU,
    // pc = imm
    IR_OP_JMP,
    // rd = pc + 4; pc = (rs1 + imm) & ~1
    IR_OP_JALR,
    // traps to host ecall handler
    IR_OP_ECALL,
    // emits debug breakpoint
    IR_OP_EBREAK,
    // emits undefined instruction crash
    IR_OP_UD2,
    // rd = (rs1 * rs2)[63:0]
    IR_OP_MUL,
    // rd = (signed(rs1) * signed(rs2))[127:64]
    IR_OP_MULH,
    // rd = (signed(rs1) * unsigned(rs2))[127:64]
    IR_OP_MULHSU,
    // rd = (unsigned(rs1) * unsigned(rs2))[127:64]
    IR_OP_MULHU,
    // rd = rs1 / rs2 (signed)
    IR_OP_DIV,
    // rd = rs1 / rs2 (unsigned)
    IR_OP_DIVU,
    // rd = rs1 % rs2 (signed)
    IR_OP_REM,
    // rd = rs1 % rs2 (unsigned)
    IR_OP_REMU,
    // rd = sign_extend32((rs1 * rs2)[31:0])
    IR_OP_MULW,
    // rd = sign_extend32(rs1[31:0] / rs2[31:0]) (signed)
    IR_OP_DIVW,
    // rd = sign_extend32(rs1[31:0] / rs2[31:0]) (unsigned)
    IR_OP_DIVUW,
    // rd = sign_extend32(rs1[31:0] % rs2[31:0]) (signed)
    IR_OP_REMW,
    // rd = sign_extend32(rs1[31:0] % rs2[31:0]) (unsigned)
    IR_OP_REMUW
} ir_opcode;

enum {
    IR_NO_REGISTER  = 0,
    IR_NO_IMMEDIATE = 0,
};

typedef struct {
    ir_opcode op;
    uint32_t  rd;
    uint32_t  rs1;
    uint32_t  rs2;
    int64_t   imm;
    // the original RISC-V PC this maps to
    uint64_t pc;
} ir_instr;

typedef struct {
    ir_instr *instrs;
    size_t    count;
    size_t    cap;
} ir_builder;

void ir_builder_init(ir_builder *builder, size_t init_cap);
void ir_builder_push(ir_builder *builder, ir_instr instr);
void ir_builder_free(ir_builder *builder);

void ir_translate(const riscv_elf *elf, ir_builder *builder);

#endif // RISCV_IR_H

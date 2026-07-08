// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

#include "riscv_jit_cg_x86.h"
#include "riscv_instr.h"
#include "riscv_ir.h"
#include "riscv_jit.h"
#include "riscv_mem.h"

#include <stdlib.h>

// -- internal --

typedef enum {
    X86_RAX = 0,
    X86_RCX,
    X86_RDX,
    X86_RBX,
    X86_RSP,
    X86_RBP,
    X86_RSI,
    X86_RDI,
    X86_R8,
    X86_R9,
    X86_R10,
    X86_R11,
    X86_R12,
    X86_R13,
    X86_R14,
    X86_R15
} x86_reg;

typedef enum {
    X86_COND_B  = 2,
    X86_COND_AE = 3,
    X86_COND_E  = 4,
    X86_COND_NE = 5,
    X86_COND_A  = 7,
    X86_COND_L  = 12,
    X86_COND_GE = 13,
    X86_COND_LE = 14,
} x86_cond;

enum x86_op {
    X86_OP_ADD_RM_R      = 0x01,
    X86_OP_ADD_EAX_IMM32 = 0x05,
    X86_OP_OR_RM_R       = 0x09,
    X86_OP_AND_RM_R      = 0x21,
    X86_OP_SUB_RM_R      = 0x29,
    X86_OP_XOR_RM_R      = 0x31,
    X86_OP_CMP_RM_R      = 0x39,
    X86_OP_PUSH_R        = 0x50,
    X86_OP_POP_R         = 0x58,
    X86_OP_MOVSXD_R_RM   = 0x63,
    X86_OP_OPERAND_SIZE  = 0x66,
    X86_OP_JB_REL8       = 0x72,
    X86_OP_ALU_IMM32     = 0x81,
    X86_OP_ALU_IMM8      = 0x83,
    X86_OP_TEST_RM_R     = 0x85,
    X86_OP_MOV_RM_R8     = 0x88,
    X86_OP_MOV_RM_R      = 0x89,
    X86_OP_MOV_R_RM      = 0x8B,
    X86_OP_MOV_R_IMM64   = 0xB8,
    X86_OP_SHIFT_RM_IMM8 = 0xC1,
    X86_OP_RET           = 0xC3,
    X86_OP_SHIFT_RM_CL   = 0xD3,
    X86_OP_INT3          = 0xCC,
    X86_OP_CQO_CDQ       = 0x99,
    X86_OP_GRP3          = 0xF7,
    X86_OP_JMP_REL32     = 0xE9,
    X86_OP_GRP5          = 0xFF,

    X86_OP2_PREFIX         = 0x0F,
    X86_OP2_UD2            = 0x0B,
    X86_OP2_IMUL_R_RM      = 0xAF,
    X86_OP2_JCC_REL32_BASE = 0x80,
    X86_OP2_SETCC_BASE     = 0x90,
    X86_OP2_MOVSX_R8       = 0xBE,
    X86_OP2_MOVSX_R16      = 0xBF,
    X86_OP2_MOVZX_R8       = 0xB6,
    X86_OP2_MOVZX_R16      = 0xB7,
};

enum x86_encoding {
    X86_BYTE_BITS = 8,
    X86_WORD_BITS = 32,

    X86_REX_BASE = 0x40,
    X86_REX_B    = 0x01,
    X86_REX_X    = 0x02,
    X86_REX_R    = 0x04,
    X86_REX_W    = 0x08,

    X86_REGISTER_CODE_MASK     = 0x7,
    X86_REGISTER_EXTENSION_BIT = 3,

    X86_MOD_MEMORY = 0,
    X86_MOD_DISP8  = 1,
    X86_MOD_DISP32 = 2,
    X86_MOD_REG    = 3,

    X86_MOD_SHIFT = 6,
    X86_REG_SHIFT = 3,
    X86_MOD_MASK  = 0x3,

    X86_SIB_SCALE_1  = 0,
    X86_SIB_NO_INDEX = 4,

    X86_I8_MIN = -128,
    X86_I8_MAX = 127,

    X86_REL32_SIZE                 = sizeof(uint32_t),
    X86_JMP_REL32_SIZE             = 1 + sizeof(uint32_t),
    X86_JMP_REL32_IMMEDIATE_OFFSET = 1,
    X86_JCC_REL32_IMMEDIATE_OFFSET = 2,
};

enum x86_alu_extension {
    X86_ALU_ADD = 0,
    X86_ALU_OR  = 1,
    X86_ALU_AND = 4,
    X86_ALU_XOR = 6,
    X86_ALU_CMP = 7,
};

enum x86_shift_extension {
    X86_SHIFT_SHL = 4,
    X86_SHIFT_SHR = 5,
    X86_SHIFT_SAR = 7,
};

enum x86_group5_extension {
    X86_GRP5_CALL = 2,
    X86_GRP5_JMP  = 4,
};

enum x86_group3_extension {
    X86_GRP3_MUL  = 4,
    X86_GRP3_IMUL = 5,
    X86_GRP3_DIV  = 6,
    X86_GRP3_IDIV = 7,
};

static inline int x86_reg_code(x86_reg reg) {
    return reg & X86_REGISTER_CODE_MASK;
}

static inline int x86_reg_ext(x86_reg reg) {
    return reg >> X86_REGISTER_EXTENSION_BIT;
}

static inline bool x86_i8_fits(int32_t value) {
    return value >= X86_I8_MIN && value <= X86_I8_MAX;
}

static inline void x86_cg_emit8(riscv_jit *jit, uint8_t byte) {
    if (jit->code_size < jit->code_cap) {
        jit->code_buf[jit->code_size++] = byte;
    }
}

static inline void x86_cg_emit32(riscv_jit *jit, uint32_t val) {
    x86_cg_emit8(jit, (uint8_t)val);
    x86_cg_emit8(jit, (uint8_t)(val >> X86_BYTE_BITS));
    x86_cg_emit8(jit, (uint8_t)(val >> (2 * X86_BYTE_BITS)));
    x86_cg_emit8(jit, (uint8_t)(val >> (3 * X86_BYTE_BITS)));
}

static inline void x86_cg_emit64(riscv_jit *jit, uint64_t val) {
    x86_cg_emit32(jit, (uint32_t)val);
    x86_cg_emit32(jit, (uint32_t)(val >> X86_WORD_BITS));
}

static inline void x86_cg_rex(riscv_jit *jit, int w, int r, int x, int b) {
    uint8_t rex = X86_REX_BASE;
    if (w) {
        rex |= X86_REX_W;
    }
    if (r) {
        rex |= X86_REX_R;
    }
    if (x) {
        rex |= X86_REX_X;
    }
    if (b) {
        rex |= X86_REX_B;
    }
    if (rex != X86_REX_BASE) {
        x86_cg_emit8(jit, rex);
    }
}

static inline void x86_cg_modrm(riscv_jit *jit, int mod, int reg, int rm) {
    x86_cg_emit8(jit, ((mod & X86_MOD_MASK) << X86_MOD_SHIFT) | ((reg & X86_REGISTER_CODE_MASK) << X86_REG_SHIFT) | (rm & X86_REGISTER_CODE_MASK));
}

static inline void x86_cg_sib(riscv_jit *jit, int scale, int index, int base) {
    x86_cg_emit8(jit, ((scale & X86_MOD_MASK) << X86_MOD_SHIFT) | ((index & X86_REGISTER_CODE_MASK) << X86_REG_SHIFT) | (base & X86_REGISTER_CODE_MASK));
}

static inline void x86_cg_emit_alu_r_r(riscv_jit *jit, uint8_t opcode, int w, x86_reg dst, x86_reg src) {
    x86_cg_rex(jit, w, x86_reg_ext(src), 0, x86_reg_ext(dst));
    x86_cg_emit8(jit, opcode);
    x86_cg_modrm(jit, X86_MOD_REG, src, dst);
}

static inline void x86_cg_emit_alu_r_m(riscv_jit *jit, uint8_t opcode, int w, x86_reg reg, x86_reg base, int32_t disp) {
    x86_cg_rex(jit, w, x86_reg_ext(reg), 0, x86_reg_ext(base));
    x86_cg_emit8(jit, opcode);

    if (x86_i8_fits(disp)) {
        x86_cg_modrm(jit, X86_MOD_DISP8, reg, base);

        if (x86_reg_code(base) == X86_RSP) {
            x86_cg_sib(jit, X86_SIB_SCALE_1, X86_SIB_NO_INDEX, base);
        }

        x86_cg_emit8(jit, (uint8_t)disp);
    } else {
        x86_cg_modrm(jit, X86_MOD_DISP32, reg, base);

        if (x86_reg_code(base) == X86_RSP) {
            x86_cg_sib(jit, X86_SIB_SCALE_1, X86_SIB_NO_INDEX, base);
        }

        x86_cg_emit32(jit, (uint32_t)disp);
    }
}

static inline void x86_cg_mov_r64_r64(riscv_jit *jit, x86_reg dst, x86_reg src) {
    x86_cg_emit_alu_r_r(jit, X86_OP_MOV_RM_R, true, dst, src);
}

static inline void x86_cg_add_r64_r64(riscv_jit *jit, x86_reg dst, x86_reg src) {
    x86_cg_emit_alu_r_r(jit, X86_OP_ADD_RM_R, true, dst, src);
}

static inline void x86_cg_add_r32_r32(riscv_jit *jit, x86_reg dst, x86_reg src) {
    x86_cg_emit_alu_r_r(jit, X86_OP_ADD_RM_R, false, dst, src);
}

static inline void x86_cg_movsxd_r64_r32(riscv_jit *jit, x86_reg dst, x86_reg src) {
    x86_cg_emit_alu_r_r(jit, X86_OP_MOVSXD_R_RM, true, src, dst);
}

static inline void x86_cg_mov_r64_m64(riscv_jit *jit, x86_reg dst, x86_reg base, int32_t disp) {
    x86_cg_emit_alu_r_m(jit, X86_OP_MOV_R_RM, true, dst, base, disp);
}

static inline void x86_cg_mov_m64_r64(riscv_jit *jit, x86_reg base, int32_t disp, x86_reg src) {
    x86_cg_emit_alu_r_m(jit, X86_OP_MOV_RM_R, true, src, base, disp);
}

static inline void x86_cg_mov_m32_r32(riscv_jit *jit, x86_reg base, int32_t disp, x86_reg src) {
    x86_cg_emit_alu_r_m(jit, X86_OP_MOV_RM_R, false, src, base, disp);
}

static inline void x86_cg_movsxd_r64_m32(riscv_jit *jit, x86_reg dst, x86_reg base, int32_t disp) {
    x86_cg_emit_alu_r_m(jit, X86_OP_MOVSXD_R_RM, true, dst, base, disp);
}

static inline void x86_cg_zero_extend_r32_to_r64(riscv_jit *jit, x86_reg reg) {
    x86_cg_emit_alu_r_r(jit, X86_OP_MOV_RM_R, false, reg, reg);
}

static inline void x86_cg_mov_r64_imm64(riscv_jit *jit, x86_reg dst, uint64_t imm) {
    x86_cg_rex(jit, true, 0, 0, x86_reg_ext(dst));
    x86_cg_emit8(jit, X86_OP_MOV_R_IMM64 | x86_reg_code(dst));
    x86_cg_emit64(jit, imm);
}

static inline void x86_cg_alu_r_imm32(riscv_jit *jit, int w, int op_ext, x86_reg dst, int32_t imm) {
    x86_cg_rex(jit, w, 0, 0, x86_reg_ext(dst));

    if (x86_i8_fits(imm)) {
        x86_cg_emit8(jit, X86_OP_ALU_IMM8);
        x86_cg_modrm(jit, X86_MOD_REG, op_ext, dst);
        x86_cg_emit8(jit, (uint8_t)imm);
    } else if (dst == X86_RAX && op_ext == X86_ALU_ADD) {
        x86_cg_emit8(jit, X86_OP_ADD_EAX_IMM32);
        x86_cg_emit32(jit, (uint32_t)imm);
    } else {
        x86_cg_emit8(jit, X86_OP_ALU_IMM32);
        x86_cg_modrm(jit, X86_MOD_REG, op_ext, dst);
        x86_cg_emit32(jit, (uint32_t)imm);
    }
}

static inline void x86_cg_add_r_imm32(riscv_jit *jit, int w, x86_reg dst, int32_t imm) {
    x86_cg_alu_r_imm32(jit, w, X86_ALU_ADD, dst, imm);
}

static inline void x86_cg_add_r64_imm32(riscv_jit *jit, x86_reg dst, int32_t imm) {
    x86_cg_add_r_imm32(jit, true, dst, imm);
}

static inline void x86_cg_add_r32_imm32(riscv_jit *jit, x86_reg dst, int32_t imm) {
    x86_cg_add_r_imm32(jit, false, dst, imm);
}

static inline void x86_cg_emit_register_opcode(riscv_jit *jit, uint8_t opcode_base, x86_reg reg) {
    if (reg >= X86_R8) {
        x86_cg_rex(jit, false, 0, 0, x86_reg_ext(reg));
    }
    x86_cg_emit8(jit, opcode_base | x86_reg_code(reg));
}

static inline void x86_cg_push_r64(riscv_jit *jit, x86_reg reg) {
    x86_cg_emit_register_opcode(jit, X86_OP_PUSH_R, reg);
}

static inline void x86_cg_pop_r64(riscv_jit *jit, x86_reg reg) {
    x86_cg_emit_register_opcode(jit, X86_OP_POP_R, reg);
}

static inline void x86_cg_jmp_rel32(riscv_jit *jit, int32_t rel) {
    x86_cg_emit8(jit, X86_OP_JMP_REL32);
    x86_cg_emit32(jit, (uint32_t)rel);
}

static inline void x86_cg_ret(riscv_jit *jit) {
    x86_cg_emit8(jit, X86_OP_RET);
}

static inline void x86_cg_emit_group5_r64(riscv_jit *jit, int op_ext, x86_reg reg) {
    if (reg >= X86_R8) {
        x86_cg_rex(jit, false, 0, 0, x86_reg_ext(reg));
    }
    x86_cg_emit8(jit, X86_OP_GRP5);
    x86_cg_modrm(jit, X86_MOD_REG, op_ext, reg);
}

static inline void x86_cg_call_r64(riscv_jit *jit, x86_reg reg) {
    x86_cg_emit_group5_r64(jit, X86_GRP5_CALL, reg);
}

static inline void x86_cg_test_r64_r64(riscv_jit *jit, x86_reg r1, x86_reg r2) {
    x86_cg_emit_alu_r_r(jit, X86_OP_TEST_RM_R, true, r2, r1);
}

static inline void x86_cg_jmp_r64(riscv_jit *jit, x86_reg reg) {
    x86_cg_emit_group5_r64(jit, X86_GRP5_JMP, reg);
}

static inline void x86_cg_jcc_rel32(riscv_jit *jit, uint8_t cond, int32_t rel) {
    x86_cg_emit8(jit, X86_OP2_PREFIX);
    x86_cg_emit8(jit, X86_OP2_JCC_REL32_BASE | (cond & 0xF));
    x86_cg_emit32(jit, (uint32_t)rel);
}

static inline void x86_cg_je_rel32(riscv_jit *jit, int32_t rel) {
    x86_cg_jcc_rel32(jit, X86_COND_E, rel);
}

static inline void x86_cg_shift_r_imm8(riscv_jit *jit, int w, int op_ext, x86_reg dst, uint8_t imm) {
    x86_cg_rex(jit, w, 0, 0, x86_reg_ext(dst));
    x86_cg_emit8(jit, X86_OP_SHIFT_RM_IMM8);
    x86_cg_modrm(jit, X86_MOD_REG, op_ext, dst);
    x86_cg_emit8(jit, imm);
}

static inline void x86_cg_shift_r_cl(riscv_jit *jit, int w, int op_ext, x86_reg dst) {
    x86_cg_rex(jit, w, 0, 0, x86_reg_ext(dst));
    x86_cg_emit8(jit, X86_OP_SHIFT_RM_CL);
    x86_cg_modrm(jit, X86_MOD_REG, op_ext, dst);
}

static inline void x86_cg_shl_r64_imm8(riscv_jit *jit, x86_reg dst, uint8_t imm) {
    x86_cg_shift_r_imm8(jit, true, X86_SHIFT_SHL, dst, imm);
}

static inline void x86_cg_shr_r64_imm8(riscv_jit *jit, x86_reg dst, uint8_t imm) {
    x86_cg_shift_r_imm8(jit, true, X86_SHIFT_SHR, dst, imm);
}

static inline void x86_cg_sar_r64_imm8(riscv_jit *jit, x86_reg dst, uint8_t imm) {
    x86_cg_shift_r_imm8(jit, true, X86_SHIFT_SAR, dst, imm);
}

static inline void x86_cg_shl_r32_imm8(riscv_jit *jit, x86_reg dst, uint8_t imm) {
    x86_cg_shift_r_imm8(jit, false, X86_SHIFT_SHL, dst, imm);
}

static inline void x86_cg_shr_r32_imm8(riscv_jit *jit, x86_reg dst, uint8_t imm) {
    x86_cg_shift_r_imm8(jit, false, X86_SHIFT_SHR, dst, imm);
}

static inline void x86_cg_sar_r32_imm8(riscv_jit *jit, x86_reg dst, uint8_t imm) {
    x86_cg_shift_r_imm8(jit, false, X86_SHIFT_SAR, dst, imm);
}

static inline void x86_cg_int3(riscv_jit *jit) {
    x86_cg_emit8(jit, X86_OP_INT3);
}

static inline void x86_cg_ud2(riscv_jit *jit) {
    x86_cg_emit8(jit, X86_OP2_PREFIX);
    x86_cg_emit8(jit, X86_OP2_UD2);
}

static inline void x86_cg_and_r64_r64(riscv_jit *jit, x86_reg dst, x86_reg src) {
    x86_cg_emit_alu_r_r(jit, X86_OP_AND_RM_R, true, dst, src);
}

static inline void x86_cg_sub_r64_r64(riscv_jit *jit, x86_reg dst, x86_reg src) {
    x86_cg_emit_alu_r_r(jit, X86_OP_SUB_RM_R, true, dst, src);
}

static inline void x86_cg_sub_r32_r32(riscv_jit *jit, x86_reg dst, x86_reg src) {
    x86_cg_emit_alu_r_r(jit, X86_OP_SUB_RM_R, false, dst, src);
}

static inline void x86_cg_xor_r64_r64(riscv_jit *jit, x86_reg dst, x86_reg src) {
    x86_cg_emit_alu_r_r(jit, X86_OP_XOR_RM_R, true, dst, src);
}

static inline void x86_cg_or_r64_r64(riscv_jit *jit, x86_reg dst, x86_reg src) {
    x86_cg_emit_alu_r_r(jit, X86_OP_OR_RM_R, true, dst, src);
}

static inline void x86_cg_cmp_r64_r64(riscv_jit *jit, x86_reg r1, x86_reg r2) {
    x86_cg_emit_alu_r_r(jit, X86_OP_CMP_RM_R, true, r1, r2);
}

static inline void x86_cg_and_r64_imm32(riscv_jit *jit, x86_reg dst, int32_t imm) {
    x86_cg_alu_r_imm32(jit, true, X86_ALU_AND, dst, imm);
}

static inline void x86_cg_or_r64_imm32(riscv_jit *jit, x86_reg dst, int32_t imm) {
    x86_cg_alu_r_imm32(jit, true, X86_ALU_OR, dst, imm);
}

static inline void x86_cg_xor_r64_imm32(riscv_jit *jit, x86_reg dst, int32_t imm) {
    x86_cg_alu_r_imm32(jit, true, X86_ALU_XOR, dst, imm);
}

static inline void x86_cg_cmp_r64_imm32(riscv_jit *jit, x86_reg dst, int32_t imm) {
    x86_cg_alu_r_imm32(jit, true, X86_ALU_CMP, dst, imm);
}

static inline void x86_cg_shl_r64_cl(riscv_jit *jit, x86_reg dst) {
    x86_cg_shift_r_cl(jit, true, X86_SHIFT_SHL, dst);
}

static inline void x86_cg_shr_r64_cl(riscv_jit *jit, x86_reg dst) {
    x86_cg_shift_r_cl(jit, true, X86_SHIFT_SHR, dst);
}

static inline void x86_cg_sar_r64_cl(riscv_jit *jit, x86_reg dst) {
    x86_cg_shift_r_cl(jit, true, X86_SHIFT_SAR, dst);
}

static inline void x86_cg_shl_r32_cl(riscv_jit *jit, x86_reg dst) {
    x86_cg_shift_r_cl(jit, false, X86_SHIFT_SHL, dst);
}

static inline void x86_cg_shr_r32_cl(riscv_jit *jit, x86_reg dst) {
    x86_cg_shift_r_cl(jit, false, X86_SHIFT_SHR, dst);
}

static inline void x86_cg_sar_r32_cl(riscv_jit *jit, x86_reg dst) {
    x86_cg_shift_r_cl(jit, false, X86_SHIFT_SAR, dst);
}

static inline void x86_cg_setcc_r8(riscv_jit *jit, uint8_t cond, x86_reg dst) {
    if (dst >= X86_RSP) {
        x86_cg_rex(jit, false, 0, 0, x86_reg_ext(dst));
    }

    x86_cg_emit8(jit, X86_OP2_PREFIX);
    x86_cg_emit8(jit, X86_OP2_SETCC_BASE | (cond & 0xF));
    x86_cg_modrm(jit, X86_MOD_REG, 0, dst);
}

static inline void x86_cg_movzx_r64_r8(riscv_jit *jit, x86_reg dst, x86_reg src) {
    x86_cg_rex(jit, true, x86_reg_ext(dst), 0, x86_reg_ext(src));
    x86_cg_emit8(jit, X86_OP2_PREFIX);
    x86_cg_emit8(jit, X86_OP2_MOVZX_R8);
    x86_cg_modrm(jit, X86_MOD_REG, dst, src);
}

static inline void x86_cg_test_r32_r32(riscv_jit *jit, x86_reg r1, x86_reg r2) {
    x86_cg_emit_alu_r_r(jit, X86_OP_TEST_RM_R, false, r2, r1);
}

static inline void x86_cg_xor_r32_r32(riscv_jit *jit, x86_reg dst, x86_reg src) {
    x86_cg_emit_alu_r_r(jit, X86_OP_XOR_RM_R, false, dst, src);
}

static inline void x86_cg_cmp_r32_imm32(riscv_jit *jit, x86_reg dst, int32_t imm) {
    x86_cg_alu_r_imm32(jit, false, X86_ALU_CMP, dst, imm);
}

static inline void x86_cg_cqo(riscv_jit *jit) {
    x86_cg_rex(jit, true, 0, 0, 0);
    x86_cg_emit8(jit, X86_OP_CQO_CDQ);
}

static inline void x86_cg_cdq(riscv_jit *jit) {
    x86_cg_emit8(jit, X86_OP_CQO_CDQ);
}

static inline void x86_cg_imul_r_r(riscv_jit *jit, int w, x86_reg dst, x86_reg src) {
    x86_cg_rex(jit, w, x86_reg_ext(dst), 0, x86_reg_ext(src));
    x86_cg_emit8(jit, X86_OP2_PREFIX);
    x86_cg_emit8(jit, X86_OP2_IMUL_R_RM);
    x86_cg_modrm(jit, X86_MOD_REG, dst, src);
}

static inline void x86_cg_imul_r64_r64(riscv_jit *jit, x86_reg dst, x86_reg src) {
    x86_cg_imul_r_r(jit, true, dst, src);
}

static inline void x86_cg_imul_r32_r32(riscv_jit *jit, x86_reg dst, x86_reg src) {
    x86_cg_imul_r_r(jit, false, dst, src);
}

static inline void x86_cg_grp3_r(riscv_jit *jit, int w, int op_ext, x86_reg r) {
    x86_cg_rex(jit, w, 0, 0, x86_reg_ext(r));
    x86_cg_emit8(jit, X86_OP_GRP3);
    x86_cg_modrm(jit, X86_MOD_REG, op_ext, r);
}

static inline void x86_cg_imul_r64(riscv_jit *jit, x86_reg r) {
    x86_cg_grp3_r(jit, true, X86_GRP3_IMUL, r);
}

static inline void x86_cg_mul_r64(riscv_jit *jit, x86_reg r) {
    x86_cg_grp3_r(jit, true, X86_GRP3_MUL, r);
}

static inline void x86_cg_idiv_r(riscv_jit *jit, int w, x86_reg r) {
    x86_cg_grp3_r(jit, w, X86_GRP3_IDIV, r);
}

static inline void x86_cg_div_r(riscv_jit *jit, int w, x86_reg r) {
    x86_cg_grp3_r(jit, w, X86_GRP3_DIV, r);
}

typedef enum {
    X86_MEM_ACCESS_REGULAR,
    X86_MEM_ACCESS_STORE,
} x86_mem_access_type;

typedef enum {
    X86_MEM_TYPE_8BIT,
    X86_MEM_TYPE_8BIT_U,
    X86_MEM_TYPE_16BIT,
    X86_MEM_TYPE_16BIT_U,
    X86_MEM_TYPE_32BIT,
    X86_MEM_TYPE_32BIT_U,
    X86_MEM_TYPE_64BIT,
} x86_mem_type;

static inline void x86_cg_emit_movsx_or_movzx_m(riscv_jit *jit, x86_reg dst, x86_reg base, uint8_t opcode) {
    x86_cg_rex(jit, true, x86_reg_ext(dst), 0, x86_reg_ext(base));
    x86_cg_emit8(jit, X86_OP2_PREFIX);
    x86_cg_emit8(jit, opcode);
    x86_cg_modrm(jit, X86_MOD_MEMORY, dst, base);
}

static inline void x86_cg_emit_load(riscv_jit *jit, x86_mem_type type, x86_reg reg) {
    if (type == X86_MEM_TYPE_8BIT) {
        x86_cg_emit_movsx_or_movzx_m(jit, reg, X86_RCX, X86_OP2_MOVSX_R8);
    } else if (type == X86_MEM_TYPE_8BIT_U) {
        x86_cg_emit_movsx_or_movzx_m(jit, reg, X86_RCX, X86_OP2_MOVZX_R8);
    } else if (type == X86_MEM_TYPE_16BIT) {
        x86_cg_emit_movsx_or_movzx_m(jit, reg, X86_RCX, X86_OP2_MOVSX_R16);
    } else if (type == X86_MEM_TYPE_16BIT_U) {
        x86_cg_emit_movsx_or_movzx_m(jit, reg, X86_RCX, X86_OP2_MOVZX_R16);
    } else if (type == X86_MEM_TYPE_32BIT) {
        x86_cg_movsxd_r64_m32(jit, reg, X86_RCX, 0);
    } else if (type == X86_MEM_TYPE_32BIT_U) {
        x86_cg_emit_alu_r_m(jit, X86_OP_MOV_R_RM, false, reg, X86_RCX, 0);
    } else {
        x86_cg_mov_r64_m64(jit, reg, X86_RCX, 0);
    }
}

static inline void x86_cg_emit_store(riscv_jit *jit, x86_mem_type type, x86_reg reg) {
    if (type == X86_MEM_TYPE_8BIT) {
        x86_cg_emit_alu_r_m(jit, X86_OP_MOV_RM_R8, false, reg, X86_RCX, 0);
    } else if (type == X86_MEM_TYPE_16BIT) {
        x86_cg_emit8(jit, X86_OP_OPERAND_SIZE);
        x86_cg_emit_alu_r_m(jit, X86_OP_MOV_RM_R, false, reg, X86_RCX, 0);
    } else if (type == X86_MEM_TYPE_32BIT) {
        x86_cg_mov_m32_r32(jit, X86_RCX, 0, reg);
    } else {
        x86_cg_mov_m64_r64(jit, X86_RCX, 0, reg);
    }
}

static inline int32_t x86_mem_type_size(x86_mem_type type) {
    switch (type) {
    case X86_MEM_TYPE_8BIT:
    case X86_MEM_TYPE_8BIT_U:
        return 1;
    case X86_MEM_TYPE_16BIT:
    case X86_MEM_TYPE_16BIT_U:
        return 2;
    case X86_MEM_TYPE_32BIT:
    case X86_MEM_TYPE_32BIT_U:
        return 4;
    case X86_MEM_TYPE_64BIT:
        return 8;
    }

    return 8;
}

static inline void x86_cg_patch_rel32_local(riscv_jit *jit, int rel32_offset, int target_offset) {
    int32_t rel = (int32_t)(target_offset - (rel32_offset + X86_REL32_SIZE));
    riscv_mem_write_u32(jit->code_buf + rel32_offset, (uint32_t)rel);
}

static inline void x86_cg_emit_mem_access(riscv_jit *jit, x86_mem_access_type access_type, x86_mem_type type, x86_reg reg, int32_t imm, uint64_t pc) {
    int32_t access_size = x86_mem_type_size(type);

    if (imm != 0) {
        x86_cg_add_r64_imm32(jit, X86_RCX, imm);
    }

    x86_cg_cmp_r64_imm32(jit, X86_RCX, RISCV_STACK_GUARD_BYTES);
    x86_cg_jcc_rel32(jit, X86_COND_B, 0);
    int stack_low_patch = (int)jit->code_size - X86_REL32_SIZE;

    x86_cg_mov_r64_r64(jit, X86_RDX, X86_R13);
    x86_cg_add_r64_imm32(jit, X86_RDX, -access_size);
    x86_cg_cmp_r64_r64(jit, X86_RDX, X86_RCX);
    x86_cg_jcc_rel32(jit, X86_COND_AE, 0);
    int stack_success_patch = (int)jit->code_size - X86_REL32_SIZE;

    int text_check_offset = (int)jit->code_size;
    x86_cg_patch_rel32_local(jit, stack_low_patch, text_check_offset);

    int fault_patches[3];
    int fault_patch_count  = 0;
    int text_success_patch = -1;
    int data_success_patch = -1;

    // there is one nice trick we can do -- since bounds are compile-time
    // constants they can be encoded as immediates using RSI! which means
    // that this avoids wasting a callee-saved register just to survive
    // ecall calls
    if (jit->elf.data_size != 0) {
        uint64_t data_lo    = jit->elf.data_addr;
        uint64_t data_hi    = jit->elf.data_addr + jit->elf.data_size - (uint64_t)access_size;
        uint64_t host_delta = (uint64_t)(uintptr_t)jit->elf.data_mem - jit->elf.data_addr;

        x86_cg_mov_r64_imm64(jit, X86_RSI, data_lo);
        x86_cg_cmp_r64_r64(jit, X86_RCX, X86_RSI);
        x86_cg_jcc_rel32(jit, X86_COND_B, 0);
        int data_low_patch = (int)jit->code_size - X86_REL32_SIZE;

        x86_cg_mov_r64_imm64(jit, X86_RSI, data_hi);
        x86_cg_cmp_r64_r64(jit, X86_RCX, X86_RSI);
        x86_cg_jcc_rel32(jit, X86_COND_A, 0);
        int data_high_patch = (int)jit->code_size - X86_REL32_SIZE;

        x86_cg_mov_r64_imm64(jit, X86_RSI, host_delta);
        x86_cg_add_r64_r64(jit, X86_RCX, X86_RSI);
        x86_cg_jmp_rel32(jit, 0);
        data_success_patch = (int)jit->code_size - X86_REL32_SIZE;

        int after_data_offset = (int)jit->code_size;
        x86_cg_patch_rel32_local(jit, data_low_patch, after_data_offset);
        x86_cg_patch_rel32_local(jit, data_high_patch, after_data_offset);
    }

    if (access_type == X86_MEM_ACCESS_STORE) {
        x86_cg_jmp_rel32(jit, 0);
        fault_patches[fault_patch_count++] = (int)jit->code_size - X86_REL32_SIZE;
    } else {
        x86_cg_cmp_r64_r64(jit, X86_RCX, X86_RBX);
        x86_cg_jcc_rel32(jit, X86_COND_B, 0);
        fault_patches[fault_patch_count++] = (int)jit->code_size - X86_REL32_SIZE;

        x86_cg_mov_r64_r64(jit, X86_RDX, X86_RBP);
        x86_cg_add_r64_imm32(jit, X86_RDX, -access_size);
        x86_cg_cmp_r64_r64(jit, X86_RDX, X86_RCX);
        x86_cg_jcc_rel32(jit, X86_COND_B, 0);
        fault_patches[fault_patch_count++] = (int)jit->code_size - X86_REL32_SIZE;

        x86_cg_mov_r64_r64(jit, X86_RDX, X86_RCX);
        x86_cg_sub_r64_r64(jit, X86_RDX, X86_RBX);
        x86_cg_add_r64_r64(jit, X86_RDX, X86_R12);
        x86_cg_mov_r64_r64(jit, X86_RCX, X86_RDX);
        x86_cg_jmp_rel32(jit, 0);
        text_success_patch = (int)jit->code_size - X86_REL32_SIZE;
    }

    int stack_success_offset = (int)jit->code_size;
    x86_cg_patch_rel32_local(jit, stack_success_patch, stack_success_offset);
    x86_cg_add_r64_r64(jit, X86_RCX, X86_R14);
    x86_cg_jmp_rel32(jit, 0);
    int stack_access_patch = (int)jit->code_size - X86_REL32_SIZE;

    int fault_jump_offset = (int)jit->code_size;
    if (access_type == X86_MEM_ACCESS_STORE) {
        jit->write_fault_patches[jit->write_fault_patch_count] = fault_jump_offset + X86_JMP_REL32_IMMEDIATE_OFFSET;
        jit->write_fault_pcs[jit->write_fault_patch_count++]   = pc;
    } else {
        jit->read_fault_patches[jit->read_fault_patch_count] = fault_jump_offset + X86_JMP_REL32_IMMEDIATE_OFFSET;
        jit->read_fault_pcs[jit->read_fault_patch_count++]   = pc;
    }
    x86_cg_jmp_rel32(jit, 0);

    for (int i = 0; i < fault_patch_count; i++) {
        x86_cg_patch_rel32_local(jit, fault_patches[i], fault_jump_offset);
    }

    int access_offset = (int)jit->code_size;
    if (text_success_patch >= 0) {
        x86_cg_patch_rel32_local(jit, text_success_patch, access_offset);
    }
    if (data_success_patch >= 0) {
        x86_cg_patch_rel32_local(jit, data_success_patch, access_offset);
    }
    x86_cg_patch_rel32_local(jit, stack_access_patch, access_offset);

    if (access_type == X86_MEM_ACCESS_STORE) {
        x86_cg_emit_store(jit, type, reg);
    } else {
        x86_cg_emit_load(jit, type, reg);
    }
}

static inline void x86_cg_emit_fuel_check(riscv_jit *jit, uint64_t pc, int32_t fuel_offset) {
    x86_cg_mov_r64_m64(jit, X86_RAX, X86_R15, fuel_offset);
    x86_cg_add_r64_imm32(jit, X86_RAX, -1);
    x86_cg_mov_m64_r64(jit, X86_R15, fuel_offset, X86_RAX);

    x86_cg_test_r64_r64(jit, X86_RAX, X86_RAX);
    x86_cg_jcc_rel32(jit, X86_COND_LE, 0);

    jit->timeout_patches[jit->timeout_patch_count] = jit->code_size - X86_REL32_SIZE;
    jit->timeout_pcs[jit->timeout_patch_count++]   = pc;
}

#define X86_TRAMPOLINE_MAX_BYTES      160u
#define X86_GUEST_INSTR_MAX_BYTES     224u
#define X86_EPILOGUE_AND_PATCH_BYTES  256u
#define RISCV_JALR_CLEAR_LOW_BIT_MASK ((int32_t)-2)

typedef void (*x86_emit_reg_reg)(riscv_jit *jit, x86_reg dst, x86_reg src);
typedef void (*x86_emit_reg)(riscv_jit *jit, x86_reg dst);
typedef void (*x86_emit_reg_imm32)(riscv_jit *jit, x86_reg dst, int32_t imm);
typedef void (*x86_emit_reg_imm8)(riscv_jit *jit, x86_reg dst, uint8_t imm);

extern void ecall(riscv_jit *jit);

typedef enum {
    RV_DIV_UNSIGNED,
    RV_DIV_SIGNED,
} rv_div_signedness;

typedef enum {
    // ful 64-bit DIV/DIVU/REM/REMU
    RV_DIV_XLEN,
    // 32-bit W variants and result sign-extended to 64
    RV_DIV_WORD,
} rv_div_width;

typedef enum {
    RV_DIV_QUOTIENT,  // DIV / DIVU / DIVW / DIVUW
    RV_DIV_REMAINDER, // REM / REMU / REMW / REMUW
} rv_div_result;

static int32_t cpu_register_offset(uint32_t reg) {
    return (int32_t)(offsetof(riscv_cpu, x) + reg * sizeof(uint64_t));
}

static int32_t cpu_pc_offset(void) {
    return (int32_t)offsetof(riscv_cpu, pc);
}

static int32_t cpu_fuel_offset(void) {
    return (int32_t)offsetof(riscv_cpu, fuel);
}

static int32_t cpu_fault_offset(void) {
    return (int32_t)offsetof(riscv_cpu, fault);
}

static bool jit_has_code_space(const riscv_jit *jit, size_t needed_bytes) {
    return jit->code_size + needed_bytes < jit->code_cap;
}

static bool jit_pc_is_in_text(const riscv_jit *jit, uint64_t pc) {
    return pc >= jit->elf.text_addr && pc < jit->elf.text_addr + jit->elf.text_size;
}

static void load_guest_reg(riscv_jit *jit, x86_reg host_reg, uint32_t guest_reg) {
    x86_cg_mov_r64_m64(jit, host_reg, X86_R15, cpu_register_offset(guest_reg));
}

static void store_guest_reg(riscv_jit *jit, uint32_t guest_reg, x86_reg host_reg) {
    x86_cg_mov_m64_r64(jit, X86_R15, cpu_register_offset(guest_reg), host_reg);
}

static void store_cpu_pc(riscv_jit *jit, x86_reg host_reg) {
    x86_cg_mov_m64_r64(jit, X86_R15, cpu_pc_offset(), host_reg);
}

static void store_cpu_fault(riscv_jit *jit, riscv_jit_fault_type fault) {
    x86_cg_mov_r64_imm64(jit, X86_RAX, fault);
    x86_cg_mov_m64_r64(jit, X86_R15, cpu_fault_offset(), X86_RAX);
}

static void store_cpu_pc_immediate(riscv_jit *jit, uint64_t pc) {
    x86_cg_mov_r64_imm64(jit, X86_RAX, pc);
    store_cpu_pc(jit, X86_RAX);
}

static void zero_guest_zero_register(riscv_jit *jit) {
    x86_cg_mov_r64_imm64(jit, X86_RAX, 0);
    store_guest_reg(jit, RISCV_REG_ZERO, X86_RAX);
}

static bool ir_op_must_run_even_when_rd_is_zero(ir_opcode op) {
    switch (op) {
    case IR_OP_FUEL_CHECK:
    case IR_OP_STORE8:
    case IR_OP_STORE16:
    case IR_OP_STORE32:
    case IR_OP_STORE64:
    case IR_OP_BEQZ:
    case IR_OP_BEQ:
    case IR_OP_BNE:
    case IR_OP_BLT:
    case IR_OP_BGE:
    case IR_OP_BLTU:
    case IR_OP_BGEU:
    case IR_OP_JMP:
    case IR_OP_JMP_REG:
    case IR_OP_JALR:
    case IR_OP_ECALL:
    case IR_OP_EBREAK:
    case IR_OP_UD2:
        return true;
    default:
        return false;
    }
}

static bool ir_result_is_discarded(const ir_instr *instr) {
    return instr->rd == RISCV_REG_ZERO && !ir_op_must_run_even_when_rd_is_zero(instr->op);
}

static bool allocate_codegen_buffers(riscv_jit *jit, size_t patch_capacity) {
    jit->epilogue_jump_buf   = malloc(patch_capacity * sizeof(int));
    jit->branch_jump_buf     = malloc(patch_capacity * sizeof(int));
    jit->branch_target_buf   = malloc(patch_capacity * sizeof(uint64_t));
    jit->read_fault_patches  = malloc(patch_capacity * sizeof(int));
    jit->read_fault_pcs      = malloc(patch_capacity * sizeof(uint64_t));
    jit->write_fault_patches = malloc(patch_capacity * sizeof(int));
    jit->write_fault_pcs     = malloc(patch_capacity * sizeof(uint64_t));
    jit->timeout_patches     = malloc(patch_capacity * sizeof(int));
    jit->timeout_pcs         = malloc(patch_capacity * sizeof(uint64_t));

    if (!jit->epilogue_jump_buf || !jit->branch_jump_buf || !jit->branch_target_buf ||
        !jit->read_fault_patches || !jit->read_fault_pcs || !jit->write_fault_patches ||
        !jit->write_fault_pcs || !jit->timeout_patches || !jit->timeout_pcs) {
        return false;
    }

    jit->epilogue_jump_buf_size  = 0;
    jit->branch_jump_buf_size    = 0;
    jit->read_fault_patch_count  = 0;
    jit->write_fault_patch_count = 0;
    jit->timeout_patch_count     = 0;

    return true;
}

static void free_codegen_buffers(riscv_jit *jit) {
    free(jit->epilogue_jump_buf);
    free(jit->branch_jump_buf);
    free(jit->branch_target_buf);
    free(jit->read_fault_patches);
    free(jit->read_fault_pcs);
    free(jit->write_fault_patches);
    free(jit->write_fault_pcs);
    free(jit->timeout_patches);
    free(jit->timeout_pcs);

    jit->epilogue_jump_buf   = 0;
    jit->branch_jump_buf     = 0;
    jit->branch_target_buf   = 0;
    jit->read_fault_patches  = 0;
    jit->read_fault_pcs      = 0;
    jit->write_fault_patches = 0;
    jit->write_fault_pcs     = 0;
    jit->timeout_patches     = 0;
    jit->timeout_pcs         = 0;
}

static void remember_branch_patch(riscv_jit *jit, int rel32_offset, uint64_t target_pc) {
    jit->branch_jump_buf[jit->branch_jump_buf_size]     = rel32_offset;
    jit->branch_target_buf[jit->branch_jump_buf_size++] = target_pc;
}

static void remember_epilogue_jump(riscv_jit *jit) {
    jit->epilogue_jump_buf[jit->epilogue_jump_buf_size++] = jit->code_size;
}

static void emit_jump_to_epilogue(riscv_jit *jit) {
    remember_epilogue_jump(jit);
    x86_cg_jmp_rel32(jit, 0);
}

static int relative_offset_from_rel32(int rel32_offset, int target_offset) {
    return target_offset - (rel32_offset + X86_REL32_SIZE);
}

static int relative_offset_from_next_jmp(riscv_jit *jit, int target_offset) {
    return target_offset - ((int)jit->code_size + X86_JMP_REL32_SIZE);
}

static void patch_rel32(riscv_jit *jit, int rel32_offset, int target_offset) {
    riscv_mem_write_u32(jit->code_buf + rel32_offset, (uint32_t)relative_offset_from_rel32(rel32_offset, target_offset));
}

static void emit_binary_reg_result(riscv_jit *jit, const ir_instr *instr, x86_emit_reg_reg emit) {
    load_guest_reg(jit, X86_RAX, instr->rs1);
    load_guest_reg(jit, X86_RCX, instr->rs2);
    emit(jit, X86_RAX, X86_RCX);
    store_guest_reg(jit, instr->rd, X86_RAX);
}

static void emit_binary_reg_result_ext32(riscv_jit *jit, const ir_instr *instr, x86_emit_reg_reg emit) {
    load_guest_reg(jit, X86_RAX, instr->rs1);
    load_guest_reg(jit, X86_RCX, instr->rs2);
    emit(jit, X86_RAX, X86_RCX);
    x86_cg_movsxd_r64_r32(jit, X86_RAX, X86_RAX);
    store_guest_reg(jit, instr->rd, X86_RAX);
}

static void emit_imm32_result(riscv_jit *jit, const ir_instr *instr, x86_emit_reg_imm32 emit) {
    load_guest_reg(jit, X86_RAX, instr->rs1);
    emit(jit, X86_RAX, (int32_t)instr->imm);
    store_guest_reg(jit, instr->rd, X86_RAX);
}

static void emit_imm32_result_ext32(riscv_jit *jit, const ir_instr *instr, x86_emit_reg_imm32 emit) {
    load_guest_reg(jit, X86_RAX, instr->rs1);
    emit(jit, X86_RAX, (int32_t)instr->imm);
    x86_cg_movsxd_r64_r32(jit, X86_RAX, X86_RAX);
    store_guest_reg(jit, instr->rd, X86_RAX);
}

static void emit_imm8_result(riscv_jit *jit, const ir_instr *instr, x86_emit_reg_imm8 emit) {
    load_guest_reg(jit, X86_RAX, instr->rs1);
    emit(jit, X86_RAX, (uint8_t)instr->imm);
    store_guest_reg(jit, instr->rd, X86_RAX);
}

static void emit_imm8_result_ext32(riscv_jit *jit, const ir_instr *instr, x86_emit_reg_imm8 emit) {
    load_guest_reg(jit, X86_RAX, instr->rs1);
    emit(jit, X86_RAX, (uint8_t)instr->imm);
    x86_cg_movsxd_r64_r32(jit, X86_RAX, X86_RAX);
    store_guest_reg(jit, instr->rd, X86_RAX);
}

static void emit_shift_reg_result(riscv_jit *jit, const ir_instr *instr, x86_emit_reg emit) {
    load_guest_reg(jit, X86_RAX, instr->rs1);
    load_guest_reg(jit, X86_RCX, instr->rs2);
    emit(jit, X86_RAX);
    store_guest_reg(jit, instr->rd, X86_RAX);
}

static void emit_shift_reg_result_ext32(riscv_jit *jit, const ir_instr *instr, x86_emit_reg emit) {
    load_guest_reg(jit, X86_RAX, instr->rs1);
    load_guest_reg(jit, X86_RCX, instr->rs2);
    emit(jit, X86_RAX);
    x86_cg_movsxd_r64_r32(jit, X86_RAX, X86_RAX);
    store_guest_reg(jit, instr->rd, X86_RAX);
}

static void emit_compare_reg_result(riscv_jit *jit, const ir_instr *instr, x86_cond cond) {
    load_guest_reg(jit, X86_RAX, instr->rs1);
    load_guest_reg(jit, X86_RCX, instr->rs2);
    x86_cg_cmp_r64_r64(jit, X86_RAX, X86_RCX);
    x86_cg_setcc_r8(jit, cond, X86_RAX);
    x86_cg_movzx_r64_r8(jit, X86_RAX, X86_RAX);
    store_guest_reg(jit, instr->rd, X86_RAX);
}

static void emit_compare_imm_result(riscv_jit *jit, const ir_instr *instr, x86_cond cond) {
    load_guest_reg(jit, X86_RAX, instr->rs1);
    x86_cg_cmp_r64_imm32(jit, X86_RAX, (int32_t)instr->imm);
    x86_cg_setcc_r8(jit, cond, X86_RAX);
    x86_cg_movzx_r64_r8(jit, X86_RAX, X86_RAX);
    store_guest_reg(jit, instr->rd, X86_RAX);
}

static void emit_conditional_branch(riscv_jit *jit, const ir_instr *instr, x86_cond cond) {
    load_guest_reg(jit, X86_RAX, instr->rs1);
    load_guest_reg(jit, X86_RCX, instr->rs2);
    x86_cg_cmp_r64_r64(jit, X86_RAX, X86_RCX);
    remember_branch_patch(jit, jit->code_size + X86_JCC_REL32_IMMEDIATE_OFFSET, instr->imm);
    x86_cg_jcc_rel32(jit, cond, 0);
}

static void emit_load(riscv_jit *jit, const ir_instr *instr, x86_mem_type mem_type) {
    load_guest_reg(jit, X86_RCX, instr->rs1);
    x86_cg_emit_mem_access(jit, X86_MEM_ACCESS_REGULAR, mem_type, X86_RAX, (int32_t)instr->imm, instr->pc);
    store_guest_reg(jit, instr->rd, X86_RAX);
}

static void emit_store(riscv_jit *jit, const ir_instr *instr, x86_mem_type mem_type) {
    load_guest_reg(jit, X86_RCX, instr->rs1);
    load_guest_reg(jit, X86_RAX, instr->rs2);
    x86_cg_emit_mem_access(jit, X86_MEM_ACCESS_STORE, mem_type, X86_RAX, (int32_t)instr->imm, instr->pc);
}

static void emit_ecall(riscv_jit *jit) {
    x86_cg_push_r64(jit, X86_RAX);
    x86_cg_mov_r64_imm64(jit, X86_RDI, (uint64_t)(uintptr_t)jit);
    x86_cg_mov_r64_imm64(jit, X86_RAX, (uint64_t)(uintptr_t)ecall);
    x86_cg_call_r64(jit, X86_RAX);
    x86_cg_pop_r64(jit, X86_RAX);
}

static int emit_jcc_to_patch(riscv_jit *jit, x86_cond cond) {
    x86_cg_jcc_rel32(jit, cond, 0);
    return (int)jit->code_size - X86_REL32_SIZE;
}

static int emit_jmp_to_patch(riscv_jit *jit) {
    x86_cg_jmp_rel32(jit, 0);
    return (int)jit->code_size - X86_REL32_SIZE;
}

static void patch_jump_to_here(riscv_jit *jit, int rel32_offset) {
    x86_cg_patch_rel32_local(jit, rel32_offset, (int)jit->code_size);
}

static void emit_muldiv_load_operands(riscv_jit *jit, const ir_instr *instr) {
    load_guest_reg(jit, X86_RAX, instr->rs1);
    load_guest_reg(jit, X86_RCX, instr->rs2);
}

static void emit_mul(riscv_jit *jit, const ir_instr *instr) {
    emit_muldiv_load_operands(jit, instr);

    switch (instr->op) {
    case IR_OP_MUL: {
        x86_cg_imul_r64_r64(jit, X86_RAX, X86_RCX);
    } break;

    case IR_OP_MULW: {
        x86_cg_imul_r32_r32(jit, X86_RAX, X86_RCX);
        x86_cg_movsxd_r64_r32(jit, X86_RAX, X86_RAX);
    } break;

    case IR_OP_MULH: {
        x86_cg_imul_r64(jit, X86_RCX);
        x86_cg_mov_r64_r64(jit, X86_RAX, X86_RDX);
    } break;

    case IR_OP_MULHU: {
        x86_cg_mul_r64(jit, X86_RCX);
        x86_cg_mov_r64_r64(jit, X86_RAX, X86_RDX);
    } break;

    case IR_OP_MULHSU: {
        x86_cg_mov_r64_r64(jit, X86_RSI, X86_RAX); // save rs1
        x86_cg_mul_r64(jit, X86_RCX);              // RDX = high(unsigned product)
        x86_cg_sar_r64_imm8(jit, X86_RSI, 63);     // RSI = rs1 < 0 ? -1 : 0
        x86_cg_and_r64_r64(jit, X86_RSI, X86_RCX); // RSI = rs1 < 0 ? rs2 : 0
        x86_cg_sub_r64_r64(jit, X86_RDX, X86_RSI);
        x86_cg_mov_r64_r64(jit, X86_RAX, X86_RDX);
    } break;

    default: {
    } break;
    }

    store_guest_reg(jit, instr->rd, X86_RAX);
}

static void emit_divrem(riscv_jit *jit, const ir_instr *instr, rv_div_signedness sign, rv_div_width width, rv_div_result result) {
    int rexw = width == RV_DIV_WORD ? false : true;

    emit_muldiv_load_operands(jit, instr);

    if (width == RV_DIV_WORD) {
        x86_cg_test_r32_r32(jit, X86_RCX, X86_RCX);
    } else {
        x86_cg_test_r64_r64(jit, X86_RCX, X86_RCX);
    }
    int divisor_nonzero = emit_jcc_to_patch(jit, X86_COND_NE);

    if (result == RV_DIV_REMAINDER) {
        if (width == RV_DIV_WORD) {
            x86_cg_movsxd_r64_r32(jit, X86_RAX, X86_RAX);
        }
    } else {
        x86_cg_mov_r64_imm64(jit, X86_RAX, (uint64_t)-1);
    }
    int done_zero = emit_jmp_to_patch(jit);

    patch_jump_to_here(jit, divisor_nonzero);

    int done_overflow = -1;
    if (sign == RV_DIV_SIGNED) {
        if (width == RV_DIV_WORD) {
            x86_cg_cmp_r32_imm32(jit, X86_RCX, -1);
        } else {
            x86_cg_cmp_r64_imm32(jit, X86_RCX, -1);
        }
        int not_overflow_a = emit_jcc_to_patch(jit, X86_COND_NE);

        if (width == RV_DIV_WORD) {
            x86_cg_cmp_r32_imm32(jit, X86_RAX, (int32_t)0x80000000);
        } else {
            x86_cg_mov_r64_imm64(jit, X86_RSI, (uint64_t)INT64_MIN);
            x86_cg_cmp_r64_r64(jit, X86_RAX, X86_RSI);
        }
        int not_overflow_b = emit_jcc_to_patch(jit, X86_COND_NE);

        if (result == RV_DIV_REMAINDER) {
            x86_cg_xor_r64_r64(jit, X86_RAX, X86_RAX);
        } else if (width == RV_DIV_WORD) {
            x86_cg_movsxd_r64_r32(jit, X86_RAX, X86_RAX);
        }
        done_overflow = emit_jmp_to_patch(jit);

        patch_jump_to_here(jit, not_overflow_a);
        patch_jump_to_here(jit, not_overflow_b);
    }

    if (sign == RV_DIV_SIGNED) {
        if (width == RV_DIV_WORD) {
            x86_cg_cdq(jit);
        } else {
            x86_cg_cqo(jit);
        }
        x86_cg_idiv_r(jit, rexw, X86_RCX);
    } else {
        x86_cg_xor_r32_r32(jit, X86_RDX, X86_RDX);
        x86_cg_div_r(jit, rexw, X86_RCX);
    }

    if (result == RV_DIV_REMAINDER) {
        x86_cg_mov_r64_r64(jit, X86_RAX, X86_RDX);
    }
    if (width == RV_DIV_WORD) {
        x86_cg_movsxd_r64_r32(jit, X86_RAX, X86_RAX);
    }

    patch_jump_to_here(jit, done_zero);
    if (done_overflow >= 0) {
        patch_jump_to_here(jit, done_overflow);
    }

    store_guest_reg(jit, instr->rd, X86_RAX);
}

static void emit_instruction(riscv_jit *jit, const ir_instr *instr) {
    switch (instr->op) {
    case IR_OP_NOP: {
    } break;

    case IR_OP_FUEL_CHECK: {
        x86_cg_emit_fuel_check(jit, instr->pc, cpu_fuel_offset());
    } break;

    case IR_OP_MOV: {
        load_guest_reg(jit, X86_RAX, instr->rs2);
        store_guest_reg(jit, instr->rd, X86_RAX);
    } break;

    case IR_OP_LI: {
        x86_cg_mov_r64_imm64(jit, X86_RAX, instr->imm);
        store_guest_reg(jit, instr->rd, X86_RAX);
    } break;

    case IR_OP_ADD: {
        emit_binary_reg_result(jit, instr, x86_cg_add_r64_r64);
    } break;

    case IR_OP_ADDI: {
        emit_imm32_result(jit, instr, x86_cg_add_r64_imm32);
    } break;

    case IR_OP_ADD_EXT32: {
        emit_binary_reg_result_ext32(jit, instr, x86_cg_add_r32_r32);
    } break;

    case IR_OP_ADDI_EXT32: {
        emit_imm32_result_ext32(jit, instr, x86_cg_add_r32_imm32);
    } break;

    case IR_OP_SLLI: {
        emit_imm8_result(jit, instr, x86_cg_shl_r64_imm8);
    } break;

    case IR_OP_SRLI: {
        emit_imm8_result(jit, instr, x86_cg_shr_r64_imm8);
    } break;

    case IR_OP_SRAI: {
        emit_imm8_result(jit, instr, x86_cg_sar_r64_imm8);
    } break;

    case IR_OP_BEQZ: {
        load_guest_reg(jit, X86_RAX, instr->rs1);
        x86_cg_test_r64_r64(jit, X86_RAX, X86_RAX);
        remember_branch_patch(jit, jit->code_size + X86_JCC_REL32_IMMEDIATE_OFFSET, instr->imm);
        x86_cg_je_rel32(jit, 0);
    } break;

    case IR_OP_JMP_REG: {
        load_guest_reg(jit, X86_RAX, instr->rs1);
        store_cpu_pc(jit, X86_RAX);
        emit_jump_to_epilogue(jit);
    } break;

    case IR_OP_BEQ: {
        emit_conditional_branch(jit, instr, X86_COND_E);
    } break;

    case IR_OP_BNE: {
        emit_conditional_branch(jit, instr, X86_COND_NE);
    } break;

    case IR_OP_BLT: {
        emit_conditional_branch(jit, instr, X86_COND_L);
    } break;

    case IR_OP_BGE: {
        emit_conditional_branch(jit, instr, X86_COND_GE);
    } break;

    case IR_OP_BLTU: {
        emit_conditional_branch(jit, instr, X86_COND_B);
    } break;

    case IR_OP_BGEU: {
        emit_conditional_branch(jit, instr, X86_COND_AE);
    } break;

    case IR_OP_JMP: {
        remember_branch_patch(jit, jit->code_size + X86_JMP_REL32_IMMEDIATE_OFFSET, instr->imm);
        x86_cg_jmp_rel32(jit, 0);
    } break;

    case IR_OP_JALR: {
        load_guest_reg(jit, X86_RAX, instr->rs1);
        x86_cg_add_r64_imm32(jit, X86_RAX, (int32_t)instr->imm);
        x86_cg_and_r64_imm32(jit, X86_RAX, RISCV_JALR_CLEAR_LOW_BIT_MASK);

        if (instr->rd != RISCV_REG_ZERO) {
            x86_cg_mov_r64_imm64(jit, X86_RCX, instr->pc + RV_INSTR_BYTES);
            store_guest_reg(jit, instr->rd, X86_RCX);
        }

        store_cpu_pc(jit, X86_RAX);
        emit_jump_to_epilogue(jit);
    } break;

    case IR_OP_SUB: {
        emit_binary_reg_result(jit, instr, x86_cg_sub_r64_r64);
    } break;

    case IR_OP_SUB_EXT32: {
        emit_binary_reg_result_ext32(jit, instr, x86_cg_sub_r32_r32);
    } break;

    case IR_OP_XOR: {
        emit_binary_reg_result(jit, instr, x86_cg_xor_r64_r64);
    } break;

    case IR_OP_XORI: {
        emit_imm32_result(jit, instr, x86_cg_xor_r64_imm32);
    } break;

    case IR_OP_OR: {
        emit_binary_reg_result(jit, instr, x86_cg_or_r64_r64);
    } break;

    case IR_OP_ORI: {
        emit_imm32_result(jit, instr, x86_cg_or_r64_imm32);
    } break;

    case IR_OP_AND: {
        emit_binary_reg_result(jit, instr, x86_cg_and_r64_r64);
    } break;

    case IR_OP_ANDI: {
        emit_imm32_result(jit, instr, x86_cg_and_r64_imm32);
    } break;

    case IR_OP_SLL: {
        emit_shift_reg_result(jit, instr, x86_cg_shl_r64_cl);
    } break;

    case IR_OP_SRL: {
        emit_shift_reg_result(jit, instr, x86_cg_shr_r64_cl);
    } break;

    case IR_OP_SRA: {
        emit_shift_reg_result(jit, instr, x86_cg_sar_r64_cl);
    } break;

    case IR_OP_SLL_EXT32: {
        emit_shift_reg_result_ext32(jit, instr, x86_cg_shl_r32_cl);
    } break;

    case IR_OP_SRL_EXT32: {
        emit_shift_reg_result_ext32(jit, instr, x86_cg_shr_r32_cl);
    } break;

    case IR_OP_SRA_EXT32: {
        emit_shift_reg_result_ext32(jit, instr, x86_cg_sar_r32_cl);
    } break;

    case IR_OP_SLLI_EXT32: {
        emit_imm8_result_ext32(jit, instr, x86_cg_shl_r32_imm8);
    } break;

    case IR_OP_SRLI_EXT32: {
        emit_imm8_result_ext32(jit, instr, x86_cg_shr_r32_imm8);
    } break;

    case IR_OP_SRAI_EXT32: {
        emit_imm8_result_ext32(jit, instr, x86_cg_sar_r32_imm8);
    } break;

    case IR_OP_SLT: {
        emit_compare_reg_result(jit, instr, X86_COND_L);
    } break;

    case IR_OP_SLTU: {
        emit_compare_reg_result(jit, instr, X86_COND_B);
    } break;

    case IR_OP_SLTI: {
        emit_compare_imm_result(jit, instr, X86_COND_L);
    } break;

    case IR_OP_SLTIU: {
        emit_compare_imm_result(jit, instr, X86_COND_B);
    } break;

    case IR_OP_LOAD8: {
        emit_load(jit, instr, X86_MEM_TYPE_8BIT);
    } break;

    case IR_OP_LOAD8_U: {
        emit_load(jit, instr, X86_MEM_TYPE_8BIT_U);
    } break;

    case IR_OP_LOAD16: {
        emit_load(jit, instr, X86_MEM_TYPE_16BIT);
    } break;

    case IR_OP_LOAD16_U: {
        emit_load(jit, instr, X86_MEM_TYPE_16BIT_U);
    } break;

    case IR_OP_LOAD32: {
        emit_load(jit, instr, X86_MEM_TYPE_32BIT);
    } break;

    case IR_OP_LOAD32_U: {
        emit_load(jit, instr, X86_MEM_TYPE_32BIT_U);
    } break;

    case IR_OP_LOAD64: {
        emit_load(jit, instr, X86_MEM_TYPE_64BIT);
    } break;

    case IR_OP_STORE8: {
        emit_store(jit, instr, X86_MEM_TYPE_8BIT);
    } break;

    case IR_OP_STORE16: {
        emit_store(jit, instr, X86_MEM_TYPE_16BIT);
    } break;

    case IR_OP_STORE32: {
        emit_store(jit, instr, X86_MEM_TYPE_32BIT);
    } break;

    case IR_OP_STORE64: {
        emit_store(jit, instr, X86_MEM_TYPE_64BIT);
    } break;

    case IR_OP_ECALL: {
        emit_ecall(jit);
    } break;

    case IR_OP_EBREAK: {
        x86_cg_int3(jit);
    } break;

    case IR_OP_UD2: {
        x86_cg_ud2(jit);
    } break;

    case IR_OP_MUL:
    case IR_OP_MULH:
    case IR_OP_MULHSU:
    case IR_OP_MULHU:
    case IR_OP_MULW: {
        emit_mul(jit, instr);
    } break;

    case IR_OP_DIV: {
        emit_divrem(jit, instr, RV_DIV_SIGNED, RV_DIV_XLEN, RV_DIV_QUOTIENT);
    } break;

    case IR_OP_DIVU: {
        emit_divrem(jit, instr, RV_DIV_UNSIGNED, RV_DIV_XLEN, RV_DIV_QUOTIENT);
    } break;

    case IR_OP_REM: {
        emit_divrem(jit, instr, RV_DIV_SIGNED, RV_DIV_XLEN, RV_DIV_REMAINDER);
    } break;

    case IR_OP_REMU: {
        emit_divrem(jit, instr, RV_DIV_UNSIGNED, RV_DIV_XLEN, RV_DIV_REMAINDER);
    } break;

    case IR_OP_DIVW: {
        emit_divrem(jit, instr, RV_DIV_SIGNED, RV_DIV_WORD, RV_DIV_QUOTIENT);
    } break;

    case IR_OP_DIVUW: {
        emit_divrem(jit, instr, RV_DIV_UNSIGNED, RV_DIV_WORD, RV_DIV_QUOTIENT);
    } break;

    case IR_OP_REMW: {
        emit_divrem(jit, instr, RV_DIV_SIGNED, RV_DIV_WORD, RV_DIV_REMAINDER);
    } break;

    case IR_OP_REMUW: {
        emit_divrem(jit, instr, RV_DIV_UNSIGNED, RV_DIV_WORD, RV_DIV_REMAINDER);
    } break;
    }
}

static void patch_guest_branches(riscv_jit *jit, int epilogue_offset) {
    for (int i = 0; i < jit->branch_jump_buf_size; i++) {
        int      patch     = jit->branch_jump_buf[i];
        uint64_t target_pc = jit->branch_target_buf[i];
        int      target    = epilogue_offset;

        if (jit_pc_is_in_text(jit, target_pc)) {
            target = (int)jit->pc_map[target_pc - jit->elf.text_addr];
        }

        patch_rel32(jit, patch, target);
    }
}

static void patch_epilogue_jumps(riscv_jit *jit, int epilogue_offset) {
    for (int i = 0; i < jit->epilogue_jump_buf_size; i++) {
        int jump_opcode_offset = jit->epilogue_jump_buf[i];
        patch_rel32(jit, jump_opcode_offset + X86_JMP_REL32_IMMEDIATE_OFFSET, epilogue_offset);
    }
}

static void emit_fault_stub(riscv_jit *jit, int rel32_patch, uint64_t pc, riscv_jit_fault_type fault, int epilogue_offset) {
    patch_rel32(jit, rel32_patch, (int)jit->code_size);
    store_cpu_pc_immediate(jit, pc);
    store_cpu_fault(jit, fault);
    x86_cg_jmp_rel32(jit, relative_offset_from_next_jmp(jit, epilogue_offset));
}

static void emit_fault_stubs(riscv_jit *jit, const int *patches, const uint64_t *pcs, int count, riscv_jit_fault_type fault, int epilogue_offset) {
    for (int i = 0; i < count; i++) {
        emit_fault_stub(jit, patches[i], pcs[i], fault, epilogue_offset);
    }
}

riscv_result riscv_jit_cg_x86_compile(riscv_jit *jit, ir_builder *builder) {
    static const x86_reg callee_saved_regs[] = {
        X86_RBP,
        X86_RBX,
        X86_R12,
        X86_R13,
        X86_R14,
        X86_R15,
    };

    if (!allocate_codegen_buffers(jit, builder->count)) {
        free_codegen_buffers(jit);
        return RISCV_JIT_ERR_OOM;
    }

    jit->trampoline_offset = jit->code_size;

    if (!jit_has_code_space(jit, X86_TRAMPOLINE_MAX_BYTES)) {
        free_codegen_buffers(jit);
        return RISCV_JIT_ERR_OOM;
    }

    for (size_t i = 0; i < RISCV_ARRAY_COUNT(callee_saved_regs); i++) {
        x86_cg_push_r64(jit, callee_saved_regs[i]);
    }

    x86_cg_mov_r64_r64(jit, X86_R15, X86_RDI);
    x86_cg_mov_r64_m64(jit, X86_R14, X86_R15, offsetof(riscv_cpu, stack_mem));
    x86_cg_mov_r64_m64(jit, X86_R13, X86_R15, offsetof(riscv_cpu, stack_size));
    x86_cg_mov_r64_imm64(jit, X86_R12, (uint64_t)jit->elf.text_data);
    x86_cg_mov_r64_imm64(jit, X86_RBX, jit->elf.text_addr);
    x86_cg_mov_r64_imm64(jit, X86_RBP, jit->elf.text_addr + jit->elf.text_size);

    x86_cg_jmp_r64(jit, X86_RSI);

    for (size_t i = 0; i < builder->count; i++) {
        ir_instr *instr = &builder->instrs[i];

        if (!jit_has_code_space(jit, X86_GUEST_INSTR_MAX_BYTES)) {
            free_codegen_buffers(jit);
            return RISCV_JIT_ERR_OOM;
        }

        if (i == 0 || builder->instrs[i - 1].pc != instr->pc) {
            jit->pc_map[instr->pc - jit->elf.text_addr] = jit->code_size;
        }

        zero_guest_zero_register(jit);

        if (ir_result_is_discarded(instr)) {
            continue;
        }

        emit_instruction(jit, instr);
    }

    if (!jit_has_code_space(jit, X86_EPILOGUE_AND_PATCH_BYTES)) {
        free_codegen_buffers(jit);
        return RISCV_JIT_ERR_OOM;
    }

    int epilogue_offset  = (int)jit->code_size;
    jit->epilogue_offset = (size_t)epilogue_offset;

    patch_guest_branches(jit, epilogue_offset);
    patch_epilogue_jumps(jit, epilogue_offset);

    for (size_t i = RISCV_ARRAY_COUNT(callee_saved_regs); i > 0; i--) {
        x86_cg_pop_r64(jit, callee_saved_regs[i - 1]);
    }
    x86_cg_ret(jit);

    emit_fault_stubs(jit, jit->read_fault_patches, jit->read_fault_pcs, jit->read_fault_patch_count, RISCV_FAULT_READ, epilogue_offset);
    emit_fault_stubs(jit, jit->write_fault_patches, jit->write_fault_pcs, jit->write_fault_patch_count, RISCV_FAULT_WRITE, epilogue_offset);
    emit_fault_stubs(jit, jit->timeout_patches, jit->timeout_pcs, jit->timeout_patch_count, RISCV_FAULT_TIMEOUT, epilogue_offset);

    free_codegen_buffers(jit);
    return RISCV_OK;
}

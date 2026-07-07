// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

#ifndef RISCV_RESULT_H
#define RISCV_RESULT_H

typedef enum {
    RISCV_OK = 0,

    // JIT errors
    RISCV_JIT_ERR_OOM, // out of memory
    RISCV_JIT_ERR_IO,  // i/o error
    RISCV_JIT_BAD_PC,  // invalid or an unmapped program counter

    // ELF parsing erorrs
    RISCV_ELF_ERR_IO,        // i/o error
    RISCV_ELF_ERR_TOO_SMALL, // file is too small to even contain an ELF header
    RISCV_ELF_ERR_BAD_MAGIC, // missing/corrupt ELF magic bytes
    RISCV_ELF_ERR_NOT_64BIT, // expected 64-bit ELF file
    RISCV_ELF_ERR_NOT_RISCV, // ELF file is not a RISC-V binary
    RISCV_ELF_ERR_OOB,       // corrupt ELF file (out of bounds)
    RISCV_ELF_ERR_NO_TEXT,   // could not find a .text section
} riscv_result;

static inline const char *riscv_result_str(riscv_result result) {
    switch (result) {

    case RISCV_OK: {
        return "no error. did you mess up your error check condition?";
    }

    case RISCV_JIT_ERR_OOM: {
        return "out of memory";
    } break;

    case RISCV_JIT_BAD_PC: {
        return "invalid or an unmapped program counter";
    } break;

    case RISCV_JIT_ERR_IO:
    case RISCV_ELF_ERR_IO: {
        return "I/O error";
    }
    case RISCV_ELF_ERR_TOO_SMALL: {
        return "file is too small to even contain an ELF header";
    }
    case RISCV_ELF_ERR_BAD_MAGIC: {
        return "missing/corrupt ELF magic bytes";
    }
    case RISCV_ELF_ERR_NOT_64BIT: {
        return "expected 64-bit ELF file";
    }
    case RISCV_ELF_ERR_NOT_RISCV: {
        return "ELF file is not a RISC-V binary";
    }
    case RISCV_ELF_ERR_OOB: {
        return "corrupt ELF file (out of bounds)";
    }
    case RISCV_ELF_ERR_NO_TEXT: {
        return "could not find a .text section";
    }

    default: {
        return "unknown error";
    }
    }
}

#endif // RISCV_RESULT_H

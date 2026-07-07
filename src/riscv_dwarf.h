// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

#ifndef RISCV_DWARF_H
#define RISCV_DWARF_H

#include "riscv_elf.h"

enum {
    RISCV_SOURCE_PATH_SIZE = 1024,
};

#define DWARF_EXTENDED_OPCODE 0x00

#define DW_LNS_COPY               0x01
#define DW_LNS_ADVANCE_PC         0x02
#define DW_LNS_ADVANCE_LINE       0x03
#define DW_LNS_SET_FILE           0x04
#define DW_LNS_SET_COLUMN         0x05
#define DW_LNS_NEGATE_STMT        0x06
#define DW_LNS_SET_BASIC_BLOCK    0x07
#define DW_LNS_CONST_ADD_PC       0x08
#define DW_LNS_FIXED_ADVANCE_PC   0x09
#define DW_LNS_SET_PROLOGUE_END   0x0A
#define DW_LNS_SET_EPILOGUE_BEGIN 0x0B
#define DW_LNS_SET_ISA            0x0C

#define DW_FORM_ADDR         0x01
#define DW_FORM_BLOCK2       0x03
#define DW_FORM_BLOCK4       0x04
#define DW_FORM_DATA2        0x05
#define DW_FORM_DATA4        0x06
#define DW_FORM_DATA8        0x07
#define DW_FORM_STRING       0x08
#define DW_FORM_BLOCK        0x09
#define DW_FORM_EXPRLOC      0x18
#define DW_FORM_BLOCK1       0x0A
#define DW_FORM_DATA1        0x0B
#define DW_FORM_FLAG         0x0C
#define DW_FORM_SDATA        0x0D
#define DW_FORM_STRP         0x0E
#define DW_FORM_UDATA        0x0F
#define DW_FORM_REF_ADDR     0x10
#define DW_FORM_REF1         0x11
#define DW_FORM_REF2         0x12
#define DW_FORM_REF4         0x13
#define DW_FORM_REF8         0x14
#define DW_FORM_REF_UDATA    0x15
#define DW_FORM_INDIRECT     0x16
#define DW_FORM_SEC_OFFSET   0x17
#define DW_FORM_FLAG_PRESENT 0x19
#define DW_FORM_REF_SIG8     0x20

#define DW_LNE_END_SEQUENCE 0x01
#define DW_LNE_SET_ADDRESS  0x02
#define DW_LNE_DEFINE_FILE  0x03

#define DW_TAG_COMPILE_UNIT 0x11

#define DW_AT_COMP_DIR  0x1B
#define DW_AT_STMT_LIST 0x10

typedef struct {
    // relative path for printing
    char rel_path[RISCV_SOURCE_PATH_SIZE];
    // absolute path for loading
    char abs_path[RISCV_SOURCE_PATH_SIZE];

    int line;
} riscv_source_loc;

static inline riscv_source_loc riscv_source_loc_unknown(void) {
    return (riscv_source_loc){.rel_path = "??", .abs_path = "", .line = 0};
}

bool riscv_dwarf_lookup_pc(const riscv_elf *elf, uint64_t target_pc, riscv_source_loc *out_loc);
bool riscv_dwarf_get_source_line(riscv_source_loc loc, char *out_buf, size_t buf_size);

#endif // RISCV_DWARF_H

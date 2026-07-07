// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

#include "riscv_dwarf.h"
#include "riscv_elf.h"
#include "riscv_mem.h"
#include "riscv_types.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define DWARF_ROOT_DIR                  "."
#define DWARF_32BIT_LENGTH_SENTINEL     UINT32_MAX
#define DWARF_INVALID_STMT_LIST         UINT32_MAX
#define DWARF_MAX_SPECIAL_OPCODE        UINT8_MAX
#define DWARF_EXTENDED_OPCODE_NAME_SIZE sizeof(uint8_t)

// -- internal --

enum {
    LEB128_PAYLOAD_BITS     = 7,
    LEB128_PAYLOAD_MASK     = 0x7F,
    LEB128_CONTINUATION_BIT = 0x80,
    LEB128_SIGN_BIT         = 0x40,
    DWARF_INT64_BITS        = 64,

    DWARF_MAX_SUPPORTED_VERSION  = 4,
    DWARF_DEFAULT_OPS_PER_INSTR  = 1,
    DWARF_FIRST_STANDARD_OPCODE  = 1,
    DWARF_MAX_INCLUDE_DIRS       = 4096,
    DWARF_MAX_FILE_NAMES         = 4096,
};

typedef struct {
    const char *file;

    const char *file_dir;
    const char *comp_dir;

    int      line;
    uint64_t diff;
} match;

static const uint8_t *read_uleb128(const uint8_t *p, uint64_t *out) {
    uint64_t result = 0;
    int      shift  = 0;

    while (true) {
        uint8_t byte = *p++;
        result |= (uint64_t)(byte & LEB128_PAYLOAD_MASK) << shift;
        shift += LEB128_PAYLOAD_BITS;

        if ((byte & LEB128_CONTINUATION_BIT) == 0) {
            break;
        }
    }

    if (out) {
        *out = result;
    }
    return p;
}

static const uint8_t *read_sleb128(const uint8_t *p, int64_t *out) {
    int64_t result = 0;
    int     shift  = 0;
    uint8_t byte;

    do {
        byte = *p++;
        result |= (int64_t)(byte & LEB128_PAYLOAD_MASK) << shift;
        shift += LEB128_PAYLOAD_BITS;
    } while (byte & LEB128_CONTINUATION_BIT);

    if ((shift < DWARF_INT64_BITS) && (byte & LEB128_SIGN_BIT)) {
        result |= -(1ULL << shift);
    }

    if (out) {
        *out = result;
    }
    return p;
}

static const uint8_t *skip_dwarf_form(const uint8_t *p, uint64_t form, uint8_t addr_size) {
    switch (form) {
    case DW_FORM_ADDR: {
        return p + addr_size;
    }
    case DW_FORM_BLOCK2: {
        return p + sizeof(uint16_t) + riscv_mem_read_u16(p);
    }
    case DW_FORM_BLOCK4: {
        return p + sizeof(uint32_t) + riscv_mem_read_u32(p);
    }
    case DW_FORM_DATA2: {
        return p + sizeof(uint16_t);
    }
    case DW_FORM_DATA4: {
        return p + sizeof(uint32_t);
    }
    case DW_FORM_DATA8: {
        return p + sizeof(uint64_t);
    }
    case DW_FORM_STRING: {
        return p + strlen((const char *)p) + sizeof(char);
    }
    case DW_FORM_BLOCK:
    case DW_FORM_EXPRLOC: {
        uint64_t len;
        p = read_uleb128(p, &len);

        return p + len;
    }
    case DW_FORM_BLOCK1: {
        return p + sizeof(uint8_t) + *p;
    }
    case DW_FORM_DATA1:
    case DW_FORM_FLAG:
    case DW_FORM_REF1: {
        return p + sizeof(uint8_t);
    }
    case DW_FORM_SDATA: {
        return read_sleb128(p, 0);
    }
    case DW_FORM_STRP:
    case DW_FORM_REF_ADDR:
    case DW_FORM_REF4:
    case DW_FORM_SEC_OFFSET: {
        return p + sizeof(uint32_t);
    }
    case DW_FORM_REF2: {
        return p + sizeof(uint16_t);
    }
    case DW_FORM_REF8:
    case DW_FORM_REF_SIG8: {
        return p + sizeof(uint64_t);
    }
    case DW_FORM_UDATA:
    case DW_FORM_REF_UDATA: {
        return read_uleb128(p, 0);
    }
    case DW_FORM_INDIRECT: {
        uint64_t f;
        p = read_uleb128(p, &f);

        return skip_dwarf_form(p, f, addr_size);
    }
    case DW_FORM_FLAG_PRESENT: {
        return p;
    }
    default:
        return p;
    }
}

static const uint8_t *skip_abbrev_attributes(const uint8_t *abbrev) {
    while (true) {
        uint64_t name;
        uint64_t form;

        abbrev = read_uleb128(abbrev, &name);
        abbrev = read_uleb128(abbrev, &form);

        if (name == 0 && form == 0) {
            return abbrev;
        }
    }
}

static const uint8_t *find_abbrev(const riscv_elf *elf, uint32_t abbrev_offset, uint64_t target_code) {
    const uint8_t *abbrev = elf->debug_abbrev + abbrev_offset;
    const uint8_t *end    = elf->debug_abbrev + elf->debug_abbrev_size;

    while (abbrev < end) {
        uint64_t code;
        abbrev = read_uleb128(abbrev, &code);

        if (code == target_code) {
            return abbrev;
        }

        abbrev = read_uleb128(abbrev, 0); // tag
        abbrev++;                            // has_children
        abbrev = skip_abbrev_attributes(abbrev);
    }

    return end;
}

static const char *get_comp_dir(const riscv_elf *elf, uint32_t target_stmt_list) {
    if (!elf->debug_info || !elf->debug_abbrev) {
        return 0;
    }

    const uint8_t *p   = elf->debug_info;
    const uint8_t *end = p + elf->debug_info_size;

    while (p < end) {
        uint32_t unit_len = riscv_mem_read_u32(p);
        p += sizeof(uint32_t);

        if (unit_len == DWARF_32BIT_LENGTH_SENTINEL) {
            break;
        }

        const uint8_t *unit_end = p + unit_len;

        p += sizeof(uint16_t); // version
        uint32_t abbrev_offset = riscv_mem_read_u32(p);
        p += sizeof(uint32_t);

        uint8_t addr_size = *p++;

        uint64_t abbrev_code;
        p = read_uleb128(p, &abbrev_code);

        if (abbrev_code == 0) {
            p = unit_end;
            continue;
        }

        const uint8_t *abbrev = find_abbrev(elf, abbrev_offset, abbrev_code);

        uint64_t tag;
        abbrev = read_uleb128(abbrev, &tag);
        abbrev++; // has_children

        const char *comp_dir  = 0;
        uint32_t    stmt_list = DWARF_INVALID_STMT_LIST;

        while (p < unit_end) {
            uint64_t attr_name;
            uint64_t attr_form;

            abbrev = read_uleb128(abbrev, &attr_name);
            abbrev = read_uleb128(abbrev, &attr_form);

            if (attr_name == 0 && attr_form == 0) {
                break;
            }

            if (tag == DW_TAG_COMPILE_UNIT && attr_name == DW_AT_COMP_DIR) {
                if (attr_form == DW_FORM_STRING) {
                    comp_dir = (const char *)p;
                } else if (attr_form == DW_FORM_STRP && elf->debug_str) {
                    comp_dir = (const char *)(elf->debug_str + riscv_mem_read_u32(p));
                }
            } else if (tag == DW_TAG_COMPILE_UNIT && attr_name == DW_AT_STMT_LIST) {
                if (attr_form == DW_FORM_SEC_OFFSET || attr_form == DW_FORM_DATA4) {
                    stmt_list = riscv_mem_read_u32(p);
                }
            }

            p = skip_dwarf_form(p, attr_form, addr_size);
        }

        if (tag == DW_TAG_COMPILE_UNIT && stmt_list == target_stmt_list) {
            return comp_dir;
        }

        p = unit_end;
    }

    return 0;
}

static bool string_is_absolute_path(const char *path) {
    return path[0] == '/';
}

static bool string_is_current_dir(const char *path) {
    return path[0] == '.' && path[1] == '\0';
}

static void path_append_char(char *path, size_t *len, char ch) {
    if (*len + 1 < RISCV_SOURCE_PATH_SIZE) {
        path[(*len)++] = ch;
    }
}

static void path_append_string(char *path, size_t *len, const char *text) {
    while (*text) {
        path_append_char(path, len, *text++);
    }
}

static void path_append_separator(char *path, size_t *len) {
    if (*len > 0 && path[*len - 1] != '/') {
        path_append_char(path, len, '/');
    }
}

static void path_finish(char *path, size_t len) {
    path[len] = '\0';
}

static void path_copy(char *dst, const char *src) {
    size_t len = 0;
    path_append_string(dst, &len, src);
    path_finish(dst, len);
}

static void resolve_paths(match match, riscv_source_loc *out_loc) {
    if (string_is_absolute_path(match.file)) {
        path_copy(out_loc->rel_path, match.file);
        path_copy(out_loc->abs_path, match.file);
        return;
    }

    size_t rel_len = 0;
    size_t abs_len = 0;

    const char *dir = match.file_dir ? match.file_dir : DWARF_ROOT_DIR;

    if (!string_is_absolute_path(dir) && match.comp_dir && string_is_absolute_path(match.comp_dir)) {
        path_append_string(out_loc->abs_path, &abs_len, match.comp_dir);
        path_append_separator(out_loc->abs_path, &abs_len);
    }

    if (!string_is_current_dir(dir)) {
        path_append_string(out_loc->rel_path, &rel_len, dir);
        path_append_separator(out_loc->rel_path, &rel_len);

        path_append_string(out_loc->abs_path, &abs_len, dir);
        path_append_separator(out_loc->abs_path, &abs_len);
    }

    path_append_string(out_loc->rel_path, &rel_len, match.file);
    path_append_string(out_loc->abs_path, &abs_len, match.file);

    path_finish(out_loc->rel_path, rel_len);
    path_finish(out_loc->abs_path, abs_len);
}

static void remember_line_row(
    match            *best,
    uint64_t          target_pc,
    uint64_t          addr,
    uint32_t          file,
    uint32_t          line,
    const char       *const files[DWARF_MAX_FILE_NAMES],
    const uint64_t    file_dirs[DWARF_MAX_FILE_NAMES],
    int               files_count,
    const char       *const dirs[DWARF_MAX_INCLUDE_DIRS],
    int               dirs_count,
    const char       *comp_dir) {
    if (addr > target_pc || (target_pc - addr) >= best->diff || file >= (uint32_t)files_count) {
        return;
    }

    best->diff     = target_pc - addr;
    best->file     = files[file];
    best->file_dir = (file_dirs[file] < (uint64_t)dirs_count) ? dirs[file_dirs[file]] : DWARF_ROOT_DIR;
    best->comp_dir = comp_dir;
    best->line     = (int)line;
}

// -- implementation --

bool riscv_dwarf_lookup_pc(const riscv_elf *elf, uint64_t target_pc, riscv_source_loc *out_loc) {
    if (!elf->debug_line) {
        return false;
    }

    const uint8_t *p   = elf->debug_line;
    const uint8_t *end = p + elf->debug_line_size;

    match match = {0};
    match.diff  = UINT64_MAX;

    while (p < end) {
        uint32_t unit_offset = (uint32_t)(p - elf->debug_line);
        uint32_t unit_len    = riscv_mem_read_u32(p);
        p += sizeof(uint32_t);

        if (unit_len == DWARF_32BIT_LENGTH_SENTINEL) {
            return false;
        }

        const uint8_t *unit_end = p + unit_len;
        uint16_t       version  = riscv_mem_read_u16(p);
        p += sizeof(uint16_t);

        if (version > DWARF_MAX_SUPPORTED_VERSION) {
            p = unit_end;
            continue;
        }

        uint32_t header_len = riscv_mem_read_u32(p);
        p += sizeof(uint32_t);
        const uint8_t *program_start = p + header_len;

        uint8_t min_instr_len     = *p++;
        uint8_t max_ops_per_instr = (version >= DWARF_MAX_SUPPORTED_VERSION) ? *p++ : DWARF_DEFAULT_OPS_PER_INSTR;
        uint8_t default_is_stmt   = *p++;
        int8_t  line_base         = (int8_t)*p++;
        uint8_t line_range        = *p++;
        uint8_t opcode_base       = *p++;

        (void)max_ops_per_instr;
        (void)default_is_stmt;

        const uint8_t *std_opcode_lens = p;
        p += (opcode_base - DWARF_FIRST_STANDARD_OPCODE);

        const char *comp_dir = get_comp_dir(elf, unit_offset);

        const char *dirs[DWARF_MAX_INCLUDE_DIRS];
        int         dirs_count = 1;
        dirs[0]                = comp_dir ? comp_dir : DWARF_ROOT_DIR;

        while (*p != 0) {
            if (dirs_count < DWARF_MAX_INCLUDE_DIRS) {
                dirs[dirs_count++] = (const char *)p;
            }
            p += strlen((const char *)p) + sizeof(char);
        }

        p++; // 0 terminator for include directories

        const char *files[DWARF_MAX_FILE_NAMES];
        uint64_t    file_dirs[DWARF_MAX_FILE_NAMES];
        int         files_count = 1;

        while (*p != 0) {
            if (files_count < DWARF_MAX_FILE_NAMES) {
                files[files_count] = (const char *)p;
            }
            p += strlen((const char *)p) + sizeof(char);

            uint64_t dir_idx;
            p = read_uleb128(p, &dir_idx);

            if (files_count < DWARF_MAX_FILE_NAMES) {
                file_dirs[files_count] = dir_idx;
                files_count++;
            }

            p = read_uleb128(p, 0); // modification time
            p = read_uleb128(p, 0); // file size
        }

        p++; // 0 terminator for file list

        uint64_t addr         = 0;
        uint32_t file         = 1;
        uint32_t line         = 1;
        bool     end_sequence = false;

        p = program_start;

        while (p < unit_end && !end_sequence) {
            uint8_t op = *p++;

            if (op >= opcode_base) {
                uint8_t adjusted = op - opcode_base;

                line += line_base + (adjusted % line_range);
                addr += (adjusted / line_range) * min_instr_len;

                remember_line_row(&match, target_pc, addr, file, line, files, file_dirs, files_count, dirs, dirs_count, comp_dir);
                continue;
            }

            if (op == DWARF_EXTENDED_OPCODE) {
                uint64_t ext_len;
                p = read_uleb128(p, &ext_len);

                uint8_t sub_op = *p++;

                switch (sub_op) {
                case DW_LNE_END_SEQUENCE: {
                    remember_line_row(&match, target_pc, addr, file, line, files, file_dirs, files_count, dirs, dirs_count, comp_dir);
                    end_sequence = true;
                } break;

                case DW_LNE_SET_ADDRESS: {
                    addr = riscv_mem_read_u64(p);
                } break;
                }

                p += ext_len - DWARF_EXTENDED_OPCODE_NAME_SIZE;
                continue;
            }

            switch (op) {
            case DW_LNS_COPY: {
                remember_line_row(&match, target_pc, addr, file, line, files, file_dirs, files_count, dirs, dirs_count, comp_dir);
            } break;

            case DW_LNS_ADVANCE_PC: {
                uint64_t adv;
                p = read_uleb128(p, &adv);

                addr += adv * min_instr_len;
            } break;

            case DW_LNS_ADVANCE_LINE: {
                int64_t adv;
                p = read_sleb128(p, &adv);

                line += adv;
            } break;

            case DW_LNS_SET_FILE: {
                uint64_t i;
                p = read_uleb128(p, &i);

                file = (uint32_t)i;
            } break;

            case DW_LNS_CONST_ADD_PC: {
                uint8_t adjusted = DWARF_MAX_SPECIAL_OPCODE - opcode_base;

                addr += (adjusted / line_range) * min_instr_len;
            } break;

            case DW_LNS_FIXED_ADVANCE_PC: {
                addr += riscv_mem_read_u16(p);
                p += sizeof(uint16_t);
            } break;

            default: {
                for (int i = 0; i < std_opcode_lens[op - DWARF_FIRST_STANDARD_OPCODE]; i++) {
                    p = read_uleb128(p, 0);
                }
            } break;
            }
        }

        p = unit_end;
    }

    if (match.file) {
        resolve_paths(match, out_loc);
        out_loc->line = match.line;

        return true;
    }

    return false;
}

bool riscv_dwarf_get_source_line(riscv_source_loc loc, char *out_buf, size_t buf_size) {
    if (!loc.abs_path[0] || loc.line <= 0 || !out_buf || buf_size == 0) {
        return false;
    }

    static char        path[RISCV_SOURCE_PATH_SIZE] = {0};
    static const char *data                         = 0;
    static size_t      size                         = 0;

    if (strcmp(path, loc.abs_path) != 0) {
        if (data) {
            free((void *)data);
        }

        path_copy(path, loc.abs_path);
        data = riscv_mem_load_file(path, &size);
    }

    if (!data) {
        return false;
    }

    int    current_line = 1;
    size_t i            = 0;

    while (i < size && current_line < loc.line) {
        if (data[i++] == '\n') {
            current_line++;
        }
    }

    if (current_line != loc.line || i >= size) {
        return false;
    }

    size_t out_i = 0;
    while (i < size && data[i] != '\n' && data[i] != '\r' && out_i + 1 < buf_size) {
        out_buf[out_i++] = data[i++];
    }

    out_buf[out_i] = '\0';

    return true;
}

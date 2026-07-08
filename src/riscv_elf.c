// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

#include "riscv_elf.h"
#include "riscv_mem.h"

#include <stdlib.h>
#include <string.h>

#define ELF_SECTION_TEXT         ".text"
#define ELF_SECTION_DEBUG_LINE   ".debug_line"
#define ELF_SECTION_DEBUG_INFO   ".debug_info"
#define ELF_SECTION_DEBUG_ABBREV ".debug_abbrev"
#define ELF_SECTION_DEBUG_STR    ".debug_str"
#define ELF_UNKNOWN_FUNCTION     "??"

static bool elf_range_fits(size_t file_size, uint64_t offset, uint64_t bytes) {
    return offset <= file_size && bytes <= file_size - offset;
}

static bool elf_section_fits(size_t file_size, const Elf64_Shdr *section) {
    return elf_range_fits(file_size, section->offset, section->size);
}

static const uint8_t *elf_section_data(const uint8_t *data, const Elf64_Shdr *section) {
    return data + section->offset;
}

static bool elf_section_name_is(const char *section_name, const char *expected_name) {
    return strcmp(section_name, expected_name) == 0;
}

static riscv_result elf_find_sections(riscv_elf *elf, const Elf64_Ehdr *ehdr) {
    const uint8_t *data     = elf->data;
    Elf64_Shdr    *sections = (Elf64_Shdr *)(data + ehdr->shoff);

    if (ehdr->shstrndx >= ehdr->shnum) {
        return RISCV_ELF_ERR_OOB;
    }

    Elf64_Shdr *section_names = &sections[ehdr->shstrndx];
    if (!elf_section_fits(elf->size, section_names)) {
        return RISCV_ELF_ERR_OOB;
    }

    const char *section_name_table = (const char *)elf_section_data(data, section_names);

    for (Elf64_Half i = 0; i < ehdr->shnum; i++) {
        Elf64_Shdr *section = &sections[i];

        if (section->name >= section_names->size || !elf_section_fits(elf->size, section)) {
            return RISCV_ELF_ERR_OOB;
        }

        const char *section_name = section_name_table + section->name;

        if (elf_section_name_is(section_name, ELF_SECTION_TEXT)) {
            elf->text_addr = section->addr;
            elf->text_size = section->size;
            elf->text_data = elf_section_data(data, section);
        } else if (section->type == SHT_SYMTAB) {
            if (section->link >= ehdr->shnum || !elf_section_fits(elf->size, &sections[section->link])) {
                return RISCV_ELF_ERR_OOB;
            }

            elf->symtab      = elf_section_data(data, section);
            elf->symtab_size = section->size;
            elf->strtab      = (const char *)elf_section_data(data, &sections[section->link]);
        } else if (elf_section_name_is(section_name, ELF_SECTION_DEBUG_LINE)) {
            elf->debug_line      = elf_section_data(data, section);
            elf->debug_line_size = section->size;
        } else if (elf_section_name_is(section_name, ELF_SECTION_DEBUG_INFO)) {
            elf->debug_info      = elf_section_data(data, section);
            elf->debug_info_size = section->size;
        } else if (elf_section_name_is(section_name, ELF_SECTION_DEBUG_ABBREV)) {
            elf->debug_abbrev      = elf_section_data(data, section);
            elf->debug_abbrev_size = section->size;
        } else if (elf_section_name_is(section_name, ELF_SECTION_DEBUG_STR)) {
            elf->debug_str      = (const char *)elf_section_data(data, section);
            elf->debug_str_size = section->size;
        }
    }

    return elf->text_data ? RISCV_OK : RISCV_ELF_ERR_NO_TEXT;
}

// gathers every writable allocated section into a single private mutable
// region spanning [min_addr, max_end)
static riscv_result elf_build_data_region(riscv_elf *elf, const Elf64_Ehdr *ehdr) {
    const uint8_t *data     = elf->data;
    Elf64_Shdr    *sections = (Elf64_Shdr *)(data + ehdr->shoff);

    const uint64_t writable_alloc = SHF_ALLOC | SHF_WRITE;

    uint64_t lo    = UINT64_MAX;
    uint64_t hi    = 0;
    bool     found = false;

    for (Elf64_Half i = 0; i < ehdr->shnum; i++) {
        Elf64_Shdr *section = &sections[i];

        if ((section->flags & writable_alloc) != writable_alloc || section->size == 0) {
            continue;
        }

        if (section->addr < lo) {
            lo = section->addr;
        }
        if (section->addr + section->size > hi) {
            hi = section->addr + section->size;
        }
        found = true;
    }

    if (!found) {
        return RISCV_OK;
    }

    uint64_t region_size = hi - lo;

    uint8_t *region = calloc(1, region_size);
    if (!region) {
        return RISCV_JIT_ERR_OOM;
    }

    for (Elf64_Half i = 0; i < ehdr->shnum; i++) {
        Elf64_Shdr *section = &sections[i];

        if ((section->flags & writable_alloc) != writable_alloc || section->size == 0) {
            continue;
        }

        if (section->type == SHT_NOBITS) {
            continue;
        }

        if (!elf_section_fits(elf->size, section)) {
            free(region);
            return RISCV_ELF_ERR_OOB;
        }

        memcpy(region + (section->addr - lo), elf_section_data(data, section), section->size);
    }

    elf->data_mem  = region;
    elf->data_addr = lo;
    elf->data_size = region_size;

    return RISCV_OK;
}

riscv_result riscv_elf_init_from_mem(riscv_elf *elf, const uint8_t *data, size_t size) {
    memset(elf, 0, sizeof(*elf));

    elf->owned = false;
    elf->data  = (uint8_t *)data;
    elf->size  = size;

    if (size < sizeof(Elf64_Ehdr)) {
        return RISCV_ELF_ERR_TOO_SMALL;
    }

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)data;

    if (memcmp(ehdr->ident, ELFMAG, SELFMAG) != 0) {
        return RISCV_ELF_ERR_BAD_MAGIC;
    }

    if (ehdr->ident[EI_CLASS] != ELFCLASS64) {
        return RISCV_ELF_ERR_NOT_64BIT;
    }

    if (ehdr->machine != EM_RISCV) {
        return RISCV_ELF_ERR_NOT_RISCV;
    }

    uint64_t section_table_size = (uint64_t)ehdr->shentsize * ehdr->shnum;
    if (!elf_range_fits(size, ehdr->shoff, section_table_size)) {
        return RISCV_ELF_ERR_OOB;
    }

    riscv_result result = elf_find_sections(elf, ehdr);
    if (result != RISCV_OK) {
        return result;
    }

    return elf_build_data_region(elf, ehdr);
}

riscv_result riscv_elf_init_from_file(riscv_elf *elf, const char *file) {
    size_t size = 0;
    void  *data = riscv_mem_load_file(file, &size);

    if (!data) {
        return RISCV_ELF_ERR_IO;
    }

    riscv_result result = riscv_elf_init_from_mem(elf, (const uint8_t *)data, size);
    if (result != RISCV_OK) {
        free(data);
        return result;
    }

    elf->owned = true;
    return RISCV_OK;
}

void riscv_elf_destroy(riscv_elf *elf) {
    if (elf->owned && elf->data) {
        free(elf->data);
    }
    free(elf->data_mem);
    memset(elf, 0, sizeof(*elf));
}

uint64_t riscv_elf_find_sym(const riscv_elf *elf, const char *name) {
    if (!elf->symtab || !elf->strtab) {
        return 0;
    }

    Elf64_Sym *syms  = (Elf64_Sym *)elf->symtab;
    size_t     count = elf->symtab_size / sizeof(Elf64_Sym);

    for (size_t i = 0; i < count; i++) {
        if (strcmp(elf->strtab + syms[i].name, name) == 0) {
            return syms[i].value;
        }
    }

    return 0;
}

const char *riscv_elf_lookup_func_by_pc(const riscv_elf *elf, uint64_t pc, uint64_t *out_offset) {
    if (!elf->symtab || !elf->strtab) {
        return ELF_UNKNOWN_FUNCTION;
    }

    Elf64_Sym *syms  = (Elf64_Sym *)elf->symtab;
    size_t     count = elf->symtab_size / sizeof(Elf64_Sym);

    for (size_t i = 0; i < count; i++) {
        if (ELF64_ST_TYPE(syms[i].info) != STT_FUNC) {
            continue;
        }

        if (pc < syms[i].value || pc >= syms[i].value + syms[i].size) {
            continue;
        }

        if (out_offset) {
            *out_offset = pc - syms[i].value;
        }

        return elf->strtab + syms[i].name;
    }

    return ELF_UNKNOWN_FUNCTION;
}

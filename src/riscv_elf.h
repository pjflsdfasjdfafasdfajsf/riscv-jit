// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

#ifndef RISCV_ELF_H
#define RISCV_ELF_H

#include "riscv_result.h"
#include "riscv_types.h"

typedef struct {
    // mmap'd ELF file data
    uint8_t *data;
    // total mapped size
    size_t size;
    // whether data above is owned, if this was initialized with
    // `riscv_elf_init_from_mem` then this is `false`, otherwise - true
    bool owned;

    const uint8_t *text_data;
    uint64_t       text_addr;
    uint64_t       text_size;

    // writable data region, gathered from all SHF_ALLOC|SHF_WRITE sections.
    //
    // unlike text_data (which aliases the mapped ELF and is read-only), this
    // is a private and MUTABLE copy the guest can load from and store to
    //
    // always owned/freed by the riscv_elf!
    uint8_t *data_mem;
    uint64_t data_addr;
    uint64_t data_size;

    const uint8_t *symtab;
    size_t         symtab_size;
    const char    *strtab;

    const uint8_t *debug_line;
    size_t         debug_line_size;

    const uint8_t *debug_info;
    size_t         debug_info_size;

    const uint8_t *debug_abbrev;
    size_t         debug_abbrev_size;

    const char *debug_str;
    size_t      debug_str_size;
} riscv_elf;

riscv_result riscv_elf_init_from_mem(riscv_elf *elf, const uint8_t *data, size_t size);
riscv_result riscv_elf_init_from_file(riscv_elf *elf, const char *file);

void riscv_elf_destroy(riscv_elf *elf);

uint64_t riscv_elf_find_sym(const riscv_elf *elf, const char *sym);

const char *riscv_elf_lookup_func_by_pc(const riscv_elf *elf, uint64_t pc, uint64_t *out_offset);

// -- elf.h --

typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Xword;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Section;

#define EI_NIDENT 16
#define ELFMAG    "\177ELF"
#define SELFMAG   4

#define EI_CLASS   4
#define ELFCLASS64 2

#define EM_RISCV   243
#define SHT_SYMTAB 2
#define SHT_NOBITS 8

#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2

#define ELF64_ST_TYPE(info) ((info) & 0xF)
#define STT_FUNC            2

typedef struct {
    unsigned char ident[EI_NIDENT];
    Elf64_Half    type;
    Elf64_Half    machine;
    Elf64_Word    version;
    Elf64_Addr    entry;
    Elf64_Off     phoff;
    Elf64_Off     shoff;
    Elf64_Word    flags;
    Elf64_Half    ehsize;
    Elf64_Half    phentsize;
    Elf64_Half    phnum;
    Elf64_Half    shentsize;
    Elf64_Half    shnum;
    Elf64_Half    shstrndx;
} Elf64_Ehdr;

typedef struct {
    Elf64_Word  name;
    Elf64_Word  type;
    Elf64_Xword flags;
    Elf64_Addr  addr;
    Elf64_Off   offset;
    Elf64_Xword size;
    Elf64_Word  link;
    Elf64_Word  info;
    Elf64_Xword addralign;
    Elf64_Xword entsize;
} Elf64_Shdr;

typedef struct {
    Elf64_Word    name;
    unsigned char info;
    unsigned char other;
    Elf64_Section shndx;
    Elf64_Addr    value;
    Elf64_Xword   size;
} Elf64_Sym;

#endif // RISCV_ELF_H

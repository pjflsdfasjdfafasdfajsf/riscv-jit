// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

#ifndef RISCV_MEM_H
#define RISCV_MEM_H

#include "riscv_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>

#define RISCV_ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define RISCV_KIB(count) ((size_t)(count) * 1024u)
#define RISCV_MIB(count) (RISCV_KIB(count) * 1024u)

static inline void *riscv_mem_alloc_exec(size_t size) {
    void *p = mmap(0, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (p == MAP_FAILED) ? 0 : p;
}

static inline void riscv_mem_free_exec(void *p, size_t size) {
    if (p) {
        munmap(p, size);
    }
}

// returned buffer is `malloc`'d
static inline void *riscv_mem_load_file(const char *filename, size_t *out_size) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        return 0;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }

    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return 0;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }

    void *data = malloc((size_t)size);
    if (!data) {
        fclose(f);
        return 0;
    }

    if (fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return 0;
    }

    fclose(f);

    if (out_size) {
        *out_size = (size_t)size;
    }
    return data;
}

static inline uint16_t riscv_mem_read_u16(const void *p) {
    uint16_t val;
    memcpy(&val, p, sizeof(val));
    return val;
}

static inline uint32_t riscv_mem_read_u32(const void *p) {
    uint32_t val;
    memcpy(&val, p, sizeof(val));
    return val;
}

static inline uint64_t riscv_mem_read_u64(const void *p) {
    uint64_t val;
    memcpy(&val, p, sizeof(val));
    return val;
}

static inline void riscv_mem_write_u32(void *p, uint32_t val) {
    memcpy(p, &val, sizeof(val));
}

#endif // RISCV_MEM_H

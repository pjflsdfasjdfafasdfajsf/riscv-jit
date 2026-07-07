// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

#ifndef RISCV_JIT_H
#define RISCV_JIT_H

#include "riscv_dwarf.h"
#include "riscv_elf.h"
#include "riscv_result.h"

typedef struct riscv_jit riscv_jit;

typedef enum {
    RISCV_REG_ZERO = 0,
    RISCV_REG_RA   = 1,
    RISCV_REG_SP   = 2,
    RISCV_REG_GP   = 3,
    RISCV_REG_TP   = 4,
    RISCV_REG_T0   = 5,
    RISCV_REG_T1   = 6,
    RISCV_REG_T2   = 7,
    RISCV_REG_S0   = 8,
    RISCV_REG_FP   = 8,
    RISCV_REG_S1   = 9,
    RISCV_REG_A0   = 10,
    RISCV_REG_A1   = 11,
    RISCV_REG_A2   = 12,
    RISCV_REG_A3   = 13,
    RISCV_REG_A4   = 14,
    RISCV_REG_A5   = 15,
    RISCV_REG_A6   = 16,
    RISCV_REG_A7   = 17,
    RISCV_REG_S2   = 18,
    RISCV_REG_S3   = 19,
    RISCV_REG_S4   = 20,
    RISCV_REG_S5   = 21,
    RISCV_REG_S6   = 22,
    RISCV_REG_S7   = 23,
    RISCV_REG_S8   = 24,
    RISCV_REG_S9   = 25,
    RISCV_REG_S10  = 26,
    RISCV_REG_S11  = 27,
    RISCV_REG_T3   = 28,
    RISCV_REG_T4   = 29,
    RISCV_REG_T5   = 30,
    RISCV_REG_T6   = 31
} riscv_reg;

enum {
    RISCV_INTEGER_REGISTER_COUNT      = RISCV_REG_T6 + 1,
    RISCV_FUNCTION_ARG_REGISTER_COUNT = RISCV_REG_A7 - RISCV_REG_A0 + 1,
    RISCV_MAX_STACKTRACE_DEPTH        = 32,
    RISCV_STACK_GUARD_BYTES           = sizeof(uint64_t),
};

// -- ecall --

// invoked natively by JIT whenever an ecall instruction is executed
#define RISCV_ECALL_HANDLER(name) void name(riscv_jit *jit, void *user_data)
typedef RISCV_ECALL_HANDLER(riscv_ecall_handler);

void riscv_jit_set_ecall_handler(riscv_jit *jit, riscv_ecall_handler handler, void *user_data);

// do not use this function! use the macro below instead.
uint64_t riscv_jit_get_reg_inner(riscv_jit *jit, riscv_reg reg);
// `reg` corresponds to RISC-V registers from x0 to x31.
// usually you need the syscall number from a7 (x17) and args from a0-a5
// (x10-x15)
#define riscv_jit_get_reg(jit, reg, type) ((type)(uintptr_t)riscv_jit_get_reg_inner(jit, reg))

void riscv_jit_set_reg(riscv_jit *jit, riscv_reg reg, uint64_t val);

// -- fault handling --

typedef enum {
    RISCV_FAULT_NONE = 0,
    RISCV_FAULT_TIMEOUT,
    RISCV_FAULT_READ,
    RISCV_FAULT_WRITE,
    // the guest hit a debugger event (breakpoint / step / async-stop)
    // this is swallowed and is actually never sent to your handler!
    RISCV_FAULT_DEBUG,
} riscv_jit_fault_type;

static inline const char *riscv_jit_fault_type_str(riscv_jit_fault_type fault) {
    switch (fault) {
    case RISCV_FAULT_NONE: {
        return "no fault. did you mess up your fault handler?";
    }
    case RISCV_FAULT_TIMEOUT: {
        return "timeout";
    };
    case RISCV_FAULT_READ: {
        return "read";
    };
    case RISCV_FAULT_WRITE: {
        return "write";
    };
    case RISCV_FAULT_DEBUG: {
        return "debug";
    };
    default: {
        return "unknown";
    }
    }
}

#define RISCV_FAULT_HANDLER(name) void name(riscv_jit *jit, riscv_jit_fault_type fault, void *user_data)
typedef RISCV_FAULT_HANDLER(riscv_fault_handler);

void riscv_jit_set_fault_handler(riscv_jit *jit, riscv_fault_handler handler, void *user_data);

typedef struct {
    uint64_t         pc;
    const char      *func_name;
    uint64_t         func_offset;
    riscv_source_loc loc;
} riscv_stack_frame;

typedef struct {
    riscv_stack_frame frames[RISCV_MAX_STACKTRACE_DEPTH];
    int               depth;
} riscv_stacktrace;

void riscv_jit_get_stacktrace(const riscv_jit *jit, riscv_stacktrace *out_trace);

typedef struct {
    uint64_t x[RISCV_INTEGER_REGISTER_COUNT];
    uint64_t pc;
    // decrements over execution
    uint64_t fuel;
    // populated if a fault occurs -- see riscv_fault_type
    uint64_t fault;
    // stack
    uint8_t *stack_mem;
    uint64_t stack_size;
} riscv_cpu;

// TODO: make this hidden for users
struct riscv_jit {
    uint8_t *code_buf;
    size_t   code_size;
    size_t   code_cap;

    uint8_t *exec_mem;
    size_t   exec_size;

    riscv_elf elf;
    // (guest_pc - text_addr) -> host code offset
    uint32_t *pc_map;
    size_t    pc_map_size;

    riscv_cpu cpu;
    uint8_t  *stack_mem;
    size_t    stack_size;
    uint64_t  initial_sp;

    size_t trampoline_offset;
    // set by the codegen so the gdb stub can build trap stubs that jmp to
    // the exact same epilogue used by fuel/fault stubs
    size_t epilogue_offset;

    riscv_ecall_handler *ecall_handler;
    void                *ecall_user_data;

    riscv_fault_handler *fault_handler;
    void                *fault_user_data;

    int      *read_fault_patches;
    uint64_t *read_fault_pcs;
    int       read_fault_patch_count;

    int      *write_fault_patches;
    uint64_t *write_fault_pcs;
    int       write_fault_patch_count;

    int      *timeout_patches;
    uint64_t *timeout_pcs;
    int       timeout_patch_count;

    int *epilogue_jump_buf;
    int  epilogue_jump_buf_size;

    int      *branch_jump_buf;
    uint64_t *branch_target_buf;
    int       branch_jump_buf_size;

    // all other gdb_* fields are only valid when this is set
    bool gdb_attached;

    int gdb_listen_fd;
    int gdb_client_fd;

    uint8_t *gdb_stub_buf;
    size_t   gdb_stub_used;
    size_t   gdb_stub_cap;

    struct {
        // guest PC of the breakpoint
        uint64_t pc;
        // offset into exec_mem where we scribbled a jmp
        uint32_t host_off;
        // offset into exec_mem of the trap stub
        uint32_t stub_off;

        uint8_t saved[5];
        uint8_t used;
    } gdb_bp[64];

    uint8_t gdb_bp_count;

    // 0 = running
    // 1 = user step (report after)
    // 2 = step-over-BP (keep running after)
    uint8_t gdb_stepping;
    // QStartNoAckMode enabled
    uint8_t gdb_no_ack;
    // set be `k`/`D`
    uint8_t gdb_should_detach;
    // bp index whose 5 bytes were restored
    int8_t  gdb_temp_unpatched_bp;

    // saved caller fuel
    uint64_t gdb_saved_fuel;
};

riscv_result riscv_jit_init_from_elf_mem(riscv_jit *jit, const uint8_t *data, size_t size);
riscv_result riscv_jit_init_from_elf_file(riscv_jit *jit, const char *file);

void riscv_jit_destroy(riscv_jit *jit);

riscv_result riscv_jit_call(riscv_jit *jit, const char *func_name, uint64_t *func_out_result, size_t argc, const uint64_t *argv);

// call this before riscv_jit_call() to open a TCP listener on `port` and
// block until a GDB client connects
//
// while a client is connected, subsequent riscv_jit_call() runs under control
// of the debugger
riscv_result riscv_jit_gdb_wait_for_client(riscv_jit *jit, uint16_t port);
void         riscv_jit_gdb_close(riscv_jit *jit);

// TODO: there could be API for accepting the text section directly instead
// of elf mem/path?

#endif // RISCV_JIT_H

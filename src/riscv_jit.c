// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

#include "riscv_jit.h"
#include "riscv_elf.h"
#include "riscv_gdb.h"
#include "riscv_instr.h"
#include "riscv_ir.h"
#include "riscv_jit_cg_x86.h"
#include "riscv_mem.h"
#include "riscv_result.h"

#include <stdlib.h>
#include <string.h>

#define JIT_DEFAULT_STACK_SIZE        RISCV_MIB(1)
#define JIT_EXEC_BYTES_PER_GUEST_BYTE 128u
#define JIT_MIN_EXEC_SIZE             RISCV_KIB(64)
// this is 10 million, thank me later
#define JIT_INITIAL_FUEL                10000000u
#define RISCV_FRAME_LINK_SIZE           (2u * sizeof(uint64_t))
#define RISCV_FRAME_RETURN_ADDRESS_SLOT sizeof(uint64_t)
#define RISCV_FRAME_PREVIOUS_FP_SLOT    (2u * sizeof(uint64_t))

static bool jit_pc_is_in_text(const riscv_jit *jit, uint64_t pc) {
    return pc >= jit->elf.text_addr && pc < jit->elf.text_addr + jit->elf.text_size;
}

static size_t jit_exec_size_for_text(size_t text_size) {
    size_t exec_size = text_size * JIT_EXEC_BYTES_PER_GUEST_BYTE;
    return (exec_size < JIT_MIN_EXEC_SIZE) ? JIT_MIN_EXEC_SIZE : exec_size;
}

static uint32_t jit_code_offset_for_pc(const riscv_jit *jit, uint64_t pc) {
    if (!jit_pc_is_in_text(jit, pc)) {
        return 0;
    }
    return jit->pc_map[pc - jit->elf.text_addr];
}

static size_t jit_arg_count_to_copy(size_t argc) {
    return (argc > RISCV_FUNCTION_ARG_REGISTER_COUNT) ? RISCV_FUNCTION_ARG_REGISTER_COUNT : argc;
}

void ecall(riscv_jit *jit) {
    if (jit->ecall_handler) {
        jit->ecall_handler(jit, jit->ecall_user_data);
    }
}

void riscv_jit_set_ecall_handler(riscv_jit *jit, riscv_ecall_handler handler, void *user_data) {
    jit->ecall_handler   = handler;
    jit->ecall_user_data = user_data;
}

void riscv_jit_set_fault_handler(riscv_jit *jit, riscv_fault_handler handler, void *user_data) {
    jit->fault_handler   = handler;
    jit->fault_user_data = user_data;
}

uint64_t riscv_jit_get_reg_inner(riscv_jit *jit, riscv_reg reg) {
    return jit->cpu.x[reg];
}

void riscv_jit_set_reg(riscv_jit *jit, riscv_reg reg, uint64_t val) {
    if (reg != RISCV_REG_ZERO) {
        jit->cpu.x[reg] = val;
    }
}

void riscv_jit_get_stacktrace(const riscv_jit *jit, riscv_stacktrace *out_trace) {
    if (!out_trace) {
        return;
    }

    out_trace->depth    = 0;
    uint64_t current_pc = jit->cpu.pc;
    uint64_t fp         = jit->cpu.x[RISCV_REG_FP];

    while (out_trace->depth < RISCV_MAX_STACKTRACE_DEPTH) {
        riscv_stack_frame *frame = &out_trace->frames[out_trace->depth++];

        frame->pc        = current_pc;
        frame->func_name = riscv_elf_lookup_func_by_pc(&jit->elf, current_pc, &frame->func_offset);
        frame->loc       = riscv_source_loc_unknown();
        riscv_dwarf_lookup_pc(&jit->elf, current_pc, &frame->loc);

        if (fp < RISCV_FRAME_LINK_SIZE || fp > jit->stack_size) {
            break;
        }

        uint64_t ra   = riscv_mem_read_u64(jit->stack_mem + fp - RISCV_FRAME_RETURN_ADDRESS_SLOT);
        uint64_t next = riscv_mem_read_u64(jit->stack_mem + fp - RISCV_FRAME_PREVIOUS_FP_SLOT);

        if (ra == 0) {
            break;
        }

        current_pc = ra - RV_INSTR_BYTES;

        if (next == 0 || next <= fp || next > jit->stack_size) {
            break;
        }

        fp = next;
    }
}

riscv_result riscv_jit_init_from_elf_mem(riscv_jit *jit, const uint8_t *data, size_t size) {
    memset(jit, 0, sizeof(*jit));

    riscv_result result = riscv_elf_init_from_mem(&jit->elf, data, size);
    if (result != RISCV_OK) {
        return result;
    }

    jit->exec_size = jit_exec_size_for_text(jit->elf.text_size);
    jit->exec_mem  = riscv_mem_alloc_exec(jit->exec_size);
    if (!jit->exec_mem) {
        return RISCV_JIT_ERR_IO;
    }

    jit->stack_size = JIT_DEFAULT_STACK_SIZE;
    jit->stack_mem  = malloc(jit->stack_size + RISCV_STACK_GUARD_BYTES);
    if (!jit->stack_mem) {
        riscv_mem_free_exec(jit->exec_mem, jit->exec_size);

        return RISCV_JIT_ERR_OOM;
    }
    jit->initial_sp = jit->stack_size;

    jit->code_buf  = jit->exec_mem;
    jit->code_cap  = jit->exec_size;
    jit->code_size = 0;

    jit->pc_map_size = jit->elf.text_size * sizeof(uint32_t);
    jit->pc_map      = malloc(jit->pc_map_size);
    if (!jit->pc_map) {
        free(jit->stack_mem);
        riscv_mem_free_exec(jit->exec_mem, jit->exec_size);

        return RISCV_JIT_ERR_OOM;
    }

    ir_builder builder;
    ir_builder_init(&builder, jit->elf.text_size);

    ir_translate(&jit->elf, &builder);

    result = riscv_jit_cg_x86_compile(jit, &builder);
    ir_builder_free(&builder);

    return result;
}

riscv_result riscv_jit_init_from_elf_file(riscv_jit *jit, const char *file) {
    size_t size = 0;
    void  *data = riscv_mem_load_file(file, &size);

    if (!data) {
        return RISCV_JIT_ERR_IO;
    }

    riscv_result result = riscv_jit_init_from_elf_mem(jit, (const uint8_t *)data, size);
    if (result != RISCV_OK) {
        free(data);

        return result;
    }

    jit->elf.owned = true;
    return RISCV_OK;
}

void riscv_jit_destroy(riscv_jit *jit) {
    riscv_jit_gdb_close(jit);

    if (jit->pc_map) {
        free(jit->pc_map);
    }
    if (jit->exec_mem) {
        riscv_mem_free_exec(jit->exec_mem, jit->exec_size);
    }
    if (jit->stack_mem) {
        free(jit->stack_mem);
    }

    riscv_elf_destroy(&jit->elf);
    memset(jit, 0, sizeof(*jit));
}

riscv_result riscv_jit_call(riscv_jit *jit, const char *func_name, uint64_t *func_out_result, size_t argc, const uint64_t *argv) {
    uint64_t pc = riscv_elf_find_sym(&jit->elf, func_name);
    if (!pc || !jit_pc_is_in_text(jit, pc)) {
        return RISCV_JIT_BAD_PC;
    }

    uint32_t offset = jit_code_offset_for_pc(jit, pc);
    if (offset == 0) {
        return RISCV_JIT_BAD_PC;
    }

    memset(&jit->cpu, 0, sizeof(jit->cpu));

    jit->cpu.x[RISCV_REG_SP] = jit->initial_sp;
    jit->cpu.fuel            = JIT_INITIAL_FUEL;
    jit->cpu.fault           = RISCV_FAULT_NONE;
    jit->cpu.stack_mem       = jit->stack_mem;
    jit->cpu.stack_size      = jit->stack_size;
    jit->cpu.pc              = pc;

    for (size_t i = 0; i < jit_arg_count_to_copy(argc); i++) {
        jit->cpu.x[RISCV_REG_A0 + i] = argv[i];
    }

    void (*trampoline)(riscv_cpu *, void *) = (void (*)(riscv_cpu *, void *))(jit->exec_mem + jit->trampoline_offset);

    if (jit->gdb_attached) {
        if (!riscv_gdb_on_stop(jit, 0)) {
            return RISCV_OK;
        }
    }

    while (true) {
        bool debugging = jit->gdb_attached && (jit->gdb_stepping || jit->gdb_temp_unpatched_bp >= 0);
        if (debugging) {
            jit->gdb_saved_fuel = jit->cpu.fuel;
            jit->cpu.fuel       = 2;
        }

        trampoline(&jit->cpu, (void *)(jit->exec_mem + offset));

        if (debugging) {
            // restore the fuel we borrowed minus what we actually spent
            jit->cpu.fuel = jit->gdb_saved_fuel;
        }

        if (jit->cpu.fault == RISCV_FAULT_DEBUG) {
            jit->cpu.fault = RISCV_FAULT_NONE;
            if (!jit->gdb_attached) {
                if (jit->fault_handler) {
                    jit->fault_handler(jit, RISCV_FAULT_DEBUG, jit->fault_user_data);
                }
                return RISCV_OK;
            }

            if (!riscv_gdb_on_stop(jit, 0x05)) {
                return RISCV_OK;
            }
            riscv_gdb_temp_unpatch(jit, jit->cpu.pc);

            offset = jit_code_offset_for_pc(jit, jit->cpu.pc);
            if (offset == 0) {
                return RISCV_JIT_BAD_PC;
            }

            continue;
        }

        if (jit->cpu.fault == RISCV_FAULT_TIMEOUT && debugging) {
            jit->cpu.fault = RISCV_FAULT_NONE;

            bool was_stepping_over_bp = (jit->gdb_temp_unpatched_bp >= 0);
            bool was_user_step        = jit->gdb_stepping != 0;
            jit->gdb_stepping         = 0;

            if (was_stepping_over_bp) {
                riscv_gdb_repatch(jit, jit->gdb_bp[jit->gdb_temp_unpatched_bp].pc);
            }

            if (was_user_step) {
                if (!riscv_gdb_on_stop(jit, 0x05)) {
                    return RISCV_OK;
                }
            }

            offset = jit_code_offset_for_pc(jit, jit->cpu.pc);
            if (offset == 0) {
                return RISCV_JIT_BAD_PC;
            }

            continue;
        }

        if (jit->cpu.fault != RISCV_FAULT_NONE) {
            if (jit->gdb_attached) {
                (void)riscv_gdb_on_stop(jit, 0x0B);
                return RISCV_OK;
            }
            if (jit->fault_handler) {
                jit->fault_handler(jit, (riscv_jit_fault_type)jit->cpu.fault, jit->fault_user_data);
            }

            return RISCV_OK;
        }

        uint64_t next_pc = jit->cpu.pc;
        if (next_pc == 0) {
            if (func_out_result) {
                *func_out_result = jit->cpu.x[RISCV_REG_A0];
            }

            return RISCV_OK;
        }

        if (jit_pc_is_in_text(jit, next_pc)) {
            offset = jit_code_offset_for_pc(jit, next_pc);

            if (offset == 0) {
                return RISCV_JIT_BAD_PC;
            }
        } else {
            return RISCV_JIT_BAD_PC;
        }

        if (jit->gdb_attached && riscv_gdb_poll_async(jit)) {
            if (!riscv_gdb_on_stop(jit, 0x02)) {
                return RISCV_OK;
            }
        }

        if (jit->gdb_attached && jit->gdb_should_detach) {
            return RISCV_OK;
        }
    }

    return RISCV_OK;
}

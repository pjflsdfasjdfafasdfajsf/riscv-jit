// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

#include "riscv_jit.h"

#include <string.h>
#include <stdio.h>

// this example also shows a very beautiful fault handler that you should
// probably just copy in your project.

RISCV_FAULT_HANDLER(fault_handler) {
    riscv_stacktrace trace = {0};
    riscv_jit_get_stacktrace(jit, &trace);

    printf("%s fault:\n", riscv_jit_fault_type_str(fault));

    for (int i = 0; i < trace.depth; i++) {
        riscv_stack_frame *frame = &trace.frames[i];

        printf(" #%d  0x%016lx in %s+0x%lx at %s:%d\n", i, frame->pc, frame->func_name, frame->func_offset, frame->loc.rel_path, frame->loc.line);

        char buf[256];
        if (riscv_dwarf_get_source_line(frame->loc, buf, sizeof(buf))) {
            printf("      | %s\n", buf);
        }
    }
}

int main(int argc, char **argv) {
    char path[1024] = "guest.elf";

    if (argc > 0) {
        char *slash = strrchr(argv[0], '/');

        if (slash) {
            int len = (int)(slash - argv[0] + 1);
            snprintf(path, sizeof(path), "%.*sguest.elf", len, argv[0]);
        }
    }

    riscv_jit guest = {0};

    riscv_result result = riscv_jit_init_from_elf_file(&guest, path);
    if (result != RISCV_OK) {
        printf("%s\n", riscv_result_str(result));
        return 1;
    }

    riscv_jit_set_fault_handler(&guest, fault_handler, 0);

    result = riscv_jit_call(&guest, "main", 0, 0, 0);
    if (result == RISCV_OK) {
        printf("but the host didn't crash!\n");
    }

    riscv_jit_destroy(&guest);

    return 0;
}

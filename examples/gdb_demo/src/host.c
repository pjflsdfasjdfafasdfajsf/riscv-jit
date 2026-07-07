// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

#include "riscv_jit.h"

#include <stdio.h>
#include <string.h>

RISCV_FAULT_HANDLER(fault_handler) {
    (void)user_data;
    (void)jit;
    printf("guest fault: %s\n", riscv_jit_fault_type_str(fault));
}

int main(int argc, char **argv) {
    setvbuf(stdout, 0, _IOLBF, 0);
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
        printf("init: %s\n", riscv_result_str(result));
        return 1;
    }

    riscv_jit_set_fault_handler(&guest, fault_handler, 0);

    uint16_t port = 1234;

    result = riscv_jit_gdb_wait_for_client(&guest, port);
    if (result != RISCV_OK) {
        printf("gdb wait: %s\n", riscv_result_str(result));
        riscv_jit_destroy(&guest);

        return 1;
    }

    uint64_t call_result = 0;
    result = riscv_jit_call(&guest, "main", &call_result, 0, 0);
    if (result == RISCV_OK) {
        printf("main returned %lu\n", (unsigned long)call_result);
    } else {
        printf("call: %s\n", riscv_result_str(result));
    }

    riscv_jit_destroy(&guest);
    return 0;
}

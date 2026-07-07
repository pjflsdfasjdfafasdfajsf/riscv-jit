// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

#include "riscv_jit.h"
#include "riscv_result.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>

RISCV_ECALL_HANDLER(ecall_handler) {
    (void)user_data;

    uint64_t a7 = riscv_jit_get_reg(jit, RISCV_REG_A7, uint64_t);
    uint64_t a0 = riscv_jit_get_reg(jit, RISCV_REG_A0, uint64_t);

    if (a7 == 93) {
        exit((int)(a0 & 0xFF));
    }
}

void run_child(const char *path) {
    riscv_jit jit = {0};

    riscv_result result = riscv_jit_init_from_elf_file(&jit, path);
    if (result != RISCV_OK) {
        exit(2);
    }

    riscv_jit_set_ecall_handler(&jit, ecall_handler, 0);

    result = riscv_jit_call(&jit, "_start", 0, 0, 0);
    if (result != RISCV_OK) {
        exit(2);
    }

    riscv_jit_destroy(&jit);
    exit(0);
}

int main(int argc, char **argv) {
    if (argc >= 3 && strcmp(argv[1], "--run-child") == 0) {
        run_child(argv[2]);

        return 0;
    }

    if (argc < 2) {
        printf("usage: %s <elf1> <elf2> ...\n", argv[0]);

        return 1;
    }

    int pass  = 0;
    int total = argc - 1;

    for (int i = 1; i < argc; i++) {
        const char *path = argv[i];

        const char *name = strrchr(path, '/');
        name             = name ? name + 1 : path;

        pid_t pid = fork();
        if (pid == 0) {
            execl(argv[0], argv[0], "--run-child", path, 0);

            exit(1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);

            if (WIFEXITED(status)) {
                if (WEXITSTATUS(status) == 0) {
                    printf("%s: pass\n", name);

                    pass++;
                } else {
                    printf("%s: fail\n", name);
                }
            } else if (WIFSIGNALED(status)) {
                if (WTERMSIG(status) == SIGILL) {
                    printf("%s: SIGILL\n", name);
                } else {
                    printf("%s: %d", name, WTERMSIG(status));
                }
            }
        }
    }
}

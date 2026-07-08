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

enum {
    EXIT_TEST_PASS  = 0, // RVMODEL_HALT_PASS
    EXIT_TEST_FAIL  = 1, // RVMODEL_HALT_FAIL
    EXIT_TEST_ERROR = 2, // could not load/run the ELF
    EXIT_TEST_FAULT = 3, // the guest took a memory/timeout fault mid-test
};

// reference model dumps the signature region as one hex value
// per line, so to turn that into the self-checking ELF we must preload the same
// values back into the signature region, which means emitting them as
// assembler data directives that the test's `#include SIGNATURE_FILE` pulls in

#define XLEN_BYTES        8
#define TRAP_CANARY_XLEN64 "d3a91f6c8b47e25d"

static int convert_signature(const char *in_path, const char *out_path) {
    FILE *in = fopen(in_path, "r");
    if (!in) {
        fprintf(stderr, "convert-sig: cannot open %s\n", in_path);
        return 1;
    }

    FILE *out = fopen(out_path, "w");
    if (!out) {
        fprintf(stderr, "convert-sig: cannot open %s\n", out_path);
        fclose(in);
        return 1;
    }

    char line[256];
    while (fgets(line, sizeof(line), in)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' ')) {
            line[--len] = '\0';
        }
        if (len == 0) {
            continue;
        }

        fprintf(out, ".quad 0x%s\n", line);

        if (strstr(line, TRAP_CANARY_XLEN64)) {
            fprintf(out, "mtrap_sigptr:\n");
        }
    }

    fclose(in);
    fclose(out);
    return 0;
}

RISCV_ECALL_HANDLER(ecall_handler) {
    (void)user_data;

    uint64_t a7 = riscv_jit_get_reg(jit, RISCV_REG_A7, uint64_t);
    uint64_t a0 = riscv_jit_get_reg(jit, RISCV_REG_A0, uint64_t);

    // li a7, 93
    if (a7 == 93) {
        exit((int)(a0 & 0xFF));
    }
}

RISCV_FAULT_HANDLER(fault_handler) {
    (void)jit;
    (void)user_data;
    // a self-checking test, obviously, should never fault
    (void)fault;
    exit(EXIT_TEST_FAULT);
}

void run_child(const char *path) {
    riscv_jit jit = {0};

    riscv_result result = riscv_jit_init_from_elf_file(&jit, path);
    if (result != RISCV_OK) {
        exit(EXIT_TEST_ERROR);
    }

    riscv_jit_set_ecall_handler(&jit, ecall_handler, 0);
    riscv_jit_set_fault_handler(&jit, fault_handler, 0);

    result = riscv_jit_call(&jit, "_start", 0, 0, 0);
    if (result != RISCV_OK) {
        exit(EXIT_TEST_ERROR);
    }

    // reaching here means the guest returned without ever calling
    // RVMODEL_HALT_PASS/FAIL
    riscv_jit_destroy(&jit);
    exit(EXIT_TEST_ERROR);
}

static int run_one(const char *self, const char *path) {
    pid_t pid = fork();
    if (pid == 0) {
        execl(self, self, "--run-child", path, (char *)0);
        _exit(EXIT_TEST_ERROR);
    }
    if (pid < 0) {
        return EXIT_TEST_ERROR;
    }

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return EXIT_TEST_ERROR;
}

static const char *failure_reason(int code) {
    switch (code) {
    case EXIT_TEST_FAIL:  return "signature mismatch";
    case EXIT_TEST_FAULT: return "fault";
    case EXIT_TEST_ERROR: return "error";
    case 128 + SIGILL:    return "illegal instruction";
    default:              return "crash";
    }
}

int main(int argc, char **argv) {
    if (argc >= 4 && strcmp(argv[1], "--convert-sig") == 0) {
        return convert_signature(argv[2], argv[3]);
    }

    if (argc >= 3 && strcmp(argv[1], "--run-child") == 0) {
        run_child(argv[2]);

        return 0;
    }

    if (argc < 2) {
        printf("usage: %s <elf1> <elf2> ...\n", argv[0]);
        printf("       %s --convert-sig <in.sig> <out.results>\n", argv[0]);

        return 1;
    }

    const int tty = isatty(STDOUT_FILENO);

    int fail = 0;

    for (int i = 1; i < argc; i++) {
        const char *path = argv[i];

        const char *name = strrchr(path, '/');
        name             = name ? name + 1 : path;

        if (tty) {
            printf("\r\x1b[K%s", name);
            fflush(stdout);
        }

        int code = run_one(argv[0], path);

        if (code != EXIT_TEST_PASS) {
            fail++;
            if (tty) {
                printf("\r\x1b[K");
            }
            printf("%s\n%s\n", name, failure_reason(code));
            fflush(stdout);
        }
    }

    if (tty) {
        printf("\r\x1b[K");
        fflush(stdout);
    }

    return (fail == 0) ? 0 : 1;
}

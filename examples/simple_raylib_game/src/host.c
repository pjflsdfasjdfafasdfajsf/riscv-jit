// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

#include "riscv_jit.h"
#include "shared.h"

#include <raylib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// TODO: fix the 'unknwon instructions' at start

RISCV_ECALL_HANDLER(ecall_handler) {
    int syscall = riscv_jit_get_reg(jit, RISCV_REG_A7, int);

    switch (syscall) {
    case SYS_BEGIN_DRAWING: {
        BeginDrawing();
    } break;

    case SYS_CLEAR_BACKGROUND: {
        unsigned int c     = riscv_jit_get_reg(jit, RISCV_REG_A0, unsigned int);
        Color        color = {(c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF};
        ClearBackground(color);
    } break;

    case SYS_DRAW_RECTANGLE: {
        int          x = riscv_jit_get_reg(jit, RISCV_REG_A0, int);
        int          y = riscv_jit_get_reg(jit, RISCV_REG_A1, int);
        int          w = riscv_jit_get_reg(jit, RISCV_REG_A2, int);
        int          h = riscv_jit_get_reg(jit, RISCV_REG_A3, int);
        unsigned int c = riscv_jit_get_reg(jit, RISCV_REG_A4, unsigned int);

        Color color = {(c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF};
        DrawRectangle(x, y, w, h, color);
        break;
    } break;

    case SYS_END_DRAWING: {
        EndDrawing();
    } break;

    case SYS_IS_KEY_DOWN: {
        int key = riscv_jit_get_reg(jit, RISCV_REG_A0, int);
        riscv_jit_set_reg(jit, RISCV_REG_A0, IsKeyDown(key));
        break;
    } break;
    }
}

int main(int argc, char **argv) {
    InitWindow(1280, 720, "Simple Game");
    SetTargetFPS(60);

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

    riscv_jit_set_ecall_handler(&guest, ecall_handler, 0);

    state *s = (state *)(guest.stack_mem + 0x1000);

    s->x = 1280 / 2;
    s->y = 720 / 2;

    while (!WindowShouldClose()) {
        uint64_t argv[1] = {0x1000};

        result = riscv_jit_call(&guest, UPDATE_FUNCTION_NAME, 0, 1, argv);
        if (result != RISCV_OK) {
            printf("%s\n", riscv_result_str(result));

            break;
        }
    }

    CloseWindow();

    riscv_jit_destroy(&guest);

    return 0;
}

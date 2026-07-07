// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

#include "shared.h"

// unfortunately we can't have the raylib header here
enum key {
    KEY_W = 87,
    KEY_A = 65,
    KEY_S = 83,
    KEY_D = 68,
};

// so we can call host functions we need to do 'syscalls', the host then
// 'catches' them and executes the corresponding logic, let's create a
// simple wrapper so we can easily implement all other functions we need
//
// one function though unfortunately would not be enough! we need to define
// multiple of them since different functions take different amount of
// arguments.
//
// ideally you should put this in a header file (SDK.h)

static inline long ecall0(long id) {
    // the system call number goes into a7
    register long a7 __asm__("a7") = id;
    // and arguments go in a0-a5
    register long a0 __asm__("a0");
    // keep in mind -- more than 8 arguments is not currently supported!
    __asm__ volatile("ecall" : "=r"(a0) : "r"(a7) : "memory");
    return a0;
}

static inline long ecall1(long id, long arg0) {
    register long a7 __asm__("a7") = id;
    register long a0 __asm__("a0") = arg0;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
    return a0;
}

static inline long ecall2(long id, long arg0, long arg1) {
    register long a7 __asm__("a7") = id;
    register long a0 __asm__("a0") = arg0;
    register long a1 __asm__("a1") = arg1;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7), "r"(a1) : "memory");
    return a0;
}

static inline long ecall3(long id, long arg0, long arg1, long arg2) {
    register long a7 __asm__("a7") = id;
    register long a0 __asm__("a0") = arg0;
    register long a1 __asm__("a1") = arg1;
    register long a2 __asm__("a2") = arg2;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7), "r"(a1), "r"(a2) : "memory");
    return a0;
}

static inline long ecall4(long id, long arg0, long arg1, long arg2, long arg3) {
    register long a7 __asm__("a7") = id;
    register long a0 __asm__("a0") = arg0;
    register long a1 __asm__("a1") = arg1;
    register long a2 __asm__("a2") = arg2;
    register long a3 __asm__("a3") = arg3;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7), "r"(a1), "r"(a2), "r"(a3) : "memory");
    return a0;
}

static inline long ecall5(long id, long arg0, long arg1, long arg2, long arg3, long arg4) {
    register long a7 __asm__("a7") = id;
    register long a0 __asm__("a0") = arg0;
    register long a1 __asm__("a1") = arg1;
    register long a2 __asm__("a2") = arg2;
    register long a3 __asm__("a3") = arg3;
    register long a4 __asm__("a4") = arg4;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7), "r"(a1), "r"(a2), "r"(a3), "r"(a4) : "memory");
    return a0;
}

// all logic below should be self-explanatory

// ---

static inline void begin_drawing(void) {
    ecall0(SYS_BEGIN_DRAWING);
}

static inline void clear_background(unsigned int color) {
    ecall1(SYS_CLEAR_BACKGROUND, color);
}

static inline void draw_rectangle(int x, int y, int w, int h, unsigned int color) {
    ecall5(SYS_DRAW_RECTANGLE, x, y, w, h, color);
}

static inline void end_drawing(void) {
    ecall0(SYS_END_DRAWING);
}

static inline int is_key_down(int key) {
    return (int)ecall1(SYS_IS_KEY_DOWN, key);
}

// ---

UPDATE {
    int speed = 5;

    if (is_key_down(KEY_W)) {
        state->y -= speed;
    }
    if (is_key_down(KEY_S)) {
        state->y += speed;
    }
    if (is_key_down(KEY_A)) {
        state->x -= speed;
    }
    if (is_key_down(KEY_D)) {
        state->x += speed;
    }

    begin_drawing();
    // R: 245, G: 245, B: 245, A: 245
    clear_background(0xF5F5F5FF);
    // R: 230, G: 41, B: 55, A: 255
    draw_rectangle(state->x, state->y, 50, 50, 0xE62937FF);
    end_drawing();
}

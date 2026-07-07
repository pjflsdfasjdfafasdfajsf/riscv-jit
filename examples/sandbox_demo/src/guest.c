// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

int main(void) {
    volatile int *invalid = (volatile int *)0x0;
    *invalid              = 0x123;
}

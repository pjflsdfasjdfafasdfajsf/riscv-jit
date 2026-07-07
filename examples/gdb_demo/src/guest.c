// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

int add(int a, int b) {
    return a + b;
}

int main(void) {
    int total = 0;

    for (int i = 0; i < 10; i++) {
        total = add(total, i);
    }

    return total;
}

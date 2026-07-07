// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

#ifndef RISCV_GDB_H
#define RISCV_GDB_H

#include "riscv_jit.h"
#include "riscv_types.h"

#define RISCV_GDB_SIGINT  0x02
#define RISCV_GDB_SIGTRAP 0x05
#define RISCV_GDB_SIGSEGV 0x0B

bool riscv_gdb_on_stop(riscv_jit *jit, int signal);
bool riscv_gdb_poll_async(riscv_jit *jit);
// re-arm all software breakpoints after a resume that unpatched them (used
// after single-stepping over a hit BP)
//
// idempotent
void riscv_gdb_rearm_breakpoints(riscv_jit *jit);
// unpatch a BP at `guest_pc` if one is set there
bool riscv_gdb_temp_unpatch(riscv_jit *jit, uint64_t guest_pc);
void riscv_gdb_repatch(riscv_jit *jit, uint64_t guest_pc);

#endif // RISCV_GDB_H

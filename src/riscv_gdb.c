// ----------------------------------------------------------------------------
// RISCV-JIT  by  pjflsdfasjdfafasdfajsf
// Released for public domain
//
// 2026
// ----------------------------------------------------------------------------

//
// see: https://sourceware.org/gdb/onlinedocs/gdb/Remote-Protocol.html
//

#include "riscv_gdb.h"
#include "riscv_jit.h"

#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>

#if !defined(__linux__) || !defined(__x86_64__)
#error "riscv_gdb.c currently only supports linux/x86_64"
#endif

// -- internal --

static int gdb_hex_from_nibble(uint8_t n) {
    return (n < 10) ? ('0' + n) : ('a' + n - 10);
}

static int gdb_nibble_from_hex(uint8_t c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// write a byte as two hex chars
static void gdb_hex_byte(char *out, uint8_t b) {
    out[0] = (char)gdb_hex_from_nibble(b >> 4);
    out[1] = (char)gdb_hex_from_nibble(b & 0xF);
}

// read two hex chars into a byte
static int gdb_read_hex_byte(const char *in) {
    int hi = gdb_nibble_from_hex((uint8_t)in[0]);
    int lo = gdb_nibble_from_hex((uint8_t)in[1]);

    if (hi < 0 || lo < 0) {
        return -1;
    }

    return (hi << 4) | lo;
}

// write a u64 little-endian as 16 hex chars
static void gdb_hex_u64_le(char *out, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        gdb_hex_byte(out + i * 2, (uint8_t)(v >> (i * 8)));
    }
}

// read u64 little-endian from 16 hex chars
static bool gdb_parse_hex_u64_le(const char *in, uint64_t *out) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        int b = gdb_read_hex_byte(in + i * 2);
        if (b < 0) {
            return false;
        }

        v |= (uint64_t)b << (i * 8);
    }

    *out = v;
    return true;
}

// parse a hex number of arbitrary length (up to `*consumed` bytes)
static bool gdb_parse_hex(const char *s, size_t max_len, uint64_t *out, size_t *consumed) {
    uint64_t v = 0;
    size_t   i = 0;

    while (i < max_len) {
        int n = gdb_nibble_from_hex((uint8_t)s[i]);
        if (n < 0) {
            break;
        }

        v = (v << 4) | (uint64_t)n;
        i++;
    }
    if (i == 0) {
        return false;
    }

    *out      = v;
    *consumed = i;
    return true;
}

static bool gdb_str_eq(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

static bool gdb_starts_with(const char *buf, size_t buf_len, const char *prefix) {
    size_t p_len = strlen(prefix);

    if (buf_len < p_len) {
        return false;
    }

    return gdb_str_eq(buf, prefix, p_len);
}

enum {
    GDB_PACKET_CAP   = 16384,
    GDB_MAX_BREAKPTS = 64,
    GDB_STUB_BYTES   = 39,
    GDB_STUB_SLOT    = 40,
    GDB_STUB_RESERVE = GDB_STUB_SLOT * GDB_MAX_BREAKPTS, // 2560 bytes
};

typedef struct {
    char   buf[GDB_PACKET_CAP];
    size_t len;
} gdb_packet;

static int gdb_read_byte(int fd, char *out) {
    while (true) {
        ssize_t r = read(fd, out, 1);

        if (r == 1) {
            return 1;
        }

        if (r == 0) {
            return 0;
        }

        return 0;
    }
}

static bool gdb_write_all(int fd, const char *buf, size_t len) {
    size_t written = 0;

    while (written < len) {
        ssize_t r = write(fd, buf + written, len - written);

        if (r <= 0) {
            return false;
        }

        written += (size_t)r;
    }

    return true;
}

static int gdb_bytes_available(int fd) {
    struct pollfd pfd;
    pfd.fd      = fd;
    pfd.events  = POLLIN;
    pfd.revents = 0;

    return poll(&pfd, 1, 0);
}

// read one $...#xx packet from the client
static bool gdb_read_packet(riscv_jit *jit, gdb_packet *pkt) {
    int fd = jit->gdb_client_fd;

    while (true) {
        char c;
        if (!gdb_read_byte(fd, &c)) {
            return false;
        }

        if (c == '\x03') {
            // Ctrl-C outside a packet
            pkt->buf[0] = '\x03';
            pkt->len    = 1;
            return true;
        }
        if (c == '+' || c == '-') {
            continue;
        }

        if (c == '$') {
            break;
        }
        // garbage
    }

    uint8_t cksum    = 0;
    size_t  n        = 0;
    bool    overflow = false;
    while (true) {
        char c;
        if (!gdb_read_byte(fd, &c)) {
            return false;
        }

        if (c == '#') {
            break;
        }
        cksum = (uint8_t)(cksum + (uint8_t)c);
        if (n < GDB_PACKET_CAP - 1) {
            pkt->buf[n++] = c;
        } else {
            overflow = true;
        }
    }
    pkt->buf[n] = 0;
    pkt->len    = n;

    char cksum_hex[2];

    if (!gdb_read_byte(fd, &cksum_hex[0])) {
        return false;
    }
    if (!gdb_read_byte(fd, &cksum_hex[1])) {
        return false;
    }

    if (!jit->gdb_no_ack) {
        int  expected = gdb_read_hex_byte(cksum_hex);
        bool cksum_ok = !overflow && expected >= 0 && (uint8_t)expected == cksum;
        char ack      = cksum_ok ? '+' : '-';

        if (!gdb_write_all(fd, &ack, 1)) {
            return false;
        }

        if (!cksum_ok) {
            return gdb_read_packet(jit, pkt);
        }
    }

    if (overflow) {
        return false;
    }
    return true;
}

// send $<payload>#<cksum>
static bool gdb_send_packet(riscv_jit *jit, const char *payload, size_t payload_len) {
    int fd = jit->gdb_client_fd;

    char header = '$';
    if (!gdb_write_all(fd, &header, 1)) {
        return false;
    }

    if (payload_len && !gdb_write_all(fd, payload, payload_len)) {
        return false;
    }

    uint8_t cksum = 0;
    for (size_t i = 0; i < payload_len; i++) {
        cksum = (uint8_t)(cksum + (uint8_t)payload[i]);
    }

    char tail[3];
    tail[0] = '#';

    gdb_hex_byte(tail + 1, cksum);
    if (!gdb_write_all(fd, tail, 3)) {
        return false;
    }

    if (!jit->gdb_no_ack) {
        char ack;
        while (true) {
            if (!gdb_read_byte(fd, &ack)) {
                return false;
            }

            if (ack == '+') {
                return true;
            }

            if (ack == '-') {
                // resend
                if (!gdb_write_all(fd, &header, 1)) {
                    return false;
                }

                if (payload_len && !gdb_write_all(fd, payload, payload_len)) {
                    return false;
                }

                if (!gdb_write_all(fd, tail, 3)) {
                    return false;
                }
            }
            // stray bytes
        }
    }
    return true;
}

static bool gdb_send_cstr(riscv_jit *jit, const char *s) {
    return gdb_send_packet(jit, s, strlen(s));
}

static bool gdb_send_empty(riscv_jit *jit) {
    return gdb_send_packet(jit, "", 0);
}

static bool gdb_send_ok(riscv_jit *jit) {
    return gdb_send_cstr(jit, "OK");
}

static bool gdb_send_err(riscv_jit *jit, uint8_t code) {
    char buf[3];
    buf[0] = 'E';
    gdb_hex_byte(buf + 1, code);
    return gdb_send_packet(jit, buf, 3);
}

static bool gdb_send_signal(riscv_jit *jit, int signal) {
    char buf[3];
    buf[0] = 'S';
    gdb_hex_byte(buf + 1, (uint8_t)signal);
    return gdb_send_packet(jit, buf, 3);
}

// resolve a guest [addr, addr+len) into a host pointer and writable flag
static bool gdb_translate_guest(riscv_jit *jit, uint64_t addr, uint64_t len, uint8_t **out_ptr, bool *out_writable) {
    if (len == 0) {
        *out_ptr      = 0;
        *out_writable = false;
        return true;
    }

    uint64_t text_start = jit->elf.text_addr;
    uint64_t text_end   = text_start + jit->elf.text_size;
    if (addr >= text_start && addr < text_end) {
        if (len > text_end - addr) {
            return false;
        }

        *out_ptr      = (uint8_t *)jit->elf.text_data + (addr - text_start);
        *out_writable = false;
        return true;
    }

    if (addr < jit->stack_size) {
        if (len > jit->stack_size - addr) {
            return false;
        }

        *out_ptr      = jit->stack_mem + addr;
        *out_writable = true;
        return true;
    }

    return false;
}

// returns the exec_mem offset of the guest instruction's host code
static uint32_t gdb_host_offset_for_pc(const riscv_jit *jit, uint64_t pc) {
    if (pc < jit->elf.text_addr) {
        return 0;
    }

    uint64_t rel = pc - jit->elf.text_addr;
    if (rel >= jit->elf.text_size) {
        return 0;
    }

    return jit->pc_map[rel];
}

//   movabs rax, imm64=<pc>            ; 48 B8 <8 bytes>          10
//   mov    [r15 + off_pc], rax        ; 49 89 87 <disp32>         7
//   movabs rax, imm64=<DEBUG>         ; 48 B8 <8 bytes>          10
//   mov    [r15 + off_fault], rax     ; 49 89 87 <disp32>         7
//   jmp    rel32 <epilogue>           ; E9 <4 bytes>              5
static bool gdb_ensure_stub_buf(riscv_jit *jit) {
    if (jit->gdb_stub_buf) {
        return true;
    }

    if (jit->exec_size < jit->code_size + GDB_STUB_RESERVE) {
        return false;
    }

    jit->gdb_stub_cap  = GDB_STUB_RESERVE;
    jit->gdb_stub_buf  = jit->exec_mem + jit->exec_size - jit->gdb_stub_cap;
    jit->gdb_stub_used = 0;
    return true;
}

static void gdb_write_stub_bytes(uint8_t *stub, uint64_t guest_pc, uint32_t epilogue_rel_from_end) {
    size_t i = 0;

    // movabs rax, guest_pc          ; 48 B8 <imm64>
    stub[i++] = 0x48;
    stub[i++] = 0xB8;
    for (int j = 0; j < 8; j++) {
        stub[i++] = (uint8_t)(guest_pc >> (j * 8));
    }

    // mov [r15 + off_pc], rax       ; 49 89 87 <disp32>
    uint32_t pc_disp = (uint32_t)(int32_t)offsetof(riscv_cpu, pc);
    stub[i++]        = 0x49;
    stub[i++]        = 0x89;
    stub[i++]        = 0x87;
    for (int j = 0; j < 4; j++) {
        stub[i++] = (uint8_t)(pc_disp >> (j * 8));
    }

    // movabs rax, RISCV_FAULT_DEBUG  ; 48 B8 <imm64>
    stub[i++]      = 0x48;
    stub[i++]      = 0xB8;
    uint64_t fault = (uint64_t)RISCV_FAULT_DEBUG;
    for (int j = 0; j < 8; j++) {
        stub[i++] = (uint8_t)(fault >> (j * 8));
    }

    // mov [r15 + off_fault], rax    ; 49 89 87 <disp32>
    uint32_t fault_disp = (uint32_t)(int32_t)offsetof(riscv_cpu, fault);
    stub[i++]           = 0x49;
    stub[i++]           = 0x89;
    stub[i++]           = 0x87;
    for (int j = 0; j < 4; j++) {
        stub[i++] = (uint8_t)(fault_disp >> (j * 8));
    }

    // jmp rel32 <epilogue>          ; E9 <rel32>
    stub[i++] = 0xE9;
    for (int j = 0; j < 4; j++) {
        stub[i++] = (uint8_t)(epilogue_rel_from_end >> (j * 8));
    }
}

// idempotent
static bool gdb_bp_install(riscv_jit *jit, uint64_t pc) {
    for (int i = 0; i < jit->gdb_bp_count; i++) {
        if (jit->gdb_bp[i].used && jit->gdb_bp[i].pc == pc) {
            return true;
        }
    }

    if (jit->gdb_bp_count >= GDB_MAX_BREAKPTS) {
        return false;
    }

    uint32_t host_offset = gdb_host_offset_for_pc(jit, pc);
    if (host_offset == 0) {
        return false;
    }

    if (!gdb_ensure_stub_buf(jit)) {
        return false;
    }

    if (jit->gdb_stub_used + GDB_STUB_SLOT > jit->gdb_stub_cap) {
        return false;
    }

    uint8_t *jmp_site = jit->exec_mem + host_offset;
    uint8_t *stub     = jit->gdb_stub_buf + jit->gdb_stub_used;

    int64_t end_of_stub_jmp = (int64_t)(uintptr_t)(stub + GDB_STUB_BYTES);
    int64_t epilogue_abs    = (int64_t)(uintptr_t)(jit->exec_mem + jit->epilogue_offset);
    int64_t rel             = epilogue_abs - end_of_stub_jmp;
    if (rel < -(int64_t)0x7FFFFFFF || rel > (int64_t)0x7FFFFFFF) {
        return false;
    }

    gdb_write_stub_bytes(stub, pc, (uint32_t)(int32_t)rel);

    int i                   = jit->gdb_bp_count++;
    jit->gdb_bp[i].pc       = pc;
    jit->gdb_bp[i].host_off = host_offset;
    jit->gdb_bp[i].stub_off = (uint32_t)jit->gdb_stub_used;
    jit->gdb_bp[i].used     = 1;
    memcpy(jit->gdb_bp[i].saved, jmp_site, 5);
    jit->gdb_stub_used += GDB_STUB_SLOT;

    int64_t end_of_jmp = (int64_t)(uintptr_t)(jmp_site + 5);
    int64_t stub_abs   = (int64_t)(uintptr_t)stub;
    int64_t site_rel   = stub_abs - end_of_jmp;
    if (site_rel < -(int64_t)0x7FFFFFFF || site_rel > (int64_t)0x7FFFFFFF) {
        // very unlikely
        jit->gdb_bp_count--;
        return false;
    }

    jmp_site[0] = 0xE9;
    for (int j = 0; j < 4; j++) {
        jmp_site[1 + j] = (uint8_t)((uint32_t)(int32_t)site_rel >> (j * 8));
    }

    return true;
}

static int gdb_bp_find(const riscv_jit *jit, uint64_t pc) {
    for (int i = 0; i < jit->gdb_bp_count; i++) {
        if (jit->gdb_bp[i].used && jit->gdb_bp[i].pc == pc) {
            return i;
        }
    }

    return -1;
}

// restore the original bytes at a BP site
static void gdb_bp_unpatch(riscv_jit *jit, int idx) {
    if (idx < 0 || idx >= jit->gdb_bp_count || !jit->gdb_bp[idx].used) {
        return;
    }

    uint8_t *jmp_site = jit->exec_mem + jit->gdb_bp[idx].host_off;
    memcpy(jmp_site, jit->gdb_bp[idx].saved, 5);
}

// re-scribble the `jmp rel32 <stub>` bytes
static void gdb_bp_repatch(riscv_jit *jit, int idx) {
    if (idx < 0 || idx >= jit->gdb_bp_count || !jit->gdb_bp[idx].used) {
        return;
    }

    uint8_t *jmp_site   = jit->exec_mem + jit->gdb_bp[idx].host_off;
    uint8_t *stub       = jit->gdb_stub_buf + jit->gdb_bp[idx].stub_off;
    int64_t  end_of_jmp = (int64_t)(uintptr_t)(jmp_site + 5);
    int64_t  stub_abs   = (int64_t)(uintptr_t)stub;
    int32_t  site_rel   = (int32_t)(stub_abs - end_of_jmp);

    jmp_site[0] = 0xE9;

    for (int j = 0; j < 4; j++) {
        jmp_site[1 + j] = (uint8_t)((uint32_t)site_rel >> (j * 8));
    }
}

static void gdb_bp_remove(riscv_jit *jit, uint64_t pc) {
    int i = gdb_bp_find(jit, pc);
    if (i < 0) {
        return;
    }

    gdb_bp_unpatch(jit, i);
    jit->gdb_bp[i].used = 0;

    while (jit->gdb_bp_count > 0 && !jit->gdb_bp[jit->gdb_bp_count - 1].used) {
        jit->gdb_bp_count--;
    }
}

bool riscv_gdb_temp_unpatch(riscv_jit *jit, uint64_t guest_pc) {
    int i = gdb_bp_find(jit, guest_pc);
    if (i < 0) {
        return false;
    }

    gdb_bp_unpatch(jit, i);
    jit->gdb_temp_unpatched_bp = (int8_t)i;

    return true;
}

void riscv_gdb_repatch(riscv_jit *jit, uint64_t guest_pc) {
    int i = gdb_bp_find(jit, guest_pc);
    if (i < 0) {
        return;
    }

    gdb_bp_repatch(jit, i);

    jit->gdb_temp_unpatched_bp = -1;
}

void riscv_gdb_rearm_breakpoints(riscv_jit *jit) {
    for (int i = 0; i < jit->gdb_bp_count; i++) {
        if (jit->gdb_bp[i].used) {
            gdb_bp_repatch(jit, i);
        }
    }
}

enum {
    GDB_REG_COUNT   = 33,
    GDB_REG_HEX_LEN = GDB_REG_COUNT * 16,
};

static void gdb_build_all_regs(const riscv_jit *jit, char *out) {
    for (int i = 0; i < 32; i++) {
        gdb_hex_u64_le(out + i * 16, jit->cpu.x[i]);
    }
    gdb_hex_u64_le(out + 32 * 16, jit->cpu.pc);
}

static bool gdb_apply_all_regs(riscv_jit *jit, const char *in, size_t len) {
    if (len < GDB_REG_HEX_LEN) {
        return false;
    }

    for (int i = 0; i < 32; i++) {
        uint64_t v;
        if (!gdb_parse_hex_u64_le(in + i * 16, &v)) {
            return false;
        }

        if (i != 0) {
            jit->cpu.x[i] = v; // x0 stays zero
        }
    }

    uint64_t pc;
    if (!gdb_parse_hex_u64_le(in + 32 * 16, &pc)) {
        return false;
    }

    jit->cpu.pc = pc;
    return true;
}

typedef enum {
    // expect next packer
    GDB_CMD_STAY,
    // resume execution
    GDB_CMD_CONTINUE,
    // single step then re-enter
    GDB_CMD_STEP,
    // exit
    GDB_CMD_DETACH,
} gdb_cmd_result;

static gdb_cmd_result gdb_handle_packet(riscv_jit *jit, gdb_packet *pkt) {
    if (pkt->len == 0) {
        gdb_send_empty(jit);
        return GDB_CMD_STAY;
    }

    if (pkt->len == 1 && pkt->buf[0] == '\x03') {
        gdb_send_signal(jit, RISCV_GDB_SIGINT);

        return GDB_CMD_STAY;
    }

    char        cmd  = pkt->buf[0];
    const char *args = pkt->buf + 1;
    size_t      alen = pkt->len - 1;

    switch (cmd) {
    case '?': {
        gdb_send_signal(jit, RISCV_GDB_SIGTRAP);
        return GDB_CMD_STAY;
    }

    case 'g': {
        char reply[GDB_REG_HEX_LEN + 1];
        gdb_build_all_regs(jit, reply);
        gdb_send_packet(jit, reply, GDB_REG_HEX_LEN);

        return GDB_CMD_STAY;
    }

    case 'G': {
        if (!gdb_apply_all_regs(jit, args, alen)) {
            gdb_send_err(jit, 0x22);
        } else {
            gdb_send_ok(jit);
        }
        return GDB_CMD_STAY;
    }

    case 'p': {
        uint64_t n;
        size_t   used;

        if (!gdb_parse_hex(args, alen, &n, &used) || n >= GDB_REG_COUNT) {
            gdb_send_err(jit, 0x14);
            return GDB_CMD_STAY;
        }

        uint64_t v = (n == 32) ? jit->cpu.pc : jit->cpu.x[n];
        char     reply[17];

        gdb_hex_u64_le(reply, v);
        gdb_send_packet(jit, reply, 16);

        return GDB_CMD_STAY;
    }

    case 'P': {
        uint64_t n;
        size_t   used;
        if (!gdb_parse_hex(args, alen, &n, &used) || n >= GDB_REG_COUNT ||
            used + 1 + 16 > alen || args[used] != '=') {
            gdb_send_err(jit, 0x14);

            return GDB_CMD_STAY;
        }

        uint64_t v;
        if (!gdb_parse_hex_u64_le(args + used + 1, &v)) {
            gdb_send_err(jit, 0x22);

            return GDB_CMD_STAY;
        }

        if (n == 32) {
            jit->cpu.pc = v;
        } else if (n != 0) {
            jit->cpu.x[n] = v;
        }

        gdb_send_ok(jit);
        return GDB_CMD_STAY;
    }

    case 'm': {
        uint64_t addr, len;
        size_t   off = 0, used;

        if (!gdb_parse_hex(args, alen, &addr, &used)) {
            gdb_send_err(jit, 0x14);

            return GDB_CMD_STAY;
        }

        off = used;
        if (off >= alen || args[off] != ',') {
            gdb_send_err(jit, 0x14);
            return GDB_CMD_STAY;
        }

        off++;
        if (!gdb_parse_hex(args + off, alen - off, &len, &used)) {
            gdb_send_err(jit, 0x14);

            return GDB_CMD_STAY;
        }

        if (len == 0) {
            gdb_send_packet(jit, "", 0);
            return GDB_CMD_STAY;
        }

        if (len * 2 + 1 > GDB_PACKET_CAP) {
            len = (GDB_PACKET_CAP - 1) / 2;
        }

        uint8_t *host;
        bool     writable;
        if (!gdb_translate_guest(jit, addr, len, &host, &writable)) {
            gdb_send_err(jit, 0x14);

            return GDB_CMD_STAY;
        }

        char reply[GDB_PACKET_CAP];
        for (uint64_t i = 0; i < len; i++) {
            gdb_hex_byte(reply + i * 2, host[i]);
        }

        gdb_send_packet(jit, reply, (size_t)(len * 2));

        return GDB_CMD_STAY;
    }

    case 'M': {
        uint64_t addr, len;
        size_t   off = 0, used;

        if (!gdb_parse_hex(args, alen, &addr, &used)) {
            gdb_send_err(jit, 0x14);
            return GDB_CMD_STAY;
        }

        off = used;
        if (off >= alen || args[off] != ',') {
            gdb_send_err(jit, 0x14);
            return GDB_CMD_STAY;
        }

        off++;
        if (!gdb_parse_hex(args + off, alen - off, &len, &used)) {
            gdb_send_err(jit, 0x14);
            return GDB_CMD_STAY;
        }

        off += used;

        if (off >= alen || args[off] != ':') {
            gdb_send_err(jit, 0x14);
            return GDB_CMD_STAY;
        }

        off++;

        if (alen - off < len * 2) {
            gdb_send_err(jit, 0x22);
            return GDB_CMD_STAY;
        }

        uint8_t *host;
        bool     writable;
        if (!gdb_translate_guest(jit, addr, len, &host, &writable) || !writable) {
            gdb_send_err(jit, 0x0d);
            return GDB_CMD_STAY;
        }

        for (uint64_t i = 0; i < len; i++) {
            int b = gdb_read_hex_byte(args + off + i * 2);
            if (b < 0) {
                gdb_send_err(jit, 0x22);

                return GDB_CMD_STAY;
            }
            host[i] = (uint8_t)b;
        }

        gdb_send_ok(jit);
        return GDB_CMD_STAY;
    }

    case 'Z':
    case 'z': {
        if (alen < 1 || args[0] != '0') {
            gdb_send_empty(jit);

            return GDB_CMD_STAY;
        }

        if (alen < 3 || args[1] != ',') {
            gdb_send_err(jit, 0x14);

            return GDB_CMD_STAY;
        }

        uint64_t addr;
        size_t   used;
        if (!gdb_parse_hex(args + 2, alen - 2, &addr, &used)) {
            gdb_send_err(jit, 0x14);

            return GDB_CMD_STAY;
        }

        if (cmd == 'Z') {
            if (!gdb_bp_install(jit, addr)) {
                gdb_send_err(jit, 0x1e);
                return GDB_CMD_STAY;
            }
        } else {
            gdb_bp_remove(jit, addr);
        }

        gdb_send_ok(jit);
        return GDB_CMD_STAY;
    }

    case 'c': {
        if (alen > 0) {
            uint64_t addr;
            size_t   used;

            if (gdb_parse_hex(args, alen, &addr, &used)) {
                jit->cpu.pc = addr;
            }
        }
        return GDB_CMD_CONTINUE;
    }

    case 's': {
        if (alen > 0) {
            uint64_t addr;
            size_t   used;

            if (gdb_parse_hex(args, alen, &addr, &used)) {
                jit->cpu.pc = addr;
            }
        }
        return GDB_CMD_STEP;
    }

    case 'k': {
        jit->gdb_should_detach = 1;

        return GDB_CMD_DETACH;
    }

    case 'D': {
        gdb_send_ok(jit);
        jit->gdb_should_detach = 1;

        return GDB_CMD_DETACH;
    }

    case 'H': {
        gdb_send_ok(jit);

        return GDB_CMD_STAY;
    }

    case 'q': {
        if (gdb_starts_with(pkt->buf, pkt->len, "qSupported")) {
            gdb_send_cstr(jit, "PacketSize=4000;QStartNoAckMode+;swbreak+");

            return GDB_CMD_STAY;
        }
        if (gdb_starts_with(pkt->buf, pkt->len, "qAttached")) {
            gdb_send_cstr(jit, "1");

            return GDB_CMD_STAY;
        }
        if (gdb_starts_with(pkt->buf, pkt->len, "qC")) {
            gdb_send_cstr(jit, "QC1");

            return GDB_CMD_STAY;
        }
        if (gdb_starts_with(pkt->buf, pkt->len, "qfThreadInfo")) {
            gdb_send_cstr(jit, "m1");

            return GDB_CMD_STAY;
        }
        if (gdb_starts_with(pkt->buf, pkt->len, "qsThreadInfo")) {
            gdb_send_cstr(jit, "l");

            return GDB_CMD_STAY;
        }

        gdb_send_empty(jit);
        return GDB_CMD_STAY;
    }

    case 'Q': {
        if (gdb_starts_with(pkt->buf, pkt->len, "QStartNoAckMode")) {
            gdb_send_ok(jit);
            jit->gdb_no_ack = 1;

            return GDB_CMD_STAY;
        }
        gdb_send_empty(jit);
        return GDB_CMD_STAY;
    }

    case 'v': {
        if (gdb_starts_with(pkt->buf, pkt->len, "vCont?")) {
            gdb_send_cstr(jit, "vCont;c;s");
            return GDB_CMD_STAY;
        }
        if (gdb_starts_with(pkt->buf, pkt->len, "vCont;")) {
            const char *p   = pkt->buf + 6;
            size_t      rem = pkt->len - 6;

            if (rem == 0) {
                gdb_send_empty(jit);
                return GDB_CMD_STAY;
            }

            char action = p[0];
            if (action == 'c' || action == 'C') {
                return GDB_CMD_CONTINUE;
            }

            if (action == 's' || action == 'S') {
                return GDB_CMD_STEP;
            }
            gdb_send_empty(jit);

            return GDB_CMD_STAY;
        }
        gdb_send_empty(jit);
        return GDB_CMD_STAY;
    }

    default: {
        gdb_send_empty(jit);
        return GDB_CMD_STAY;
    }
    }
}

bool riscv_gdb_on_stop(riscv_jit *jit, int signal) {
    if (!jit->gdb_attached)
        return true;

    if (jit->gdb_temp_unpatched_bp >= 0) {
        gdb_bp_repatch(jit, jit->gdb_temp_unpatched_bp);

        jit->gdb_temp_unpatched_bp = -1;
    }

    if (signal != 0) {
        gdb_send_signal(jit, signal);
    }

    gdb_packet pkt;
    while (true) {
        if (!gdb_read_packet(jit, &pkt)) {
            jit->gdb_should_detach = 1;

            return false;
        }

        gdb_cmd_result result = gdb_handle_packet(jit, &pkt);
        if (result == GDB_CMD_STAY) {
            continue;
        }

        if (result == GDB_CMD_DETACH) {
            return false;
        }

        jit->gdb_stepping = (result == GDB_CMD_STEP) ? 1 : 0;
        return true;
    }
}

bool riscv_gdb_poll_async(riscv_jit *jit) {
    if (!jit->gdb_attached) {
        return false;
    }

    int64_t r = gdb_bytes_available(jit->gdb_client_fd);
    if (r <= 0) {
        return false;
    }

    char c;
    if (!gdb_read_byte(jit->gdb_client_fd, &c)) {
        jit->gdb_should_detach = 1;

        return true;
    }

    return c == '\x03';
}

// -- implementation --

riscv_result riscv_jit_gdb_wait_for_client(riscv_jit *jit, uint16_t port) {
    if (jit->gdb_attached) {
        return RISCV_OK;
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_fd < 0) {
        return RISCV_JIT_ERR_IO;
    }

    int one = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(listen_fd);

        return RISCV_JIT_ERR_IO;
    }

    if (listen(listen_fd, 1) < 0) {
        close(listen_fd);

        return RISCV_JIT_ERR_IO;
    }

    struct sockaddr_in peer;
    socklen_t          peer_len  = sizeof(peer);
    int                client_fd = accept(listen_fd, (struct sockaddr *)&peer, &peer_len);
    if (client_fd < 0) {
        close(listen_fd);

        return RISCV_JIT_ERR_IO;
    }

    jit->gdb_listen_fd         = listen_fd;
    jit->gdb_client_fd         = client_fd;
    jit->gdb_attached          = true;
    jit->gdb_bp_count          = 0;
    jit->gdb_stepping          = 0;
    jit->gdb_no_ack            = 0;
    jit->gdb_should_detach     = 0;
    jit->gdb_temp_unpatched_bp = -1;
    return RISCV_OK;
}

void riscv_jit_gdb_close(riscv_jit *jit) {
    if (!jit->gdb_attached) {
        return;
    }

    if (jit->gdb_client_fd >= 0) {
        shutdown(jit->gdb_client_fd, SHUT_RDWR);
        close(jit->gdb_client_fd);

        jit->gdb_client_fd = -1;
    }
    if (jit->gdb_listen_fd >= 0) {
        close(jit->gdb_listen_fd);
        jit->gdb_listen_fd = -1;
    }

    if (jit->gdb_stub_buf) {
        jit->gdb_stub_buf  = 0;
        jit->gdb_stub_used = 0;
        jit->gdb_stub_cap  = 0;
    }

    // restore any patched code so a later non-debug run works normally
    for (int i = 0; i < jit->gdb_bp_count; i++) {
        gdb_bp_unpatch(jit, i);

        jit->gdb_bp[i].used = 0;
    }

    jit->gdb_bp_count = 0;
    jit->gdb_attached = false;
}

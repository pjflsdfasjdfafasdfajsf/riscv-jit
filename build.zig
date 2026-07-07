const std = @import("std");

pub fn build(b: *std.Build) void {
    // TODO: target whitelist
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const mod = b.addModule("riscv", .{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    mod.addCSourceFiles(.{
        .root = b.path("src"),
        .files = &.{
            "riscv_dwarf.c",
            "riscv_elf.c",
            "riscv_gdb.c",
            "riscv_jit.c",
            "riscv_ir.c",
            "riscv_jit_cg_x86.c",
        },
    });
    mod.addIncludePath(b.path("src"));

    const lib = b.addLibrary(.{
        .name = "riscv",
        .root_module = mod,
    });

    lib.installHeadersDirectory(b.path("src"), "", .{
        .include_extensions = &.{"h"},
    });
    b.installArtifact(lib);

    // when the library is instantiated as a dependency riscv_arch_test is
    // obviously not needed
    if (b.pkg_hash.len != 0) {
        return;
    }

    const test_step = b.step("test", "Run tests against RISC-V arch tests");
    const riscv_arch_test = b.lazyDependency("riscv_arch_test", .{}) orelse return;

    const env = b.addWriteFiles();
    _ = env.add("rvtest_config.h",
        \\#ifndef _RVTEST_CONFIG_H_
        \\#define _RVTEST_CONFIG_H_
        \\#define __riscv_xlen 64
        \\#define TEST_XLEN 64
        \\#define TEST_FLEN 64
        \\#define XLEN 64
        \\#endif
    );
    _ = env.add("rvmodel_macros.h",
        \\#ifndef _RVMODEL_MACROS_H_
        \\#define _RVMODEL_MACROS_H_
        \\#define RVMODEL_HALT_PASS li a7, 93; li a0, 0; ecall;
        \\#define RVMODEL_HALT_FAIL li a7, 93; li a0, 1; ecall;
        \\#define RVMODEL_BOOT .global _start; _start:
        \\#define RVMODEL_DATA_SECTION .data
        \\#define RVMODEL_DATA_BEGIN
        \\#define RVMODEL_DATA_END
        \\#define RVMODEL_IO_INIT
        \\#define RVMODEL_IO_WRITE_STR(x, y, z)
        \\#define RVMODEL_IO_ASSERT_GPR_EQ(x, y, z)
        \\#define RVMODEL_SET_MSW_INT
        \\#define RVMODEL_CLEAR_MSW_INT
        \\#define RVMODEL_CLEAR_MTIMER_INT
        \\#define RVMODEL_CLEAR_MEXT_INT
        \\#endif
    );
    const link_ld = env.add("link.ld",
        \\OUTPUT_ARCH(riscv)
        \\ENTRY(rvtest_entry_point)
        \\_start = rvtest_entry_point;
        \\SECTIONS {
        \\  . = 0x80000000;
        \\ .text : {
        \\   *(.text.init)
        \\   *(.text.rvtest) *(.text.rvtest.*)
        \\   *(.text.rvmodel) *(.text.rvmodel.*) *(.text) *(.text.*)
        \\ }
        \\  .data : { *(.data) }
        \\  .bss : { *(.bss) }
        \\}
    );

    const test_runner = b.addExecutable(.{
        .name = "test_runner",

        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,

            .link_libc = true,
        }),
    });
    test_runner.root_module.addCSourceFile(.{
        .file = b.path("src/_riscv_jit_test_runner.c"),
    });
    test_runner.root_module.addIncludePath(b.path("src"));
    test_runner.root_module.linkLibrary(lib);

    // TODO: tests currently don't actually validate the results, need to
    // figure out how to do that
    const run_cmd = b.addRunArtifact(test_runner);
    test_step.dependOn(&run_cmd.step);

    // zig fmt: off
    const tests = .{
        "I/I-add-00",    "I/I-bgeu-00",   "I/I-ld-00",      "I/I-sb-00",       "I/I-sltiu-00",   "I/I-srlw-00",
        "I/I-addi-00",   "I/I-blt-00",    "I/I-lh-00",      "I/I-sd-00",       "I/I-sltu-00",    "I/I-sub-00",
        "I/I-addiw-00",  "I/I-bltu-00",   "I/I-lhu-00",     "I/I-sh-00",       "I/I-sra-00",     "I/I-subw-00",
        "I/I-addw-00",   "I/I-bne-00",    "I/I-lui-00",     "I/I-sll-00",      "I/I-srai-00",    "I/I-sw-00",
        "I/I-and-00",    "I/I-fence-00",  "I/I-lw-00",      "I/I-slli-00",     "I/I-sraiw-00",   "I/I-xor-00",
        "I/I-andi-00",   "I/I-jal-00",    "I/I-lwu-00",     "I/I-slliw-00",    "I/I-sraw-00",    "I/I-xori-00",
        "I/I-auipc-00",  "I/I-jalr-00",   "I/I-nop-00",     "I/I-sllw-00",     "I/I-srl-00",     "I/I-beq-00",
        "I/I-lb-00",     "I/I-or-00",     "I/I-slt-00",     "I/I-srli-00",     "I/I-bge-00",     "I/I-lbu-00",
        "I/I-ori-00",    "I/I-slti-00",   "I/I-srliw-00",
        // math extension
        "M/M-div-00",  "M/M-divu-00",  "M/M-divuw-00",
        "M/M-divw-00", "M/M-mul-00",  "M/M-mulh-00",  "M/M-mulhsu-00", "M/M-mulhu-00", "M/M-mulw-00",
        "M/M-rem-00",  "M/M-remu-00", "M/M-remuw-00", "M/M-remw-00",
    };
    // zig-fmt on

    inline for (tests) |name| {
        @setEvalBranchQuota(10_000);

        const elf = b.addExecutable(.{
            .name = std.Io.Dir.path.basename(b.fmt(("{s}.elf"), .{name})),

            .root_module = b.createModule(.{
                .target = b.resolveTargetQuery(.{
                    .cpu_arch = .riscv64,
                    .os_tag = .freestanding,
                    .abi = .none,
                }),
                .optimize = .Debug,
            }),
        });
        elf.root_module.addAssemblyFile(riscv_arch_test.path(b.fmt("tests/rv64i/{s}.S", .{name})));

        elf.root_module.addIncludePath(riscv_arch_test.path("tests/env"));
        elf.root_module.addIncludePath(riscv_arch_test.path("tests/rv64i/I"));
        elf.root_module.addIncludePath(riscv_arch_test.path("tests/rv64i/M"));
        elf.root_module.addIncludePath(env.getDirectory());
        elf.setLinkerScript(link_ld);

        run_cmd.addArtifactArg(elf);
    }
}

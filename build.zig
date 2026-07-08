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

    // pass 1 compiles a signature elf, runs it on sail and then dumps the signature
    // region, the next pass recompiles the same test with the reference values baked
    // in so that every RV_TEST_SIGUPD compares instead of stores

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
    // macros for the self-checking elf
    _ = env.add("rvmodel_macros.h",
        \\#ifndef _RVMODEL_MACROS_H_
        \\#define _RVMODEL_MACROS_H_
        \\
        \\#define RVMODEL_HALT_PASS li a7, 93; li a0, 0; ecall;
        \\#define RVMODEL_HALT_FAIL li a7, 93; li a0, 1; ecall;
        \\
        \\#define RVMODEL_BOOT .global _start; _start:
        \\#define RVMODEL_DATA_SECTION .data
        \\
        \\#define RVMODEL_IO_INIT(_R1, _R2, _R3)
        \\#define RVMODEL_IO_WRITE_STR(_R1, _R2, _R3, _STR_PTR)
        \\
        \\#define RVMODEL_INTERRUPT_LATENCY 10
        \\#define RVMODEL_TIMER_INT_SOON_DELAY 100
        \\#define RVMODEL_SET_MEXT_INT(_R1, _R2)
        \\#define RVMODEL_CLR_MEXT_INT(_R1, _R2)
        \\#define RVMODEL_SET_MSW_INT(_R1, _R2)
        \\#define RVMODEL_CLR_MSW_INT(_R1, _R2)
        \\#define RVMODEL_SET_SEXT_INT(_R1, _R2)
        \\#define RVMODEL_CLR_SEXT_INT(_R1, _R2)
        \\#define RVMODEL_SET_SSW_INT(_R1, _R2)
        \\#define RVMODEL_CLR_SSW_INT(_R1, _R2)
        \\#endif
    );
    const link_ld = env.add("link.ld",
        \\OUTPUT_ARCH(riscv)
        \\ENTRY(rvtest_entry_point)
        \\_start = rvtest_entry_point;
        \\SECTIONS {
        \\  . = 0x80000000;
        \\  .data : { *(.data) *(.data.*) *(.sdata) *(.sdata.*) }
        \\  . = ALIGN(0x1000);
        \\  .text : {
        \\    *(.text.init)
        \\    *(.text.rvtest) *(.text.rvtest.*)
        \\    *(.text.rvmodel) *(.text.rvmodel.*) *(.text) *(.text.*)
        \\    *(.rodata) *(.rodata.*) *(.srodata) *(.srodata.*)
        \\  }
        \\  .tohost : { *(.tohost) }
        \\  .bss : { *(.bss) *(.bss.*) *(.sbss) *(.sbss.*) }
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

    const run_cmd = b.addRunArtifact(test_runner);
    run_cmd.stdio = .inherit;

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

        const base = std.Io.Dir.path.basename(name);

        const sig_elf = archTestElf(b, riscv_arch_test, env, link_ld, name, base, .regular);
        sig_elf.root_module.addCMacro("SIGNATURE", "");

        const sail = b.addSystemCommand(&.{"sail_riscv_sim"});
        sail.addArg("--test-signature");
        const sig_file = sail.addOutputFileArg(b.fmt("{s}.sig", .{base}));
        sail.addArgs(&.{ "--signature-granularity", "8" });
        sail.addFileArg(sig_elf.getEmittedBin());
        // sail spams stderr with its own progress which makes zig think that
        // it failed and increases the spam even more
        _ = sail.captureStdErr(.{});

        const convert = b.addRunArtifact(test_runner);
        convert.addArg("--convert-sig");
        convert.addFileArg(sig_file);
        const results = convert.addOutputFileArg("test.results");

        const elf = archTestElf(b, riscv_arch_test, env, link_ld, name, base, .self_check);
        elf.root_module.addCMacro("RVTEST_SELFCHECK", "");
        elf.root_module.addIncludePath(results.dirname());
        elf.root_module.addCMacro("SIGNATURE_FILE", "\"test.results\"");

        run_cmd.addArtifactArg(elf);
    }
}

fn archTestElf(
    b: *std.Build,
    riscv_arch_test: *std.Build.Dependency,
    env: *std.Build.Step.WriteFile,
    link_ld: std.Build.LazyPath,
    name: []const u8,
    base: []const u8,
    mode: enum {regular, self_check},
) *std.Build.Step.Compile {
    const elf = b.addExecutable(.{
        .name = if (mode == .self_check) base else b.fmt("{s}.sig", .{base}),

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

    return elf;
}

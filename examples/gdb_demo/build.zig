const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const riscv_dep = b.dependency("riscv", .{
        .target = target,
        .optimize = optimize,
    });

    const host = b.addExecutable(.{
        .name = "host",

        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });
    host.root_module.addCSourceFile(.{
        .file = b.path("src/host.c"),
    });
    host.root_module.linkLibrary(riscv_dep.artifact("riscv"));

    b.installArtifact(host);

    const guest = b.addExecutable(.{
        .name = "guest",

        .root_module = b.createModule(.{
            .target = b.resolveTargetQuery(.{
                .cpu_arch = .riscv64,
                .os_tag = .freestanding,
                .abi = .none,
            }),
            .optimize = .Debug,
        }),
    });
    guest.out_filename = "guest.elf";

    guest.root_module.addCSourceFile(.{
        .file = b.path("src/guest.c"),
    });

    b.installArtifact(guest);

    const run_cmd = b.addRunArtifact(host);
    run_cmd.step.dependOn(b.getInstallStep());

    const run_step = b.step("run", "Run the example");
    run_step.dependOn(&run_cmd.step);
}

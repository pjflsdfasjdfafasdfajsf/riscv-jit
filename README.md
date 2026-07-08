this is an extremely simple RISC-V JIT compiler written in C99
for game dev purposes, released into public domain!

this project supports the zig build system:
```sh
zig fetch --save https://github.com/pjflsdfasjdfafasdfajsf/riscv-jit/archive/refs/tags/1.0.0.zip
```
then, in your `build.zig`:
```zig

// - - - snip - - -

const riscv_dep = b.dependency("riscv", .{
    .target = target,
    .optimize = optimize,
});

// - - - snip - - -

// replace std.Build.Module with your actual module
std.Build.Module.linkLibrary(riscv_dep.artifact("riscv"));

```

this library is intended to be debuggable, but gdb support is currently
experimental!

you can see a few examples of the API usage in [`examples`](examples)

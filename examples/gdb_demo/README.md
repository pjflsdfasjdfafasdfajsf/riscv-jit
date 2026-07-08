demonstrates the GDB remote debugger

run `zig build run` to start the host and then in another terminal:

```sh
gdb zig-out/bin/guest.elf
(gdb) target remote :1234
(gdb) b add
(gdb) c
(gdb) info registers
(gdb) stepi
(gdb) c
```

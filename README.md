# freestandcasm-fun

An interactive sandbox demonstrating how to write cross-architecture, ultra-tiny binaries using freestanding C and raw inline assembly macros. This repository bypasses the standard C runtime (libc) entirely, providing a direct portal to the Linux kernel.

It is designed for systems engineers, developers, and hobbyists who want to practically learn the Linux ABI, CPU register manipulation, and the core application lifecycle.

---

## 🏛 The Architecture

In a standard application stack, execution jumps through multiple layers of middleware:

`Your Code` ➔ `Standard Library (glibc/musl)` ➔ `System Call Wrapper` ➔ `Linux Kernel`

This repository cuts out the middleware to talk directly to the CPU and kernel scheduler:

`Your Code` ➔ `Extended Inline Assembly` ➔ `CPU Instruction (syscall / svc #0)` ➔ `Linux Kernel`

By discarding runtime setup wrappers, exception handling tables, and dynamic linking hooks, binaries in this repository compile down to a pristine layout of under 600 bytes (fitting within a fraction of a single hardware memory page).

---

## 🔄 Multi-Architecture Syscall Wrappers

Linux system call numbers, calling conventions, and hardware trap instructions differ between x86_64 and aarch64. Instead of relying on libc, each program uses small inline-assembly wrappers that bind arguments directly to the ABI registers and then trap into the kernel.

On x86_64 the kernel expects arguments in `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9`, with the syscall number in `rax`, and the trap instruction is `syscall`:

```c
static inline long sys_write(int fd, const void *buf, long n) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_WRITE), "D"((long)fd), "S"(buf), "d"(n)
        : "rcx", "r11", "memory"
    );
    return ret;
}
```

On aarch64 the kernel uses `x0`–`x7` for arguments, `x8` for the syscall number, and the trap is `svc #0`:

```c
static inline long sys_write(int fd, const void *buf, long n) {
    register long x0 __asm__("x0") = fd;
    register long x1 __asm__("x1") = (long)buf;
    register long x2 __asm__("x2") = n;
    register long x8 __asm__("x8") = SYS_WRITE;
    __asm__ volatile (
        "svc #0"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x8)
        : "memory", "cc"
    );
    return x0;
}
```

Because there is no libc startup code, each program also provides its own `_start` entry point in assembly, aligning the stack before calling into the C code:

```asm
# x86_64
_start:
    xorl %ebp, %ebp
    andq $-16, %rsp        # align stack
    call hello_main
    ...

# aarch64
_start:
    mov x4, sp
    bic x4, x4, #15        # align stack
    mov sp, x4
    bl hello_main
    ...
```

------------------------------

## ⚡ The Binary Optimization Pipeline
To strip out default ELF metadata padding, asynchronous stack unwind data, and cryptographic build hashes, compile using the included helpers. Each helper uses the same targeted `gcc` configuration:

```bash
./compile-hello.sh
./compile-proc-top3.sh
```

The underlying flags are:

```
gcc -nostdlib -static -fno-stack-protector -fno-asynchronous-unwind-tables \
    -fno-builtin -O2 -s -Wl,--build-id=none -Wl,-z,noseparate-code \
    -Wl,-z,max-page-size=4096 \
    -Wall -Wextra <source.c> -o build/<binary>
```

## 🔬 Inspection Tools
Verify the absolute minimization of your hardware footprint:

```bash
# Read the physical file footprint
ls -lh build/hello-world build/proc-top3

# Inspect internal segment sections
size build/hello-world build/proc-top3
```

------------------------------
## 📂 Example: /proc traversal (`proc-top3`)
`proc-top3.c` demonstrates a slightly more complex freestanding program: it opens `/proc` with `openat`, walks process directories with `getdents64`, reads each PID's `/proc/<pid>/stat` and `/proc/<pid>/status`, and prints a tiny ASCII table of the three longest-running processes sorted by cumulative CPU time.

Build and run it with the included helper:

```bash
./compile-proc-top3.sh
./build/proc-top3
```

Example output:

```
PID      NAME                 CPU (ticks)       RAM (bytes)
-------  --------------  ----------------  ----------------
   3221  firefox-esr               253498        1069064192
  25335  opencode                   62635        1020600320
   2239  kwin_x11                   56465         375255040
```

Like `hello-world.c`, it has no libc dependency, uses architecture-specific inline-asm syscall wrappers, and keeps the binary under a few kilobytes. Both programs build and run on x86_64 and aarch64.

------------------------------

## 🎓 Learning Objectives: Master the Lifecycle
This repository serves as an interactive guide to exploring how the kernel manages running processes:

   1. Context Switching: Watch how the CPU flips execution rings from user mode to kernel space when striking `syscall` or `svc #0`.
   2. Register Allocation: Learn how architecture-specific inline-assembly constraints (e.g., `"a"`/`"D"`/`"S"`/`"d"` on x86_64 and explicit `register long xN __asm__("xN")` variables on aarch64) map C values directly into the ABI registers expected by the kernel.
   3. Freestanding Entry Points: See how a tiny assembly `_start` can replace the entire libc startup routine, including the stack alignment that the C ABI expects before calling into compiled C code.
   4. Subreaping and Signals: See the exact foundational code layout used to build lightweight container tools (like the sub-5KB tuxreaperd micro-init daemon) to trap signals and handle child execution tracking directly from a clean slate.
   5. Filesystem Traversal: Use raw directory syscalls (`openat` + `getdents64`) to inspect the kernel's live process table without any standard library helpers.




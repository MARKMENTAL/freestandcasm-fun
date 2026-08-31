## freestandcasm-fun
An interactive sandbox demonstrating how to write cross-platform, ultra-tiny binaries using freestanding C and raw inline assembly macros. This repository bypasses the standard C runtime (libc) entirely, providing a direct portal to the Linux kernel.
It is designed for systems engineers, developers, and hobbyists who want to practically learn the Linux ABI, CPU register manipulation, and the core application lifecycle.
------------------------------
## 🏛️ The Architecture
In a standard application stack, execution jumps through multiple layers of middleware:
Your Code ➔ Standard Library (glibc/musl) ➔ System Call Wrapper ➔ Linux Kernel
This repository cuts out the middleware to talk directly to the CPU and kernel scheduler:
Your Code ➔ Extended Inline Assembly ➔ CPU Instruction (syscall / svc #0) ➔ Linux Kernel
By discarding runtime setup wrappers, exception handling tables, and dynamic linking hooks, binaries in this repository compile down to a pristine layout of under 600 bytes (fitting within a fraction of a single hardware memory page).
------------------------------
## 🔄 Multi-Architecture Macro Abstraction
Linux system call numbers and hardware trap instructions differ completely between processor architectures. This project utilizes the C preprocessor as a zero-overhead macro-templating engine to resolve these variations seamlessly at compile time.

#if defined(__x86_64__)
    #define REG_SYS    "rax"
    #define REG_ARG1   "rdi"
    #define REG_ARG2   "rsi"
    #define REG_ARG3   "rdx"
    #define ASM_TRAP   "syscall"
    #define SYS_WRITE  1
    #define SYS_WAIT4  61
    #define SYS_EXIT   60
#elif defined(__aarch64__)
    #define REG_SYS    "x8"
    #define REG_ARG1   "x0"
    #define REG_ARG2   "x1"
    #define REG_ARG3   "x2"
    #define ASM_TRAP   "svc #0"
    #define SYS_WRITE  64
    #define SYS_WAIT4  260
    #define SYS_EXIT   93#endif

------------------------------

## ⚡ The Binary Optimization Pipeline
To strip out default ELF metadata padding, asynchronous stack unwind data, and cryptographic build hashes, compile using this targeted configuration:

# Define your platform's native memory page size boundary
page_size=4096
# Compile with strict hardware alignment flags
gcc -nostdlib -static -fno-stack-protector -fno-asynchronous-unwind-tables \
    -fno-builtin -O2 -s -Wl,--build-id=none -Wl,-z,noseparate-code \
    -Wl,-z,max-page-size=${page_size} \
    -Wall -Wextra hello-world.c -o hello-world

## 🔬 Inspection Tools
Verify the absolute minimization of your hardware footprint:

# Read the physical file footprint
ls -lh hello-world
# Inspect internal segment sections
size hello-world

------------------------------
## 🎓 Learning Objectives: Master the Lifecycle
This repository serves as an interactive guide to exploring how the kernel manages running processes:

   1. Context Switching: Watch how the CPU flips execution rings from user mode to kernel space when striking ASM_TRAP.
   2. Register Allocation: Learn how extended inline assembly constraints ("r", "memory") direct the compiler to map local C variables into raw hardware registers without memory corruption.
   3. Subreaping and Signals: See the exact foundational code layout used to build lightweight container tools (like the sub-5KB tuxreaperd micro-init daemon) to trap signals and handle child execution tracking directly from a clean slate.




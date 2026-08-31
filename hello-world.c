// Freestanding "Hello, world!" example.
//
// This program bypasses the C runtime entirely and uses raw inline assembly
// to invoke the Linux write and exit syscalls directly. It works on both
// x86_64 (syscall instruction) and aarch64 (svc #0).

#if defined(__x86_64__)
    #define SYS_WRITE  1
    #define SYS_EXIT   60
#elif defined(__aarch64__)
    #define SYS_WRITE  64
    #define SYS_EXIT   93
#else
    #error "Unsupported architecture: only x86_64 and aarch64 are supported"
#endif

// ----------------------------------------------------------------------------
// Raw syscall wrappers 
// ----------------------------------------------------------------------------

#if defined(__x86_64__)

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

// Kernel entry point. The initial stack may not be 16-byte aligned, so we
// align it before calling C code, then exit cleanly with status 0.
__asm__(
    ".text\n"
    ".globl _start\n"
    "_start:\n"
    "    xorl %ebp, %ebp\n"
    "    andq $-16, %rsp\n"        /* align stack */
    "    call hello_main\n"
    "    xorl %edi, %edi\n"         /* exit status 0 */
    "    movq $60, %rax\n"         /* sys_exit */
    "    syscall\n"
);

#elif defined(__aarch64__)

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

// aarch64 kernel entry point: align the stack, call hello_main, then exit.
__asm__(
    ".text\n"
    ".globl _start\n"
    "_start:\n"
    "    mov x4, sp\n"
    "    bic x4, x4, #15\n"      /* align stack */
    "    mov sp, x4\n"
    "    bl hello_main\n"
    "    mov x0, #0\n"           /* exit status 0 */
    "    mov x8, #93\n"          /* sys_exit */
    "    svc #0\n"
);

#endif

// ----------------------------------------------------------------------------
// Application logic called by the per-arch asm _start
// ----------------------------------------------------------------------------

void hello_main(void) {
    static const char msg[] = "Hello, world!\n";

    // Write message to stdout (file descriptor 1)
    sys_write(1, msg, (long)(sizeof(msg) - 1));
}

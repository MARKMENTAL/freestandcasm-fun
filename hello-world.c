// 1. Architecture-Specific Register and Syscall Definitions
#if defined(__x86_64__)
    #define REG_SYS    "rax"
    #define REG_ARG1   "rdi"
    #define REG_ARG2   "rsi"
    #define REG_ARG3   "rdx"
    #define ASM_TRAP   "syscall"
    #define SYS_WRITE  1
    #define SYS_EXIT   60
#elif defined(__aarch64__)
    #define REG_SYS    "x8"
    #define REG_ARG1   "x0"
    #define REG_ARG2   "x1"
    #define REG_ARG3   "x2"
    #define ASM_TRAP   "svc #0"
    #define SYS_WRITE  64
    #define SYS_EXIT   93
#endif

// 2. Freestanding Assembly Macro Templates using C
#define KERNEL_PORTAL_WRITE(fd, buf, count)                       \
    __asm__ __volatile__(                                         \
        "mov %0, %%" REG_SYS "\n\t"                               \
        "mov %1, %%" REG_ARG1 "\n\t"                              \
        "mov %2, %%" REG_ARG2 "\n\t"                              \
        "mov %3, %%" REG_ARG3 "\n\t"                              \
        ASM_TRAP                                                  \
        :                                                         \
        : "r"((long)SYS_WRITE), "r"((long)fd), "r"(buf), "r"((long)count) \
        : REG_SYS, REG_ARG1, REG_ARG2, REG_ARG3, "memory"         \
    )

#define KERNEL_PORTAL_EXIT(status)                                \
    __asm__ __volatile__(                                         \
        "mov %0, %%" REG_SYS "\n\t"                               \
        "mov %1, %%" REG_ARG1 "\n\t"                              \
        ASM_TRAP                                                  \
        :                                                         \
        : "r"((long)SYS_EXIT), "r"((long)status)                  \
        : REG_SYS, REG_ARG1, "memory"                             \
    )

// 3. Application Entry Point (Bypassing main() and libc)
void _start(void) {
    const char msg[] = "Hello, world!\n";
    
    // Write message to stdout (file descriptor 1)
    KERNEL_PORTAL_WRITE(1, msg, sizeof(msg) - 1);
    
    // Exit cleanly with status code 0
    KERNEL_PORTAL_EXIT(0);
}


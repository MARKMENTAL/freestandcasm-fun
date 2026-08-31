// Freestanding /proc traversal example.
//
// This program bypasses the C runtime entirely and talks to the Linux kernel
// through raw syscalls. It opens /proc, walks each PID directory, reads
// /proc/<pid>/stat (CPU time) and /proc/<pid>/status (resident RAM), then
// prints a tiny ASCII table of the three longest-running processes.
//
// /proc is a fake filesystem: it is a virtual device, Linux's window into RAM,
// so every open/read/close is a direct conversation with the kernel's task
// and memory subsystems rather than a trip to a physical disk.

// Syscall numbers and flag octals are architecture-specific because the Linux
// kernel ABI differs between x86_64 and aarch64. O_DIRECTORY also has a
// different value on each architecture.
#if defined(__x86_64__)
    #define SYS_READ       0
    #define SYS_WRITE      1
    #define SYS_CLOSE      3
    #define SYS_OPENAT     257
    #define SYS_GETDENTS64 217
    #define SYS_EXIT       60
    #define O_DIRECTORY    00200000   /* 0x10000 */
#elif defined(__aarch64__)
    #define SYS_OPENAT     56
    #define SYS_CLOSE      57
    #define SYS_READ       63
    #define SYS_WRITE      64
    #define SYS_GETDENTS64 61
    #define SYS_EXIT       93
    #define O_DIRECTORY    040000     /* 0x4000 */
#else
    #error "Unsupported architecture: only x86_64 and aarch64 are supported"
#endif

// AT_FDCWD lets openat() resolve paths relative to the current working
// directory. O_RDONLY asks for read-only access; O_DIRECTORY tells the VFS
// layer that we expect a directory node. Without O_DIRECTORY, openat on /proc
// can fail with EINVAL or ENOTDIR depending on the platform.
#define AT_FDCWD      -100
#define O_RDONLY      0

// ----------------------------------------------------------------------------
// Raw syscall wrappers (style matches reference/tuxreaperdasm.c)
//
// Each wrapper is a minimal portal to the kernel:
//   openat  -> handshake: resolve a path and receive a file descriptor.
//   read    -> stream: kernel formats live state into our userspace buffer.
//   getdents64 -> directory iteration: returns dirent records, not file bytes.
//   close   -> purge: release the FD and any temporary kernel structures.
// ----------------------------------------------------------------------------

#if defined(__x86_64__)

// Write n bytes from buf to the already-open file descriptor fd.
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

// Read up to n bytes from fd into buf. For procfs files this usually
// captures the entire generated record in one call.
static inline long sys_read(int fd, void *buf, long n) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_READ), "D"((long)fd), "S"(buf), "d"(n)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// Resolve path relative to dirfd and return a new file descriptor.
// The kernel checks the flags here; no data is transferred yet.
static inline long sys_openat(int dirfd, const char *path, int flags) {
    long ret;
    register long r10 __asm__("r10") = 0; /* mode */
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_OPENAT), "D"((long)dirfd), "S"(path), "d"(flags), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// Read directory entries from fd. Returns a packed array of linux_dirent64
// records; each record's d_reclen tells us how far to advance.
static inline long sys_getdents64(int fd, void *dirp, unsigned int count) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_GETDENTS64), "D"((long)fd), "S"(dirp), "d"(count)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// Release a file descriptor. For procfs this tears down the temporary
// kernel-side state created for this virtual-file session.
static inline long sys_close(int fd) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_CLOSE), "D"((long)fd)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// Terminate the process. Used for early errors; the asm _start also calls
// sys_exit directly after proc_main returns.
static inline long sys_exit(int status) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(SYS_EXIT), "D"((long)status)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// Kernel entry point. The initial stack may not be 16-byte aligned, so we
// align it before calling C code, then exit with proc_main's return value.
__asm__(
    ".text\n"
    ".globl _start\n"
    "_start:\n"
    "    xorl %ebp, %ebp\n"
    "    andq $-16, %rsp\n"        /* align stack */
    "    call proc_main\n"
    "    movq %rax, %rdi\n"
    "    movq $60, %rax\n"         /* sys_exit */
    "    syscall\n"
);

#elif defined(__aarch64__)

// aarch64 syscall wrappers: bind arguments to x0-x3 and the syscall number
// to x8, then trap into the kernel with svc #0. x0 holds the return value.

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

static inline long sys_read(int fd, void *buf, long n) {
    register long x0 __asm__("x0") = fd;
    register long x1 __asm__("x1") = (long)buf;
    register long x2 __asm__("x2") = n;
    register long x8 __asm__("x8") = SYS_READ;
    __asm__ volatile (
        "svc #0"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x8)
        : "memory", "cc"
    );
    return x0;
}

// Resolve path relative to dirfd and return a new file descriptor.
static inline long sys_openat(int dirfd, const char *path, int flags) {
    register long x0 __asm__("x0") = dirfd;
    register long x1 __asm__("x1") = (long)path;
    register long x2 __asm__("x2") = flags;
    register long x3 __asm__("x3") = 0; /* mode */
    register long x8 __asm__("x8") = SYS_OPENAT;
    __asm__ volatile (
        "svc #0"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x3), "r"(x8)
        : "memory", "cc"
    );
    return x0;
}

// Read directory entries from fd; see the x86_64 version for the record layout.
static inline long sys_getdents64(int fd, void *dirp, unsigned int count) {
    register long x0 __asm__("x0") = fd;
    register long x1 __asm__("x1") = (long)dirp;
    register long x2 __asm__("x2") = count;
    register long x8 __asm__("x8") = SYS_GETDENTS64;
    __asm__ volatile (
        "svc #0"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x8)
        : "memory", "cc"
    );
    return x0;
}

static inline long sys_close(int fd) {
    register long x0 __asm__("x0") = fd;
    register long x8 __asm__("x8") = SYS_CLOSE;
    __asm__ volatile (
        "svc #0"
        : "+r"(x0)
        : "r"(x8)
        : "memory", "cc"
    );
    return x0;
}

// Release a file descriptor and its kernel-side virtual-file state.
static inline long sys_close(int fd) {
    register long x0 __asm__("x0") = fd;
    register long x8 __asm__("x8") = SYS_CLOSE;
    __asm__ volatile (
        "svc #0"
        : "+r"(x0)
        : "r"(x8)
        : "memory", "cc"
    );
    return x0;
}

// Terminate the process.
static inline long sys_exit(int status) {
    register long x0 __asm__("x0") = status;
    register long x8 __asm__("x8") = SYS_EXIT;
    __asm__ volatile (
        "svc #0"
        : "+r"(x0)
        : "r"(x8)
        : "memory", "cc"
    );
    return x0;
}

// aarch64 kernel entry point: align the stack, call proc_main, then exit.
__asm__(
    ".text\n"
    ".globl _start\n"
    "_start:\n"
    "    mov x4, sp\n"
    "    bic x4, x4, #15\n"      /* align stack */
    "    mov sp, x4\n"
    "    bl proc_main\n"
    "    mov x8, #93\n"          /* sys_exit */
    "    svc #0\n"
);

#endif

// ----------------------------------------------------------------------------
// Minimal string / formatting helpers
//
// No libc means no printf. These helpers measure strings, copy paths, and
// pad/align fields so we can build the ASCII table with raw sys_write calls.
// ----------------------------------------------------------------------------

static unsigned int str_len(const char *s) {
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

static int str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (i + 1 < max && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return i;
}

// /proc contains non-process nodes like 'sys', 'net', and 'self'. Only
// directories whose names are purely numeric represent actual PIDs.
static int is_pid_dir(const char *name) {
    int i = 0;
    if (name[0] == '\0') return 0;
    while (name[i]) {
        if (name[i] < '0' || name[i] > '9') return 0;
        i++;
    }
    return 1;
}

// Parse a base-10 unsigned integer from [p, end), stopping at the first
// non-digit character.
static unsigned long atoul(const char *p, const char *end) {
    unsigned long v = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        v = v * 10 + (unsigned long)(*p - '0');
        p++;
    }
    return v;
}

static void write_buf(long fd, const void *buf, unsigned long len) {
    sys_write((int)fd, buf, (long)len);
}

static void write_fill(long fd, char c, int n) {
    char buf[32];
    if (n > 32) n = 32;
    for (int i = 0; i < n; i++) buf[i] = c;
    write_buf(fd, buf, (unsigned long)n);
}

static void write_str_left(long fd, const char *s, int width) {
    int len = (int)str_len(s);
    if (len > width) len = width;
    write_buf(fd, s, (unsigned long)len);
    if (len < width) write_fill(fd, ' ', width - len);
}

static void write_str_right(long fd, const char *s, int width) {
    int len = (int)str_len(s);
    if (len > width) len = width;
    if (len < width) write_fill(fd, ' ', width - len);
    write_buf(fd, s, (unsigned long)len);
}

static void write_ulong_right(long fd, unsigned long v, int width) {
    char digits[24];
    int n = 0;
    if (v == 0) {
        digits[n++] = '0';
    } else {
        while (v > 0) {
            digits[n++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    if (n < width) write_fill(fd, ' ', width - n);
    for (int i = n - 1; i >= 0; i--) {
        char ch = digits[i];
        write_buf(fd, &ch, 1);
    }
}

// ----------------------------------------------------------------------------
// /proc parsing
//
// For each numeric /proc directory we read two kernel-generated files:
//   stat   -> PID, command name, user/system CPU ticks.
//   status -> VmRSS, the resident set size in kB (converted to bytes).
// ----------------------------------------------------------------------------

// Layout of a 64-bit directory entry as returned by getdents64.
// d_name starts at offset 19 and is null-terminated; d_reclen is the total
// record size including padding.
struct linux_dirent64 {
    unsigned long  d_ino;
    unsigned long  d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[];
};

struct procinfo {
    long          pid;
    char          name[16];
    unsigned long cpu;
    unsigned long ram;
};

// Open a procfs file, read its generated contents into buf, and close it.
// Returns the number of bytes read, or -1 if openat failed.
static int read_file(const char *path, char *buf, unsigned long bufsz) {
    long fd = sys_openat(AT_FDCWD, path, O_RDONLY);
    if (fd < 0) return -1;

    unsigned long total = 0;
    long n;
    while (total < bufsz && (n = sys_read((int)fd, buf + total, (long)(bufsz - total))) > 0) {
        total += (unsigned long)n;
    }
    sys_close((int)fd);
    return (int)total;
}

// Parse a /proc/<pid>/stat line. The command name is wrapped in parentheses
// and may itself contain spaces or parentheses, so we locate the last ')'
// and count space-delimited fields from there. Fields 14 and 15 (0-indexed
// 11 and 12 after the closing paren) are utime and stime.
static int parse_stat(const char *buf, int len, struct procinfo *info) {
    const char *end = buf + len;
    const char *first_paren = 0;
    const char *last_paren = 0;

    for (const char *p = buf; p < end; p++) {
        if (*p == '(') first_paren = p;
        else if (*p == ')') last_paren = p;
    }
    if (!first_paren || !last_paren || first_paren > last_paren)
        return 0;

    int namelen = (int)(last_paren - first_paren - 1);
    if (namelen > 15) namelen = 15;
    for (int i = 0; i < namelen; i++) info->name[i] = first_paren[1 + i];
    info->name[namelen] = '\0';

    info->pid = (long)atoul(buf, first_paren);

    const char *p = last_paren + 1;
    if (p < end && *p == ' ') p++;

    int tok = 0;
    unsigned long utime = 0, stime = 0;
    while (p < end && tok <= 12) {
        while (p < end && *p == ' ') p++;
        if (p >= end) break;
        const char *t = p;
        while (p < end && *p != ' ') p++;
        if (tok == 11) utime = atoul(t, p);
        else if (tok == 12) stime = atoul(t, p);
        tok++;
    }
    if (tok <= 12) return 0;

    info->cpu = utime + stime;
    return 1;
}

// Scan /proc/<pid>/status for the VmRSS line and convert kB to bytes.
// Kernel threads and some special tasks have no VmRSS; those return 0.
static unsigned long parse_status_ram(const char *buf, int len) {
    const char *end = buf + len;
    for (const char *p = buf; p < end; ) {
        if (p + 6 <= end && p[0] == 'V' && p[1] == 'm' &&
            p[2] == 'R' && p[3] == 'S' && p[4] == 'S' && p[5] == ':') {
            p += 6;
            while (p < end && (*p == ' ' || *p == '\t')) p++;
            unsigned long kb = atoul(p, end);
            return kb << 10; // kB -> bytes
        }
        while (p < end && *p != '\n') p++;
        if (p < end) p++;
    }
    return 0;
}

// Gather one process's stats and insert it into the top[3] leaderboard,
// sorted by cumulative CPU time. This measures "longest running since boot",
// not instantaneous load.
static void consider_process(struct procinfo *top, const char *name) {
    char stat_path[64];
    char status_path[64];
    int off;

    off = str_copy(stat_path, "/proc/", sizeof(stat_path));
    off += str_copy(stat_path + off, name, sizeof(stat_path) - off);
    str_copy(stat_path + off, "/stat", sizeof(stat_path) - off);

    char stat_buf[4096];
    int stat_len = read_file(stat_path, stat_buf, sizeof(stat_buf));
    if (stat_len <= 0) return;

    struct procinfo info = {0};
    if (!parse_stat(stat_buf, stat_len, &info)) return;

    off = str_copy(status_path, "/proc/", sizeof(status_path));
    off += str_copy(status_path + off, name, sizeof(status_path) - off);
    str_copy(status_path + off, "/status", sizeof(status_path) - off);

    char status_buf[4096];
    int status_len = read_file(status_path, status_buf, sizeof(status_buf));
    if (status_len > 0) {
        info.ram = parse_status_ram(status_buf, status_len);
    }

    for (int i = 0; i < 3; i++) {
        if (info.cpu > top[i].cpu) {
            for (int j = 2; j > i; j--) top[j] = top[j - 1];
            top[i] = info;
            break;
        }
    }
}

// ----------------------------------------------------------------------------
// C entry point called by the per-arch asm _start
// ----------------------------------------------------------------------------

int proc_main(void) {
    // Handshake: open /proc as a directory. O_DIRECTORY is required because
    // we will iterate it with getdents64 rather than reading file bytes.
    long proc_fd = sys_openat(AT_FDCWD, "/proc", O_RDONLY | O_DIRECTORY);
    if (proc_fd < 0) sys_exit(1);

    struct procinfo top[3];
    for (int i = 0; i < 3; i++) {
        top[i].pid = -1;
        top[i].name[0] = '\0';
        top[i].cpu = 0;
        top[i].ram = 0;
    }

    // Stream directory records in batches, stepping through each dirent by
    // its d_reclen value.
    char dirbuf[8192];
    long n;
    while ((n = sys_getdents64((int)proc_fd, dirbuf, sizeof(dirbuf))) > 0) {
        unsigned long pos = 0;
        while (pos < (unsigned long)n) {
            struct linux_dirent64 *de = (struct linux_dirent64 *)(dirbuf + pos);
            if (is_pid_dir(de->d_name)) {
                consider_process(top, de->d_name);
            }
            pos += de->d_reclen;
        }
    }

    sys_close((int)proc_fd);

    // Draw the ASCII table. We manually pad each column because there is no
    // printf in a freestanding binary.
    const long out = 1;
    write_str_left(out, "PID", 7);
    write_fill(out, ' ', 2);
    write_str_left(out, "NAME", 14);
    write_fill(out, ' ', 2);
    write_str_right(out, "CPU (ticks)", 16);
    write_fill(out, ' ', 2);
    write_str_right(out, "RAM (bytes)", 16);
    write_buf(out, "\n", 1);

    write_fill(out, '-', 7);
    write_fill(out, ' ', 2);
    write_fill(out, '-', 14);
    write_fill(out, ' ', 2);
    write_fill(out, '-', 16);
    write_fill(out, ' ', 2);
    write_fill(out, '-', 16);
    write_buf(out, "\n", 1);

    for (int i = 0; i < 3; i++) {
        if (top[i].pid < 0) continue;
        write_ulong_right(out, (unsigned long)top[i].pid, 7);
        write_fill(out, ' ', 2);
        write_str_left(out, top[i].name, 14);
        write_fill(out, ' ', 2);
        write_ulong_right(out, top[i].cpu, 16);
        write_fill(out, ' ', 2);
        write_ulong_right(out, top[i].ram, 16);
        write_buf(out, "\n", 1);
    }

    return 0;
}

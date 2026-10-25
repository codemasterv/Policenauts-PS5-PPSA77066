/*
 * app_shims.c — native-title (PPSA) only.
 *
 * Carried over from the Project Picori (TMC) PPSA, builds B1-B4. These names
 * are referenced by code paths that never run on PS5 (SDL's gtk
 * helper, the desktop update check, the crash backtrace), but resolving them
 * from the payload SDK's static libc.a would drag in its raw-syscall
 * (syscalls.o) and fork/exec (popen.o) objects, which belong to the payload
 * environment. Defining them here keeps those objects out of eboot.bin.
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * Directory-relative file calls (openat/unlinkat/fchmodat/utimensat) used by
 * libc++'s std::filesystem (remove_all in particular). The game libkernel
 * exports only the path-based calls, and libc.a's versions are raw payload
 * syscalls, so emulate them: remember the path behind each fd openat()
 * returns and resolve relative names against it.
 */
#define FD_TABLE 256
static char* sFdPath[FD_TABLE];
static pthread_mutex_t sFdLock = PTHREAD_MUTEX_INITIALIZER;

static int ResolveAt(int dirfd, const char* path, char* out, size_t n) {
    if (!path) {
        errno = EFAULT;
        return -1;
    }
    if (path[0] == '/' || dirfd == AT_FDCWD) {
        if (strlen(path) >= n) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(out, path);
        return 0;
    }
    int rc = -1;
    pthread_mutex_lock(&sFdLock);
    if (dirfd >= 0 && dirfd < FD_TABLE && sFdPath[dirfd]) {
        int w = snprintf(out, n, "%s/%s", sFdPath[dirfd], path);
        if (w >= 0 && (size_t)w < n)
            rc = 0;
        else
            errno = ENAMETOOLONG;
    } else {
        errno = EBADF;
    }
    pthread_mutex_unlock(&sFdLock);
    return rc;
}

int openat(int dirfd, const char* path, int flags, ...) {
    char full[PATH_MAX];
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    if (ResolveAt(dirfd, path, full, sizeof(full)) != 0)
        return -1;
    int fd = open(full, flags, mode);
    if (fd >= 0 && fd < FD_TABLE) {
        char* copy = strdup(full);
        pthread_mutex_lock(&sFdLock);
        free(sFdPath[fd]);
        sFdPath[fd] = copy;
        pthread_mutex_unlock(&sFdLock);
    }
    return fd;
}

int unlinkat(int dirfd, const char* path, int flag) {
    char full[PATH_MAX];
    if (ResolveAt(dirfd, path, full, sizeof(full)) != 0)
        return -1;
    return (flag & AT_REMOVEDIR) ? rmdir(full) : unlink(full);
}

int fchmodat(int dirfd, const char* path, mode_t mode, int flag) {
    char full[PATH_MAX];
    (void)flag;
    if (ResolveAt(dirfd, path, full, sizeof(full)) != 0)
        return -1;
    return chmod(full, mode);
}

int utimensat(int dirfd, const char* path, const struct timespec times[2], int flag) {
    char full[PATH_MAX];
    (void)flag;
    if (ResolveAt(dirfd, path, full, sizeof(full)) != 0)
        return -1;
    if (!times || times[0].tv_nsec == UTIME_NOW || times[1].tv_nsec == UTIME_NOW)
        return utimes(full, NULL);
    if (times[0].tv_nsec == UTIME_OMIT && times[1].tv_nsec == UTIME_OMIT)
        return 0;
    struct timeval tv[2];
    for (int i = 0; i < 2; i++) {
        tv[i].tv_sec = times[i].tv_sec;
        tv[i].tv_usec = times[i].tv_nsec == UTIME_OMIT ? 0 : times[i].tv_nsec / 1000;
    }
    return utimes(full, tv);
}

long pathconf(const char* path, int name) {
    (void)path;
    switch (name) {
        case _PC_NAME_MAX: return 255;
        case _PC_PATH_MAX: return PATH_MAX;
        default: errno = EINVAL; return -1;
    }
}

/* No symlinks or hard links on the app's data partition: emulate link() as
 * an exclusive copy (keeps its "fail with EEXIST" contract, which
 * rando_save.c relies on) and refuse the rest. */
int link(const char* from, const char* to) {
    int in = open(from, O_RDONLY);
    if (in < 0)
        return -1;
    int out = open(to, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (out < 0) {
        int e = errno;
        close(in);
        errno = e;
        return -1;
    }
    char buf[16384];
    ssize_t r;
    int rc = 0;
    while ((r = read(in, buf, sizeof(buf))) > 0) {
        if (write(out, buf, (size_t)r) != r) {
            rc = -1;
            break;
        }
    }
    if (r < 0)
        rc = -1;
    close(in);
    close(out);
    return rc;
}

int symlink(const char* from, const char* to) {
    (void)from;
    (void)to;
    errno = EPERM;
    return -1;
}

ssize_t readlink(const char* path, char* buf, size_t n) {
    (void)path;
    (void)buf;
    (void)n;
    errno = EINVAL; /* "not a symlink" */
    return -1;
}

/* No child processes in a title (SDL_process is compiled out; ImGui's
 * open-URL helper is never reached). */
pid_t fork(void) {
    errno = ENOSYS;
    return -1;
}

pid_t vfork(void) {
    errno = ENOSYS;
    return -1;
}

pid_t setsid(void) {
    errno = ENOSYS;
    return -1;
}

int getresuid(uid_t* r, uid_t* e, uid_t* s) {
    if (r) *r = 0;
    if (e) *e = 0;
    if (s) *s = 0;
    return 0;
}

int getresgid(gid_t* r, gid_t* e, gid_t* s) {
    if (r) *r = 0;
    if (e) *e = 0;
    if (s) *s = 0;
    return 0;
}

FILE* popen(const char* cmd, const char* mode) {
    (void)cmd;
    (void)mode;
    errno = ENOSYS;
    return NULL;
}

int pclose(FILE* f) {
    (void)f;
    errno = ENOSYS;
    return -1;
}

int backtrace(void** frames, int n) {
    (void)frames;
    (void)n;
    return 0;
}

char** backtrace_symbols(void* const* frames, int n) {
    (void)frames;
    (void)n;
    return NULL;
}

void backtrace_symbols_fd(void* const* frames, int n, int fd) {
    (void)frames;
    (void)n;
    (void)fd;
}

/* thread_local destructors. libc++abi references this weakly; the converter
 * needs every import resolvable, so provide it: a per-thread list run by a
 * pthread key destructor when the thread exits. */
typedef struct TlsDtor {
    void (*fn)(void*);
    void* obj;
    struct TlsDtor* next;
} TlsDtor;

static pthread_key_t sTlsKey;
static pthread_once_t sTlsOnce = PTHREAD_ONCE_INIT;

static void RunTlsDtors(void* head) {
    TlsDtor* d = (TlsDtor*)head;
    while (d) {
        TlsDtor* next = d->next;
        d->fn(d->obj);
        free(d);
        d = next;
    }
}

static void MakeTlsKey(void) {
    pthread_key_create(&sTlsKey, RunTlsDtors);
}

int __cxa_thread_atexit_impl(void (*fn)(void*), void* obj, void* dso) {
    (void)dso;
    pthread_once(&sTlsOnce, MakeTlsKey);
    TlsDtor* d = (TlsDtor*)malloc(sizeof(*d));
    if (!d)
        return -1;
    d->fn = fn;
    d->obj = obj;
    d->next = (TlsDtor*)pthread_getspecific(sTlsKey);
    pthread_setspecific(sTlsKey, d);
    return 0;
}

/* mkstemp: the console exports it only from libScePosixForWebKit; implement
 * it here so the title does not depend on that module. */
int mkstemp(char* tmpl) {
    static const char kChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    size_t len = tmpl ? strlen(tmpl) : 0;
    if (len < 6 || strcmp(tmpl + len - 6, "XXXXXX") != 0) {
        errno = EINVAL;
        return -1;
    }
    static unsigned long long sSeed;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    sSeed ^= ((unsigned long long)tv.tv_sec << 20) ^ (unsigned long long)tv.tv_usec ^ (unsigned long long)(uintptr_t)tmpl;
    for (int attempt = 0; attempt < 1000; attempt++) {
        for (int i = 0; i < 6; i++) {
            sSeed = sSeed * 6364136223846793005ULL + 1442695040888963407ULL;
            tmpl[len - 6 + i] = kChars[(sSeed >> 33) % (sizeof(kChars) - 1)];
        }
        int fd = open(tmpl, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0 || errno != EEXIST)
            return fd;
    }
    errno = EEXIST;
    return -1;
}

/* libSceKeyboard does not bind for a fake-signed title (its imports resolve
 * to NULL and the first call faults at rip=0; seen in the TMC PPSA B2).
 * Local definitions make SDL's PS5 keyboard driver report "no keyboard" and
 * keep the module out of the import table. DualSense input is unaffected. */
int sceKeyboardInit(void) { return -1; }
int sceKeyboardOpen(int user, int type, int index, void* param) {
    (void)user; (void)type; (void)index; (void)param;
    return -1;
}
int sceKeyboardReadState(int handle, void* state) {
    (void)handle; (void)state;
    return -1;
}
int sceKeyboardClose(int handle) {
    (void)handle;
    return 0;
}

/* ---------------------------------------------------------------------------
 * Working directory. A fake-signed title may not chdir() (EPERM) and the
 * console's getcwd() faults. Keep a virtual CWD here and, via ld --wrap
 * (ps5/package_app.sh), prefix it onto every relative path handed to the
 * file API. ps5_app_main.c points it at the data folder at startup.
 * ------------------------------------------------------------------------- */
#include <dirent.h>
#include <sys/statvfs.h>

static char sCwd[PATH_MAX] = "/";

static const char* AbsPath(const char* p, char* buf, size_t n) {
    if (!p || p[0] == '/' || p[0] == '\0')
        return p;
    if (p[0] == '.' && p[1] == '\0')
        return sCwd;
    if (p[0] == '.' && p[1] == '/')
        p += 2;
    size_t cl = strlen(sCwd);
    int w = snprintf(buf, n, "%s%s%s", sCwd, (cl && sCwd[cl - 1] == '/') ? "" : "/", p);
    return (w >= 0 && (size_t)w < n) ? buf : p;
}
#define ABS(p) AbsPath((p), abs_buf_, sizeof(abs_buf_))
#define ABSBUF char abs_buf_[PATH_MAX]

/* Collapse ".", ".." and duplicate slashes of an absolute path in place. */
static void Normalize(char* path) {
    char out[PATH_MAX];
    size_t o = 0;
    const char* s = path;
    out[o++] = '/';
    while (*s) {
        while (*s == '/')
            s++;
        if (!*s)
            break;
        const char* e = s;
        while (*e && *e != '/')
            e++;
        size_t len = (size_t)(e - s);
        if (len == 1 && s[0] == '.') {
            /* skip */
        } else if (len == 2 && s[0] == '.' && s[1] == '.') {
            /* drop the last component and the slash before it */
            while (o > 1 && out[o - 1] != '/')
                o--;
            if (o > 1)
                o--;
        } else if (o + len + 1 < sizeof(out)) {
            if (o > 1)
                out[o++] = '/';
            memcpy(out + o, s, len);
            o += len;
        }
        s = e;
    }
    if (o > 1 && out[o - 1] == '/')
        o--;
    out[o] = '\0';
    strcpy(path, out);
}

int __real_open(const char*, int, ...);
FILE* __real_fopen(const char*, const char*);
FILE* __real_freopen(const char*, const char*, FILE*);
int __real_stat(const char*, struct stat*);
int __real_lstat(const char*, struct stat*);
int __real_mkdir(const char*, mode_t);
int __real_rmdir(const char*);
int __real_unlink(const char*);
int __real_remove(const char*);
int __real_rename(const char*, const char*);
int __real_chmod(const char*, mode_t);
int __real_truncate(const char*, off_t);
int __real_utimes(const char*, const struct timeval*);
int __real_statvfs(const char*, struct statvfs*);
int __real_access(const char*, int);

int __wrap_open(const char* p, int flags, ...) {
    ABSBUF;
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    return __real_open(ABS(p), flags, mode);
}
FILE* __wrap_fopen(const char* p, const char* m) { ABSBUF; return __real_fopen(ABS(p), m); }
FILE* __wrap_freopen(const char* p, const char* m, FILE* f) { ABSBUF; return __real_freopen(p ? ABS(p) : NULL, m, f); }
int __wrap_stat(const char* p, struct stat* st) { ABSBUF; return __real_stat(ABS(p), st); }
int __wrap_lstat(const char* p, struct stat* st) { ABSBUF; return __real_lstat(ABS(p), st); }
int __wrap_mkdir(const char* p, mode_t m) { ABSBUF; return __real_mkdir(ABS(p), m); }
int __wrap_rmdir(const char* p) { ABSBUF; return __real_rmdir(ABS(p)); }
int __wrap_unlink(const char* p) { ABSBUF; return __real_unlink(ABS(p)); }
int __wrap_remove(const char* p) { ABSBUF; return __real_remove(ABS(p)); }
int __wrap_rename(const char* a, const char* b) {
    char ba[PATH_MAX], bb[PATH_MAX];
    return __real_rename(AbsPath(a, ba, sizeof(ba)), AbsPath(b, bb, sizeof(bb)));
}
int __wrap_chmod(const char* p, mode_t m) { ABSBUF; return __real_chmod(ABS(p), m); }
int __wrap_truncate(const char* p, off_t len) { ABSBUF; return __real_truncate(ABS(p), len); }
int __wrap_utimes(const char* p, const struct timeval* tv) { ABSBUF; return __real_utimes(ABS(p), tv); }
int __wrap_statvfs(const char* p, struct statvfs* st) { ABSBUF; return __real_statvfs(ABS(p), st); }
int __wrap_access(const char* p, int mode) { ABSBUF; return __real_access(ABS(p), mode); }

char* __wrap_getcwd(char* buf, size_t size) {
    size_t need = strlen(sCwd) + 1;
    if (!buf) {
        size_t n = size ? size : need;
        if (n < need) {
            errno = ERANGE;
            return NULL;
        }
        buf = (char*)malloc(n);
        if (!buf)
            return NULL;
    } else if (size < need) {
        errno = ERANGE;
        return NULL;
    }
    memcpy(buf, sCwd, need);
    return buf;
}

int __wrap_chdir(const char* p) {
    char full[PATH_MAX];
    ABSBUF;
    const char* a = ABS(p);
    if (!a || strlen(a) >= sizeof(full)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(full, a);
    Normalize(full);
    struct stat st;
    if (__real_stat(full, &st) != 0)
        return -1;
    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    strcpy(sCwd, full);
    return 0;
}

char* __wrap_realpath(const char* p, char* resolved) {
    char full[PATH_MAX];
    ABSBUF;
    if (!p) {
        errno = EINVAL;
        return NULL;
    }
    const char* a = ABS(p);
    if (strlen(a) >= sizeof(full)) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    strcpy(full, a);
    Normalize(full);
    struct stat st;
    if (__real_stat(full, &st) != 0)
        return NULL;
    if (!resolved) {
        resolved = (char*)malloc(PATH_MAX);
        if (!resolved)
            return NULL;
    }
    strcpy(resolved, full);
    return resolved;
}

/* Directory reading on the kernel's getdents() rather than the system
 * libSceLibcInternal (whose path calls faulted in the TMC title). Callers
 * must tolerate failure: on some firmware this returns EINVAL. */
int getdents(int fd, char* buf, int nbytes);

struct _dirdesc {
    int fd;
    int pos;
    int len;
    char buf[16384] __attribute__((aligned(8)));
};

DIR* fdopendir(int fd) {
    struct stat st;
    if (fstat(fd, &st) != 0)
        return NULL;
    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return NULL;
    }
    DIR* d = (DIR*)calloc(1, sizeof(*d));
    if (!d)
        return NULL;
    d->fd = fd;
    return d;
}

DIR* __wrap_opendir(const char* p) {
    ABSBUF;
    int fd = __real_open(ABS(p), O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0);
    if (fd < 0)
        return NULL;
    DIR* d = fdopendir(fd);
    if (!d) {
        int e = errno;
        close(fd);
        errno = e;
    }
    return d;
}

struct dirent* readdir(DIR* d) {
    if (!d) {
        errno = EBADF;
        return NULL;
    }
    for (;;) {
        if (d->pos >= d->len) {
            int n = getdents(d->fd, d->buf, (int)sizeof(d->buf));
            if (n <= 0)
                return NULL;
            d->len = n;
            d->pos = 0;
        }
        struct dirent* e = (struct dirent*)(d->buf + d->pos);
        if (e->d_reclen == 0) {
            d->pos = d->len;
            continue;
        }
        d->pos += e->d_reclen;
        if (e->d_fileno != 0)
            return e;
    }
}

int closedir(DIR* d) {
    if (!d) {
        errno = EBADF;
        return -1;
    }
    int rc = close(d->fd);
    free(d);
    return rc;
}

int dirfd(DIR* d) {
    return d ? d->fd : -1;
}

void rewinddir(DIR* d) {
    if (!d)
        return;
    lseek(d->fd, 0, SEEK_SET);
    d->pos = d->len = 0;
}

/* Called once at startup (ps5_app_main.c) to set the data folder. */
void ps5_set_virtual_cwd(const char* abs_dir) {
    if (abs_dir && abs_dir[0] == '/' && strlen(abs_dir) < sizeof(sCwd)) {
        strcpy(sCwd, abs_dir);
        Normalize(sCwd);
    }
}

/* Overlay autocompile (spawning gcc/tcc) is desktop-only; on PS5 every overlay
 * is pre-compiled or interpreted, so these are never reached. */
int nice(int incr) {
    (void)incr;
    return 0;
}

int execl(const char* path, const char* arg, ...) {
    (void)path;
    (void)arg;
    errno = ENOSYS;
    return -1;
}

/* Keep libkernel_sys and libScePosixForWebKit out of the import table (the
 * TMC title avoided both): setpgid is only used by the desktop overlay
 * compiler spawn; strcasestr is implemented here. */
int setpgid(pid_t pid, pid_t pgid) {
    (void)pid;
    (void)pgid;
    errno = ENOSYS;
    return -1;
}

char* strcasestr(const char* hay, const char* needle) {
    if (!*needle)
        return (char*)hay;
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        if (strncasecmp(hay, needle, n) == 0)
            return (char*)hay;
    }
    return NULL;
}

/* SDL's on-screen text input (IME dialog) is never used by the game; local
 * definitions keep libSceImeDialog out of the import table, the same way as
 * the keyboard library. SDL treats a failed init as "no text input". */
int sceImeDialogInit(const void* param, void* extended) {
    (void)param;
    (void)extended;
    return -1;
}
int sceImeDialogGetStatus(void) { return 0; }
int sceImeDialogGetResult(void* result) {
    (void)result;
    return -1;
}
int sceImeDialogTerm(void) { return 0; }

/* Environment. A native title has none, and the runtime queries getenv() for
 * dozens of debug/tuning switches from the first frame on; keep those lookups
 * off the system libSceLibcInternal (not covered by libc.prx, the same import
 * path where getcwd() faulted in the TMC title). Small private table. */
#define ENV_MAX 64
static char* sEnv[ENV_MAX];
static pthread_mutex_t sEnvLock = PTHREAD_MUTEX_INITIALIZER;

static int EnvFind(const char* name, size_t n) {
    for (int i = 0; i < ENV_MAX; i++)
        if (sEnv[i] && strncmp(sEnv[i], name, n) == 0 && sEnv[i][n] == '=')
            return i;
    return -1;
}

char* getenv(const char* name) {
    if (!name || !*name)
        return NULL;
    size_t n = strlen(name);
    pthread_mutex_lock(&sEnvLock);
    int i = EnvFind(name, n);
    char* v = i >= 0 ? sEnv[i] + n + 1 : NULL;
    pthread_mutex_unlock(&sEnvLock);
    return v;
}

int setenv(const char* name, const char* value, int overwrite) {
    if (!name || !*name || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }
    size_t n = strlen(name);
    pthread_mutex_lock(&sEnvLock);
    int i = EnvFind(name, n);
    if (i >= 0 && !overwrite) {
        pthread_mutex_unlock(&sEnvLock);
        return 0;
    }
    char* kv = (char*)malloc(n + strlen(value ? value : "") + 2);
    if (!kv) {
        pthread_mutex_unlock(&sEnvLock);
        errno = ENOMEM;
        return -1;
    }
    sprintf(kv, "%s=%s", name, value ? value : "");
    if (i < 0)
        for (i = 0; i < ENV_MAX && sEnv[i]; i++) {}
    if (i >= ENV_MAX) {
        pthread_mutex_unlock(&sEnvLock);
        free(kv);
        errno = ENOMEM;
        return -1;
    }
    /* Old strings are leaked on purpose: callers may still hold getenv()
     * pointers into them. */
    sEnv[i] = kv;
    pthread_mutex_unlock(&sEnvLock);
    return 0;
}

int unsetenv(const char* name) {
    if (!name || !*name)
        return 0;
    pthread_mutex_lock(&sEnvLock);
    int i = EnvFind(name, strlen(name));
    if (i >= 0)
        sEnv[i] = NULL;
    pthread_mutex_unlock(&sEnvLock);
    return 0;
}

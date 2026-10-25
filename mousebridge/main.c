/* mousebridge -- PS5 payload: gives homebrew native titles USB/Bluetooth
 * mouse access.
 *
 * A fake-signed title can load libSceMouse.sprx but cannot look up its
 * functions (dlsym -> 0x80020003, imports bind to NULL). This payload runs
 * resident with kernel access and does the lookup for it:
 *
 *   game:    loads libSceMouse, fills a mailbox in its own memory with
 *            MB_MAGIC, writes /data/mousebridge/request-<pid>.txt:
 *                "<pid> <module handle> <mailbox address hex> <elevate>"
 *   bridge:  resolves the mouse functions inside the game's process
 *            (kernel_dynlib_dlsym), writes them into the mailbox, optionally
 *            raises the game's caps (elevate 1) / authid (elevate 2), then sets
 *            the mailbox status. The game polls the status each frame.
 *
 * Send once per boot (elfldr, port 9021). It keeps running; a second copy
 * exits straight away. Log: /data/mousebridge/mousebridge.log
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <ps5/kernel.h>

#define MB_DIR "/data/mousebridge"
#define MB_MAGIC 0x3152424553554F4DULL /* "MOUSEBR1" */

/* Mailbox in the game's memory; the game declares the same layout. */
struct mb_mailbox {
    uint64_t magic;
    uint64_t fn[5];          /* init, open, read, close, setProcessPrivilege */
    int32_t status;          /* 0 waiting, >0 done (elevation level + 1), <0 error */
    int32_t pad;
};

static const char *const k_names[5] = {"sceMouseInit", "sceMouseOpen", "sceMouseRead",
                                       "sceMouseClose", "sceMouseSetProcessPrivilege"};
static const char *const k_nids[5] = {"Qs0wWulgl7U", "RaqxZIf6DvE", "x8qnXqh-tiM",
                                      "cAnT0Rw-IwU", "6aANndpS0Wo"};

int sceNotificationSend(int userId, bool isLogged, const char *payload);

static FILE *g_log;

static void logf_(const char *fmt, ...) {
    if (!g_log) return;
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    fprintf(g_log, "%02d:%02d:%02d ", tm.tm_hour, tm.tm_min, tm.tm_sec);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static void toast(const char *msg) {
    char buf[1024];
    snprintf(buf, sizeof(buf),
             "{\"rawData\":{\"viewTemplateType\":\"InteractiveToastTemplateB\","
             "\"channelType\":\"Downloads\",\"useCaseId\":\"IDC\","
             "\"toastOverwriteType\":\"No\",\"isImmediate\":true,\"priority\":100,"
             "\"viewData\":{\"message\":{\"body\":\"%s\"}},"
             "\"platformViews\":{\"previewDisabled\":{\"viewData\":{\"message\":{\"body\":\"%s\"}}}}},"
             "\"createdDateTime\":\"2026-10-25T00:00:00.000Z\",\"localNotificationId\":\"77066001\"}",
             msg, msg);
    sceNotificationSend(0xFE, true, buf);
}

static int set_status(pid_t pid, intptr_t mailbox, int32_t status) {
    return kernel_proc_copyin(pid, &status, mailbox + offsetof(struct mb_mailbox, status),
                              sizeof(status));
}

static void serve(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    int pid = 0;
    unsigned handle = 0, elevate = 0;
    unsigned long long mailbox = 0;
    const int n = fscanf(f, "%d %u %llx %u", &pid, &handle, &mailbox, &elevate);
    fclose(f);
    unlink(path);
    if (n < 3 || pid <= 0 || !mailbox) {
        logf_("bad request %s (%d fields)", path, n);
        return;
    }
    if (!kernel_get_proc(pid)) {
        logf_("pid %d gone; dropped", pid);
        return;
    }
    struct mb_mailbox mb;
    if (kernel_proc_copyout(pid, (intptr_t)mailbox, &mb, sizeof(mb)) || mb.magic != MB_MAGIC) {
        logf_("pid %d: no mailbox at 0x%llx (magic %016llx)", pid, mailbox,
              (unsigned long long)mb.magic);
        return;
    }
    logf_("pid %d: handle 0x%x mailbox 0x%llx elevate %u (authid %016lx)", pid, handle, mailbox,
          elevate, kernel_get_ucred_authid(pid));

    if (elevate >= 1) {
        static const uint8_t all[16] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        logf_("pid %d: caps -> all: %d", pid, kernel_set_ucred_caps(pid, all));
    }
    if (elevate >= 2)
        logf_("pid %d: authid -> 0x4800000000010003: %d", pid,
              kernel_set_ucred_authid(pid, 0x4800000000010003ULL));

    /* The game's handle is the one sceKernelLoadStartModule gave it; also ask
     * the kernel for the module's handle by name in case they differ. */
    uint32_t by_name = 0;
    const int hr = kernel_dynlib_handle(pid, "libSceMouse.sprx", &by_name);
    logf_("pid %d: handle by name -> %d, 0x%x", pid, hr, by_name);
    int found = 0;
    for (int i = 0; i < 5; i++) {
        intptr_t a = kernel_dynlib_dlsym(pid, handle, k_names[i]);
        if (!a) a = kernel_dynlib_resolve(pid, handle, k_nids[i]);
        if (!a && hr == 0 && by_name != handle) {
            a = kernel_dynlib_dlsym(pid, by_name, k_names[i]);
            if (!a) a = kernel_dynlib_resolve(pid, by_name, k_nids[i]);
        }
        mb.fn[i] = (uint64_t)a;
        if (a) found++;
        logf_("pid %d: %s = 0x%lx", pid, k_names[i], (long)a);
    }
    kernel_proc_copyin(pid, mb.fn, (intptr_t)mailbox + offsetof(struct mb_mailbox, fn),
                       sizeof(mb.fn));
    const int32_t status = (mb.fn[0] && mb.fn[1] && mb.fn[2]) ? (int32_t)elevate + 1 : -1;
    set_status(pid, (intptr_t)mailbox, status);
    logf_("pid %d: %d/5 resolved, status %d", pid, found, status);
    if (status > 0 && elevate == 0) toast("Mouse bridge: game linked");
    if (status < 0) toast("Mouse bridge: could not resolve libSceMouse");
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    mkdir(MB_DIR, 0777);
    chmod(MB_DIR, 0777);
    const int lock = open(MB_DIR "/lock", O_CREAT | O_RDWR, 0666);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) != 0) {
        toast("Mouse bridge already running");
        return 0;
    }
    g_log = fopen(MB_DIR "/mousebridge.log", "a");
    logf_("mousebridge started (fw %08x, pid %d)", kernel_get_fw_version(), getpid());
    toast("Mouse bridge running");

    for (;;) {
        DIR *d = opendir(MB_DIR);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                if (strncmp(e->d_name, "request-", 8) != 0) continue;
                char path[300];
                snprintf(path, sizeof(path), MB_DIR "/%s", e->d_name);
                serve(path);
            }
            closedir(d);
        }
        usleep(250 * 1000);
    }
}

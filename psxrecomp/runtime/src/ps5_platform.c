/*
 * ps5_platform.c -- PS5 homebrew startup (native PPSA title or payload).
 *
 * Called first thing from main() on __PROSPERO__. A PS5 title starts with no
 * usable working directory, no console and the system splash screen up, so:
 *   - make the per-game data folder (PSX_PS5_DATA_DIR) and work from it
 *     (chdir() is refused in a fake-signed title; app_shims.c then provides a
 *     virtual one through ps5_set_virtual_cwd()),
 *   - send stdout/stderr to <data>/psx_ps5.log (truncated each launch),
 *   - write the game's default game.toml / .cue files there if missing,
 *   - hide the splash screen,
 *   - and, when launched without arguments (always, for a home-screen title),
 *     supply --game/--disc/--renderer software/--no-launcher.
 */
#if defined(__PROSPERO__)

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PSX_PS5_DATA_DIR
#define PSX_PS5_DATA_DIR "/data/psxrecomp"
#endif

int sceSystemServiceHideSplashScreen(void);
int sceSystemServiceLoadExec(const char* path, const char* const* argv);

/* Returning from main() in a native title ends in an exit call the system
 * rejects (SIGSYS, PPSA77066 B2). Registered first, this runs last among the
 * atexit handlers and hands the app back to the shell, like the PS5 SDL2
 * port's own main() wrapper does. */
static void ps5_exit_to_shell(void) {
    fflush(stdout);
    fflush(stderr);
    sceSystemServiceLoadExec("exit", NULL);
}

/* Native-title build only (ps5/app_shims.c). */
void ps5_set_virtual_cwd(const char* abs_dir) __attribute__((weak));

/* Provided by the game's PS5 glue: files to create in the data folder when
 * absent (NULL-terminated name/contents pairs), and the disc to boot. */
extern const char* const psx_ps5_default_files[] __attribute__((weak));
extern const char* const psx_ps5_boot_disc __attribute__((weak));

/* Binary files to create when absent (bundled OpenBIOS). Terminated by a
 * NULL name. */
struct psx_ps5_blob { const char* name; const unsigned char* data; unsigned long size; };
extern const struct psx_ps5_blob psx_ps5_default_blobs[] __attribute__((weak));

static char s_data_dir[256];

const char* psx_ps5_data_dir(void) {
    return s_data_dir[0] ? s_data_dir : PSX_PS5_DATA_DIR;
}

static int file_exists(const char* p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static void mkdirs(const char* path) {
    char tmp[256];
    size_t n = strlen(path);
    if (n >= sizeof(tmp))
        return;
    memcpy(tmp, path, n + 1);
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

static void write_blob(const char* name, const void* data, size_t size) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", s_data_dir, name);
    struct stat st;
    if (stat(path, &st) == 0 && (size_t)st.st_size == size)
        return;
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", path);
    char* slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdirs(dir);
    }
    FILE* out = fopen(path, "wb");
    if (!out || fwrite(data, 1, size, out) != size) {
        fprintf(stderr, "[ps5] cannot write %s: %s\n", path, strerror(errno));
        if (out)
            fclose(out);
        return;
    }
    fclose(out);
    fprintf(stderr, "[ps5] wrote %s (%lu bytes)\n", path, (unsigned long)size);
}

static void write_default_files(void) {
    if (&psx_ps5_default_blobs)
        for (const struct psx_ps5_blob* b = psx_ps5_default_blobs; b->name; b++)
            write_blob(b->name, b->data, b->size);
    if (!&psx_ps5_default_files)
        return;
    for (const char* const* f = psx_ps5_default_files; f[0] && f[1]; f += 2) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", s_data_dir, f[0]);
        if (file_exists(path))
            continue;
        char dir[512];
        snprintf(dir, sizeof(dir), "%s", path);
        char* slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            mkdirs(dir);
        }
        FILE* out = fopen(path, "wb");
        if (!out) {
            fprintf(stderr, "[ps5] cannot create %s: %s\n", path, strerror(errno));
            continue;
        }
        fputs(f[1], out);
        fclose(out);
        fprintf(stderr, "[ps5] wrote default %s\n", path);
    }
}

void psx_ps5_startup(int* argc, char*** argv) {
    atexit(ps5_exit_to_shell);
    snprintf(s_data_dir, sizeof(s_data_dir), "%s", PSX_PS5_DATA_DIR);
    mkdirs(s_data_dir);

    char log_path[300];
    snprintf(log_path, sizeof(log_path), "%s/psx_ps5.log", s_data_dir);
    /* Both streams into the log. dup2() onto stdout's descriptor did not
     * carry stdout (printf) into the file on the console (B3 log had only
     * stderr lines), so reopen each stream; append mode keeps the two
     * writers from overwriting each other. */
    FILE* trunc = fopen(log_path, "w");
    if (trunc)
        fclose(trunc);
    if (freopen(log_path, "a", stderr))
        setvbuf(stderr, NULL, _IOLBF, BUFSIZ);
    if (freopen(log_path, "a", stdout))
        setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
    fprintf(stderr, "[ps5] psxrecomp starting; data folder %s\n", s_data_dir);

    if (chdir(s_data_dir) != 0) {
        fprintf(stderr, "[ps5] chdir(%s): %s%s\n", s_data_dir, strerror(errno),
                ps5_set_virtual_cwd ? " (using virtual cwd)" : "");
        if (ps5_set_virtual_cwd)
            ps5_set_virtual_cwd(s_data_dir);
    }
    mkdirs("saves");
    /* No /tmp for a title: std::filesystem::temp_directory_path() (the mod
     * media cache) throws without TMPDIR pointing at a real folder. */
    char tmp_dir[300];
    snprintf(tmp_dir, sizeof(tmp_dir), "%s/tmp", s_data_dir);
    mkdirs(tmp_dir);
    setenv("TMPDIR", tmp_dir, 1);
    /* FPS readout (on-screen status + [FPS] log lines) and the engine's
     * per-section timing were on by default during bring-up; both are off now
     * (B8: steady 60 fps). Options + L3 shows the readout; setting
     * PSX_RUNTIME_PERF_DIAG=1 here brings the timing back. */
    write_default_files();

    sceSystemServiceHideSplashScreen();

    if (*argc > 1)
        return; /* payload launched with explicit arguments */
    static char* args[16];
    int n = 0;
    args[n++] = (*argc > 0 && (*argv)[0]) ? (*argv)[0] : (char*)"psx-runtime";
    args[n++] = (char*)"--game";
    args[n++] = (char*)"game.toml";
    if (&psx_ps5_boot_disc && psx_ps5_boot_disc) {
        args[n++] = (char*)"--disc";
        args[n++] = (char*)psx_ps5_boot_disc;
    }
    args[n++] = (char*)"--renderer";
    args[n++] = (char*)"software";
    args[n++] = (char*)"--no-launcher";
    args[n] = NULL;
    *argc = n;
    *argv = args;
    if (&psx_ps5_boot_disc && psx_ps5_boot_disc && !file_exists(psx_ps5_boot_disc))
        fprintf(stderr, "[ps5] disc image missing: %s/%s\n", s_data_dir, psx_ps5_boot_disc);
}

#endif /* __PROSPERO__ */

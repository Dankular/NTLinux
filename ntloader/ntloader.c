/*
 * ntloader - PE executable dispatcher for NTLinux.
 *
 * docs/ARCHITECTURE.md section 4 lists ntloader's eventual responsibilities
 * as everything from PE header parsing through PEB/TEB construction and
 * ntdll init. This implementation is deliberately Generation 1 in the
 * migration table (ARCHITECTURE.md section 3.2): it reads just enough of
 * the PE header to route the binary correctly, establishes a per-application
 * NT environment (ARCHITECTURE.md section 37), and hands actual loading off
 * to Wine. Wine already owns PE loading, PEB/TEB construction, and ntdll
 * (CLAUDE.md Rule 1) - re-deriving that here would be exactly the kind of
 * unnecessary reimplementation the project's rules argue against.
 *
 * Registered as a binfmt_misc interpreter (see
 * distro/rootfs/usr/lib/binfmt.d/ntlinux-pe.conf) so that `./game.exe`
 * dispatches through here automatically. Kernel binfmt_misc calling
 * convention (without the 'C'/'O' flags this project uses): argv[1] is the
 * resolved path to the target binary, argv[2..] are the original arguments
 * the binary was invoked with.
 *
 * Later generations move PE loading, wineserver traffic, and NT object/wait
 * semantics into libntabi/ntd (ARCHITECTURE.md sections 3.2, Phases 1-3).
 * ntloader's job then becomes routing to *those* facilities instead of (or
 * in addition to) Wine - the per-app environment and dispatch logic here
 * should still apply.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <inttypes.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define NTLOADER_VERSION "0.1.0"

/* IMAGE_FILE_HEADER.Machine values we care about (winnt.h). */
#define IMAGE_FILE_MACHINE_I386  0x014c
#define IMAGE_FILE_MACHINE_AMD64 0x8664
#define IMAGE_FILE_MACHINE_ARM64 0xaa64

struct pe_info {
    uint16_t machine;
    uint16_t subsystem; /* IMAGE_SUBSYSTEM_* from the optional header */
    int is_pe32plus;    /* optional header magic == PE32+ (0x20b) */
};

static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "ntloader: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(127);
}

/* snprintf wrapper that treats truncation as a hard error instead of
 * silently producing a shortened path - PATH_MAX-sized buffers make actual
 * truncation vanishingly unlikely, but a silently-truncated path used to
 * create/exec things is exactly the kind of bug worth refusing to run
 * rather than guessing about. */
static void xsnprintf(char *buf, size_t bufsz, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, bufsz, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= bufsz)
        die("internal error: path too long (%s...)", buf);
}

/* Read and validate just enough of the PE header to route the binary:
 * DOS stub -> e_lfanew -> "PE\0\0" signature -> IMAGE_FILE_HEADER.Machine
 * -> IMAGE_OPTIONAL_HEADER.Magic/Subsystem. This is intentionally shallow;
 * Wine performs the real, complete PE load (section table, imports,
 * relocations, TLS, ...). We only need enough to pick an architecture and
 * fail fast on something that isn't a PE binary at all. */
static int read_pe_info(const char *path, struct pe_info *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    unsigned char dos[64];
    if (fread(dos, 1, sizeof(dos), f) != sizeof(dos)) { fclose(f); return -1; }
    if (dos[0] != 'M' || dos[1] != 'Z') { fclose(f); return -1; }

    int32_t e_lfanew;
    memcpy(&e_lfanew, &dos[0x3c], sizeof(e_lfanew));
    if (e_lfanew < 0) { fclose(f); return -1; }

    if (fseek(f, e_lfanew, SEEK_SET) != 0) { fclose(f); return -1; }
    unsigned char pe_sig[4];
    if (fread(pe_sig, 1, 4, f) != 4) { fclose(f); return -1; }
    if (memcmp(pe_sig, "PE\0\0", 4) != 0) { fclose(f); return -1; }

    unsigned char file_header[20];
    if (fread(file_header, 1, sizeof(file_header), f) != sizeof(file_header)) {
        fclose(f); return -1;
    }
    uint16_t machine;
    memcpy(&machine, &file_header[0], sizeof(machine));

    unsigned char opt_magic[2];
    long opt_start = ftell(f);
    int is_pe32plus = 0;
    uint16_t subsystem = 0;
    if (fread(opt_magic, 1, 2, f) == 2) {
        uint16_t magic;
        memcpy(&magic, opt_magic, sizeof(magic));
        is_pe32plus = (magic == 0x20b);
        /* Subsystem is at a fixed offset from the start of the optional
         * header for both PE32 (offset 68) and PE32+ (offset 68) layouts. */
        if (fseek(f, opt_start + 68 - 2, SEEK_SET) == 0) {
            unsigned char sub[2];
            if (fread(sub, 1, 2, f) == 2) memcpy(&subsystem, sub, sizeof(subsystem));
        }
    }

    fclose(f);
    out->machine = machine;
    out->subsystem = subsystem;
    out->is_pe32plus = is_pe32plus;
    return 0;
}

static const char *machine_name(uint16_t m) {
    switch (m) {
        case IMAGE_FILE_MACHINE_I386:  return "i386";
        case IMAGE_FILE_MACHINE_AMD64: return "amd64";
        case IMAGE_FILE_MACHINE_ARM64: return "arm64";
        default: return "unknown";
    }
}

/* FNV-1a 64-bit, used only to make a short, stable, collision-resistant
 * suffix for the per-app directory name - not a security boundary. */
static uint64_t fnv1a64(const char *s) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 0x100000001b3ULL;
    }
    return h;
}

static char *xdg_data_home(void) {
    const char *xdg = getenv("XDG_DATA_HOME");
    char *out = malloc(PATH_MAX);
    if (!out) die("out of memory");
    if (xdg && *xdg) {
        xsnprintf(out, PATH_MAX, "%s", xdg);
    } else {
        const char *home = getenv("HOME");
        if (!home) die("HOME is not set and XDG_DATA_HOME is not set");
        xsnprintf(out, PATH_MAX, "%s/.local/share", home);
    }
    return out;
}

static int mkdir_p(const char *path) {
    char tmp[PATH_MAX];
    xsnprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len == 0) return -1;
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

/* ARCHITECTURE.md section 37's per-app layout:
 *   ~/.local/share/ntlinux/apps/<id>/
 *   |-- drive-c/       (symlink to prefix/drive_c - Wine owns the real tree)
 *   |-- registry/      (reserved; Wine's own *.reg files live in prefix/)
 *   |-- config.toml    (simple KEY=VALUE overrides, see load_config)
 *   |-- compat/         (reserved for Phase-36 compatibility profiles)
 *   |-- dll-overrides/  (reserved; WINEDLLOVERRIDES-style overrides.conf)
 *   |-- state/          (reserved for future ntd-managed state)
 *   `-- prefix/          (the actual WINEPREFIX Wine manages)
 */
static void ensure_app_dirs(const char *appdir) {
    char path[PATH_MAX];
    const char *subdirs[] = {"", "registry", "compat", "dll-overrides", "state", "prefix"};
    for (size_t i = 0; i < sizeof(subdirs) / sizeof(subdirs[0]); i++) {
        if (subdirs[i][0])
            xsnprintf(path, sizeof(path), "%s/%s", appdir, subdirs[i]);
        else
            xsnprintf(path, sizeof(path), "%s", appdir);
        if (mkdir_p(path) != 0)
            die("failed to create %s: %s", path, strerror(errno));
    }

    char drive_c[PATH_MAX], link_target[PATH_MAX];
    xsnprintf(drive_c, sizeof(drive_c), "%s/drive-c", appdir);
    xsnprintf(link_target, sizeof(link_target), "prefix/drive_c");
    struct stat st;
    if (lstat(drive_c, &st) != 0) {
        /* Symlink target is relative and won't exist until Wine's first
         * run creates prefix/drive_c - that's fine, symlink() doesn't
         * require the target to exist yet. */
        if (symlink(link_target, drive_c) != 0 && errno != EEXIST)
            die("failed to create %s: %s", drive_c, strerror(errno));
    }
}

/* Minimal KEY=VALUE reader for config.toml. Not a real TOML parser - Gen1
 * scope is a couple of flat overrides (wine_binary, windows_version). A
 * real implementation should replace this once ntd owns app config
 * (ARCHITECTURE.md section 37) rather than growing an ad hoc parser here. */
struct app_config {
    char wine_binary[64];
    char windows_version[32];
};

static void load_config(const char *appdir, struct app_config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    xsnprintf(cfg->wine_binary, sizeof(cfg->wine_binary), "wine");

    char path[PATH_MAX];
    xsnprintf(path, sizeof(path), "%s/config.toml", appdir);
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line, *val = eq + 1;
        while (*key == ' ' || *key == '\t') key++;
        while (*val == ' ' || *val == '\t' || *val == '"') val++;
        size_t vlen = strlen(val);
        while (vlen > 0 && (val[vlen-1] == ' ' || val[vlen-1] == '\t' ||
                             val[vlen-1] == '"' || val[vlen-1] == '\r'))
            val[--vlen] = '\0';
        if (strcmp(key, "wine_binary") == 0)
            xsnprintf(cfg->wine_binary, sizeof(cfg->wine_binary), "%s", val);
        else if (strcmp(key, "windows_version") == 0)
            xsnprintf(cfg->windows_version, sizeof(cfg->windows_version), "%s", val);
    }
    fclose(f);
}

/* dll-overrides/overrides.conf: one "dllname=mode" per line, mode being
 * any value WINEDLLOVERRIDES accepts (native, builtin, native,builtin, ...).
 * Collapsed into a single WINEDLLOVERRIDES value the way `wine` expects:
 * "dll1,dll2=mode;dll3=mode". */
static char *load_dll_overrides(const char *appdir) {
    char path[PATH_MAX];
    xsnprintf(path, sizeof(path), "%s/dll-overrides/overrides.conf", appdir);
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    size_t cap = 256, len = 0;
    char *out = malloc(cap);
    if (!out) die("out of memory");
    out[0] = '\0';

    char line[256];
    int first = 1;
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;
        size_t linelen = strlen(line);
        if (len + linelen + 2 > cap) {
            cap = (len + linelen + 2) * 2;
            out = realloc(out, cap);
            if (!out) die("out of memory");
        }
        if (!first) { strcat(out, ";"); len += 1; }
        strcat(out, line);
        len += linelen;
        first = 0;
    }
    fclose(f);
    if (out[0] == '\0') { free(out); return NULL; }
    return out;
}

int main(int argc, char *argv[]) {
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        printf("ntloader %s\n", NTLOADER_VERSION);
        return 0;
    }
    if (argc < 2)
        die("usage: ntloader <path-to-pe-binary> [args...] "
            "(normally invoked by binfmt_misc, not directly)");

    const char *target = argv[1];
    char resolved[PATH_MAX];
    if (!realpath(target, resolved))
        die("cannot resolve path %s: %s", target, strerror(errno));

    struct pe_info info;
    if (read_pe_info(resolved, &info) != 0)
        die("%s is not a valid PE binary (no MZ/PE signature)", resolved);

    if (info.machine != IMAGE_FILE_MACHINE_I386 &&
        info.machine != IMAGE_FILE_MACHINE_AMD64)
        die("%s: unsupported machine type 0x%04x (%s) - only i386/amd64 "
            "are handled in this Phase 1 implementation",
            resolved, info.machine, machine_name(info.machine));

    /* App id: basename + short hash of the resolved path, so re-running
     * the same binary from the same location reuses its environment, and
     * two different "game.exe" in different directories don't collide. */
    const char *base = strrchr(resolved, '/');
    base = base ? base + 1 : resolved;
    uint64_t h = fnv1a64(resolved);

    char data_home[PATH_MAX];
    char *xdg = xdg_data_home();
    xsnprintf(data_home, sizeof(data_home), "%s", xdg);
    free(xdg);

    char appdir[PATH_MAX];
    xsnprintf(appdir, sizeof(appdir), "%s/ntlinux/apps/%s-%016" PRIx64,
              data_home, base, h);

    ensure_app_dirs(appdir);

    struct app_config cfg;
    load_config(appdir, &cfg);

    char prefix[PATH_MAX];
    xsnprintf(prefix, sizeof(prefix), "%s/prefix", appdir);

    char *dll_overrides = load_dll_overrides(appdir);

    fprintf(stderr, "ntloader: %s (%s, %s) -> app %s\n",
            resolved, machine_name(info.machine),
            info.is_pe32plus ? "PE32+" : "PE32", appdir);

    /* Build argv for wine: wine <target> <original argv[2..]> */
    int wine_argc = argc - 1; /* drop ntloader's own argv[0] */
    char **wine_argv = malloc(sizeof(char *) * (wine_argc + 2));
    if (!wine_argv) die("out of memory");
    wine_argv[0] = cfg.wine_binary;
    wine_argv[1] = resolved;
    for (int i = 2; i < argc; i++) wine_argv[i] = argv[i];
    wine_argv[wine_argc + 1] = NULL;

    setenv("WINEPREFIX", prefix, 1);
    if (dll_overrides)
        setenv("WINEDLLOVERRIDES", dll_overrides, 1);
    /* cfg.windows_version is parsed but not yet applied - wiring it to the
     * prefix's Windows-version registry key (what `winecfg` sets) needs a
     * `wine reg add` pass on first run, which is more machinery than this
     * Gen1 dispatcher owns. Left as a documented config.toml field for a
     * follow-up rather than silently ignored. */

    execvp(cfg.wine_binary, wine_argv);
    /* execvp only returns on failure. */
    die("failed to exec '%s': %s (is Wine installed?)",
        cfg.wine_binary, strerror(errno));
    return 127;
}

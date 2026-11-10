#include "gui.h"
#include "../fs/ntmem_fs.h"

/* ==================================================================
 * Рабочий стол теперь управляется ДАННЫМИ, а не жёстким кодом:
 *   1) читает /etc/desktop.cfg (встроенные приложения KERNEL://...)
 *   2) автоматически находит все ELF в /bin и добавляет их ярлыками
 * Таким образом новые приложения добавляются БЕЗ пересборки ядра —
 * достаточно положить ELF в /bin и пересобрать initrd.
 * ================================================================== */

#define MAX_SHORTCUTS 32

desktop_shortcut_t g_desktop_shortcuts[MAX_SHORTCUTS];
int g_desktop_shortcut_count = 0;

/* Хранилище строк (указатели в ярлыках ссылаются сюда) */
static char g_names[MAX_SHORTCUTS][64];
static char g_execs[MAX_SHORTCUTS][96];
static char g_icons[MAX_SHORTCUTS][8];

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int starts_with(const char *s, const char *p) {
    while (*p) { if (*s++ != *p++) return 0; }
    return 1;
}

static int ends_with(const char *s, const char *suffix) {
    int sl = 0, pl = 0;
    while (s[sl]) sl++;
    while (suffix[pl]) pl++;
    if (pl > sl) return 0;
    for (int i = 0; i < pl; i++)
        if (s[sl - pl + i] != suffix[i]) return 0;
    return 1;
}

static uint32_t parse_color(const char *s) {
    uint32_t v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s) {
        char c = *s++;
        int d = (c >= '0' && c <= '9') ? c - '0' :
                (c >= 'a' && c <= 'f') ? c - 'a' + 10 :
                (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (d < 0) break;
        v = (v << 4) | (uint32_t)d;
    }
    return v;
}

static void add_shortcut(const char *name, const char *exec, const char *icon, uint32_t color) {
    if (g_desktop_shortcut_count >= MAX_SHORTCUTS) return;
    int i = g_desktop_shortcut_count;
    str_copy(g_names[i], name, 64);
    str_copy(g_execs[i], exec, 96);
    str_copy(g_icons[i], icon, 8);
    g_desktop_shortcuts[i].name       = g_names[i];
    g_desktop_shortcuts[i].exec_path  = g_execs[i];
    g_desktop_shortcuts[i].icon_text  = g_icons[i];
    g_desktop_shortcuts[i].icon_color = color;
    g_desktop_shortcut_count++;
}

/* Простой парсер /etc/desktop.cfg: NAME | EXEC | ICON | COLOR */
static void load_desktop_cfg(void) {
    ntmem_file_t *f = ntmem_fs_find("etc/desktop.cfg");
    if (!f) return;

    char line[128];
    int li = 0;

    for (uint64_t i = 0; i <= f->size; i++) {
        char c = (i < f->size) ? (char)f->data[i] : '\n';
        if (c == '\n' || c == '\r') {
            if (li > 0) {
                line[li] = 0;
                char *name = line;
                while (*name == ' ' || *name == '\t') name++;
                if (*name && *name != '#') {
                    /* NAME | EXEC | ICON | COLOR — делим на 4 поля и триммим */
                    char *fields[4] = { name, 0, 0, 0 };
                    int fi = 0;
                    for (char *p = name; *p && fi < 3; p++) {
                        if (*p == '|') { *p = 0; fields[++fi] = p + 1; }
                    }
                    for (int f = 0; f < 4 && fields[f]; f++) {
                        char *s = fields[f];
                        while (*s == ' ' || *s == '\t') s++;
                        fields[f] = s;
                        char *e = s;
                        while (*e) e++;
                        while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
                    }
                    if (fields[1] && fields[2]) {
                        add_shortcut(fields[0], fields[1], fields[2],
                                     parse_color(fields[3] ? fields[3] : "0"));
                    }
                }
            }
            li = 0;
            continue;
        }
        if (li < 127) line[li++] = c;
    }
}

/* Автообнаружение приложений в /bin */
static void discover_bin_apps(void) {
    static const uint32_t palette[] = {
        0x0044DDFF, 0x00FF66AA, 0x0088FF44, 0x00CC66FF,
        0x00FFCC00, 0x0066FFCC, 0x00FF8833, 0x0033CCFF,
    };
    int n = ntmem_fs_count();
    int color_idx = 0;

    for (int i = 0; i < n; i++) {
        ntmem_file_t *f = ntmem_fs_get(i);
        if (!f) continue;
        if (!starts_with(f->name, "bin/")) continue;
        if (!ends_with(f->name, ".elf")) continue;

        const char *base = f->name + 4; /* пропускаем "bin/" */
        char name[64];
        int k = 0;
        while (base[k] && k < 63) { name[k] = base[k]; k++; }
        name[k] = 0;

        /* убираем ".elf" */
        if (k >= 4 && name[k-4] == '.' && name[k-3] == 'e' &&
            name[k-2] == 'l' && name[k-1] == 'f') name[k-4] = 0;

        if (name[0] >= 'a' && name[0] <= 'z') name[0] -= 32;

        add_shortcut(name, f->name, "EXE", palette[color_idx % 8]);
        color_idx++;
    }
}

void desktop_init(void) {
    g_desktop_shortcut_count = 0;
    load_desktop_cfg();
    discover_bin_apps();
}
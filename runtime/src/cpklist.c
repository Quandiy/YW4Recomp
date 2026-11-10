/* Mods' loose files in place of files inside the game's .cpk archives (cpklist.h).
 *
 * data/cpk_list.cfg.bin (a Level-5 cfg.bin) lists every file the game loads: its path, the .cpk it is in ("" or no
 * string for a loose file) and its size. The game opens a file from its archive whenever the list names one, so a
 * mod's loose data/common/chr/... next to chara.cpk is never read. Here such entries are pointed at the loose file:
 * no archive, the loose file's size. The list's entries: crc32 of the entry name, a value count, 2-bit value types
 * (0 string offset into the table after the entries, 1 int), the values. */
#include "cpklist.h"

#include <stdio.h>
#include <string.h>

static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }

#define NO_STRING 0xFFFFFFFFu

/* Calls fn(path, archive or NULL, value address of archive, value address of size) for each (string, string, int)
 * entry; stops early when fn returns nonzero. -1 when the list isn't laid out as expected. */
static int walk(uint8_t *list, uint64_t size, int (*fn)(const char *, const char *, uint8_t *, uint8_t *, void *),
                void *arg) {
    if (size < 0x10) return -1;
    uint32_t n = get32(list), str_off = get32(list + 4);
    if (str_off > size) return -1;
    uint64_t pos = 0x10;
    for (uint32_t i = 0; i < n; i++) {
        if (pos + 5 > str_off) return -1;
        unsigned cnt = list[pos + 4];
        uint64_t vals = (pos + 5 + (cnt + 3) / 4 + 3) & ~3ull;
        if (vals + 4ull * cnt > str_off) return -1;
        if (cnt >= 3 && (list[pos + 5] & 0x3F) == 0x10) {  /* string, string, int */
            uint32_t a = get32(list + vals), b = get32(list + vals + 4);
            uint64_t pa = (uint64_t)str_off + a, pb = (uint64_t)str_off + b;
            if (a != NO_STRING && pa < size && memchr(list + pa, 0, (size_t)(size - pa))) {
                const char *archive = b != NO_STRING && pb < size && memchr(list + pb, 0, (size_t)(size - pb))
                                          ? (const char *)list + pb : NULL;
                if (fn((const char *)list + pa, archive, list + vals + 4, list + vals + 8, arg)) return 0;
            }
        }
        pos = vals + 4ull * cnt;
    }
    return 0;
}

static int find_empty(const char *path, const char *archive, uint8_t *archive_value, uint8_t *size_value, void *arg) {
    (void)path, (void)size_value;
    if (archive && !*archive) {
        *(uint32_t *)arg = get32(archive_value);
        return 1;
    }
    return 0;
}

typedef struct {
    int (*loose)(const char *path, uint64_t *size, void *arg);
    void *arg;
    uint32_t empty;  /* how this list says "no archive": an empty string's offset, or no string */
    int redirected, shown;
} Redirect;

static int redirect_one(const char *path, const char *archive, uint8_t *archive_value, uint8_t *size_value, void *arg) {
    Redirect *r = arg;
    uint64_t size;
    if (!archive || !*archive || !r->loose(path, &size, r->arg) || size > 0xFFFFFFFFu) return 0;
    if (r->shown++ < 8) fprintf(stderr, "yw4: mods: %s from the mod, not from %s\n", path, archive);
    put32(archive_value, r->empty);
    put32(size_value, (uint32_t)size);
    r->redirected++;
    return 0;
}

int cpklist_redirect(uint8_t *list, uint64_t size, int (*loose)(const char *path, uint64_t *size, void *arg), void *arg) {
    Redirect r = {loose, arg, NO_STRING, 0, 0};
    if (walk(list, size, find_empty, &r.empty) < 0 || walk(list, size, redirect_one, &r) < 0) return -1;
    return r.redirected;
}

typedef struct {
    const char *path;
    uint32_t size;
    int done;
} SetSize;

static int set_size(const char *path, const char *archive, uint8_t *archive_value, uint8_t *size_value, void *arg) {
    SetSize *s = arg;
    (void)archive_value;
    if ((archive && *archive) || strcmp(path, s->path)) return 0;
    put32(size_value, s->size);
    s->done = 1;
    return 1;
}

int cpklist_set_size(uint8_t *list, uint64_t size, const char *path, uint32_t new_size) {
    SetSize s = {path, new_size, 0};
    return walk(list, size, set_size, &s) < 0 || !s.done ? -1 : 0;
}

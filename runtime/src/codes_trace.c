/* YW4_TRACE_CODES=1: what the game does with a typed password (codes.h; debugging only).
 *
 * After the password keyboard answers (swkbd.c), the next calls of game functions are watched: an argument (x0..x7)
 * that is the CRC-32 of the text in some form (as typed, capitals, small letters, UTF-16, full-width) or one of the
 * password list's hashes, or that points at the text in some form, is printed with the function and its caller. */
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codes.h"
#include "host.h"
#include "yw4rt.h"

extern void (*yw4_entry_hook)(CPUState *ctx, uint64_t addr);  /* dispatch.c */

enum { FORMS = 8, MAXLEN = 64 };
static struct {
    const char *what;
    uint8_t bytes[MAXLEN * 3];
    size_t len;
    uint32_t crc;
} forms[FORMS];
static int nforms;
static long left, printed;
static int deref;  /* YW4_TRACE_CODES=2: also look at what the arguments point at (slow: the game crawls) */
static uint64_t seen[256];
static int nseen;

static uint32_t crc32_bytes(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

static void add_form(const char *what, const uint8_t *b, size_t n) {
    if (nforms == FORMS || n > sizeof forms[0].bytes) return;
    forms[nforms].what = what, memcpy(forms[nforms].bytes, b, n), forms[nforms].len = n;
    forms[nforms].crc = crc32_bytes(b, n);
    nforms++;
}

/* readable memory, remembered by region (VirtualQuery per pointer would be slow) */
static uint64_t ok_lo, ok_hi;
static int readable(uint64_t a, uint64_t n) {
    if (a < 0x10000 || a > 0x7FFFFFFFFFFFull) return 0;  /* first: no wrap-around below */
    if (a >= ok_lo && a + n <= ok_hi) return 1;
    if (!host_readable(a, n)) return 0;
    ok_lo = a & ~0xFFFull, ok_hi = ok_lo + 0x1000;
    return a + n <= ok_hi;
}

static int once(uint64_t key) {
    for (int i = 0; i < nseen; i++)
        if (seen[i] == key) return 0;
    if (nseen < 256) seen[nseen++] = key;
    return 1;
}

static uint8_t filter[65536 / 8];  /* the low 16 bits of each value we look for */
static void filter_add(uint32_t v) { filter[(uint16_t)v >> 3] |= (uint8_t)(1 << (v & 7)); }

static void hook(CPUState *ctx, uint64_t addr) {
    if (left <= 0 || printed > 120) return;
    left--;
    char fa[48], ca[48];
    for (int r = 0; r < 8; r++) {
        uint64_t v = ctx->x[r];
        if (!deref && !(filter[(uint16_t)v >> 3] & 1 << (v & 7))) continue;  /* none of our values: most calls */
        for (int i = 0; i < codes_nlist; i++)
            if ((uint32_t)v == codes_list[i] && v && once(addr * 7919 + (uint64_t)i)) {
                fprintf(stderr, "yw4: codes: trace: %s gets x%d = the list's password %d (%08x), from %s\n",
                        yw4_describe(addr, fa), r, i, codes_list[i], yw4_describe(ctx->x[30] - 4, ca));
                printed++;
            }
        for (int i = 0; i < nforms; i++) {
            if ((uint32_t)v == forms[i].crc && once(addr * 31 + (uint64_t)i)) {
                fprintf(stderr, "yw4: codes: trace: %s gets x%d = crc32 of the text %s (%08x), from %s\n",
                        yw4_describe(addr, fa), r, forms[i].what, forms[i].crc, yw4_describe(ctx->x[30] - 4, ca));
                printed++;
            }
            if (deref && readable(v, forms[i].len) && !memcmp((const void *)(uintptr_t)v, forms[i].bytes, forms[i].len) &&
                once(addr * 131 + (uint64_t)i * 7 + (uint64_t)r)) {
                const uint8_t *p = (const uint8_t *)(uintptr_t)v;
                char hex[100] = "";
                for (int k = 0; k < 24 && readable(v + (uint64_t)k, 1); k++) sprintf(hex + 3 * k, "%02x ", p[k]);
                fprintf(stderr, "yw4: codes: trace: %s gets x%d -> the text %s (%s), from %s\n", yw4_describe(addr, fa), r,
                        forms[i].what, hex, yw4_describe(ctx->x[30] - 4, ca));
                printed++;
            }
        }
    }
    if (left == 0) fprintf(stderr, "yw4: codes: trace: done\n");
}

void codes_trace_password(const char *text) {
    const char *e = getenv("YW4_TRACE_CODES");
    if (!e || (*e != '1' && *e != '2') || !text || !*text) return;
    deref = *e == '2';
    size_t n = strlen(text);
    if (n >= MAXLEN) return;
    uint8_t b[MAXLEN * 3];
    nforms = 0, nseen = 0, printed = 0;
    add_form("as typed (UTF-8)", (const uint8_t *)text, n);
    for (size_t i = 0; i < n; i++) b[i] = (uint8_t)toupper((unsigned char)text[i]);
    add_form("in capitals", b, n);
    for (size_t i = 0; i < n; i++) b[i] = (uint8_t)tolower((unsigned char)text[i]);
    add_form("in small letters", b, n);
    for (size_t i = 0; i < n; i++) b[2 * i] = (uint8_t)text[i], b[2 * i + 1] = 0;
    add_form("as UTF-16", b, 2 * n);
    for (size_t i = 0; i < n; i++) {  /* full-width: U+FF01..U+FF5E for '!'..'~', UTF-8 EF BC/BD xx */
        uint32_t cp = (unsigned char)text[i] >= 0x21 && (unsigned char)text[i] <= 0x7E ? 0xFF01u + (unsigned char)text[i] - 0x21 : (unsigned char)text[i];
        b[3 * i] = (uint8_t)(0xE0 | cp >> 12), b[3 * i + 1] = (uint8_t)(0x80 | (cp >> 6 & 63)), b[3 * i + 2] = (uint8_t)(0x80 | (cp & 63));
    }
    add_form("full-width (UTF-8)", b, 3 * n);
    for (size_t i = 0; i < n; i++) {
        uint32_t cp = (unsigned char)text[i] >= 0x21 && (unsigned char)text[i] <= 0x7E ? 0xFF01u + (unsigned char)text[i] - 0x21 : (unsigned char)text[i];
        b[2 * i] = (uint8_t)cp, b[2 * i + 1] = (uint8_t)(cp >> 8);
    }
    add_form("full-width (UTF-16)", b, 2 * n);
    memset(filter, 0, sizeof filter);
    for (int i = 0; i < nforms; i++) filter_add(forms[i].crc);
    for (int i = 0; i < codes_nlist; i++) filter_add(codes_list[i]);
    left = deref ? 20000000 : 400000000;
    yw4_entry_hook = hook;
    yw4_probe_any = 1;
    fprintf(stderr, "yw4: codes: trace: watching what the game does with \"%s\" (crc32 %08x)\n", text, forms[0].crc);
}

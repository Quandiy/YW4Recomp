/* Our own Post passwords, built into the game (codes.h).
 *
 * The Lambert Post app's passwords are a table: data/common/gamedata/post/password_list_config.cfg.bin, a Level-5
 * cfg.bin with PASSWORD_INFO_LIST_BEG (the entry count), one PASSWORD_INFO per password, PASSWORD_INFO_LIST_END.
 * PASSWORD_INFO holds 7 ints: CRC-32 of the password (UTF-8), the reward's item id (item_config), 0, an id of its own
 * (which password was used), 0, and the number received twice (yo-docs "Editing YW4 Passwords": Password, ParamID,
 * NumberReceived; the game's entries have 1 and 1). Found 2026-11-10 by trying in the game:
 *   - Hiragana passwords are hashed as typed (the game's own Special Coin password, GameFAQs); Latin letters are not
 *     (the English patch ships an exefs patch, "NoFullwidth", that the recomp doesn't apply), so the password keyboard
 *     (swkbd.c) hands the game a hiragana text for each of our codes, and the list holds that text's hash.
 *   - Only place 59 of the list ever took a password (2026-11-10/11): places 56 to 58 never (perhaps marked used by
 *     passwords Level-5 added later), 60 on never (the game seems to know 59 places), so three placeholders fill 56 to
 *     58 and there is room for one code of ours.
 *   - Made-up hiragana texts are refused even at place 59; the game's own passwords are taken there: the game checks
 *     something in the text itself (a check character?). So our one code rides on a real password of the game.
 * More codes of our own would need the check and the 59 places found in the game's code.
 * Only the English patch loads the table as a loose file (its cpk_list sends the game there), so that is the file we
 * extend: romfs.c hands it to codes_extend when it builds the image. No mod to download: the codes come with the game.
 */
#include "codes.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PASSWORD_INFO 0xBE511BFDu          /* crc32("PASSWORD_INFO") */
#define PASSWORD_INFO_LIST_BEG 0xC506DECCu
#define PASSWORD_INFO_LIST_END 0xBAB44038u
#define ENTRY 36                           /* crc, count 7, 2 type bytes (all int), 1 pad, 7 values */

/* Item ids are item_config's (crc32 of the item's name there). Passwords: letters, digits and signs, any case.
 * `sent`: what the keyboard hands the game for it, and what the list holds the hash of: a real password of the game
 * (works at place 59), or NULL for the code's own hiragana form (refused by the game, see above). One code fits. */
static const struct {
    const char *password;
    const char *sent;
    uint32_t item;
    uint32_t count;
    const char *what;
} CODES[] = {
    {"Route999",
     "\xE3\x82\x8B\xE3\x81\x84\xE3\x81\xA6\xE3\x81\x92\xE3\x81\x82\xE3\x81\xBF\xE3\x82\x92\xE3\x82\x89\xE3\x81\x9D"
     "\xE3\x81\x98\xE3\x81\xAA\xE3\x81\x8A",  /* るいてげあみをらそじなお, the game's Special Coin password (GameFAQs) */
     0x1E3B4579u, 1, "Special Coin"},
};
enum { NCODES = sizeof CODES / sizeof *CODES };

uint32_t codes_list[128];
int codes_nlist;

static uint32_t crc32_of(const char *s) {
    uint32_t c = 0xFFFFFFFFu;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        c ^= *p;
        for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

/* A code in small letters, without spaces: how typed text and codes are compared. */
static void fold(const char *in, char *out, size_t room) {
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && n + 1 < room; p++)
        if (!isspace(*p)) out[n++] = (char)tolower(*p);
    out[n] = 0;
}

/* The code's hiragana form: "ようかい" and the 8 hex digits of crc32 of its folded text, one hiragana per digit: 12
 * characters, like the game's own passwords (10 to 13). Hiragana reach the game's hash unchanged, and no real
 * password looks like this. */
static void hiragana_form(const char *code, char out[64]) {
    static const char *digit[16] = {"あ", "い", "う", "え", "お", "か", "き", "く",
                                    "け", "こ", "さ", "し", "す", "せ", "そ", "た"};
    char folded[64];
    fold(code, folded, sizeof folded);
    uint32_t h = crc32_of(folded);
    strcpy(out, "ようかい");
    for (int i = 7; i >= 0; i--) strcat(out, digit[h >> (4 * i) & 15]);
}

const char *codes_password_form(const char *typed) {
    static char form[64];
    char folded[64], code[64];
    fold(typed, folded, sizeof folded);
    for (int i = 0; i < NCODES; i++) {
        fold(CODES[i].password, code, sizeof code);
        if (!strcmp(folded, code)) {
            if (CODES[i].sent) return CODES[i].sent;
            hiragana_form(CODES[i].password, form);
            return form;
        }
    }
    return NULL;
}

static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }

static void put_entry(uint8_t *e, uint32_t hash, uint32_t item, uint32_t used_id, uint32_t count) {
    put32(e, PASSWORD_INFO);
    e[4] = 7, e[5] = 0x55, e[6] = 0x15, e[7] = 0xFF;
    uint32_t v[7] = {hash, item, 0, used_id, 0, count, count};
    for (int i = 0; i < 7; i++) put32(e + 8 + 4 * i, v[i]);
}

uint8_t *codes_extend(const uint8_t *in, uint64_t size, uint64_t *out_size) {
    if (size < 0x20) return NULL;
    uint32_t n = get32(in), str_off = get32(in + 4);
    /* the list's ends: BEG first (one int), END the last entry (no values) */
    if (str_off > size || str_off < 0x20 || get32(in + 0x10) != PASSWORD_INFO_LIST_BEG ||
        get32(in + str_off - 8) != PASSWORD_INFO_LIST_END) {
        fprintf(stderr, "yw4: codes: the password list isn't laid out as expected: left as it is\n");
        return NULL;
    }
    /* places 56 to 58 of the list (after the game's 55) never took a password, 59 did (2026-11-10): three placeholders
     * nobody can type (hash 0) fill them, ours come after */
    enum { PAD = 3 };
    uint64_t grow = (uint64_t)ENTRY * (NCODES + PAD);
    uint8_t *out = malloc((size_t)(size + grow));
    if (!out) return NULL;
    uint64_t at = str_off - 8;  /* before LIST_END */
    memcpy(out, in, (size_t)at);
    uint8_t *e = out + at;
    for (int i = 0; i < PAD; i++, e += ENTRY)
        put_entry(e, 0, CODES[0].item, crc32_of("YW4RECOMP_PLACEHOLDER") + (uint32_t)i, 1);
    for (int i = 0; i < NCODES; i++, e += ENTRY) {
        char folded[64], id_name[96], form[64];
        fold(CODES[i].password, folded, sizeof folded);
        snprintf(id_name, sizeof id_name, "YW4RECOMP_CODE_%s", folded);  /* which password was used: our own id */
        if (CODES[i].sent) snprintf(form, sizeof form, "%s", CODES[i].sent);
        else hiragana_form(CODES[i].password, form);
        put_entry(e, crc32_of(form), CODES[i].item, crc32_of(id_name), CODES[i].count);
        fprintf(stderr, "yw4: codes: %s (%s) gives %u x %s\n", CODES[i].password, form, CODES[i].count, CODES[i].what);
    }
    memcpy(e, in + at, (size_t)(size - at));
    /* the hashes of the finished list, for YW4_TRACE_CODES (codes_trace.c) */
    codes_nlist = 0;
    for (uint64_t g = 0x1C; g + ENTRY <= at + grow && codes_nlist < 128; g += ENTRY)
        if (get32(out + g) == PASSWORD_INFO) codes_list[codes_nlist++] = get32(out + g + 8);
    put32(out, n + (uint32_t)(NCODES + PAD));
    put32(out + 4, str_off + (uint32_t)grow);
    put32(out + 0x18, get32(in + 0x18) + (uint32_t)(NCODES + PAD));  /* LIST_BEG's count */
    *out_size = size + grow;
    return out;
}

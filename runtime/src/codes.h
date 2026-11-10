/* Our own Post passwords (Lambert Post app), built into the game: codes.c adds them to the game's password list. */
#ifndef CODES_H
#define CODES_H

#include <stdint.h>

#define CODES_FILE "data/common/gamedata/post/password_list_config.cfg.bin"

/* The password list with our codes added (malloc'd, *out_size its size), or NULL to keep `in` as it is. */
uint8_t *codes_extend(const uint8_t *in, uint64_t size, uint64_t *out_size);

/* What the password keyboard answers when the player types `typed`: one of our codes (any case, spaces ignored) gives
 * its hiragana form, the one the list holds (static storage); anything else NULL (answer as typed). */
const char *codes_password_form(const char *typed);

/* YW4_TRACE_CODES=1: watch what the game does with a typed password (codes_trace.c; debugging only). */
void codes_trace_password(const char *text);
extern uint32_t codes_list[128];  /* the password hashes in the list (codes_extend) */
extern int codes_nlist;

#endif

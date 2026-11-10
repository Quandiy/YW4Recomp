/* data/cpk_list.cfg.bin, the game's list of its files and the .cpk archive each is in (cpklist.c). */
#ifndef CPKLIST_H
#define CPKLIST_H

#include <stdint.h>

/* Points every entry whose file `loose` says a mod has as a loose file (returning 1 and its size) at that loose file
 * instead of its archive, in place. The number of entries changed, or -1 when the list isn't laid out as expected. */
int cpklist_redirect(uint8_t *list, uint64_t size, int (*loose)(const char *path, uint64_t *size, void *arg), void *arg);

/* Sets the size of the loose file `path` in the list, in place: 0, or -1 when there is no such loose entry. */
int cpklist_set_size(uint8_t *list, uint64_t size, const char *path, uint32_t new_size);

#endif

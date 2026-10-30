/* OPENMPK3 — 8 pattern slots in flash (looper contents + musical context). */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "looper.h"

#define PATTERN_SLOTS 9   /* 0..7 = P1..P8, 8 = undo buffer */
#define UNDO_SLOT 8

typedef struct { uint16_t bpm; uint8_t sound, root, loop_len, voicing; } pattern_ctx_t;

bool pattern_exists(int slot);
bool pattern_save(int slot, const looper_t *l, const pattern_ctx_t *ctx);
bool pattern_load(int slot, looper_t *l, pattern_ctx_t *ctx);   /* replaces the looper's events */
void pattern_erase(int slot);
void pattern_copy(int dst, int src);   /* flash to flash; an empty source empties dst */

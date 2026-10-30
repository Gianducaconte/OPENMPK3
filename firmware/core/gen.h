/* OPENMPK3 — generative tools for the looper (inspired by rule-based sequencers like the Torso T1). */
#pragma once
#include <stdint.h>
#include "looper.h"
#include "theory.h"

typedef int (*gen_chord_fn)(int pad, uint8_t *out);   /* the current pad chords (style or scale) */

/* Overwrite the loop with a new idea: pad chords on a Euclidean rhythm + a melody walking the scale. */
void gen_generate(looper_t *l, const theory_cfg_t *t, gen_chord_fn chord, uint8_t ch, uint32_t seed);
/* Change about `percent` % of the loop's notes: a scale step, a 1/16 nudge, or a new velocity. */
void gen_mutate(looper_t *l, const theory_cfg_t *t, int percent, uint32_t seed);

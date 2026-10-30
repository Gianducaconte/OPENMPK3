/* OPENMPK3 — chord styles. Every chord is built from scale steps, so pads never leave the scale the keys play. */
#pragma once
#include <stdint.h>

/* chord flavour = which scale steps are stacked on the chord's scale degree */
typedef enum { F_TRI, F_7, F_9, F_ADD9, F_SUS2, F_SUS4, F_6, F_11, F_5, F_QRT, F_COUNT } flavour_t;
extern const char *const FLAVOUR_NAMES[F_COUNT];

typedef struct { uint8_t deg, fl; } pack_chord_t;   /* scale degree (0 = tonic), flavour; or (semitones, X_quality) */
/* explicit chords for sparse (pentatonic) scales: semitone offset from the key root + quality; still checked in-scale */
enum { X_MAJ = 0x80, X_MIN, X_SUS2, X_SUS4, X_5, X_MAJ7, X_M7, X_7, X_ADD9, X_MADD9, X_7SUS4, X_M7B5, X_M6, X_9 };
typedef struct { const char *name; uint8_t scale; pack_chord_t ch[8]; uint8_t voicing; } pack_t;   /* voicing preset (VOICE_*) */

extern const pack_t PACKS[];
extern const int PACK_COUNT;
extern const uint8_t SCALE_VOICING[];   /* voicing preset when a plain scale is selected */

/* chord on scale degree `deg` with flavour `fl`, voiced with a VOICE_* mode. name gets e.g. "Am7" (may be NULL). */
int scale_chord(int scale, int root, int deg, int fl, int voicing, uint8_t *out, char *name);
/* pad chord of a style */
int pack_chord(int pack, int pad, int root, int voicing, uint8_t *out, char *name);

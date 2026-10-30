/* OPENMPK3 — music theory core (hardware independent). */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const char *name;     /* short name for the OLED, <= 10 chars */
    uint16_t    mask;     /* bit n set => semitone n above root is in scale */
} scale_t;

enum {
    SC_MAJOR, SC_MINOR, SC_DORIAN, SC_PHRYGIAN, SC_LYDIAN, SC_MIXOLYDIAN, SC_LOCRIAN, SC_HARM_MINOR, SC_MEL_MINOR,
    SC_MAJ_PENTA, SC_MIN_PENTA, SC_BLUES, SC_MAJ_BLUES, SC_DOM_PENTA, SC_JAPANESE, SC_HIRAJOSHI, SC_KUMOI, SC_PELOG,
    SC_SLENDRO, SC_EGYPTIAN, SC_HIJAZ, SC_BHAIRAV, SC_HUNG_MINOR, SC_NEAP_MINOR, SC_LYD_DOM, SC_BEBOP,
    SC_MIXO_B6, SC_HARM_MAJOR, SC_DORIAN_B2, SC_UKR_DORIAN, SC_MIN_HEXA, SC_MAJ_HEXA, SC_BEBOP_MIN, SC_LYD_AUG, SC_WHOLE_TONE,
    SC_CHROMATIC,
    SCALE_COUNT
};
extern const scale_t SCALES[SCALE_COUNT];
extern const char *const NOTE_NAMES[12];

/* How keys are mapped to the scale. */
typedef enum {
    KEYMAP_SPREAD = 0, /* every key plays the next scale step (no repeats); middle key = root */
    KEYMAP_WHITE,      /* white keys walk scale degrees (C4 = root); black keys add a diatonic 3rd above */
    KEYMAP_CHROMATIC,  /* bypass: classic controller */
    KEYMAP_COUNT
} keymap_t;

typedef enum {
    CHORD_TRIAD = 0, CHORD_SEVENTH, CHORD_NINTH, CHORD_SUS2, CHORD_SUS4,
    CHORD_POWER, CHORD_SIXTH, CHORD_ADD9, CHORD_TYPE_COUNT
} chord_type_t;
extern const char *const CHORD_TYPE_NAMES[CHORD_TYPE_COUNT];

typedef struct {
    uint8_t root;        /* 0..11 */
    uint8_t scale;       /* index in SCALES */
    keymap_t keymap;
    chord_type_t chord_type;
    int8_t  inversion;   /* 0..3 */
    uint8_t spread;      /* 0 = close voicing, 1 = open (drop-2-ish), 2 = wide */
} theory_cfg_t;

int  scale_size(uint8_t scale);
/* semitone offset (can exceed 11) of scale degree d (any int, negative ok) */
int  scale_degree_offset(uint8_t scale, int d);
bool in_scale(const theory_cfg_t *c, int note);
/* nearest in-scale note; ties resolve downward */
int  quantize(const theory_cfg_t *c, int note);
/* degree index (floor) of a note that is in scale; returns deg such that note == root+offset(deg) */
int  note_to_degree(const theory_cfg_t *c, int note);
/* map a physical key (MIDI note incl. octave shift) to a single played note */
int  map_key_note(const theory_cfg_t *c, int key);
/* build a diatonic chord whose root is scale degree `deg` (absolute, can span octaves). returns count */
int  build_chord(const theory_cfg_t *c, int deg, int base_octave_note, uint8_t *out, int max);
/* chord for a physical key in CHORD mode */
int  key_chord(const theory_cfg_t *c, int key, uint8_t *out, int max);
/* name a chord built from notes, e.g. "Am7", "Bdim", "Csus4". buf >= 12 */
void chord_name(const uint8_t *notes, int n, char *buf);
int  clamp_note(int n);
/* KEYMAP_SPREAD: key index 0..24 (12 = root), octave shift */
int  spread_key_note(const theory_cfg_t *c, int key_index, int octave);

/* Smooth voicing: places the chord around a fixed register so neighbouring chords move smoothly.
 * iv = chord intervals from its root (may exceed 12). Returns note count. */
enum { VOICE_SMOOTH = 0, VOICE_BASS, VOICE_OPEN, VOICE_WIDE, VOICE_HIGH, VOICE_LOW, VOICE_COUNT };
extern const char *const VOICE_NAMES[VOICE_COUNT];
int  voice_chord(int root_pc, const int8_t *iv, int n, int mode, uint8_t *out);

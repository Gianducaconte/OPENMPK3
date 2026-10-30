/* OPENMPK3 — music theory core. */
#include "theory.h"
#include <string.h>
#include <stdlib.h>

static inline uint16_t b(int n) { return (uint16_t)(1u << n); }

const scale_t SCALES[SCALE_COUNT] = {
    [SC_MAJOR]      = { "Major",      0xAB5 }, /* 0 2 4 5 7 9 11 */
    [SC_MINOR]      = { "Minor",      0x5AD }, /* 0 2 3 5 7 8 10 */
    [SC_DORIAN]     = { "Dorian",     0x6AD }, /* 0 2 3 5 7 9 10 */
    [SC_PHRYGIAN]   = { "Phrygian",   0x5AB }, /* 0 1 3 5 7 8 10 */
    [SC_LYDIAN]     = { "Lydian",     0xAD5 }, /* 0 2 4 6 7 9 11 */
    [SC_MIXOLYDIAN] = { "Mixolydian", 0x6B5 }, /* 0 2 4 5 7 9 10 */
    [SC_LOCRIAN]    = { "Locrian",    0x56B }, /* 0 1 3 5 6 8 10 */
    [SC_HARM_MINOR] = { "Harm Minor", 0x9AD }, /* 0 2 3 5 7 8 11 */
    [SC_MEL_MINOR]  = { "Mel Minor",  0xAAD }, /* 0 2 3 5 7 9 11 */
    [SC_MAJ_PENTA]  = { "Maj Penta",  0x295 }, /* 0 2 4 7 9 */
    [SC_MIN_PENTA]  = { "Min Penta",  0x4A9 }, /* 0 3 5 7 10 */
    [SC_BLUES]      = { "Blues",      0x4E9 }, /* 0 3 5 6 7 10 */
    [SC_MAJ_BLUES]  = { "Maj Blues",  0x29D }, /* 0 2 3 4 7 9 */
    [SC_DOM_PENTA]  = { "Afro Penta", 0x495 }, /* 0 2 4 7 10 */
    [SC_JAPANESE]   = { "Japanese",   0x1A3 }, /* 0 1 5 7 8  (In) */
    [SC_HIRAJOSHI]  = { "Hirajoshi",  0x18D }, /* 0 2 3 7 8 */
    [SC_KUMOI]      = { "Kumoi",      0x28D }, /* 0 2 3 7 9 */
    [SC_PELOG]      = { "Pelog",      0x18B }, /* 0 1 3 7 8 */
    [SC_SLENDRO]    = { "Slendro",    0x2A5 }, /* 0 2 5 7 9 */
    [SC_EGYPTIAN]   = { "Egyptian",   0x4A5 }, /* 0 2 5 7 10 */
    [SC_HIJAZ]      = { "Hijaz",      0x5B3 }, /* 0 1 4 5 7 8 10  (Phrygian dominant) */
    [SC_BHAIRAV]    = { "Bhairav",    0x9B3 }, /* 0 1 4 5 7 8 11  (double harmonic) */
    [SC_HUNG_MINOR] = { "Hungarian",  0x9CD }, /* 0 2 3 6 7 8 11 */
    [SC_NEAP_MINOR] = { "Neapolitan", 0x9AB }, /* 0 1 3 5 7 8 11 */
    [SC_LYD_DOM]    = { "Lydian Dom", 0x6D5 }, /* 0 2 4 6 7 9 10 */
    [SC_BEBOP]      = { "Bebop",      0xEB5 }, /* 0 2 4 5 7 9 10 11 */
    [SC_MIXO_B6]    = { "Mixo b6",    0x5B5 }, /* 0 2 4 5 7 8 10  (epic, wistful) */
    [SC_HARM_MAJOR] = { "Harm Major", 0x9B5 }, /* 0 2 4 5 7 8 11  (fairytale) */
    [SC_DORIAN_B2]  = { "Dorian b2",  0x6AB }, /* 0 1 3 5 7 9 10  (spy / noir) */
    [SC_UKR_DORIAN] = { "Ukr Dorian", 0x6CD }, /* 0 2 3 6 7 9 10  (klezmer, balkan) */
    [SC_MIN_HEXA]   = { "Min Hexa",   0x4AD }, /* 0 2 3 5 7 10 */
    [SC_MAJ_HEXA]   = { "Maj Hexa",   0x2B5 }, /* 0 2 4 5 7 9 */
    [SC_BEBOP_MIN]  = { "Bebop Min",  0x6BD }, /* 0 2 3 4 5 7 9 10 */
    [SC_LYD_AUG]    = { "Lydian Aug", 0xB55 }, /* 0 2 4 6 8 9 11  (sci-fi) */
    [SC_WHOLE_TONE] = { "Whole Tone", 0x555 }, /* 0 2 4 6 8 10  (dream) */
    [SC_CHROMATIC]  = { "Chromatic",  0xFFF },
};

const char *const NOTE_NAMES[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
const char *const CHORD_TYPE_NAMES[CHORD_TYPE_COUNT] = {
    "Triad", "7th", "9th", "Sus2", "Sus4", "Power", "6th", "Add9" };

static inline int abs10(int x) { return x < 0 ? -x : x; }
static inline int mod12(int n) { int m = n % 12; return m < 0 ? m + 12 : m; }
static inline int floordiv(int a, int d) { return (a >= 0) ? a / d : -((-a + d - 1) / d); }
int clamp_note(int n) { return n < 0 ? 0 : n > 127 ? 127 : n; }

int scale_size(uint8_t s)
{
    int n = 0;
    for (uint16_t m = SCALES[s].mask; m; m >>= 1) n += m & 1;
    return n;
}

int scale_degree_offset(uint8_t s, int d)
{
    int size = scale_size(s);
    int oct = floordiv(d, size), idx = d - oct * size, k = 0;
    for (int st = 0; st < 12; st++)
        if (SCALES[s].mask & b(st)) { if (k == idx) return oct * 12 + st; k++; }
    return oct * 12;
}

bool in_scale(const theory_cfg_t *c, int note)
{
    return SCALES[c->scale].mask & b(mod12(note - c->root));
}

int quantize(const theory_cfg_t *c, int note)
{
    for (int dist = 0; dist < 12; dist++) {
        if (in_scale(c, note - dist)) return note - dist;   /* downward first on ties */
        if (in_scale(c, note + dist)) return note + dist;
    }
    return note;
}

int note_to_degree(const theory_cfg_t *c, int note)
{
    int rel = note - c->root, oct = floordiv(rel, 12), st = rel - oct * 12, k = 0;
    for (int i = 0; i < st; i++) if (SCALES[c->scale].mask & b(i)) k++;
    return oct * scale_size(c->scale) + k;
}

/* white-key index relative to C: C=0 D=1 E=2 F=3 G=4 A=5 B=6, per octave 7. Black keys -> -1 */
static const int8_t WHITE_IDX[12] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };

static int white_degree(int key, bool *is_black)
{
    int oct = floordiv(key, 12), pc = key - oct * 12;
    *is_black = WHITE_IDX[pc] < 0;
    int w = *is_black ? WHITE_IDX[pc - 1] : WHITE_IDX[pc];
    return (oct - 5) * 7 + w;   /* middle C (60) = degree 0 */
}

int map_key_note(const theory_cfg_t *c, int key)
{
    switch (c->keymap) {
    case KEYMAP_CHROMATIC: return clamp_note(key);
    case KEYMAP_WHITE: {
        bool blk; int d = white_degree(key, &blk);
        if (blk) d += 2;   /* black key: diatonic third above the white key to its left */
        return clamp_note(60 + c->root + scale_degree_offset(c->scale, d));
    }
    default: return clamp_note(quantize(c, key));
    }
}

int spread_key_note(const theory_cfg_t *c, int key_index, int octave)
{
    int base = 60 + 12 * octave + (c->root <= 6 ? c->root : c->root - 12);
    return clamp_note(base + scale_degree_offset(c->scale, key_index - 12));
}

const char *const VOICE_NAMES[VOICE_COUNT] = { "Smooth", "+Bass", "Open", "Wide", "High", "Low" };

int voice_chord(int root_pc, const int8_t *iv, int n, int mode, uint8_t *out)
{
    int pcs[8], np = 0;
    for (int i = 0; i < n && np < 8; i++) {               /* unique pitch classes, chord-tone order */
        int pc = mod12(root_pc + iv[i]); bool dup = false;
        for (int j = 0; j < np; j++) if (pcs[j] == pc) dup = true;
        if (!dup) pcs[np++] = pc;
    }
    int center = mode == VOICE_HIGH ? 67 : mode == VOICE_LOW ? 54 : 61;
    /* close voicing: try each chord tone as the lowest note and each placement, keep the one centred best */
    int best[8], bestscore = 1 << 30;
    for (int r = 0; r < np; r++) {
        for (int lo = center - 18; lo <= center; lo++) {
            if (mod12(lo) != pcs[r]) continue;
            int v[8], k = 0, prev = lo;
            v[k++] = lo;
            for (int t = 1; t < np; t++) {
                int pc = pcs[(r + t) % np], note = prev + 1;
                while (mod12(note) != pc) note++;
                v[k++] = prev = note;
            }
            /* sort (chord-tone order may not be ascending pitch order after wrapping) */
            for (int a = 1; a < k; a++) for (int b2 = a; b2 > 0 && v[b2] < v[b2 - 1]; b2--) { int t = v[b2]; v[b2] = v[b2 - 1]; v[b2 - 1] = t; }
            int sum = 0; for (int a = 0; a < k; a++) sum += v[a];
            int score = abs10(sum * 10 / k - center * 10) + (v[k - 1] - v[0]) + (r ? 3 : 0);   /* centred, compact, root-position bonus */
            if (score < bestscore) { bestscore = score; memcpy(best, v, sizeof v); }
        }
    }
    int v[10], k = np;
    memcpy(v, best, (size_t)np * sizeof v[0]);
    if (mode == VOICE_OPEN && k >= 3) { if (k >= 4) v[k - 2] -= 12; else v[1] += 12; }       /* drop-2 / spread triad */
    if (mode == VOICE_WIDE && k >= 3) v[1] += 12;
    if (mode == VOICE_BASS || mode == VOICE_WIDE) {        /* root an octave below the voicing */
        int lowest = v[0]; for (int a = 1; a < k; a++) if (v[a] < lowest) lowest = v[a];
        int b = lowest - 1; while (mod12(b) != mod12(root_pc)) b--;
        if (lowest - b < 5) b -= 12;
        v[k++] = b;
    }
    for (int a = 1; a < k; a++) for (int b2 = a; b2 > 0 && v[b2] < v[b2 - 1]; b2--) { int t = v[b2]; v[b2] = v[b2 - 1]; v[b2 - 1] = t; }
    int m = 0;
    for (int a = 0; a < k; a++) if (v[a] >= 0 && v[a] <= 127 && (m == 0 || out[m - 1] != v[a])) out[m++] = (uint8_t)v[a];
    return m;
}

int build_chord(const theory_cfg_t *c, int deg, int base, uint8_t *out, int max)
{
    static const int8_t STACK[CHORD_TYPE_COUNT][5] = {
        /* scale-degree steps above chord root, -1 terminates; 100+n = n semitones (fixed) */
        { 0, 2, 4, -1, -1 },        /* triad */
        { 0, 2, 4, 6, -1 },         /* 7th */
        { 0, 2, 4, 6, 8 },          /* 9th */
        { 0, 1, 4, -1, -1 },        /* sus2 (diatonic 2nd) */
        { 0, 3, 4, -1, -1 },        /* sus4 (diatonic 4th) */
        { 0, -1, -1, -1, -1 },      /* power: handled separately */
        { 0, 2, 4, 5, -1 },         /* 6th */
        { 0, 2, 4, 8, -1 },         /* add9 */
    };
    int sz = scale_size(c->scale);
    int tmp[6], n = 0;
    if (c->chord_type == CHORD_POWER) {   /* fixed intervals: root, 5th, octave */
        int r = base + c->root + scale_degree_offset(c->scale, deg);
        tmp[0] = r; tmp[1] = r + 7; tmp[2] = r + 12; n = 3;
        goto voiced;
    }
    for (int i = 0; i < 5 && STACK[c->chord_type][i] >= 0; i++) {
        int step = STACK[c->chord_type][i];
        if (sz < 7) step = (step * sz + 3) / 7; /* pentatonic etc: spread evenly */
        int note = base + c->root + scale_degree_offset(c->scale, deg + step);
        if (n && note <= tmp[n - 1]) note += 12;  /* keep ascending when degrees collapse */
        tmp[n++] = note;
    }
voiced:
    /* inversions: move lowest note up an octave */
    for (int inv = 0; inv < c->inversion && n > 1; inv++) {
        int lo = tmp[0];
        memmove(tmp, tmp + 1, (size_t)(n - 1) * sizeof(int));
        tmp[n - 1] = lo + 12;
        while (tmp[n - 1] <= tmp[n - 2]) tmp[n - 1] += 12;
    }
    /* spread: open the voicing */
    if (c->spread >= 1 && n >= 3) tmp[1] += 12;                  /* drop the 2nd voice up an octave */
    if (c->spread >= 2 && n >= 3) { tmp[0] -= 12; tmp[n-1] += 12; } /* add bass + air */
    if (c->spread >= 1) { /* resort */
        for (int i = 1; i < n; i++) for (int j = i; j > 0 && tmp[j] < tmp[j-1]; j--) {
            int t = tmp[j]; tmp[j] = tmp[j-1]; tmp[j-1] = t; }
    }
    int k = 0;
    for (int i = 0; i < n && k < max; i++) {
        int v = tmp[i];
        if (v < 0 || v > 127 || (k && out[k - 1] == v)) continue;   /* drop duplicates from voicing */
        out[k++] = (uint8_t)v;
    }
    return k;
}

int key_chord(const theory_cfg_t *c, int key, uint8_t *out, int max)
{
    int deg;
    if (c->keymap == KEYMAP_WHITE) {
        bool blk; deg = white_degree(key, &blk);
        if (blk) {   /* black key = chord of the white key to its left, richer flavour */
            theory_cfg_t c2 = *c;
            c2.chord_type = c->chord_type == CHORD_SEVENTH ? CHORD_NINTH : CHORD_SEVENTH;
            return build_chord(&c2, deg, 60, out, max);
        }
        return build_chord(c, deg, 60, out, max);
    }
    int q = quantize(c, key);
    deg = note_to_degree(c, q);           /* absolute degree (includes octave) */
    return build_chord(c, deg, 0, out, max);
}

void chord_name(const uint8_t *notes, int n, char *buf)
{
    if (n <= 0) { buf[0] = 0; return; }
    /* try every note as root, pick the interpretation that matches a known quality */
    static const struct { uint16_t mask; const char *suffix; } Q[] = {
        { 0x895, "maj9" }, { 0x495, "9" }, { 0x48D, "m9" }, { 0x88D, "mMaj9" }, { 0x493, "7b9" }, { 0x48B, "m7b9" },
        { 0x499, "7#9" }, { 0x4AD, "m11" }, { 0x4A5, "11" }, { 0x8A5, "maj11" }, { 0x8D1, "maj7#11" }, { 0x445, "m9b5" },
        { 0x44D, "m9b5" }, { 0x295, "6/9" }, { 0x28D, "m6/9" },
        { 0x891, "maj7" }, { 0x491, "7" }, { 0x489, "m7" }, { 0x449, "m7b5" }, { 0x249, "dim7" }, { 0x889, "mMaj7" },
        { 0x911, "maj7#5" }, { 0x4A1, "7sus4" }, { 0x291, "6" }, { 0x289, "m6" }, { 0x095, "add9" }, { 0x08D, "madd9" },
        { 0x091, "" }, { 0x089, "m" }, { 0x049, "dim" }, { 0x111, "aug" },
        { 0x085, "sus2" }, { 0x0A1, "sus4" }, { 0x081, "5" }, { 0x041, "b5" }, { 0x101, "+5" }, { 0x001, "" },
    };
    uint16_t pcs = 0;
    for (int i = 0; i < n; i++) pcs |= b(mod12(notes[i]));
    int bass = mod12(notes[0]);
    for (int r0 = 0; r0 < 12; r0++) {
        for (unsigned q = 0; q < sizeof Q / sizeof Q[0]; q++) {
            int r = mod12(bass + r0);          /* prefer bass as root */
            uint16_t rel = 0;
            for (int pc = 0; pc < 12; pc++) if (pcs & b(pc)) rel |= b(mod12(pc - r));
            if (rel == Q[q].mask) {
                char *p = buf;
                for (const char *s = NOTE_NAMES[r]; *s; ) *p++ = *s++;
                for (const char *s = Q[q].suffix; *s; ) *p++ = *s++;
                if (r != bass) { *p++ = '/'; for (const char *s = NOTE_NAMES[bass]; *s; ) *p++ = *s++; }
                *p = 0;
                return;
            }
        }
    }
    strcpy(buf, NOTE_NAMES[bass]); strcat(buf, "*");
}

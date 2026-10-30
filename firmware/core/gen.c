/* OPENMPK3 — generative tools. */
#include "gen.h"
#include "arp.h"

static uint32_t r;
static uint32_t rnd(void) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r; }
static int rr(int n) { return n > 0 ? (int)(rnd() % (uint32_t)n) : 0; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

#define STEP 24                                     /* one 1/16 in 96 PPQN units */
#define BAR  LOOP_BAR

static bool room(const looper_t *l, int n) { return l->n + n <= LOOP_MAX_EVENTS - 8; }

static void note(looper_t *l, int pos, int dur, uint8_t ch, int n, int vel)
{
    if (!room(l, 2) || n < 0 || n > 127) return;
    looper_insert(l, (uint16_t)(pos % l->length), ch, (uint8_t)n, (uint8_t)clampi(vel, 1, 127));
    looper_insert(l, (uint16_t)((pos + dur) % l->length), ch, (uint8_t)n, 0);
}

static void compose(looper_t *l, const theory_cfg_t *t, gen_chord_fn chord, uint8_t ch, uint32_t seed, int sparse);

void gen_generate(looper_t *l, const theory_cfg_t *t, gen_chord_fn chord, uint8_t ch, uint32_t seed)
{
    /* long loops: compose sparser material until the whole loop fits in the event memory */
    for (int sparse = 0; sparse < 3; sparse++) {
        compose(l, t, chord, ch, seed, sparse);
        int bars = l->length / BAR, unique = bars > 4 ? 4 : bars;
        if (bars <= 4 || l->n * (bars / unique) <= LOOP_MAX_EVENTS - 8) break;
    }
    int bars = l->length / BAR; if (bars < 1) bars = 1;
    int unique = bars > 4 ? 4 : bars;
    int base = l->n;                                 /* repeat the composed bars to fill the loop: */
    for (int rep = 1; rep < bars / unique; rep++) {  /* one copy per block, and only whole blocks, so */
        if (!room(l, base)) break;                   /* every note-on keeps its note-off */
        for (int i = 0; i < base; i++) {
            loop_ev_t e = l->ev[i];
            looper_insert(l, (uint16_t)((e.pos + (uint32_t)rep * (uint32_t)unique * BAR) % l->length), e.ch, e.note, e.vel);
        }
    }
}

static void compose(looper_t *l, const theory_cfg_t *t, gen_chord_fn chord, uint8_t ch, uint32_t seed, int sparse)
{
    r = seed | 1;
    looper_clear(l);
    static const uint8_t CK[] = { 3, 2, 5, 3, 4 }, CN[] = { 8, 8, 16, 16, 16 };
    int ci = rr(5), ck = CK[ci], cn = CN[ci], crot = rr(3);
    int mk = 5 + rr(5), mrot = rr(4);
    if (sparse >= 1) { ck = 2; cn = 8; mk = mk > 6 ? 6 : mk; }          /* fewer stabs, shorter phrases */
    if (sparse >= 2) { ck = 1; cn = 8; mk = 4; }
    int bars = l->length / BAR; if (bars < 1) bars = 1;
    int unique = bars > 4 ? 4 : bars;               /* compose up to 4 bars, then repeat them */
    int steps = l->length < BAR ? l->length / STEP : 16;
    int pad = 0, deg = 7;
    for (int b = 0; b < unique; b++) {
        int bar0 = b * BAR;
        if (b > 0) { int np; do np = rr(8); while (np == pad); pad = np; }   /* one chord per bar, start on pad 1 */
        uint8_t cnotes[8]; int nc = chord(pad, cnotes);
        /* chord stabs on the euclidean hits */
        int hits[16], nh = 0;
        for (int s = 0; s < steps; s++) if (arp_euclid_hit((uint8_t)ck, (uint8_t)cn, (uint16_t)((s + crot) % cn))) hits[nh++] = s;
        for (int h = 0; h < nh; h++) {
            int next = h + 1 < nh ? hits[h + 1] : steps;
            int dur = (next - hits[h]) * STEP - 12; if (dur < STEP) dur = STEP;
            int vel = 88 + rr(14) - (hits[h] % 4 ? 12 : 0);
            for (int i = 0; i < nc; i++) note(l, bar0 + hits[h] * STEP, dur, ch, cnotes[i], vel);
        }
        /* melody: random walk in the scale over the chord, on its own euclidean rhythm */
        if (nc) { int q = quantize(t, cnotes[0]); if (b == 0) deg = note_to_degree(t, q) + scale_size(t->scale); }
        nh = 0;
        for (int s = 0; s < steps; s++) if (arp_euclid_hit((uint8_t)mk, 16, (uint16_t)((s + mrot) % 16))) hits[nh++] = s;
        static const int8_t WALK[] = { -2, -1, -1, 0, 1, 1, 2, 3, -3, 1 };
        for (int h = 0; h < nh; h++) {
            if (rr(100) < 18) continue;              /* breathe */
            deg += WALK[rr(10)];
            int n = t->root + scale_degree_offset(t->scale, deg);
            while (n > 86) { deg -= scale_size(t->scale); n -= 12; }
            while (n < 64) { deg += scale_size(t->scale); n += 12; }
            int next = h + 1 < nh ? hits[h + 1] : steps;
            int dur = (next - hits[h]) * STEP; dur = dur > 2 * STEP ? 2 * STEP : dur - 4;
            if (dur < 12) dur = 12;
            note(l, bar0 + hits[h] * STEP, dur, ch, n, 72 + rr(26) + (hits[h] % 4 == 0 ? 14 : 0));
        }
    }
}

/* matching note-off for the note-on at index i (first off of the same note/channel after it, circularly) */
static int pair_off(const looper_t *l, int i)
{
    const loop_ev_t *on = &l->ev[i]; int best = -1, bestd = 1 << 30;
    for (int j = 0; j < l->n; j++) {
        const loop_ev_t *e = &l->ev[j];
        if (e->vel || e->note != on->note || e->ch != on->ch) continue;
        int d = ((int)e->pos - (int)on->pos + l->length) % l->length;
        if (d == 0) d = l->length;
        if (d < bestd) { bestd = d; best = j; }
    }
    return best;
}

void gen_mutate(looper_t *l, const theory_cfg_t *t, int percent, uint32_t seed)
{
    r = seed | 1;
    looper_jump(l, l->pos);                          /* release sounding notes before editing pitches */
    for (int i = 0; i < l->n; i++) {
        loop_ev_t *e = &l->ev[i];
        if (!e->vel || rr(100) >= percent) continue;
        int off = pair_off(l, i);
        switch (rr(3)) {
        case 0: {                                    /* a scale step up or down */
            int q = quantize(t, e->note), d = note_to_degree(t, q) + (rr(2) ? 1 : -1);
            int n = clampi(t->root + scale_degree_offset(t->scale, d), 24, 108);
            e->note = (uint8_t)n; if (off >= 0) l->ev[off].note = (uint8_t)n;
            break; }
        case 1: {                                    /* nudge by a 1/16, keeping the length */
            int sh = rr(2) ? STEP : -STEP;
            e->pos = (uint16_t)((e->pos + l->length + sh) % l->length);
            if (off >= 0) l->ev[off].pos = (uint16_t)((l->ev[off].pos + l->length + sh) % l->length);
            break; }
        default:                                     /* new velocity */
            e->vel = (uint8_t)clampi(e->vel + rr(51) - 25, 20, 127);
            break;
        }
    }
    l->edits++;
}

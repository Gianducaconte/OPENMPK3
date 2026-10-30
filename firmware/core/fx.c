/* OPENMPK3 — MIDI effects stage. */
#include "fx.h"
#include "arp.h"
#include <string.h>

const char *const OCTAVER_NAMES[OCT_COUNT] = { "Off", "+1 oct", "-1 oct", "+2 oct", "-2 oct", "+/-1 oct" };
const char *const GATE_NAMES[GATE_PATTERNS] = { "Off", "1/8 pulse", "1/4 pulse", "Offbeat", "Gallop", "Syncopated",
                                                "Stutter", "Half bar", "Trance" };
static const uint16_t GATE_PAT[GATE_PATTERNS] = { 0xFFFF, 0x5555, 0x1111, 0x4444, 0xDDDD, 0x2449, 0x5777, 0x00FF, 0x5D5D };
const char *const EUCLID_NAMES[EUCLID_PRESETS] = { "Off", "3 of 8", "5 of 8", "3 of 16", "5 of 16", "7 of 16", "9 of 16", "11 of 16" };
static const uint8_t EUC_K[EUCLID_PRESETS] = { 0, 3, 5, 3, 5, 7, 9, 11 };
static const uint8_t EUC_N[EUCLID_PRESETS] = { 0, 8, 8, 16, 16, 16, 16, 16 };

const char *const HARMONY_NAMES[HARM_COUNT] = { "Off", "+3rd", "+4th", "+5th", "+6th", "-3rd", "-4th", "3rd+5th" };

static fx_cfg_t *C;
static uint32_t rng = 0x9E3779B9u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int clampn(int n) { return n < 0 ? 0 : n > 127 ? 127 : n; }

/* ---- sources: output notes each held source produced, so its note-off releases exactly those ---- */
#define NSRC 40
#define NOUT 6
typedef struct { bool used; uint8_t ch, note, from, n, vel, out[NOUT], fired, gated; } src_t;
static src_t src[NSRC];

/* ---- scheduler: delayed note-ons (strum) and generated notes (echo, ratchet) ---- */
#define NQ 96
typedef struct { bool used; uint32_t t; uint8_t ch, note, vel; int8_t s, o; } qev_t;   /* s = source or -1 */
static qev_t q[NQ];

/* ---- outputs, refcounted per (channel, note) so overlapping sources don't cut each other.
 * The same pitch on two channels is two different notes: each gets its own count and its own note-off. ---- */
#define NACT 64
static struct { uint8_t ch, note, cnt; } act[NACT];
static uint8_t nact;

static int act_find(uint8_t ch, uint8_t n)
{
    for (int i = 0; i < nact; i++) if (act[i].ch == ch && act[i].note == n) return i;
    return -1;
}
static void out_on(uint8_t ch, uint8_t n, uint8_t v)
{
    int i = act_find(ch, n);
    if (i >= 0) { if (act[i].cnt < 255) act[i].cnt++; }
    else if (nact < NACT) { act[nact].ch = ch; act[nact].note = n; act[nact].cnt = 1; nact++; }
    C->out(ch, n, v ? v : 1);
}
static void out_off(uint8_t ch, uint8_t n)
{
    int i = act_find(ch, n);
    if (i < 0) { C->out(ch, n, 0); return; }        /* untracked (table was full): a spare note-off is harmless */
    if (--act[i].cnt) return;                       /* still held by another source on this channel */
    act[i] = act[--nact];
    C->out(ch, n, 0);
}

static int free_slots(void) { int f = 0; for (int i = 0; i < NQ; i++) f += !q[i].used; return f; }
static void schedule(uint32_t t, uint8_t ch, uint8_t note, uint8_t vel, int s, int o)
{
    for (int i = 0; i < NQ; i++) if (!q[i].used) { q[i] = (qev_t){ true, t, ch, note, vel, (int8_t)s, (int8_t)o }; return; }
    if (vel == 0) out_off(ch, note);                  /* never lose a note-off */
}
/* a generated note: on at t, off at t + len (both or nothing, so it can never stick) */
static void schedule_pair(uint32_t t, uint32_t len, uint8_t ch, uint8_t note, uint8_t vel)
{
    if (free_slots() < 2 + 8) return;
    schedule(t, ch, note, vel, -1, -1);
    schedule(t + len, ch, note, 0, -1, -1);
}

static int scale_steps(int note, int steps)          /* `steps` scale steps from `note`, in key */
{
    if (!steps) return note;
    const theory_cfg_t *t = C->theory;
    int q2 = quantize(t, note);
    return clampn(t->root + scale_degree_offset(t->scale, note_to_degree(t, q2) + steps) + (note - q2));
}

static void generated(uint8_t ch, uint8_t note, uint8_t vel, uint32_t t0, bool echo_too)
{
    if (C->ratchet && C->ratchet_ms) {                /* fast repeats right after the note */
        uint32_t iv = C->ratchet_ms, len = iv / 2 > 8 ? iv / 2 : 8; int v = vel;
        for (int k = 1; k <= C->ratchet; k++) { v = v * 7 / 8; schedule_pair(t0 + iv * (uint32_t)k, len, ch, note, (uint8_t)(v < 1 ? 1 : v)); }
    }
    if (echo_too && C->echo && C->echo_ms) {          /* spiral: fading, one scale step higher each repeat */
        uint32_t iv = C->echo_ms, len = iv / 2 > 20 ? iv / 2 : 20; int v = vel;
        for (int k = 1; k <= C->echo; k++) {
            v = v * 3 / 4; if (v < 8) break;
            schedule_pair(t0 + iv * (uint32_t)k, len, ch, (uint8_t)scale_steps(note, k), (uint8_t)v);
        }
    }
}

static uint8_t out_vel(int s, int o) { return (uint8_t)(src[s].vel - (o ? src[s].vel / 5 : 0)); }

static void start_output(int s, int o, uint32_t t, uint32_t now)
{
    src_t *x = &src[s];
    if ((int32_t)(t - now) <= 0) {
        out_on(x->ch, x->out[o], out_vel(s, o));
        x->fired |= (uint8_t)(1u << o);
        generated(x->ch, x->out[o], out_vel(s, o), now, o == 0);
    } else schedule(t, x->ch, x->out[o], out_vel(s, o), s, o);
}

/* ---- strum groups ---- */
static bool grouping, strum_down;
static struct { uint8_t s, o; } grp[16]; static uint8_t ngrp;

void fx_init(fx_cfg_t *cfg)
{
    C = cfg; memset(src, 0, sizeof src); memset(q, 0, sizeof q); nact = 0;
    grouping = false; ngrp = 0;
}

void fx_note(uint8_t ch, uint8_t note, uint8_t vel, uint32_t now, uint8_t from)
{
    if (!vel) {                                       /* release whatever this source produced */
        int s = -1;                                   /* same channel + note, preferring the same origin */
        for (int i = 0; i < NSRC && s < 0; i++) if (src[i].used && src[i].ch == ch && src[i].note == note && src[i].from == from) s = i;
        for (int i = 0; i < NSRC && s < 0; i++) if (src[i].used && src[i].ch == ch && src[i].note == note) s = i;
        if (s >= 0) {
            for (int o = 0; o < src[s].n; o++) {
                if ((src[s].fired >> o & 1) && !(src[s].gated >> o & 1)) out_off(ch, src[s].out[o]);
                else if (!(src[s].fired >> o & 1))
                    for (int i = 0; i < NQ; i++) if (q[i].used && q[i].s == s && q[i].o == o) q[i].used = false;
            }
            src[s].used = false;
        }
        return;
    }
    int s = -1;
    for (int i = 0; i < NSRC; i++) if (!src[i].used) { s = i; break; }
    if (s < 0) return;                                /* table full: skip the note rather than risk a stuck one */
    src_t *x = &src[s];
    memset(x, 0, sizeof *x); x->used = true; x->ch = ch; x->note = note; x->from = from; x->vel = vel;
    if (from != FXS_LIVE) {                           /* density + euclidean filter: loop and arp only */
        if (C->thin && rnd() % 100 < C->thin) return;                       /* source kept: its off is swallowed */
        if (C->euclid && !arp_euclid_hit(EUC_K[C->euclid], EUC_N[C->euclid], C->step16)) return;
    }
    if (C->accent && from != FXS_LIVE) {             /* generated accent: beat strong, 8ths normal, 16ths ghost */
        int st = C->step16 & 3, v = x->vel;
        v = st == 0 ? v + 24 : st == 2 ? v : v * 3 / 5;
        x->vel = (uint8_t)(v < 1 ? 1 : v > 127 ? 127 : v);
    }
    int n = note;
    if (from == FXS_LOOP) n = scale_steps(n, C->shift);
    if (C->drift) {                                   /* triangular random walk within +-drift scale steps */
        int a = (int)(rnd() % (C->drift + 1)), b = (int)(rnd() % (C->drift + 1));
        n = scale_steps(n, a - b);
    }
    x->out[x->n++] = (uint8_t)n;
    switch (C->harmony) {                             /* shadow voices, in the scale */
    case HARM_3RD:    x->out[x->n++] = (uint8_t)scale_steps(n, 2); break;
    case HARM_4TH:    x->out[x->n++] = (uint8_t)scale_steps(n, 3); break;
    case HARM_5TH:    x->out[x->n++] = (uint8_t)scale_steps(n, 4); break;
    case HARM_6TH:    x->out[x->n++] = (uint8_t)scale_steps(n, 5); break;
    case HARM_3RD_DN: x->out[x->n++] = (uint8_t)scale_steps(n, -2); break;
    case HARM_4TH_DN: x->out[x->n++] = (uint8_t)scale_steps(n, -3); break;
    case HARM_TRIAD:  x->out[x->n++] = (uint8_t)scale_steps(n, 2); x->out[x->n++] = (uint8_t)scale_steps(n, 4); break;
    default: break;
    }
    switch (C->octaver) {
    case OCT_UP1:   x->out[x->n++] = (uint8_t)clampn(n + 12); break;
    case OCT_DOWN1: x->out[x->n++] = (uint8_t)clampn(n - 12); break;
    case OCT_UP2:   x->out[x->n++] = (uint8_t)clampn(n + 24); break;
    case OCT_DOWN2: x->out[x->n++] = (uint8_t)clampn(n - 24); break;
    case OCT_BOTH:  x->out[x->n++] = (uint8_t)clampn(n + 12); x->out[x->n++] = (uint8_t)clampn(n - 12); break;
    default: break;
    }
    for (int o = 0; o < x->n; o++) {
        if (grouping && ngrp < 16) { grp[ngrp].s = (uint8_t)s; grp[ngrp].o = (uint8_t)o; ngrp++; }
        else start_output(s, o, now, now);
    }
}

void fx_group_begin(void) { grouping = true; ngrp = 0; }

void fx_group_end(uint32_t now)
{
    grouping = false;
    if (!ngrp) return;
    for (int i = 1; i < ngrp; i++) for (int j = i; j > 0; j--) {      /* order by pitch */
        int a = src[grp[j - 1].s].out[grp[j - 1].o], b = src[grp[j].s].out[grp[j].o];
        if (a > b) { uint8_t ts = grp[j].s, to = grp[j].o; grp[j] = grp[j - 1]; grp[j - 1].s = ts; grp[j - 1].o = to; }
    }
    if (C->strum_ms && ngrp > 1) strum_down = !strum_down;
    for (int i = 0; i < ngrp; i++) {
        int k = strum_down ? ngrp - 1 - i : i;
        start_output(grp[i].s, grp[i].o, now + (uint32_t)k * C->strum_ms, now);
    }
    ngrp = 0;
}

void fx_step16(uint8_t step, uint32_t now)
{
    (void)now;
    C->step16 = step & 15;
    bool open = (GATE_PAT[C->gate < GATE_PATTERNS ? C->gate : 0] >> (step & 15)) & 1;
    for (int s = 0; s < NSRC; s++) {
        src_t *x = &src[s];
        if (!x->used) continue;
        for (int o = 0; o < x->n; o++) {
            if (!(x->fired >> o & 1)) continue;
            bool gated = x->gated >> o & 1;
            if (!open && !gated) { out_off(x->ch, x->out[o]); x->gated |= (uint8_t)(1u << o); }
            else if (open && gated) { out_on(x->ch, x->out[o], out_vel(s, o)); x->gated &= (uint8_t)~(1u << o); }
        }
    }
}

void fx_poll(uint32_t now)
{
    for (int i = 0; i < NQ; i++) {
        if (!q[i].used || (int32_t)(now - q[i].t) < 0) continue;
        qev_t e = q[i]; q[i].used = false;
        if (e.vel == 0) { out_off(e.ch, e.note); continue; }
        if (e.s >= 0) {
            src_t *x = &src[(int)e.s];
            if (!x->used) continue;
            x->fired |= (uint8_t)(1u << e.o);
            out_on(e.ch, e.note, e.vel);
            generated(e.ch, e.note, e.vel, now, e.o == 0);
        } else out_on(e.ch, e.note, e.vel);
    }
}

void fx_panic(void)
{
    memset(q, 0, sizeof q); memset(src, 0, sizeof src);
    for (int i = 0; i < nact; i++) C->out(act[i].ch, act[i].note, 0);
    nact = 0;
    grouping = false; ngrp = 0;
}

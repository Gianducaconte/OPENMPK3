/* OPENMPK3 — arpeggiator. */
#include "arp.h"
#include <string.h>

const char *const ARP_MODE_NAMES[ARP_MODE_COUNT] = {
    "Up", "Down", "Up/Down", "Down/Up", "Order", "Random", "Chord",
    "Converge", "Pinky", "Thumb", "ScaleWalk", "Bounce", "Up/Dn Incl" };
const char *const ARP_RATE_NAMES[ARP_RATE_COUNT] = {
    "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T" };
const uint8_t ARP_RATE_TICKS[ARP_RATE_COUNT] = { 24, 16, 12, 8, 6, 4, 3, 2 };

static uint32_t rnd(arp_t *a) { a->rng ^= a->rng << 13; a->rng ^= a->rng >> 17; a->rng ^= a->rng << 5; return a->rng; }

void arp_init(arp_t *a, arp_out_fn out, void *ctx)
{
    memset(a, 0, sizeof *a);
    a->mode = ARP_UP; a->rate = 4; a->gate = 60; a->swing = 50; a->octaves = 1; a->ratchet = 1;
    a->tick_ms = 21; a->rng = 0x1234567u; a->out = out; a->ctx = ctx;
}

static void stop_sound(arp_t *a)
{
    if (a->sounding) for (int i = 0; i < a->nplaying; i++) a->out(a->playing[i], 0, false, a->ctx);
    a->sounding = false;
}
static void start_sound(arp_t *a, uint32_t now)
{
    for (int i = 0; i < a->nplaying; i++) a->out(a->playing[i], a->last_vel, true, a->ctx);
    a->sounding = true;
    uint32_t g = a->ratchet_len_ms * a->gate / 100; if (g < 5) g = 5;
    a->off_at_ms = now + g;
}
static void all_off(arp_t *a) { stop_sound(a); a->nplaying = 0; a->ratchet_left = 0; }

void arp_note_on(arp_t *a, uint8_t note, uint8_t vel)
{
    if (a->latch && a->phys_down == 0) a->nheld = 0;   /* new gesture replaces latched chord */
    a->phys_down++;
    for (int i = 0; i < a->nheld; i++) if (a->held[i] == note) return;
    if (a->nheld < ARP_MAX_HELD) { a->held[a->nheld] = note; a->held_vel[a->nheld++] = vel; }
    a->dirty = true;
}

void arp_note_off(arp_t *a, uint8_t note)
{
    if (a->phys_down) a->phys_down--;
    if (a->latch) return;
    for (int i = 0; i < a->nheld; i++) if (a->held[i] == note) {
        memmove(a->held + i, a->held + i + 1, (size_t)(a->nheld - i - 1));
        memmove(a->held_vel + i, a->held_vel + i + 1, (size_t)(a->nheld - i - 1));
        a->nheld--; a->dirty = true; break;
    }
    if (a->nheld == 0) { all_off(a); a->step = 0; }
}

void arp_clear(arp_t *a) { all_off(a); a->nheld = 0; a->phys_down = 0; a->nseq = 0; a->step = 0; }
void arp_reset_phase(arp_t *a) { a->step = 0; a->tick_in_step = 0; }

bool arp_euclid_hit(uint8_t k, uint8_t n, uint16_t step)
{
    if (n == 0 || k >= n) return true;
    if (k == 0) return false;
    /* Bjorklund-equivalent: hit when floor((s*k)/n) changes */
    uint16_t s = step % n;
    return ((s * k) % n) < k;
}

static void sort_asc(uint8_t *v, int n)
{
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && v[j] < v[j-1]; j--) { uint8_t t = v[j]; v[j] = v[j-1]; v[j-1] = t; }
}

static void push(arp_t *a, int note) { if (a->nseq < ARP_MAX_SEQ && note >= 0 && note <= 127) a->seq[a->nseq++] = (uint8_t)note; }

static void rebuild(arp_t *a)
{
    uint8_t base[ARP_MAX_HELD * 4]; int nb = 0;
    uint8_t sorted[ARP_MAX_HELD]; memcpy(sorted, a->held, a->nheld); sort_asc(sorted, a->nheld);
    const uint8_t *src = (a->mode == ARP_ORDER) ? a->held : sorted;
    int oct = a->octaves ? a->octaves : 1;
    for (int o = 0; o < oct; o++) for (int i = 0; i < a->nheld; i++) {
        int n = src[i] + 12 * o; if (n <= 127) base[nb++] = (uint8_t)n; }
    a->nseq = 0;
    switch (a->mode) {
    case ARP_DOWN:   for (int i = nb - 1; i >= 0; i--) push(a, base[i]); break;
    case ARP_UPDOWN: for (int i = 0; i < nb; i++) push(a, base[i]); for (int i = nb - 2; i > 0; i--) push(a, base[i]); break;
    case ARP_INCL:   for (int i = 0; i < nb; i++) push(a, base[i]); for (int i = nb - 1; i >= 0; i--) push(a, base[i]); break;
    case ARP_DOWNUP: for (int i = nb - 1; i >= 0; i--) push(a, base[i]); for (int i = 1; i < nb - 1; i++) push(a, base[i]); break;
    case ARP_CONVERGE: for (int l = 0, r = nb - 1; l <= r; l++, r--) { push(a, base[l]); if (l != r) push(a, base[r]); } break;
    case ARP_PINKY:  for (int i = 0; i < nb - 1; i++) { push(a, base[i]); push(a, base[nb - 1]); } if (nb == 1) push(a, base[0]); break;
    case ARP_THUMB:  for (int i = 1; i < nb; i++) { push(a, base[0]); push(a, base[i]); } if (nb == 1) push(a, base[0]); break;
    case ARP_BOUNCE: for (int i = 0; i < nb; i++) { push(a, base[i]); if (i + 1 < nb) push(a, base[i + 1]); } break;
    case ARP_SCALEWALK:
        if (a->theory && nb) {   /* climb the scale from lowest to highest held note (+octaves) */
            int lo = base[0], hi = base[nb - 1];
            if (hi == lo) hi = lo + 12 * oct;
            int d = note_to_degree(a->theory, quantize(a->theory, lo));
            for (int n = 0; n < ARP_MAX_SEQ; n++, d++) {
                int note = a->theory->root + scale_degree_offset(a->theory->scale, d);
                while (note < lo - 6) note += 12;
                if (note > hi) break;
                push(a, note);
            }
            break;
        } /* fallthrough */
    default:         for (int i = 0; i < nb; i++) push(a, base[i]); break;   /* UP, ORDER, RANDOM, CHORD use base */
    }
    a->dirty = false;
}

static void fire(arp_t *a, uint32_t now)
{
    all_off(a);
    if (!a->nseq) return;
    uint8_t vel = a->held_vel[a->nheld ? a->nheld - 1 : 0];
    if (vel == 0) vel = 100;
    if (a->accent) vel = (a->step % 4 == 0) ? (vel + 20 > 127 ? 127 : vel + 20) : (vel > 20 ? vel - 15 : vel);
    if (a->mode == ARP_CHORD) {
        for (int i = 0; i < a->nheld && a->nplaying < 8; i++) a->playing[a->nplaying++] = a->held[i];
    } else {
        a->playing[a->nplaying++] = (a->mode == ARP_RANDOM) ? a->seq[rnd(a) % a->nseq] : a->seq[a->step % a->nseq];
    }
    a->last_vel = vel;
    uint8_t r = a->ratchet ? a->ratchet : 1;
    a->ratchet_len_ms = a->tick_ms * ARP_RATE_TICKS[a->rate] / r;
    a->ratchet_left = r - 1;
    a->ratchet_next_ms = now + a->ratchet_len_ms;
    start_sound(a, now);
}

void arp_tick(arp_t *a, uint32_t now)
{
    if (a->nheld == 0) return;
    if (a->dirty) rebuild(a);
    uint8_t len = ARP_RATE_TICKS[a->rate];
    /* swing: delay odd steps by (swing-50)% of a step */
    uint8_t fire_at = 0;
    if (a->step & 1) fire_at = (uint8_t)((uint32_t)len * (a->swing - 50) / 50);
    if (fire_at >= len) fire_at = len - 1;
    if (a->tick_in_step == fire_at) {
        if (arp_euclid_hit(a->euclid_k, a->euclid_n, a->step)) fire(a, now);
    }
    if (++a->tick_in_step >= len) {
        a->tick_in_step = 0;
        a->step++;
        if (a->mode != ARP_CHORD && a->nseq && a->step >= a->nseq * 64) a->step %= a->nseq;
    }
}

void arp_poll(arp_t *a, uint32_t now)
{
    if (a->sounding && (int32_t)(now - a->off_at_ms) >= 0) stop_sound(a);
    if (a->ratchet_left && (int32_t)(now - a->ratchet_next_ms) >= 0) {
        stop_sound(a);
        a->ratchet_left--;
        a->ratchet_next_ms += a->ratchet_len_ms;
        start_sound(a, now);
    }
}

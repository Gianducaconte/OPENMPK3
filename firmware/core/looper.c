/* OPENMPK3 — live overdub looper. */
#include "looper.h"
#include <string.h>

void looper_init(looper_t *l, loop_out_fn out, void *ctx)
{
    memset(l, 0, sizeof *l);
    l->length = 2 * LOOP_BAR; l->layer = 1; l->out = out; l->ctx = ctx;
}

bool looper_empty(const looper_t *l) { return l->n == 0; }

static void snd_on(looper_t *l, uint8_t ch, uint8_t note, uint8_t layer)
{
    for (int i = 0; i < l->nsnd; i++) if (l->snd[i].note == note && l->snd[i].ch == ch) return;
    if (l->nsnd == 32) {                             /* full: release the oldest instead of losing track of this one */
        l->out(l->snd[0].ch, l->snd[0].note, 0, l->ctx);
        memmove(&l->snd[0], &l->snd[1], 31 * sizeof l->snd[0]);
        l->nsnd = 31;
    }
    l->snd[l->nsnd].note = note; l->snd[l->nsnd].ch = ch; l->snd[l->nsnd].layer = layer; l->nsnd++;
}
static bool snd_off(looper_t *l, uint8_t ch, uint8_t note)
{
    for (int i = 0; i < l->nsnd; i++) if (l->snd[i].note == note && l->snd[i].ch == ch) {
        l->snd[i] = l->snd[--l->nsnd]; return true; }
    return false;
}
static void release_where(looper_t *l, int layer, int note)    /* -1 = any */
{
    for (int i = 0; i < l->nsnd; ) {
        if ((layer < 0 || l->snd[i].layer == layer) && (note < 0 || l->snd[i].note == note)) {
            l->out(l->snd[i].ch, l->snd[i].note, 0, l->ctx);
            l->snd[i] = l->snd[--l->nsnd];
        } else i++;
    }
}

void looper_set_length(looper_t *l, uint16_t len)
{
    if (len < LOOP_PPQN) len = LOOP_PPQN;
    l->length = len;
    if (l->pos >= len) l->pos = (uint16_t)(l->pos % len);
}

static void add(looper_t *l, uint16_t pos, uint8_t ch, uint8_t note, uint8_t vel)
{
    if (l->n >= LOOP_MAX_EVENTS) { l->full = true; return; }
    l->ev[l->n++] = (loop_ev_t){ (uint16_t)(pos % l->length), note, vel, ch, l->layer };
    l->edits++;
}

void looper_insert(looper_t *l, uint16_t pos, uint8_t ch, uint8_t note, uint8_t vel) { add(l, pos, ch, note, vel); }

static void close_held(looper_t *l)
{
    for (int i = 0; i < l->nheld; i++) {
        int p = (int)l->pos + l->held[i].shift;
        p %= l->length; if (p < 0) p += l->length;
        add(l, (uint16_t)p, l->held[i].ch, l->held[i].note, 0);
    }
    l->nheld = 0;
}

void looper_play(looper_t *l, bool on)
{
    if (on == l->playing) return;
    if (!on) { if (l->recording) { close_held(l); l->recording = false; } release_where(l, -1, -1); }
    l->playing = on; l->pos = 0;
}

void looper_record(looper_t *l, bool on)
{
    if (on == l->recording) return;
    if (on) {
        if (!l->playing) looper_play(l, true);
        /* new pass id; skip ids still used by old passes after wrap-around */
        bool used;
        do {
            l->layer = (uint8_t)(l->layer == 255 ? 1 : l->layer + 1);
            used = false;
            for (int i = 0; i < l->n; i++) if (l->ev[i].layer == l->layer) { used = true; break; }
        } while (used);
    } else close_held(l);
    l->recording = on;
}

void looper_input(looper_t *l, uint8_t ch, uint8_t note, uint8_t vel)
{
    if (!l->recording) return;
    if (vel) {
        int shift = 0;
        if (l->quant) {
            int q = l->quant, p = l->pos, snapped = ((p + q / 2) / q) * q;
            shift = snapped - p;
        }
        int p = (int)l->pos + shift; p %= l->length; if (p < 0) p += l->length;
        add(l, (uint16_t)p, ch, note, vel);
        if (l->nheld < 16) { l->held[l->nheld].note = note; l->held[l->nheld].ch = ch; l->held[l->nheld].shift = (int16_t)shift; l->nheld++; }
    } else {
        for (int i = 0; i < l->nheld; i++) if (l->held[i].note == note && l->held[i].ch == ch) {
            int p = (int)l->pos + l->held[i].shift; p %= l->length; if (p < 0) p += l->length;
            add(l, (uint16_t)p, ch, note, 0);
            l->held[i] = l->held[--l->nheld];
            break;
        }
    }
}

static void fire(looper_t *l, const loop_ev_t *e)
{
    if (e->vel) {
        if (l->recording) for (int i = 0; i < l->nheld; i++)          /* don't double a note being played live */
            if (l->held[i].note == e->note && l->held[i].ch == e->ch) return;
        if (snd_off(l, e->ch, e->note)) l->out(e->ch, e->note, 0, l->ctx);   /* retrigger cleanly */
        l->out(e->ch, e->note, e->vel, l->ctx);
        snd_on(l, e->ch, e->note, e->layer);
    } else if (snd_off(l, e->ch, e->note)) {
        l->out(e->ch, e->note, 0, l->ctx);
    }
}

void looper_advance(looper_t *l, uint16_t np)
{
    if (!l->playing) return;
    np = (uint16_t)(np % l->length);
    if (np == l->pos) return;
    uint16_t a = l->pos;
    /* offs first, then ons, so back-to-back notes at the same position retrigger */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < l->n; i++) {
            const loop_ev_t *e = &l->ev[i];
            if ((pass == 0) != (e->vel == 0)) continue;
            bool in = (np > a) ? (e->pos > a && e->pos <= np) : (e->pos > a || e->pos <= np);
            if (in) fire(l, e);
        }
    l->pos = np;
}

void looper_jump(looper_t *l, uint16_t pos)
{
    release_where(l, -1, -1);
    l->pos = (uint16_t)(pos % l->length);
}

void looper_undo(looper_t *l)
{
    if (!l->n) return;
    uint8_t last = l->recording ? l->layer : l->ev[l->n - 1].layer;
    if (l->recording) l->nheld = 0;
    int k = 0;
    for (int i = 0; i < l->n; i++) if (l->ev[i].layer != last) l->ev[k++] = l->ev[i];
    l->n = (uint16_t)k; l->full = false; l->edits++;
    release_where(l, last, -1);
}

void looper_clear(looper_t *l)
{
    release_where(l, -1, -1);
    l->n = 0; l->nheld = 0; l->full = false; l->edits++;
}

void looper_clear_channel(looper_t *l, uint8_t ch)
{
    int k = 0;
    for (int i = 0; i < l->n; i++) if (l->ev[i].ch != ch) l->ev[k++] = l->ev[i];
    l->n = (uint16_t)k; l->edits++; l->full = false;
    for (int i = 0; i < l->nheld; ) { if (l->held[i].ch == ch) l->held[i] = l->held[--l->nheld]; else i++; }
    for (int i = 0; i < l->nsnd; ) {
        if (l->snd[i].ch == ch) { l->out(ch, l->snd[i].note, 0, l->ctx); l->snd[i] = l->snd[--l->nsnd]; } else i++;
    }
}

void looper_erase_note(looper_t *l, uint8_t note, uint8_t ch)
{
    int k = 0;
    for (int i = 0; i < l->n; i++) if (l->ev[i].note != note || (ch != LOOP_ANY_CH && l->ev[i].ch != ch)) l->ev[k++] = l->ev[i];
    l->n = (uint16_t)k; l->edits++;
    for (int i = 0; i < l->nheld; ) { if (l->held[i].note == note && (ch == LOOP_ANY_CH || l->held[i].ch == ch)) l->held[i] = l->held[--l->nheld]; else i++; }
    for (int i = 0; i < l->nsnd; ) {
        if (l->snd[i].note == note && (ch == LOOP_ANY_CH || l->snd[i].ch == ch)) { l->out(l->snd[i].ch, note, 0, l->ctx); l->snd[i] = l->snd[--l->nsnd]; }
        else i++;
    }
}

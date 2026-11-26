/* OPENMPK3 — live overdub looper. */
#include "looper.h"
#include <string.h>

void looper_init(looper_t *l, loop_out_fn out, void *ctx)
{
    memset(l, 0, sizeof *l);
    l->length = 2 * LOOP_BAR; l->layer = 1; l->out = out; l->ctx = ctx;
}

bool looper_empty(const looper_t *l) { return l->n == l->hidden; }

/* ---- notes sounding from playback ---- */
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
static void release_where(looper_t *l, int layer, int note, int ch)    /* -1 = any */
{
    for (int i = 0; i < l->nsnd; ) {
        if ((layer < 0 || l->snd[i].layer == layer) && (note < 0 || l->snd[i].note == note) && (ch < 0 || l->snd[i].ch == ch)) {
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

static uint16_t wrap(const looper_t *l, int p) { p %= l->length; if (p < 0) p += l->length; return (uint16_t)p; }

/* ---- event memory ---- */
void looper_commit(looper_t *l)                      /* drop the hidden events */
{
    if (!l->hidden) return;
    int k = 0;
    for (int i = 0; i < l->n; i++) if (looper_ev_visible(&l->ev[i])) l->ev[k++] = l->ev[i];
    l->n = (uint16_t)k; l->hidden = 0;
}
static bool room(looper_t *l, int k)                 /* space for k more events (restorable edits give theirs up) */
{
    if (l->n + k > LOOP_MAX_EVENTS) looper_commit(l);
    if (l->n + k > LOOP_MAX_EVENTS) { l->full = true; return false; }
    return true;
}
static void add(looper_t *l, uint16_t pos, uint8_t ch, uint8_t note, uint8_t vel)
{
    if (!room(l, 1)) return;
    l->ev[l->n++] = (loop_ev_t){ (uint16_t)(pos % l->length), note, vel, ch, l->layer };
    l->edits++;
}

void looper_insert(looper_t *l, uint16_t pos, uint8_t ch, uint8_t note, uint8_t vel) { add(l, pos, ch, note, vel); }

/* ---- recording ---- */
static void add_off(looper_t *l, int i)              /* note-off for held[i]; never on the very spot of its note-on */
{
    uint16_t p = wrap(l, (int)l->pos + l->held[i].shift);
    if (l->held[i].age >= l->length) p = wrap(l, l->held[i].at - 1);   /* held all the way round: sustain the loop */
    else if (p == l->held[i].at) p = wrap(l, p + 1); /* a zero-length note would play as a note that never ends */
    add(l, p, l->held[i].ch, l->held[i].note, 0);
}
static void close_held(looper_t *l, int ch)          /* store the note-off of held notes now; ch -1 = all */
{
    for (int i = 0; i < l->nheld; ) {
        if (ch < 0 || l->held[i].ch == ch) { add_off(l, i); l->held[i] = l->held[--l->nheld]; }
        else i++;
    }
}
static void next_layer(looper_t *l)                  /* new pass id, skipping ids still in the loop */
{
    for (int tries = 0; tries < 255; tries++) {
        l->layer = (uint8_t)(l->layer == 255 ? 1 : l->layer + 1);
        bool used = false;
        for (int i = 0; i < l->n; i++) if (l->ev[i].layer == l->layer) { used = true; break; }
        if (!used) return;
    }
}

void looper_play(looper_t *l, bool on)
{
    if (on == l->playing) return;
    if (!on) { if (l->recording) { close_held(l, -1); l->recording = false; } release_where(l, -1, -1, -1); }
    l->playing = on; l->pos = 0;
}

void looper_record(looper_t *l, bool on)
{
    if (on == l->recording) return;
    if (on) { if (!l->playing) looper_play(l, true); next_layer(l); }
    else close_held(l, -1);
    l->recording = on;
}

void looper_new_pass(looper_t *l)
{
    if (!l->recording) return;
    close_held(l, -1);
    next_layer(l);
}

void looper_input(looper_t *l, uint8_t ch, uint8_t note, uint8_t vel)
{
    if (!l->recording) return;
    if (vel) {
        /* only store a note-on whose note-off is sure to fit later, together with those of the notes already held */
        if (l->nheld >= 16 || !room(l, 2 + l->nheld)) return;
        int shift = 0;
        if (l->quant) {
            int q = l->quant, p = l->pos, snapped = ((p + q / 2) / q) * q;
            shift = snapped - p;
        }
        uint16_t p = wrap(l, (int)l->pos + shift);
        add(l, p, ch, note, vel);
        l->held[l->nheld].note = note; l->held[l->nheld].ch = ch; l->held[l->nheld].shift = (int16_t)shift;
        l->held[l->nheld].at = p; l->held[l->nheld].age = 0; l->nheld++;
    } else {
        for (int i = 0; i < l->nheld; i++) if (l->held[i].note == note && l->held[i].ch == ch) {
            add_off(l, i);
            l->held[i] = l->held[--l->nheld];
            break;
        }
    }
}

/* ---- playback ---- */
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
    bool wrapped = np < a;
    uint16_t moved = (uint16_t)(wrapped ? np + l->length - a : np - a);
    for (int i = 0; i < l->nheld; i++) l->held[i].age = (uint16_t)(l->held[i].age > 0xFFFF - moved ? 0xFFFF : l->held[i].age + moved);
    /* offs first, then ons, so back-to-back notes at the same position retrigger */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < l->n; i++) {
            const loop_ev_t *e = &l->ev[i];
            if (!looper_ev_visible(e) || (pass == 0) != (e->vel == 0)) continue;
            bool in;
            if (e->pos >= l->length) in = wrapped && !e->vel;   /* past the end of a shortened loop: offs close at
                                                                   the loop end, ons stay silent */
            else in = wrapped ? (e->pos > a || e->pos <= np) : (e->pos > a && e->pos <= np);
            if (in) fire(l, e);
        }
    l->pos = np;
}

void looper_jump(looper_t *l, uint16_t pos)
{
    release_where(l, -1, -1, -1);
    l->pos = (uint16_t)(pos % l->length);
}

/* ---- editing ---- */
static int hide_where(looper_t *l, int layer, int ch)    /* -1 = any */
{
    int k = 0;
    for (int i = 0; i < l->n; i++) {
        loop_ev_t *e = &l->ev[i];
        if (looper_ev_visible(e) && (layer < 0 || e->layer == layer) && (ch < 0 || e->ch == ch)) { e->ch |= LOOP_HIDDEN; k++; }
    }
    l->hidden = (uint16_t)(l->hidden + k);
    if (k) { l->edits++; l->full = false; }
    return k;
}

int looper_undo(looper_t *l)
{
    looper_commit(l);
    looper_new_pass(l);                              /* recording: what was played so far becomes the latest part */
    if (!l->n) return 0;
    uint8_t last = l->ev[l->n - 1].layer;            /* events are appended, so the last one is the newest pass */
    release_where(l, last, -1, -1);
    return hide_where(l, last, -1);
}

int looper_clear_channel(looper_t *l, uint8_t ch)
{
    looper_commit(l);
    close_held(l, ch);                               /* held notes on this channel get their off, then go too */
    release_where(l, -1, -1, ch);
    return hide_where(l, -1, ch);
}

int looper_clear_all(looper_t *l)
{
    looper_commit(l);
    looper_new_pass(l);
    release_where(l, -1, -1, -1);
    return hide_where(l, -1, -1);
}

int looper_restore(looper_t *l)
{
    int k = 0;
    for (int i = 0; i < l->n; i++) if (!looper_ev_visible(&l->ev[i])) { l->ev[i].ch &= (uint8_t)~LOOP_HIDDEN; k++; }
    l->hidden = 0;
    if (k) l->edits++;
    return k;
}

void looper_clear(looper_t *l)
{
    release_where(l, -1, -1, -1);
    l->n = 0; l->hidden = 0; l->nheld = 0; l->full = false; l->edits++;
}

void looper_erase_note(looper_t *l, uint8_t note, uint8_t ch)
{
    looper_commit(l);
    int k = 0;
    for (int i = 0; i < l->n; i++) if (l->ev[i].note != note || (ch != LOOP_ANY_CH && l->ev[i].ch != ch)) l->ev[k++] = l->ev[i];
    l->n = (uint16_t)k; l->edits++; l->full = false;
    for (int i = 0; i < l->nheld; ) { if (l->held[i].note == note && (ch == LOOP_ANY_CH || l->held[i].ch == ch)) l->held[i] = l->held[--l->nheld]; else i++; }
    release_where(l, -1, note, ch == LOOP_ANY_CH ? -1 : ch);
}

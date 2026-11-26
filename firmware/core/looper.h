/* OPENMPK3 — live overdub looper (Kaossilator style). Positions are in 96 PPQN units; one bar = 384. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define LOOP_MAX_EVENTS 600
#define LOOP_PPQN 96
#define LOOP_BAR (LOOP_PPQN * 4)
#define LOOP_HIDDEN 0x80          /* set in ev.ch: removed by undo/clear, kept until committed so it can be restored */

typedef struct { uint16_t pos; uint8_t note, vel, ch, layer; } loop_ev_t;   /* vel 0 = note off */
typedef void (*loop_out_fn)(uint8_t ch, uint8_t note, uint8_t vel, void *ctx);   /* vel 0 = off */

typedef struct {
    loop_ev_t ev[LOOP_MAX_EVENTS];
    uint16_t n;               /* events in ev[], hidden ones included */
    uint16_t hidden;          /* how many of them are hidden */
    uint16_t length;          /* in 96 PPQN units */
    uint16_t pos;             /* current playhead */
    bool playing, recording;
    uint8_t layer;            /* current overdub pass id (1..255) */
    uint8_t quant;            /* record quantize grid in 96 PPQN units, 0 = off */
    bool full;                /* ran out of event memory */
    uint16_t edits;           /* bumped on every content change (for auto-save) */
    /* notes currently sounding from playback, for clean stop/undo */
    struct { uint8_t note, ch, layer; } snd[32]; uint8_t nsnd;
    /* notes held while recording (to close them when recording stops): quantize shift, where the note-on landed */
    struct { uint8_t note, ch; int16_t shift; uint16_t at, age; } held[16]; uint8_t nheld;   /* age: units held */
    loop_out_fn out; void *ctx;
} looper_t;

/* Every note-on stored in the loop always has its note-off: recording reserves room for the offs of held notes,
 * and anything that ends a recording pass (stop, record off, undo, clear, a new pass) closes the held notes first. */
void looper_init(looper_t *l, loop_out_fn out, void *ctx);
void looper_set_length(looper_t *l, uint16_t len);
void looper_play(looper_t *l, bool on);           /* start from 0 / stop (sounding notes released) */
void looper_record(looper_t *l, bool on);         /* overdub on/off; starts playback if needed */
void looper_new_pass(looper_t *l);                /* while recording: close held notes, continue in a new pass */
void looper_input(looper_t *l, uint8_t ch, uint8_t note, uint8_t vel);   /* live note (vel 0 = off) */
void looper_advance(looper_t *l, uint16_t new_pos);   /* move playhead (wraps), firing events in (pos, new_pos] */
void looper_insert(looper_t *l, uint16_t pos, uint8_t ch, uint8_t note, uint8_t vel);   /* raw event, current layer */
void looper_jump(looper_t *l, uint16_t pos);       /* move the playhead without firing, releasing sounding notes */
void looper_clear(looper_t *l);                   /* remove everything (not restorable) */

/* Restorable edits: the removed events are hidden (silent, not saved) until looper_restore() brings them back or
 * looper_commit() drops them. Each of these first commits the previous one. They return how many events went. */
int  looper_undo(looper_t *l);                    /* the most recent overdub pass */
int  looper_clear_channel(looper_t *l, uint8_t ch);
int  looper_clear_all(looper_t *l);
int  looper_restore(looper_t *l);                 /* un-hide the last removed events */
void looper_commit(looper_t *l);                  /* forget them for good (frees their memory) */

#define LOOP_ANY_CH 0xFF
void looper_erase_note(looper_t *l, uint8_t note, uint8_t ch);   /* ch = LOOP_ANY_CH for every channel */
bool looper_empty(const looper_t *l);             /* no audible events */
static inline bool looper_ev_visible(const loop_ev_t *e) { return !(e->ch & LOOP_HIDDEN); }

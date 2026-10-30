/* OPENMPK3 — MIDI effects stage. Sits between every note source and MIDI out.
 * Note-on chain: [loop/arp only: Density, Euclidean filter] -> [loop only: Scale shift] -> Pitch drift -> Harmony -> Octaver
 *                -> Strum (chord groups) -> outputs -> Spiral echo + Ratchet. Trance gate chops sustained outputs. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "theory.h"

enum { FXS_LIVE = 0, FXS_ARP = 1, FXS_LOOP = 2 };            /* where a note comes from */
enum { OCT_OFF = 0, OCT_UP1, OCT_DOWN1, OCT_UP2, OCT_DOWN2, OCT_BOTH, OCT_COUNT };
enum { GATE_PATTERNS = 9, EUCLID_PRESETS = 8 };
enum { HARM_OFF = 0, HARM_3RD, HARM_4TH, HARM_5TH, HARM_6TH, HARM_3RD_DN, HARM_4TH_DN, HARM_TRIAD, HARM_COUNT };
extern const char *const HARMONY_NAMES[HARM_COUNT];
extern const char *const OCTAVER_NAMES[OCT_COUNT];
extern const char *const GATE_NAMES[GATE_PATTERNS];
extern const char *const EUCLID_NAMES[EUCLID_PRESETS];

typedef void (*fx_out_fn)(uint8_t ch, uint8_t note, uint8_t vel);   /* vel 0 = off */

typedef struct {
    uint8_t drift;      /* pitch drift: max scale steps 0..7 */
    int8_t  shift;      /* scale shift for loop notes: -7..+7 scale steps */
    uint8_t echo;       /* spiral echo repeats 0..7 */
    uint8_t harmony;    /* HARM_*: shadow voice(s) in the scale */
    uint8_t octaver;    /* OCT_* */
    uint8_t strum_ms;   /* 0..60 per note */
    uint8_t ratchet;    /* extra repeats per note 0..4 */
    uint8_t gate;       /* trance gate pattern 0 (off)..8 */
    uint8_t thin;       /* density: % of loop/arp notes removed 0..100 */
    uint8_t euclid;     /* euclidean filter preset 0 (off)..7 */
    uint8_t accent;     /* accent pattern on loop/arp velocities: downbeats strong, 16ths ghosted */
    const theory_cfg_t *theory;
    uint32_t echo_ms, ratchet_ms;   /* set by the app from the tempo */
    uint8_t step16;     /* current 1/16 step within the bar (0..15), set by the app */
    fx_out_fn out;
} fx_cfg_t;

void fx_init(fx_cfg_t *cfg);
void fx_note(uint8_t ch, uint8_t note, uint8_t vel, uint32_t now, uint8_t from);   /* vel 0 = off */
void fx_group_begin(void);                  /* notes until fx_group_end() form one chord (strummed) */
void fx_group_end(uint32_t now);
void fx_step16(uint8_t step, uint32_t now); /* call on every 1/16 boundary (trance gate) */
void fx_poll(uint32_t now);                 /* call often: fires scheduled notes */
void fx_panic(void);                        /* release everything the stage started */

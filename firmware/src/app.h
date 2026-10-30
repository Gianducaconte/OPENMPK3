/* OPENMPK3 — application state shared between logic (app.c) and display (ui.c). */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "theory.h"
#include "arp.h"
#include "looper.h"
#include "inputs.h"

typedef struct {
    char     pop_title[16], pop_value[20];
    int16_t  pop_bar;          /* 0..1000 or -1 for none */
    uint32_t pop_until;
    bool     menu_open;
    uint8_t  menu_sel;
    char     chord_name[12];   /* last pad chord */
    uint32_t chord_until;      /* show the chord view until */
    uint8_t  last_note;        /* last key note */
    uint32_t note_until;
    uint8_t  note_count[128];  /* sounding notes (for the mini keyboard) */
    uint32_t last_input_ms, beat_ms;
    bool     shift;
    bool     loop_resume;      /* loop was paused by MIDI Stop */
    bool     rec_armed;
    uint8_t  rec_clear;        /* SHIFT + REC held: 1 = waiting, 2 = a pattern was cleared with a pad */
    bool     can_revert;
    bool     undo_ready;       /* the last destructive shortcut can be undone by repeating it */       /* SHIFT + REC reverts a generate/mutate to the saved pattern */
    bool     evolve;           /* autopilot: small mutation every 2 loop cycles */
    uint8_t  evolve_laps;
    bool     save_armed;       /* SHIFT + PROG CHANGE: next pad saves the loop */
    int8_t   slot;             /* last loaded/saved pattern slot, -1 none */
    int8_t   pending_slot;     /* queued load, switches at the loop boundary, -1 none */        /* REC pressed while stopped: first note starts loop + recording */
} ui_state_t;

enum { LOOP_LEN_COUNT = 6 };
extern const uint16_t LOOP_LENS[LOOP_LEN_COUNT];
extern const char *const LOOP_LEN_NAMES[LOOP_LEN_COUNT];

extern ui_state_t U;
extern theory_cfg_t T;
extern arp_t ARP;
extern looper_t LOOP;
#define NO_PACK 0xFF
extern uint8_t g_scale, g_pack;
int  sound_count(void);
bool sound_is_style(int s);
const char *sound_name(int s);

void app_init(void);
void app_event(const input_ev_t *e);
void app_midi_in(const uint8_t pkt[4]);
void app_poll(void);
bool app_notes_held(void);
bool app_pad_held(void);

int  menu_count(void);
void menu_item(int i, const char **name, char *value, int vlen);
void menu_adjust(int sel, int d);

void ui_boot(void);
void ui_draw(void);

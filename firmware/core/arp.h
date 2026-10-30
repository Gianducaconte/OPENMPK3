/* OPENMPK3 — arpeggiator (hardware independent). Driven by 24 PPQN clock ticks + a ms timebase. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "theory.h"

typedef enum {
    ARP_UP = 0, ARP_DOWN, ARP_UPDOWN, ARP_DOWNUP, ARP_ORDER, ARP_RANDOM, ARP_CHORD,
    ARP_CONVERGE, ARP_PINKY, ARP_THUMB, ARP_SCALEWALK, ARP_BOUNCE, ARP_INCL, ARP_MODE_COUNT
} arp_mode_t;
extern const char *const ARP_MODE_NAMES[ARP_MODE_COUNT];

enum { ARP_RATE_COUNT = 8 };
extern const char *const ARP_RATE_NAMES[ARP_RATE_COUNT];
extern const uint8_t ARP_RATE_TICKS[ARP_RATE_COUNT];

#define ARP_MAX_HELD 16
#define ARP_MAX_SEQ  64

typedef void (*arp_out_fn)(uint8_t note, uint8_t vel, bool on, void *ctx);

typedef struct {
    /* settings */
    arp_mode_t mode;
    uint8_t rate;       /* index in ARP_RATE_TICKS */
    uint8_t gate;       /* 5..100 % */
    uint8_t swing;      /* 50..75 % */
    uint8_t octaves;    /* 1..4 */
    uint8_t ratchet;    /* 1..4 repeats per step */
    uint8_t euclid_k;   /* hits ... */
    uint8_t euclid_n;   /* ... per n steps; n==0 -> off */
    bool    latch;
    bool    accent;     /* every 4th step louder */
    const theory_cfg_t *theory;   /* for SCALEWALK */

    /* state */
    uint8_t held[ARP_MAX_HELD], held_vel[ARP_MAX_HELD], nheld;
    uint8_t phys_down;  /* physical keys still down (for latch) */
    uint8_t seq[ARP_MAX_SEQ], nseq;
    bool    dirty;
    uint16_t step, tick_in_step;
    uint8_t playing[8], nplaying;   /* notes of the current step */
    bool    sounding;
    uint32_t off_at_ms, ratchet_next_ms, ratchet_len_ms;
    uint8_t ratchet_left, last_vel;
    uint32_t tick_ms;   /* measured/derived ms per tick, for gate length */
    uint32_t rng;
    arp_out_fn out; void *ctx;
} arp_t;

void arp_init(arp_t *a, arp_out_fn out, void *ctx);
void arp_note_on(arp_t *a, uint8_t note, uint8_t vel);
void arp_note_off(arp_t *a, uint8_t note);
void arp_clear(arp_t *a);            /* stop all, forget held notes */
void arp_tick(arp_t *a, uint32_t now_ms);   /* call on every 24 PPQN tick */
void arp_poll(arp_t *a, uint32_t now_ms);   /* call often (>=1 kHz) for gates/ratchets */
void arp_reset_phase(arp_t *a);      /* restart pattern at next tick (on MIDI Start) */
bool arp_euclid_hit(uint8_t k, uint8_t n, uint16_t step);

/* OPENMPK3 — input decoding: keys (velocity), buttons, pads, knobs, joystick, pedal. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

enum {
    BTN_PROG_SELECT = 0, BTN_PROG_CHANGE, BTN_CC, BTN_BANK, BTN_ARP, BTN_TAP, BTN_OCT_UP, BTN_OCT_DOWN,   /* hw test: OCT order is +,- */
    BTN_FULL_LEVEL, BTN_NOTE_REPEAT, BTN_COUNT
};
extern const char *const BTN_NAMES[BTN_COUNT];

typedef enum {
    EV_KEY_ON, EV_KEY_OFF,          /* a = key 0..24 (0 = leftmost), b = velocity */
    EV_PAD_ON, EV_PAD_OFF, EV_PAD_PRESSURE,   /* a = pad 0..7, b = velocity / pressure */
    EV_BTN_DOWN, EV_BTN_UP, EV_BTN_LONG,      /* a = button, b = 1 if it was a short press (UP only) */
    EV_KNOB,                        /* a = knob 0..7, d = signed delta (~ 1 per 2 degrees) */
    EV_JOY,                         /* a = axis 0/1, d = -8192..8191 deflection from centre */
    EV_PEDAL                        /* b = 1 down / 0 up */
} ev_type_t;

typedef struct { ev_type_t type; uint8_t a, b; int16_t d; } input_ev_t;

void inputs_init(void);
void inputs_poll(void);             /* call every main-loop pass */
bool inputs_pop(input_ev_t *e);
bool button_held(int b);
typedef void (*key_debug_fn)(uint8_t key, uint32_t dt_ticks, uint8_t first_contact, uint32_t latency_ticks);
extern key_debug_fn g_key_debug;
extern uint8_t g_vel_sens;   /* 5..20, 10 = default */

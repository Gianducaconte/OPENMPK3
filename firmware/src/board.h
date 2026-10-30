/* OPENMPK3 — board support for the MPK mini mk3 (pin map: .codex/HW_MAP.md). */
#pragma once
#include <stdint.h>
#include <stdbool.h>

extern volatile uint32_t g_ms;       /* 1 kHz, SysTick */
extern volatile uint32_t g_ticks;    /* 50 kHz (20 us), TIM3 */
void clock_init(void);
void board_init(void);

/* ---- key/button matrix: raw events from the 20 us scan ---- */
typedef struct { uint8_t line, bits, changed; uint32_t ts; } mx_event_t;
bool matrix_pop(mx_event_t *e);

/* ---- analog: 8 knobs (2 wipers each), 8 pads, joystick, pedal ---- */
uint16_t knob_a(int k);              /* k = 0..7 (K1..K8), 12-bit */
uint16_t knob_b(int k);
uint16_t pad_raw(int p);             /* p = 0..7 (pad 1..8) */
uint16_t joy_raw(int axis);          /* 0 = X, 1 = Y */
bool     pedal_down(void);

/* ---- clock in on the TE1 debug header (PA13 clock, PA14 reset) ---- */
void clockin_enable(bool on, bool transistor_input, bool pin2_clock_out);
void clockout_set(bool high);
bool clockin_pop(uint32_t *ts_ticks);
bool clockin_reset_pop(void);
int  board_probe_pins(uint8_t *out);   /* returns count; codes 0 free, 1 tied low, 2 tied high, 3 odd */

/* ---- LEDs: 24 outputs, brightness 0..32 ---- */
enum {
    LED_BANK_A = 0, LED_BANK_B = 1, LED_CC = 2, LED_PROG_CHANGE = 3, LED_PROG_SELECT = 4,
    LED_PAD1 = 8, LED_ARP = 16, LED_TAP = 17, LED_OCT_UP = 18, LED_OCT_DOWN = 19,   /* swapped vs RE map (hw test) */
    LED_FULL_LEVEL = 20, LED_NOTE_REPEAT = 21, LED_COUNT = 24
};
void led_set(int n, uint8_t level);

/* ---- OLED 128x32 SSD1306 ---- */
#define OLED_W 128
#define OLED_H 32
extern uint8_t oled_fb[OLED_W * OLED_H / 8];
void oled_flush(void);               /* non-blocking: starts a DMA frame if the previous one finished */
bool oled_busy(void);

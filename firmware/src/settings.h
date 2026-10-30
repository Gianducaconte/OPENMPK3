/* OPENMPK3 — persistent settings (flash page 0x0801E800, never the bootloader/version pages). */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum { VEL_SOFT = 0, VEL_LINEAR, VEL_HARD, VEL_FIXED, VEL_CURVE_COUNT } vel_curve_t;
typedef enum { CLK_AUTO = 0, CLK_INTERNAL, CLK_SRC_COUNT } clock_src_t;

typedef struct {
    uint32_t magic;
    uint8_t  version;
    /* harmony */
    uint8_t  root, sound, keymap, voicing, scale_chords;   /* sound: scales first, then styles (see app.c) */
    int8_t   octave;
    /* arp */
    uint8_t  arp_on, arp_mode, arp_rate, arp_gate, arp_swing, arp_octaves, arp_latch, arp_ratchet, arp_euclid_k, arp_euclid_n, arp_accent;
    uint8_t  repeat_rate;
    uint16_t bpm;
    uint8_t  clock_src, clock_out;
    /* looper */
    uint8_t  loop_len, loop_quant;   /* loop_quant: record quantize off/on */
    /* midi */
    uint8_t  midi_ch, vel_curve, vel_sens, full_level, note_repeat;
    uint8_t  joy_x_invert, joy_y_invert;
    uint8_t  knob_cc[8];
    uint8_t  clk_in, clk_in_ppqn, clk_in_direct;   /* off/on; 0=4,1=1,2=2,3=24 PPQN; 0 transistor, 1 direct */
    uint8_t  quant_grid;       /* index into QUANT_GRIDS (0 = 1/16) */
    int8_t   pad_octave;       /* -3..+3, SHIFT + OCT */
    uint8_t  cur_pattern;      /* current pattern slot 0..7 */
    /* MIDI effects */
    uint8_t  fx_drift, fx_echo, fx_octaver, fx_strum, fx_ratchet, fx_gate, fx_harmony, fx_euclid, fx_speed;
    int8_t   fx_shift;
    uint8_t  fx_accent;
    uint8_t  te_pin2, clk_out_ppqn;   /* TE1 pin 2 (PA14): 0 reset in, 1 clock out; out rate 0=4,1=1,2=2,3=24 PPQN */
    uint8_t  reserved[5];
    uint16_t checksum;
} settings_t;

extern settings_t S;
void settings_load(void);
void settings_defaults(void);
void settings_touch(void);       /* mark dirty; saved after a few idle seconds */
void settings_poll(bool safe);   /* call often; `safe` = no notes held (flash erase stalls the CPU ~20 ms) */
void settings_save_now(void);

#pragma once
#include <stdint.h>
#include <stdbool.h>
void     tempo_midi_clock(void);
void     tempo_tap(void);
int      tempo_poll(void);
bool     tempo_external(void);
uint32_t tempo_ms_per_tick(void);
uint32_t tempo_tick_count(void);
uint16_t tempo_bpm_display(void);
void     tempo_clockin_pulse(uint32_t ts_ticks);
void     tempo_clockin_reset(void);
bool     tempo_clockin(void);
void     tempo_restart(void);
void     tempo_ext_start(void);
void     tempo_song_position(uint16_t spp);

/* OPENMPK3 — settings in flash. Only ever erases/programs 0x0801E800 (inside our app region, see ld/app.ld). */
#include "settings.h"
#include "stm32f0.h"
#include "board.h"
#include "flash.h"
#include <string.h>
#include <stddef.h>

#define SETTINGS_ADDR  0x0801E800u
#define SETTINGS_MAGIC 0x4B504D4Fu   /* "OMPK" */
#define SETTINGS_VER   5

settings_t S;
static bool dirty; static uint32_t dirty_at;

static uint16_t csum(const settings_t *s)
{
    const uint8_t *p = (const uint8_t *)s; uint16_t c = 0x5A5A;
    for (unsigned i = 0; i < offsetof(settings_t, checksum); i++) c = (uint16_t)((c << 1 | c >> 15) ^ p[i]);
    return c;
}

void settings_defaults(void)
{
    memset(&S, 0, sizeof S);
    S.magic = SETTINGS_MAGIC; S.version = SETTINGS_VER;
    S.root = 0; S.sound = 1; S.keymap = 0; S.voicing = 0; S.scale_chords = 0; S.octave = 0;   /* C minor, triads */
    S.arp_mode = 0; S.arp_rate = 4; S.arp_gate = 60; S.arp_swing = 50; S.arp_octaves = 1; S.arp_ratchet = 1;
    S.repeat_rate = 4; S.bpm = 120; S.clock_src = CLK_AUTO;
    S.loop_len = 2; S.loop_quant = 0;                      /* 2 bars, no quantize */
    S.midi_ch = 0; S.vel_curve = VEL_LINEAR; S.vel_sens = 10;
    for (int i = 0; i < 8; i++) S.knob_cc[i] = (uint8_t)(70 + i);
    S.fx_speed = 1;                                        /* 0 half, 1 normal, 2 double */
}

void settings_load(void)
{
    const settings_t *f = (const settings_t *)SETTINGS_ADDR;
    if (f->magic == SETTINGS_MAGIC && f->version == SETTINGS_VER && f->checksum == csum(f)) memcpy(&S, f, sizeof S);
    else settings_defaults();
}

void settings_touch(void) { dirty = true; dirty_at = g_ms; }

void settings_save_now(void)
{
    S.checksum = csum(&S);
    if (memcmp((const void *)SETTINGS_ADDR, &S, sizeof S) == 0) { dirty = false; return; }
    flash_erase_page(SETTINGS_ADDR);
    flash_program(SETTINGS_ADDR, &S, sizeof S);
    dirty = false;
}

void settings_poll(bool safe)
{
    if (dirty && safe && g_ms - dirty_at > 4000) settings_save_now();
}

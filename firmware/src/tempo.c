/* OPENMPK3 — tempo: internal 24 PPQN clock, external MIDI clock follow (auto), tap tempo. */
#include "tempo.h"
#include "board.h"
#include "settings.h"
#include "stm32f0.h"

static uint32_t next_tick_q8;      /* next internal tick time, in 20 us ticks << 8 */
static uint32_t last_ext_ms, ext_prev_ms, ext_period_x16 = 16 * 21;
static volatile uint16_t ext_pending;
static bool running_ext;
static uint32_t tick_count;
static uint32_t taps[4]; static uint8_t ntaps;

/* ---- analog clock input (TE1): pulses at N PPQN, interpolated to 24 PPQN ---- */
static const uint8_t CI_PPQN[4] = { 4, 1, 2, 24 };
static bool ci_active;
static uint32_t ci_prev_ts, ci_interval, ci_next_q8, ci_last_ms;
static uint8_t ci_emitted, ci_pending;

void tempo_clockin_pulse(uint32_t ts)
{
    uint8_t tpp = (uint8_t)(24 / CI_PPQN[S.clk_in_ppqn & 3]);
    uint32_t dt = ts - ci_prev_ts;
    if (ci_active && dt < 50000u * 4) {           /* < 4 s: valid interval */
        ci_interval = ci_interval ? (ci_interval + 3 * dt) / 4 : dt;
        if (ci_emitted < tpp) ci_pending += (uint8_t)(tpp - ci_emitted);   /* catch up if the clock sped up */
    }
    ci_prev_ts = ts; ci_last_ms = g_ms; ci_active = true;
    ci_pending++; ci_emitted = 1;                  /* tick 0 lands exactly on the pulse */
    if (ci_interval) ci_next_q8 = (ts << 8) + (ci_interval << 8) / tpp;
}
void tempo_clockin_reset(void) { tick_count = 0xFFFFFFFFu; ci_emitted = 0; ci_pending = 0; }
bool tempo_clockin(void) { return ci_active; }

bool tempo_external(void) { return running_ext || ci_active; }

/* MIDI Start: the next clock is tick 0 of bar 1. Song Position Pointer: next clock is tick spp*6. */
void tempo_ext_start(void) { ext_pending = 0; tick_count = 0xFFFFFFFFu; }
void tempo_song_position(uint16_t spp) { ext_pending = 0; tick_count = (uint32_t)spp * 6u - 1u; }

/* restart the internal clock grid now (a beat starts here); no effect when following an external clock */
void tempo_restart(void)
{
    if (running_ext || ci_active) return;
    next_tick_q8 = (g_ticks << 8) + (uint32_t)(125000u * 256u / S.bpm);
    tick_count = 0;
}
uint32_t tempo_tick_count(void) { return tick_count; }

uint32_t tempo_ms_per_tick(void)
{
    if (ci_active && ci_interval) { uint32_t ms = ci_interval / 50u * CI_PPQN[S.clk_in_ppqn & 3] / 24u; return ms ? ms : 1; }
    if (running_ext) return (ext_period_x16 + 8) / 16;
    return 2500u / S.bpm;                  /* 60000 / (bpm * 24) */
}

void tempo_midi_clock(void)               /* call on received 0xF8 */
{
    uint32_t now = g_ms;
    if (ext_prev_ms && now - ext_prev_ms < 200) {
        uint32_t p = (now - ext_prev_ms) * 16;
        ext_period_x16 = (ext_period_x16 * 7 + p) / 8;
    }
    ext_prev_ms = now; last_ext_ms = now;
    if (S.clock_src == CLK_AUTO) { running_ext = true; ext_pending++; }
}

uint16_t tempo_bpm_display(void)
{
    if (ci_active && ci_interval) return (uint16_t)((60u * 50000u + ci_interval * CI_PPQN[S.clk_in_ppqn & 3] / 2) / (ci_interval * CI_PPQN[S.clk_in_ppqn & 3]));
    if (running_ext) { uint32_t p = ext_period_x16; return (uint16_t)(p ? (2500u * 16 + p / 2) / p : 0); }
    return S.bpm;
}

void tempo_tap(void)
{
    uint32_t now = g_ms;
    if (ntaps && now - taps[(ntaps - 1) & 3] > 2000) ntaps = 0;
    taps[ntaps & 3] = now; ntaps++;
    if (ntaps >= 2) {
        int n = ntaps > 4 ? 4 : ntaps;
        uint32_t span = taps[(ntaps - 1) & 3] - taps[(ntaps - n) & 3];
        uint32_t bpm = 60000u * (uint32_t)(n - 1) / span;
        if (bpm >= 40 && bpm <= 300) S.bpm = (uint16_t)bpm, settings_touch();
    }
}

int tempo_poll(void)                       /* returns number of 24 PPQN ticks that elapsed */
{
    if (ci_active && (!S.clk_in || g_ms - ci_last_ms > 2000)) { ci_active = false; ci_interval = 0; next_tick_q8 = g_ticks << 8; }
    if (ci_active) {
        uint8_t tpp = (uint8_t)(24 / CI_PPQN[S.clk_in_ppqn & 3]);
        int n = ci_pending; ci_pending = 0;
        ext_pending = 0;                           /* clock input has priority over USB MIDI clock */
        if (ci_interval) {
            uint32_t step_q8 = (ci_interval << 8) / tpp, now_q8 = g_ticks << 8;
            while (ci_emitted < tpp && (int32_t)(now_q8 - ci_next_q8) >= 0) { n++; ci_emitted++; ci_next_q8 += step_q8; }
        }
        tick_count += (uint32_t)n;
        return n;
    }
    if (running_ext && (g_ms - last_ext_ms > 500 || S.clock_src != CLK_AUTO)) {
        running_ext = false;
        next_tick_q8 = g_ticks << 8;
    }
    int n = 0;
    if (running_ext) {
        __disable_irq(); n = ext_pending; ext_pending = 0; __enable_irq();
    } else {
        uint32_t period_q8 = (uint32_t)(125000u * 256u / S.bpm);   /* 50000*60/(bpm*24) ticks */
        uint32_t now_q8 = g_ticks << 8;
        if ((int32_t)(now_q8 - next_tick_q8) > (int32_t)(period_q8 * 4)) next_tick_q8 = now_q8;  /* resync */
        while ((int32_t)(now_q8 - next_tick_q8) >= 0) { n++; next_tick_q8 += period_q8; }
    }
    tick_count += (uint32_t)n;
    return n;
}

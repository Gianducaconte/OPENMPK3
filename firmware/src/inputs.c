/* OPENMPK3 — input decoding. Algorithms follow the stock fw (see .codex/HW_MAP.md §2). */
#include "inputs.h"
#include "board.h"
#include <string.h>
#include <stdlib.h>

const char *const BTN_NAMES[BTN_COUNT] = {
    "PROG SEL", "PROG CHG", "CC", "BANK", "ARP", "TAP", "OCT+", "OCT-", "FULL LVL", "NOTE RPT" };

/* ---------------- event queue */
#define EVQ 48
static input_ev_t evq[EVQ]; static uint8_t evh, evt;
static void emit(ev_type_t t, uint8_t a, uint8_t b, int16_t d)
{
    uint8_t n = (uint8_t)((evh + 1) % EVQ);
    if (n == evt) return;
    evq[evh] = (input_ev_t){ t, a, b, d }; evh = n;
}
bool inputs_pop(input_ev_t *e) { if (evt == evh) return false; *e = evq[evt]; evt = (uint8_t)((evt + 1) % EVQ); return true; }

/* ---------------- factory calibration from the settings page (kept by the bootloader) */
#define CAL ((const volatile uint8_t *)0x0801F800)
static uint16_t cal16(int off) { return (uint16_t)(CAL[off] | (CAL[off + 1] << 8)); }

/* ---------------- keys: two contacts per key, velocity from contact time difference (20 us ticks) */
#define SCALED_KEYS 0x00544AA5u    /* stock treats these keys as 0.7x faster (contact geometry); we compensate */
static struct { uint32_t t1; uint8_t first; bool down; uint8_t closed; } keys[25];   /* closed: bit0 even, bit1 odd */
key_debug_fn g_key_debug;

/* Velocity from the time between the two contacts. Measured on this keybed (fw v0.2 debug):
 * hard ~10 ms, normal 12-25 ms, soft 30-45 ms+, which the stock curve (min velocity above 24 ms) squashes.
 * Breakpoints in 0.1 ms; sensitivity scales time (10 = default, higher = easier to play loud). */
static const uint16_t VT[] = {   30,  60, 100, 150, 220, 320, 450, 700, 1000 };
static const uint8_t  VV[] = {  127, 112,  96,  80,  64,  48,  34,  18,    6 };
uint8_t g_vel_sens = 10;
static uint8_t key_velocity(int key, uint32_t dt)
{
    uint32_t t = dt * 2 / g_vel_sens;                      /* 20 us ticks -> 0.1 ms, scaled by sensitivity */
    if (SCALED_KEYS >> key & 1) t = t * 10 / 7;
    if (t <= VT[0]) return VV[0];
    for (unsigned i = 1; i < sizeof VT / sizeof VT[0]; i++)
        if (t <= VT[i]) return (uint8_t)(VV[i - 1] - (int)(t - VT[i - 1]) * (VV[i - 1] - VV[i]) / (VT[i] - VT[i - 1]));
    return VV[sizeof VV - 1];
}

/* ---------------- buttons */
static struct { bool closed, reported, longed; uint32_t t; } btn[BTN_COUNT];
bool button_held(int b) { return b >= 0 && b < BTN_COUNT && btn[b].reported; }

static void handle_matrix(const mx_event_t *e)
{
    for (int bit = 0; bit < 8; bit++) {
        if (!(e->changed >> bit & 1)) continue;
        bool closed = e->bits >> bit & 1;
        if (e->line >= 8) {
            int b = (e->line - 8) * 8 + bit;
            if (b >= BTN_COUNT) continue;
            if (closed) { btn[b].closed = true; btn[b].t = g_ms; }
            else {
                if (btn[b].reported) emit(EV_BTN_UP, (uint8_t)b, !btn[b].longed, 0);
                btn[b].closed = btn[b].reported = btn[b].longed = false;
            }
            continue;
        }
        int key = 27 - ((e->line >> 1) * 8 + bit);
        if (key < 0 || key > 24) continue;
        uint8_t c = (uint8_t)(1u << (e->line & 1)), other = (uint8_t)(c ^ 3);
        if (closed) {
            keys[key].closed |= c;
            if (!(keys[key].closed & other)) { keys[key].t1 = e->ts; keys[key].first = c; }   /* first contact */
            else if (!keys[key].down && keys[key].first == other) {                       /* second -> note on */
                keys[key].down = true;
                uint32_t dt = e->ts - keys[key].t1;
                uint8_t v = key_velocity(key, dt);
                emit(EV_KEY_ON, (uint8_t)key, v, 0);
                if (g_key_debug) g_key_debug((uint8_t)key, dt, keys[key].first, g_ticks - e->ts);
            }
        } else {
            keys[key].closed &= (uint8_t)~c;
            if (keys[key].down && keys[key].closed == 0) { keys[key].down = false; emit(EV_KEY_OFF, (uint8_t)key, 0, 0); }
            if (keys[key].closed == 0) keys[key].first = 0;
        }
    }
}

static void poll_buttons(void)
{
    for (int b = 0; b < BTN_COUNT; b++) {
        if (!btn[b].closed) continue;
        uint32_t held = g_ms - btn[b].t;
        if (!btn[b].reported && held >= 8) { btn[b].reported = true; emit(EV_BTN_DOWN, (uint8_t)b, 0, 0); }
        if (btn[b].reported && !btn[b].longed && held >= 600) { btn[b].longed = true; emit(EV_BTN_LONG, (uint8_t)b, 0, 0); }
    }
}

/* ---------------- pads (FSR) */
#define PAD_ON_TH  451
#define PAD_OFF_TH 250
static struct { uint8_t st; uint16_t peak; uint32_t t; uint8_t last_p; } pads[8];

static uint8_t pad_curve(uint32_t lin)   /* 1..127 -> gentle start, then steeper (like stock LUT) */
{
    uint32_t v = lin * lin / 127 + lin;  /* blend of linear and square */
    v = v * 127 / 254;
    return (uint8_t)(v < 1 ? 1 : v > 127 ? 127 : v);
}
static uint8_t pad_scale(uint16_t raw)
{
    int v = ((int)raw - 450) * 127 / 3450;
    return pad_curve((uint32_t)(v < 1 ? 1 : v > 127 ? 127 : v));
}

static void poll_pads(void)
{
    for (int p = 0; p < 8; p++) {
        uint16_t r = pad_raw(p);
        switch (pads[p].st) {
        case 0:
            if (r >= PAD_ON_TH) { pads[p].st = 1; pads[p].peak = r; pads[p].t = g_ms; }
            break;
        case 1:   /* tracking the peak for a few ms */
            if (r > pads[p].peak) pads[p].peak = r;
            if (g_ms - pads[p].t >= 6 || r < PAD_OFF_TH) {
                emit(EV_PAD_ON, (uint8_t)p, pad_scale(pads[p].peak), 0);
                pads[p].st = 2; pads[p].t = g_ms; pads[p].last_p = 0;
            }
            break;
        case 2:
            if (r < PAD_OFF_TH && g_ms - pads[p].t >= 20) {
                if (pads[p].last_p) emit(EV_PAD_PRESSURE, (uint8_t)p, 0, 0);
                emit(EV_PAD_OFF, (uint8_t)p, 0, 0); pads[p].st = 0;
            } else if (g_ms - pads[p].t >= 250 && (g_ms & 7) == 0) {
                uint8_t pr = r > PAD_ON_TH ? pad_scale(r) : 0;
                if (abs(pr - pads[p].last_p) > 2) { pads[p].last_p = pr; emit(EV_PAD_PRESSURE, (uint8_t)p, pr, 0); }
            }
            break;
        }
    }
}

/* ---------------- knobs: endless dual-wiper pots (two wipers 90 deg apart) */
static struct { uint16_t a, b; uint32_t t_move; bool init; } kn[8];

static void poll_knobs(void)
{
    for (int k = 0; k < 8; k++) {
        int a = knob_a(k), b = knob_b(k);
        if (!kn[k].init) { kn[k].a = (uint16_t)a; kn[k].b = (uint16_t)b; kn[k].init = true; continue; }
        int d;
        if (a >= 0xE66)      d = ((int)kn[k].b - b) * 180 / 4095;
        else if (a < 0x19A)  d = (b - (int)kn[k].b) * 180 / 4095;
        else if (b >= 0x801) d = (a - (int)kn[k].a) * 180 / 4095;
        else                 d = ((int)kn[k].a - a) * 180 / 4095;
        bool idle = g_ms - kn[k].t_move > 200;
        int th = idle ? 6 : 2;
        if (d >= th || d <= -th) {
            if (idle) d = d > 0 ? 2 : -2;
            kn[k].a = (uint16_t)a; kn[k].b = (uint16_t)b; kn[k].t_move = g_ms;
            emit(EV_KNOB, (uint8_t)k, 0, (int16_t)(d / 2));
        }
    }
}

/* ---------------- joystick */
static uint16_t jmin[2] = { 0x19A, 0x19A }, jmax[2] = { 0xDFF, 0xDFF }, jctr[2] = { 0x800, 0x800 };
static int16_t jlast[2];
static void poll_joy(void)
{
    for (int a = 0; a < 2; a++) {
        int r = joy_raw(a), c = jctr[a], v;
        if (r > jmax[a] + 60) jmax[a] = (uint16_t)(r - 60);
        if (r < jmin[a] - 60) jmin[a] = (uint16_t)(r + 60);
        if (r > c + 150)      v = (r - c - 150) * 8191 / (jmax[a] - c - 150);
        else if (r < c - 150) v = -(c - 150 - r) * 8192 / (c - 150 - jmin[a]);
        else v = 0;
        if (v > 8191) v = 8191;
        if (v < -8192) v = -8192;
        if (abs(v - jlast[a]) >= 64 || (v == 0 && jlast[a] != 0)) { jlast[a] = (int16_t)v; emit(EV_JOY, (uint8_t)a, 0, (int16_t)v); }
    }
}

/* ---------------- pedal */
static bool ped; static uint32_t ped_t;
static void poll_pedal(void)
{
    bool d = pedal_down();
    if (d != ped && g_ms - ped_t > 20) { ped = d; ped_t = g_ms; emit(EV_PEDAL, 0, d, 0); }
}

void inputs_init(void)
{
    uint16_t xmax = cal16(0x20), ymax = cal16(0x22), xmin = cal16(0x24), ymin = cal16(0x26), xc = cal16(0x28), yc = cal16(0x2A);
    if (xmin < xc && xc < xmax && xmax < 4096) { jmin[0] = xmin; jmax[0] = xmax; jctr[0] = xc; }
    if (ymin < yc && yc < ymax && ymax < 4096) { jmin[1] = ymin; jmax[1] = ymax; jctr[1] = yc; }
    ped = pedal_down();
}

void inputs_poll(void)
{
    mx_event_t e;
    while (matrix_pop(&e)) handle_matrix(&e);
    poll_buttons();
    static uint32_t last;
    if (g_ms != last) {   /* analog at 1 kHz */
        last = g_ms;
        poll_pads(); poll_knobs(); poll_pedal();
        if ((g_ms & 3) == 0) poll_joy();
    }
}

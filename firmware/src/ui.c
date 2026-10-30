/* OPENMPK3 — OLED user interface (128x32). */
#include "app.h"
#include "board.h"
#include "gfx.h"
#include "settings.h"
#include "tempo.h"
#include "tusb.h"
#include "packs.h"
#include <stdio.h>
#include <string.h>

/* small fixed-point sine: x in 0..255 -> -127..127 */
static int isin(int x)
{
    static const int8_t Q[65] = { 0,3,6,9,12,16,19,22,25,28,31,34,37,40,43,46,49,51,54,57,60,63,65,68,71,73,76,78,81,83,85,88,
                                  90,92,94,96,98,100,102,104,106,107,109,111,112,113,115,116,117,118,120,121,122,122,123,124,125,
                                  125,126,126,126,127,127,127,127 };
    x &= 255;
    if (x < 64) return Q[x];
    if (x < 128) return Q[128 - x];
    if (x < 192) return -Q[x - 128];
    return -Q[256 - x];
}

static void flush_wait(void) { oled_flush(); while (oled_busy()) { tud_task(); } }

void ui_boot(void)
{
    /* 1) waveform sweeps in, 2) logo drops, 3) LED chase */
    for (int f = 0; f < 36; f++) {
        gfx_clear();
        for (int x = 0; x < 128; x++) {
            int amp = f < 18 ? f : 36 - f;
            int y = 16 + isin(x * 6 + f * 14) * amp / 254 + isin(x * 2 - f * 9) * amp / 508;
            gfx_pixel(x, y, 1);
        }
        int drop = f < 18 ? -16 + f * 2 : 20 - 16 + 2 * 18 - 16;
        if (f >= 10) gfx_text_center(drop < 4 ? drop : 4, "OPENMPK3", 2);
        if (f >= 24) gfx_text_center(24, "scales chords loops", 1);
        flush_wait();
        int l = f * LED_COUNT / 36;
        for (int i = 0; i < LED_COUNT; i++) led_set(i, (i == l || i == l - 1) ? 32 : (i < l ? 2 : 0));
        uint32_t t = g_ms; while (g_ms - t < 22) tud_task();
    }
    for (int i = 0; i < LED_COUNT; i++) led_set(i, 0);
}

/* mini keyboard, one octave starting at C: scale tones dotted, sounding pitch classes filled */
static void mini_keys(int x0, int y0)
{
    static const int8_t WHITE_PC[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int8_t BLACK_PC[5] = { 1, 3, 6, 8, 10 }, BLACK_X[5] = { 4, 10, 22, 28, 34 };
    uint16_t sounding = 0;
    for (int n = 0; n < 128; n++) if (U.note_count[n]) sounding |= (uint16_t)(1u << (n % 12));
    uint16_t mask = SCALES[T.scale].mask;
    for (int i = 0; i < 7; i++) {
        int pc = WHITE_PC[i], x = x0 + i * 6;
        gfx_rect(x, y0, 7, 16);
        bool in = mask >> ((pc - T.root + 12) % 12) & 1;
        if (sounding >> pc & 1) gfx_fill(x + 1, y0 + 1, 5, 14, 1);
        else if (in) gfx_fill(x + 2, y0 + 11, 3, 3, 1);
        if (pc == T.root) gfx_hline(x + 1, y0 + 14, 5);
    }
    for (int i = 0; i < 5; i++) {
        int pc = BLACK_PC[i], x = x0 + BLACK_X[i];
        bool in = mask >> ((pc - T.root + 12) % 12) & 1;
        gfx_fill(x, y0, 5, 9, 1);
        if (!(sounding >> pc & 1)) gfx_fill(x + 1, y0 + 1, 3, 7, 0);
        if (in && !(sounding >> pc & 1)) gfx_fill(x + 2, y0 + 5, 1, 2, 1);
    }
}

static void badge(int x, int y, const char *s)
{
    int w = gfx_text_width(s, 1) + 4;
    gfx_fill(x, y, w, 9, 0); gfx_text(x + 2, y + 1, s, 1); gfx_invert(x, y, w, 9);
}

/* Layout grid (two sizes only): small 1x labels on rows y=0 and y=25, one large 2x value line at y=10. */
#define ROW_TOP 0
#define ROW_VAL 10
#define ROW_BOT 25

/* while the looper is active, the loop length sits under the PLAY/REC badge; returns the width it reserves */
static const char *const LOOP_LEN_SHORT[LOOP_LEN_COUNT] = { "1/2 BAR", "1 BAR", "2 BARS", "4 BARS", "8 BARS", "16 BARS" };
static bool looper_active(void) { return LOOP.playing || LOOP.recording || U.rec_armed; }
static int loop_len_label(bool draw)
{
    if (!looper_active()) return 0;
    const char *s = LOOP_LEN_SHORT[S.loop_len % LOOP_LEN_COUNT];
    int w = gfx_text_width(s, 1);
    if (draw) gfx_text(128 - w, ROW_VAL + 1, s, 1);
    return w + 4;
}

static void style_label(void)                        /* style name, or "PATTERN n" for plain scales */
{
    char s[14];                                      /* max 12 chars so it never touches "P1" at x=80 */
    if (g_pack != NO_PACK) snprintf(s, sizeof s, "%.12s", PACKS[g_pack].name);
    else snprintf(s, sizeof s, "PATTERN %d", U.slot + 1);
    gfx_text(0, ROW_TOP, s, 1);
}

/* top-right: looper state (or tempo); bottom-right: octave labels; bottom edge: loop progress */
static void corners(void)
{
    char buf[16];
    if (U.pending_slot >= 0) {                       /* queued pattern: "P1->P4" blinking until the boundary */
        if (U.slot >= 0) snprintf(buf, sizeof buf, "P%d->P%d", U.slot + 1, U.pending_slot + 1);
        else snprintf(buf, sizeof buf, "->P%d", U.pending_slot + 1);
        int w = gfx_text_width(buf, 1);
        if ((g_ms / 200) & 1) badge(128 - w - 4, ROW_TOP - 1, buf); else gfx_text(128 - w - 2, ROW_TOP, buf, 1);
    }
    else if (U.rec_armed)     { if ((g_ms / 150) & 1) badge(108, ROW_TOP - 1, "ARM"); else gfx_text(110, ROW_TOP, "ARM", 1); }
    else if (LOOP.recording)  { if ((g_ms / 300) & 1) badge(108, ROW_TOP - 1, "REC"); else gfx_text(110, ROW_TOP, "REC", 1); }
    else if (LOOP.playing)    badge(102, ROW_TOP - 1, "PLAY");
    else {
        snprintf(buf, sizeof buf, "%d%s", tempo_bpm_display(), tempo_clockin() ? "c" : tempo_external() ? "e" : "");
        gfx_text(128 - gfx_text_width(buf, 1), ROW_TOP, buf, 1);
    }
    if (U.pending_slot < 0 && U.slot >= 0 && g_pack != NO_PACK) {   /* "P1".."P8" (top-left already says PATTERN n otherwise) */
        char p[8]; snprintf(p, sizeof p, "P%d", U.slot + 1);
        gfx_text(80, ROW_TOP, p, 1);
    }
    buf[0] = 0;
    if (S.octave)     snprintf(buf, sizeof buf, "O%+d", S.octave);
    if (S.pad_octave) { size_t l = strlen(buf); snprintf(buf + l, sizeof buf - l, "%sPO%+d", l ? " " : "", S.pad_octave); }
    if (buf[0]) gfx_text(128 - gfx_text_width(buf, 1), ROW_BOT, buf, 1);
    if (LOOP.playing) gfx_fill(0, 31, (int)((uint32_t)LOOP.pos * 128 / LOOP.length), 1, 1);
}

static void home(void)
{
    style_label();
    const char *root = NOTE_NAMES[T.root];
    int x = gfx_text_gap(0, ROW_VAL, root, 2, 2) + 4;
    gfx_text_fit(x, ROW_VAL, SCALES[T.scale].name, 128 - x - loop_len_label(true));
    corners();
}

static void chord_view(void)
{
    style_label();
    gfx_text_fit(0, ROW_VAL, U.chord_name, 82);
    mini_keys(85, 9);
    corners();
}

static void note_view(void)
{
    char buf[8];
    snprintf(buf, sizeof buf, "%s%d", NOTE_NAMES[U.last_note % 12], U.last_note / 12 - 1);
    style_label();
    gfx_text_fit(0, ROW_VAL, buf, 128 - loop_len_label(true));
    corners();
}

static void popup_draw(void)
{
    gfx_text(0, ROW_TOP, U.pop_title, 1);
    gfx_text_fit(0, ROW_VAL, U.pop_value, 128);
}

static void menu_draw(void)
{
    const char *name; char val[20], pos[8];
    menu_item(U.menu_sel, &name, val, sizeof val);
    gfx_text(0, ROW_TOP, name, 1);
    snprintf(pos, sizeof pos, "%d/%d", U.menu_sel + 1, menu_count());
    gfx_text(128 - gfx_text_width(pos, 1), ROW_TOP, pos, 1);
    gfx_text_fit(0, ROW_VAL, val, 128);
    gfx_text(0, ROW_BOT, "K1 setting   K2 value", 1);
}

void ui_draw(void)
{
    if (oled_busy()) return;
    gfx_clear();
    if (U.menu_open) menu_draw();
    else if ((int32_t)(U.pop_until - g_ms) > 0) popup_draw();
    else if (app_pad_held() || (int32_t)(U.chord_until - g_ms) > 0) chord_view();
    else if ((int32_t)(U.note_until - g_ms) > 0) note_view();
    else home();
    oled_flush();
}

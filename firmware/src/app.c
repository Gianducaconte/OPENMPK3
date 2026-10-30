/* OPENMPK3 — application logic: scale keys, style chord pads, arp, live looper, knob layers, menu. */
#include "app.h"
#include "board.h"
#include "settings.h"
#include "tempo.h"
#include "packs.h"
#include "looper.h"
#include "patterns.h"
#include "fx.h"
#include "gen.h"
#include "tusb.h"
#include <stdio.h>
#include <string.h>

ui_state_t U;
theory_cfg_t T;
uint8_t g_scale, g_pack;   /* derived from S.sound; g_pack = NO_PACK for plain scales */
arp_t ARP;
looper_t LOOP;

/* ------------------------------------------------------------------ tables */
const uint16_t LOOP_LENS[LOOP_LEN_COUNT] = { LOOP_BAR / 2, LOOP_BAR, 2 * LOOP_BAR, 4 * LOOP_BAR, 8 * LOOP_BAR, 16 * LOOP_BAR };
const char *const LOOP_LEN_NAMES[LOOP_LEN_COUNT] = { "1/2 bar", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" };
static const uint8_t QUANT_GRIDS[] = { 24, 48, 96, 12, 32, 16 };          /* in 96 PPQN units */
static const char *const QUANT_GRID_NAMES[] = { "1/16", "1/8", "1/4", "1/32", "1/8T", "1/16T" };
static const char *const VEL_NAMES[] = { "Soft", "Linear", "Hard", "Fixed" };
static const char *const KEYMAP_NAMES[] = { "Spread", "WhiteKeys", "Chromatic" };
static const char *const ONOFF[] = { "Off", "On" };
static const char *const CLK_NAMES[] = { "Auto", "Internal" };
static const char *const PPQN_NAMES[] = { "4 PPQN", "1 PPQN", "2 PPQN", "24 PPQN" };
static const char *const CLKPOL_NAMES[] = { "Transistr", "Direct" };
static const char *const PIN2_NAMES[] = { "Reset in", "Clock out" };
static const uint8_t PPQN_DIV[4] = { 6, 24, 12, 1 };   /* 24 PPQN ticks per pulse for 4, 1, 2, 24 PPQN */

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Change the play channel without leaving anything behind on the old one. Held keys/pads release on the channel
 * they started on (voices remember it); the arp is restarted; controllers are handed over. */
static bool pedal_is_down;
static void midi3(uint8_t s, uint8_t a, uint8_t b);
static void arp_clear_wrap(void);
static void set_midi_channel(uint8_t ch)
{
    uint8_t old = S.midi_ch;
    if (ch == old) return;
    arp_clear_wrap();                                /* its sounding note is released on the old channel */
    midi3((uint8_t)(0xE0 | old), 0x00, 0x40);       /* pitch bend back to centre */
    midi3((uint8_t)(0xB0 | old), 1, 0);             /* mod wheel off */
    if (pedal_is_down) { midi3((uint8_t)(0xB0 | old), 64, 0); midi3((uint8_t)(0xB0 | ch), 64, 127); }   /* move sustain */
    S.midi_ch = ch;
}

/* K2 list: every scale except Chromatic (diatonic chords on the pads), then every style pack */
int sound_count(void) { return (SCALE_COUNT - 1) + PACK_COUNT; }
bool sound_is_style(int s) { return s >= SCALE_COUNT - 1; }
const char *sound_name(int s) { return sound_is_style(s) ? PACKS[s - (SCALE_COUNT - 1)].name : SCALES[s].name; }
static void apply_sound(void)
{
    if (S.sound >= sound_count()) S.sound = 0;
    if (S.pad_octave < -3 || S.pad_octave > 3) S.pad_octave = 0;
    if (sound_is_style(S.sound)) { g_pack = (uint8_t)(S.sound - (SCALE_COUNT - 1)); g_scale = PACKS[g_pack].scale; }
    else { g_pack = NO_PACK; g_scale = S.sound; }
}

/* ------------------------------------------------------------------ MIDI out */
/* All channel messages go through this queue and are handed to TinyUSB as whole packets when there is room, so a
 * burst can never silently drop a note-off (a dropped note-off = a note stuck on the synth forever). If the queue
 * itself is nearly full, note-ons and CCs are dropped first; note-offs always get in. */
#define OUTQ 96
static uint8_t outq[OUTQ][4]; static uint16_t outq_head, outq_tail;
static uint16_t outq_count(void) { return (uint16_t)((outq_head + OUTQ - outq_tail) % OUTQ); }
static void midi_flush(void)
{
    while (outq_tail != outq_head && tud_midi_mounted()) {
        if (!tud_midi_packet_write(outq[outq_tail])) break;          /* TinyUSB FIFO full: try again next pass */
        outq_tail = (uint16_t)((outq_tail + 1) % OUTQ);
    }
}
static void midi3(uint8_t s, uint8_t a, uint8_t b)
{
    bool is_off = (s & 0xF0) == 0x80 || ((s & 0xF0) == 0x90 && b == 0);
    if (outq_count() >= OUTQ - 1) { midi_flush(); if (outq_count() >= OUTQ - 1) return; }
    if (!is_off && outq_count() >= OUTQ - 16) return;               /* keep the last 16 slots for note-offs */
    uint8_t *p = outq[outq_head];
    p[0] = (uint8_t)((s >> 4) & 0x0F); p[1] = s; p[2] = a; p[3] = b;   /* cable 0, CIN = status high nibble */
    outq_head = (uint16_t)((outq_head + 1) % OUTQ);
    midi_flush();
}
static void midi1(uint8_t s)                         /* single-byte realtime (clock) */
{
    if (outq_count() >= OUTQ - 16) return;
    uint8_t *p = outq[outq_head]; p[0] = 0x0F; p[1] = s; p[2] = p[3] = 0;
    outq_head = (uint16_t)((outq_head + 1) % OUTQ);
    midi_flush();
}
void app_midi_flush(void) { midi_flush(); }

/* All Notes Off (CC123) + sustain off on the channels in ch_mask */
static void midi_panic(uint16_t ch_mask)
{
    for (int ch = 0; ch < 16; ch++) if (ch_mask >> ch & 1) { midi3((uint8_t)(0xB0 | ch), 64, 0); midi3((uint8_t)(0xB0 | ch), 123, 0); }
}

static void midi_note_out(uint8_t ch, uint8_t n, uint8_t v) /* final output, after the MIDI effects */
{
    if (v) { midi3((uint8_t)(0x90 | ch), n, v); if (U.note_count[n] < 255) U.note_count[n]++; }
    else   { midi3((uint8_t)(0x80 | ch), n, 0); if (U.note_count[n]) U.note_count[n]--; }
}
static fx_cfg_t FX = { .out = midi_note_out };
static void raw_note(uint8_t ch, uint8_t n, uint8_t v)      /* v = 0 -> off; not recorded; through the effects */
{
    fx_note(ch, n, v, g_ms, FXS_LIVE);
}
static void loop_start(void);
static void loop_update(void);
static uint16_t loop_channels(void)                  /* bitmask of MIDI channels present in the loop + current */
{
    uint16_t m = (uint16_t)(1u << S.midi_ch);
    for (int i = 0; i < LOOP.n; i++) m |= (uint16_t)(1u << (LOOP.ev[i].ch & 15));
    return m;
}
static uint8_t live_from = FXS_LIVE;                 /* FXS_ARP while the arp is playing a note */
static void live_note(uint8_t ch, uint8_t n, uint8_t v)     /* what the player does: sounds + gets recorded */
{
    if (v && U.rec_armed) {                          /* armed: the first note starts the loop and the recording */
        U.rec_armed = false;
        tempo_restart();                             /* internal clock: the beat starts on this note (no-op if synced) */
        loop_start();
        looper_record(&LOOP, true);
    }
    fx_note(ch, n, v, g_ms, live_from);
    looper_input(&LOOP, ch, n, v);
}
static void loop_out(uint8_t ch, uint8_t n, uint8_t v, void *ctx) { (void)ctx; fx_note(ch, n, v, g_ms, FXS_LOOP); }
static void arp_out(uint8_t n, uint8_t v, bool on, void *ctx)
{
    (void)ctx; live_from = FXS_ARP; live_note(S.midi_ch, n, on ? (v ? v : 1) : 0); live_from = FXS_LIVE;
}

/* ------------------------------------------------------------------ voices: remember exactly what each key/pad sent */
typedef struct { uint8_t notes[8], n, ch, vel; bool to_arp, held; } voice_t;
static voice_t kv[25], pv[8];

static void voice_start(voice_t *v, const uint8_t *notes, int n, uint8_t ch, uint8_t vel, bool to_arp)
{
    v->n = (uint8_t)n; v->ch = ch; v->vel = vel ? vel : 1; v->to_arp = to_arp; v->held = true;
    memcpy(v->notes, notes, (size_t)n);
    if (n > 1 && !to_arp) fx_group_begin();
    for (int i = 0; i < n; i++) { if (to_arp) arp_note_on(&ARP, notes[i], v->vel); else live_note(ch, notes[i], v->vel); }
    if (n > 1 && !to_arp) fx_group_end(g_ms);
}
static void voice_stop(voice_t *v)
{
    if (!v->held) return;
    for (int i = 0; i < v->n; i++) { if (v->to_arp) arp_note_off(&ARP, v->notes[i]); else live_note(v->ch, v->notes[i], 0); }
    v->held = false; v->n = 0;
}
bool app_notes_held(void)
{
    for (int i = 0; i < 25; i++) if (kv[i].held) return true;
    for (int i = 0; i < 8; i++) if (pv[i].held) return true;
    return ARP.nheld != 0 || LOOP.playing;
}
bool app_pad_held(void) { for (int i = 0; i < 8; i++) if (pv[i].held) return true; return false; }

static void all_notes_off(void)
{
    for (int i = 0; i < 25; i++) voice_stop(&kv[i]);
    for (int i = 0; i < 8; i++) voice_stop(&pv[i]);
    arp_clear(&ARP);
    fx_panic();
    for (int n = 0; n < 128; n++) U.note_count[n] = 0;     /* fx_panic already sent the offs, on their own channels */
}

/* ------------------------------------------------------------------ helpers */
static void sync_all(void)
{
    apply_sound();
    T.root = S.root; T.scale = g_scale; T.keymap = (keymap_t)S.keymap;
    if (S.voicing >= VOICE_COUNT) S.voicing = VOICE_SMOOTH;
    if (S.scale_chords >= F_COUNT) S.scale_chords = F_TRI;
    T.chord_type = CHORD_TRIAD; T.inversion = 0; T.spread = 0;
    ARP.mode = (arp_mode_t)S.arp_mode; ARP.rate = S.arp_rate; ARP.gate = S.arp_gate; ARP.swing = S.arp_swing;
    ARP.octaves = S.arp_octaves; ARP.ratchet = S.arp_ratchet; ARP.euclid_k = S.arp_euclid_k; ARP.euclid_n = S.arp_euclid_n;
    ARP.latch = S.arp_latch; ARP.accent = S.arp_accent; ARP.dirty = true;
    looper_set_length(&LOOP, LOOP_LENS[S.loop_len % LOOP_LEN_COUNT]);
    if (S.quant_grid >= sizeof QUANT_GRIDS) S.quant_grid = 0;
    LOOP.quant = S.loop_quant ? QUANT_GRIDS[S.quant_grid] : 0;
    if (S.te_pin2 > 1) S.te_pin2 = 0;
    clockin_enable(S.clk_in, !S.clk_in_direct, S.te_pin2 == 1);
    if (S.fx_drift > 7) S.fx_drift = 0;
    if (S.fx_shift < -7 || S.fx_shift > 7) S.fx_shift = 0;
    if (S.fx_echo > 7) S.fx_echo = 0;
    if (S.fx_octaver >= OCT_COUNT) S.fx_octaver = 0;
    if (S.fx_strum > 60) S.fx_strum = 0;
    if (S.fx_ratchet > 4) S.fx_ratchet = 0;
    if (S.fx_gate >= GATE_PATTERNS) S.fx_gate = 0;
    if (S.fx_harmony >= HARM_COUNT) S.fx_harmony = 0;
    if (S.fx_euclid >= EUCLID_PRESETS) S.fx_euclid = 0;
    if (S.fx_speed > 2) S.fx_speed = 1;
    FX.drift = S.fx_drift; FX.shift = S.fx_shift; FX.echo = S.fx_echo; FX.octaver = S.fx_octaver; FX.strum_ms = S.fx_strum;
    FX.ratchet = S.fx_ratchet; FX.gate = S.fx_gate; FX.harmony = S.fx_harmony; FX.thin = 0; FX.accent = S.fx_accent ? 1 : 0; FX.euclid = 0; FX.theory = &T;
    g_vel_sens = S.vel_sens >= 5 && S.vel_sens <= 20 ? S.vel_sens : 10;
}

static void popup(const char *title, const char *value, int bar)
{
    snprintf(U.pop_title, sizeof U.pop_title, "%s", title);
    snprintf(U.pop_value, sizeof U.pop_value, "%s", value);
    U.pop_bar = (int16_t)bar; U.pop_until = g_ms + 1200;
}

static uint8_t curve(uint8_t v)
{
    if (S.full_level) return 127;
    switch (S.vel_curve) {
    case VEL_SOFT: { uint32_t x = (uint32_t)v * 127, r = 0; while ((r + 1) * (r + 1) <= x) r++; return (uint8_t)(r > 127 ? 127 : r); }
    case VEL_HARD: return (uint8_t)(v * v / 127 < 1 ? 1 : v * v / 127);
    case VEL_FIXED: return 100;
    default: return v;
    }
}

/* ------------------------------------------------------------------ keys: always the scale */

static int key_note(int key) { return 48 + 12 * S.octave + key; }

static void key_on(int key, uint8_t vel)
{
    char v[20];
    if (U.shift) {                                   /* SHIFT + key: the arp labels printed above the keys */
        static const uint8_t BASE[6] = { ARP_UP, ARP_DOWN, ARP_UPDOWN, ARP_INCL, ARP_ORDER, ARP_RANDOM };
        static const uint8_t ALT[6]  = { ARP_SCALEWALK, ARP_CONVERGE, ARP_BOUNCE, ARP_PINKY, ARP_THUMB, ARP_CHORD };
        static const uint8_t SWING[6] = { 50, 55, 57, 59, 61, 64 };
        if (key < 8) { S.arp_rate = (uint8_t)key; popup("ARP RATE", ARP_RATE_NAMES[key], -1); }
        else if (key < 14) {                         /* press again for the alternative mode */
            int m = key - 8;
            S.arp_mode = (S.arp_mode == BASE[m]) ? ALT[m] : BASE[m];
            popup("ARP MODE", ARP_MODE_NAMES[S.arp_mode], -1);
        }
        else if (key == 14) { S.arp_latch ^= 1; popup("ARP LATCH", ONOFF[S.arp_latch], -1); if (!S.arp_latch) arp_clear(&ARP); }
        else if (key < 19) { S.arp_octaves = (uint8_t)(key - 14); snprintf(v, sizeof v, "%d", S.arp_octaves); popup("ARP OCTAVES", v, -1); }
        else { S.arp_swing = SWING[key - 19]; snprintf(v, sizeof v, "%d%%", S.arp_swing); popup("ARP SWING", v, -1); }
        sync_all(); settings_touch();
        return;
    }
    uint8_t note = (uint8_t)(T.keymap == KEYMAP_SPREAD ? spread_key_note(&T, key, S.octave) : map_key_note(&T, key_note(key)));
    voice_stop(&kv[key]);
    voice_start(&kv[key], &note, 1, S.midi_ch, curve(vel), S.arp_on);
    U.last_note = note; U.note_until = g_ms + 900;
}

/* ------------------------------------------------------------------ pads: chords of the current style */
static int pad_notes(int pad, uint8_t *out, char *name)
{
    int n;
    if (g_pack != NO_PACK) n = pack_chord(g_pack, pad, S.root, S.voicing, out, name);
    else if (pad < 7) n = scale_chord(g_scale, S.root, pad, S.scale_chords, S.voicing, out, name);   /* I..VII */
    else n = scale_chord(g_scale, S.root, 0, S.scale_chords, VOICE_HIGH, out, name);                  /* I, high */
    int k = 0;                                                    /* pad octave shift (SHIFT + OCT) */
    for (int i = 0; i < n; i++) { int v = out[i] + 12 * S.pad_octave; if (v >= 0 && v <= 127) out[k++] = (uint8_t)v; }
    return k;
}

static int pad_notes(int pad, uint8_t *out, char *name);

/* ---- patterns: there is always a current pattern (P1..P8); the looper holds it, flash keeps all 8 ---- */
static uint16_t saved_edits;

static bool pattern_dirty(void) { return LOOP.edits != saved_edits; }

static void pattern_persist(void)                    /* write the current pattern to its slot if it changed */
{
    if (!pattern_dirty()) return;
    if (LOOP.recording) looper_record(&LOOP, false);
    for (int n = 0; n < LOOP.nsnd; n++) raw_note(LOOP.snd[n].ch, LOOP.snd[n].note, 0);   /* flash stalls ~0.15 s */
    LOOP.nsnd = 0;
    if (looper_empty(&LOOP)) pattern_erase(U.slot);
    else { pattern_ctx_t c = { S.bpm, S.sound, S.root, S.loop_len, S.voicing }; pattern_save(U.slot, &LOOP, &c); }
    saved_edits = LOOP.edits;
}

static void pattern_apply(int slot)                  /* make `slot` current (loop stopped, or at the loop boundary) */
{
    if (LOOP.recording) looper_record(&LOOP, false);
    for (int i = 0; i < 8; i++) voice_stop(&pv[i]);
    pattern_ctx_t c;
    if (pattern_load(slot, &LOOP, &c)) {
        S.sound = c.sound; S.root = c.root; S.loop_len = c.loop_len % LOOP_LEN_COUNT;
        if (c.voicing < VOICE_COUNT) S.voicing = c.voicing;
        if (!tempo_external() && c.bpm >= 40 && c.bpm <= 300) S.bpm = c.bpm;   /* synced: keep the external tempo */
    } else looper_clear(&LOOP);                      /* empty pattern: start fresh, keep the current sound */
    U.slot = (int8_t)slot; S.cur_pattern = (uint8_t)slot;
    saved_edits = LOOP.edits;
    sync_all(); settings_touch();
}

/* ---- undo momentum: repeating a destructive shortcut right away (nothing else touched) restores it ---- */
enum { UNDO_NONE, UNDO_CLEAR_CH, UNDO_CLEAR_PAT, UNDO_UNDO };
static uint8_t undo_kind; static int8_t undo_pad;

static void snapshot_current(void)                   /* current loop -> hidden undo slot */
{
    if (LOOP.recording) looper_record(&LOOP, false);
    for (int n = 0; n < LOOP.nsnd; n++) raw_note(LOOP.snd[n].ch, LOOP.snd[n].note, 0);   /* flash stall */
    LOOP.nsnd = 0;
    if (looper_empty(&LOOP)) pattern_erase(UNDO_SLOT);
    else { pattern_ctx_t c = { S.bpm, S.sound, S.root, S.loop_len, S.voicing }; pattern_save(UNDO_SLOT, &LOOP, &c); }
}
static void restore_current(void)                    /* hidden undo slot -> current loop (keeps sound/tempo) */
{
    pattern_ctx_t c;
    if (!pattern_load(UNDO_SLOT, &LOOP, &c)) looper_clear(&LOOP);
    LOOP.edits++;
}
static bool undo_repeat(uint8_t kind, int pad)       /* true if this is the immediate repeat of the same action */
{
    return U.undo_ready && undo_kind == kind && (kind != UNDO_CLEAR_PAT || undo_pad == pad);
}
static void undo_arm(uint8_t kind, int pad) { undo_kind = kind; undo_pad = (int8_t)pad; U.undo_ready = true; }

/* ---- generative (SHIFT + ARP / TAP / FULL LEVEL / NOTE REPEAT) ---- */
static int gen_pad_chord(int pad, uint8_t *out) { char nm[12]; return pad_notes(pad, out, nm); }
static void gen_snapshot(void) { pattern_persist(); U.can_revert = true; }   /* SHIFT + REC reverts to this */

static void do_generate(void)
{
    gen_snapshot();
    gen_generate(&LOOP, &T, gen_pad_chord, S.midi_ch, g_ms * 2654435761u + LOOP.edits);
    if (!LOOP.playing) loop_start();
    popup("GENERATE", "New idea", -1);
}

static void do_mutate(int percent, bool snapshot)
{
    if (looper_empty(&LOOP)) { if (snapshot) popup("MUTATE", "Loop is empty", -1); return; }
    if (snapshot) gen_snapshot();
    gen_mutate(&LOOP, &T, percent, g_ms * 2246822519u + LOOP.edits);
    if (snapshot) popup("MUTATE", "Varied", -1);
}

static void pattern_select(int slot)                 /* SHIFT + pad */
{
    char v[20];
    if (slot == U.slot && U.pending_slot < 0) { snprintf(v, sizeof v, "P%d", slot + 1); popup("PATTERN", v, -1); return; }
    pattern_persist();
    if (LOOP.playing) { U.pending_slot = (int8_t)slot; return; }     /* switches at the end of the loop */
    pattern_apply(slot);
    snprintf(v, sizeof v, "P%d%s", slot + 1, looper_empty(&LOOP) ? " (empty)" : ""); popup("PATTERN", v, -1);
}

static void pattern_copy_to(int slot)                /* SHIFT + PROG CHANGE, then pad */
{
    char v[20];
    U.save_armed = false;
    if (looper_empty(&LOOP)) { popup("COPY", "Pattern is empty", -1); return; }
    pattern_persist();
    U.slot = (int8_t)slot; S.cur_pattern = (uint8_t)slot; settings_touch();
    saved_edits = (uint16_t)(LOOP.edits - 1);        /* force a write into the new slot */
    pattern_persist();
    snprintf(v, sizeof v, "to P%d", slot + 1); popup("COPIED", v, -1);
}

static void pad_on(int pad, uint8_t vel)
{
    if (U.rec_clear == 1 || U.rec_clear == 2) {           /* SHIFT + REC + pad = clear that pattern completely */
        char v[16];
        U.rec_clear = 2; U.can_revert = false;
        if (undo_repeat(UNDO_CLEAR_PAT, pad)) {               /* same again right away: bring it back */
            if (pad == U.slot) { restore_current(); pattern_persist(); }
            else pattern_copy(pad, UNDO_SLOT);
            U.undo_ready = false;
            snprintf(v, sizeof v, "P%d", pad + 1); popup("RESTORED", v, -1);
            return;
        }
        if (pad == U.slot) { snapshot_current(); looper_clear(&LOOP); pattern_persist(); }
        else { pattern_copy(UNDO_SLOT, pad); pattern_erase(pad); if (U.pending_slot == pad) U.pending_slot = -1; }
        undo_arm(UNDO_CLEAR_PAT, pad);
        snprintf(v, sizeof v, "P%d empty", pad + 1); popup("CLEARED", v, -1);
        return;
    }
    if (U.save_armed) { pattern_copy_to(pad); return; }   /* SHIFT + PROG CHANGE armed: pad = copy destination */
    if (U.shift) { pattern_select(pad); return; }         /* SHIFT + pad = switch pattern */
    uint8_t notes[8]; char name[12];
    int n = pad_notes(pad, notes, name);
    if (S.full_level) vel = 127;
    voice_stop(&pv[pad]);
    voice_start(&pv[pad], notes, n, S.midi_ch, vel, S.arp_on);
    snprintf(U.chord_name, sizeof U.chord_name, "%s", name);
    U.chord_until = g_ms + 1500;
}

/* ------------------------------------------------------------------ knobs */
static int knob_acc[8];

static bool step_enum(int k, int d, uint8_t *v, int lo, int hi)
{
    knob_acc[k] += d;
    int steps = knob_acc[k] / 4;
    if (!steps) return false;
    knob_acc[k] -= steps * 4;
    int nv = clampi(*v + steps, lo, hi);
    if (nv == *v) return false;
    *v = (uint8_t)nv; return true;
}

static const uint8_t STUTTER_LEN[5] = { 0, 96, 48, 24, 12 };   /* 96 PPQN units: off, 1/4, 1/8, 1/16, 1/32 */
static const char *const STUTTER_NAMES[5] = { "Off", "1/4", "1/8", "1/16", "1/32" };
static const char *const SPEED_NAMES[3] = { "Half time", "Normal", "Double time" };
static uint8_t stutter;                              /* performance control, not saved */
static void stutter_set(uint8_t s);

static void knob(int k, int d)
{
    char v[24];
    if (U.menu_open) {
        if (k == 0) { uint8_t s = U.menu_sel; if (step_enum(k, d, &s, 0, menu_count() - 1)) U.menu_sel = s; }
        if (k == 1) { knob_acc[1] += d; int st = knob_acc[1] / 4; if (st) { knob_acc[1] -= st * 4; menu_adjust(U.menu_sel, st); } }
        return;
    }
    if (U.shift) {                                   /* SHIFT layer: MIDI effects */
        switch (k) {
        case 0: step_enum(k, d, &S.fx_drift, 0, 7);
                if (S.fx_drift) snprintf(v, sizeof v, "+/-%d steps", S.fx_drift); else snprintf(v, sizeof v, "Off");
                popup("PITCH DRIFT", v, -1); break;
        case 1: { uint8_t sh = (uint8_t)(S.fx_shift + 7);
                  step_enum(k, d, &sh, 0, 14); S.fx_shift = (int8_t)(sh - 7);
                  if (S.fx_shift) snprintf(v, sizeof v, "%+d steps", S.fx_shift); else snprintf(v, sizeof v, "Off");
                  popup("SCALE SHIFT", v, -1); } break;
        case 2: step_enum(k, d, &S.fx_echo, 0, 7);
                if (S.fx_echo) snprintf(v, sizeof v, "%d repeats", S.fx_echo); else snprintf(v, sizeof v, "Off");
                popup("SPIRAL ECHO", v, -1); break;
        case 3: S.fx_strum = (uint8_t)clampi(S.fx_strum + d, 0, 60);
                if (S.fx_strum) snprintf(v, sizeof v, "%d ms", S.fx_strum); else snprintf(v, sizeof v, "Off");
                popup("STRUM", v, -1); break;
        case 4: step_enum(k, d, &S.fx_gate, 0, GATE_PATTERNS - 1); popup("TRANCE GATE", GATE_NAMES[S.fx_gate], -1); break;
        case 5: step_enum(k, d, &S.fx_ratchet, 0, 4);
                if (S.fx_ratchet) snprintf(v, sizeof v, "x%d", S.fx_ratchet + 1); else snprintf(v, sizeof v, "Off");
                popup("RATCHET", v, -1); break;
        case 6: { uint8_t st = stutter; step_enum(k, d, &st, 0, 4); stutter_set(st); popup("STUTTER", STUTTER_NAMES[stutter], -1); } return;
        case 7: step_enum(k, d, &S.fx_speed, 0, 2); popup("LOOP SPEED", SPEED_NAMES[S.fx_speed], -1); break;
        }
        sync_all(); settings_touch();
        return;
    }
    switch (k) {                                     /* MUSIC layer */
    case 0: if (step_enum(k, d, &S.root, 0, 11)) popup("KEY", NOTE_NAMES[S.root], -1); break;
    case 1: { uint8_t s = S.sound;
              if (step_enum(k, d, &s, 0, sound_count() - 1)) {
                  for (int i = 0; i < 8; i++) voice_stop(&pv[i]);
                  S.sound = s;
                  S.voicing = sound_is_style(s) ? PACKS[s - (SCALE_COUNT - 1)].voicing : SCALE_VOICING[s];   /* preset */
                  char t[20]; snprintf(t, sizeof t, "%s - %s", sound_is_style(s) ? "STYLE" : "SCALE", VOICE_NAMES[S.voicing]);
                  popup(t, sound_name(s), -1); } } break;
    case 2: if (step_enum(k, d, &S.voicing, 0, VOICE_COUNT - 1)) { for (int i = 0; i < 8; i++) voice_stop(&pv[i]); popup("VOICING", VOICE_NAMES[S.voicing], -1); } break;
    case 3: step_enum(k, d, &S.fx_octaver, 0, OCT_COUNT - 1); popup("OCTAVER", OCTAVER_NAMES[S.fx_octaver], -1); break;
    case 4: { uint8_t ch = S.midi_ch;                 /* K5: MIDI channel for what you play next */
              if (step_enum(k, d, &ch, 0, 15)) set_midi_channel(ch);
              snprintf(v, sizeof v, "Ch %d", S.midi_ch + 1); popup("MIDI CHANNEL", v, -1); } break;
    case 5: step_enum(k, d, &S.fx_harmony, 0, HARM_COUNT - 1); popup("SHADOW HARMONY", HARMONY_NAMES[S.fx_harmony], -1); break;
    case 6: if (step_enum(k, d, &S.loop_len, 0, LOOP_LEN_COUNT - 1)) popup("LOOP LENGTH", LOOP_LEN_NAMES[S.loop_len], -1); break;
    case 7: S.bpm = (uint16_t)clampi(S.bpm + d, 40, 300); snprintf(v, sizeof v, "%d BPM", S.bpm); popup("TEMPO", v, -1); break;
    }
    sync_all(); settings_touch();
}

/* ------------------------------------------------------------------ menu */
typedef struct { const char *name; uint8_t *p; uint8_t lo, hi; const char *const *labels; } menu_t;
static const menu_t MENU[] = {
    { "Key map",      &S.keymap,      0, KEYMAP_COUNT - 1, KEYMAP_NAMES },
    { "Scale chords", &S.scale_chords, 0, F_COUNT - 1, FLAVOUR_NAMES },
    { "Velocity",     &S.vel_curve,   0, VEL_CURVE_COUNT - 1, VEL_NAMES },
    { "Key sensitiv", &S.vel_sens,    5, 20, NULL },
    { "MIDI channel", &S.midi_ch,     0, 15, NULL },
    { "Quantize",     &S.loop_quant,  0, 1, ONOFF },
    { "Quant grid",   &S.quant_grid,  0, 5, QUANT_GRID_NAMES },
    { "Clock",        &S.clock_src,   0, CLK_SRC_COUNT - 1, CLK_NAMES },
    { "Clock out",    &S.clock_out,   0, 1, ONOFF },
    { "Clock in TE1", &S.clk_in,      0, 1, ONOFF },
    { "Clk in rate",  &S.clk_in_ppqn, 0, 3, PPQN_NAMES },
    { "Clk in input", &S.clk_in_direct, 0, 1, CLKPOL_NAMES },
    { "TE1 pin 2",    &S.te_pin2,     0, 1, PIN2_NAMES },
    { "Clk out rate", &S.clk_out_ppqn, 0, 3, PPQN_NAMES },
    { "Arp gate",     &S.arp_gate,    5, 100, NULL },
    { "Arp accent",   &S.arp_accent,  0, 1, ONOFF },
    { "Repeat rate",  &S.repeat_rate, 0, ARP_RATE_COUNT - 1, ARP_RATE_NAMES },
    { "Joy X inv",    &S.joy_x_invert, 0, 1, ONOFF },
    { "Joy Y inv",    &S.joy_y_invert, 0, 1, ONOFF },
};
int menu_count(void) { return (int)(sizeof MENU / sizeof MENU[0]); }
void menu_item(int i, const char **name, char *value, int vlen)
{
    const menu_t *m = &MENU[i]; *name = m->name;
    if (m->labels) snprintf(value, (size_t)vlen, "%s", m->labels[*m->p]);
    else snprintf(value, (size_t)vlen, "%d", m->lo ? *m->p : *m->p + 1);
}
void menu_adjust(int sel, int d)
{
    const menu_t *m = &MENU[sel];
    int v = clampi(*m->p + d, m->lo, m->hi);
    if (m->p == &S.midi_ch) { set_midi_channel((uint8_t)v); settings_touch(); return; }
    if (v != *m->p) { all_notes_off(); *m->p = (uint8_t)v; sync_all(); settings_touch(); }
}

/* ------------------------------------------------------------------ looper transport */
/* The loop position is an accumulator of 96 PPQN units, advanced on every clock tick by 4 (2 at half time, 8 at
 * double time). It is reset by loop start, MIDI Start/SPP and the clock-in reset, so with an external clock the
 * loop stays on the external bar grid. Stutter remaps it onto a short repeating slice. */
static uint32_t loop_acc, loop_acc_ms, stut_start;
static uint16_t last_raw;
static uint32_t loop_step(void) { return S.fx_speed == 0 ? 2 : S.fx_speed == 2 ? 8 : 4; }

static uint16_t loop_raw_now(void)
{
    uint32_t mpt = tempo_ms_per_tick(); if (!mpt) mpt = 1;
    uint32_t st = loop_step(), frac = (g_ms - loop_acc_ms) * st / mpt; if (frac > st - 1) frac = st - 1;
    return (uint16_t)((loop_acc + frac) % LOOP.length);
}

static uint16_t loop_now(void)
{
    uint16_t raw = loop_raw_now();
    if (!stutter) return raw;
    uint32_t L = STUTTER_LEN[stutter], off = ((uint32_t)raw + LOOP.length - stut_start) % LOOP.length;
    return (uint16_t)((stut_start + off % L) % LOOP.length);
}

static void stutter_set(uint8_t s)
{
    if (s == stutter) return;
    uint16_t raw = loop_raw_now();
    if (s) stut_start = (raw / STUTTER_LEN[s]) * STUTTER_LEN[s];   /* slice on the grid where we are */
    stutter = s;
    if (!s && LOOP.playing) looper_jump(&LOOP, raw);                /* released: back to the real position */
}

static void loop_start(void)
{
    looper_play(&LOOP, true);
    loop_acc = 0; loop_acc_ms = g_ms; last_raw = 0;
    uint16_t now = loop_now();
    LOOP.pos = (uint16_t)((now + LOOP.length - 1) % LOOP.length);
    looper_advance(&LOOP, now);                      /* fire events sitting exactly on the start position */
}

static void loop_update_inner(void);
static void loop_update(void)                        /* loop events landing together are strummed as one chord */
{
    if (!LOOP.playing) return;
    fx_group_begin();
    loop_update_inner();
    fx_group_end(g_ms);
}
static void loop_update_inner(void)
{
    uint16_t raw = loop_raw_now();
    if (U.pending_slot >= 0 && raw < last_raw) {     /* wrapped: switch to the queued pattern on the boundary */
        looper_advance(&LOOP, (uint16_t)(LOOP.length - 1));
        pattern_apply(U.pending_slot);
        U.pending_slot = -1;
        stutter = 0;
        loop_acc = raw;                              /* the new pattern starts on this boundary */
        LOOP.pos = (uint16_t)(LOOP.length - 1);
        raw = loop_raw_now();
    }
    if (raw < last_raw && U.evolve && !LOOP.recording && ++U.evolve_laps >= 2) { U.evolve_laps = 0; do_mutate(8, false); }
    last_raw = raw;
    uint16_t now = loop_now();
    if (stutter && now < LOOP.pos && LOOP.pos - now < LOOP.length / 2)   /* slice wrapped: restart the slice */
        LOOP.pos = (uint16_t)((stut_start + LOOP.length - 1) % LOOP.length);
    looper_advance(&LOOP, now);
}

/* ------------------------------------------------------------------ buttons */
static bool oct_up_held, oct_dn_held;

static void button_down(int b)
{
    char v[20];
    switch (b) {
    case BTN_PROG_SELECT: U.shift = true; break;
    case BTN_ARP:
        if (U.shift) { do_generate(); break; }                     /* SHIFT + ARP = generate */
        all_notes_off();                                           /* ARP = on/off, on press */
        S.arp_on ^= 1; popup("ARPEGGIATOR", S.arp_on ? ARP_MODE_NAMES[S.arp_mode] : "Off", -1); settings_touch();
        break;
    case BTN_PROG_CHANGE:
        if (U.shift) {                               /* SHIFT + PROG CHANGE: arm save, next pad = slot */
            U.save_armed = !U.save_armed;
            popup("COPY PATTERN", U.save_armed ? "Hit a pad" : "Cancelled", -1);
            if (U.save_armed) U.pop_until = g_ms + 60000;
            break;
        }
        U.menu_open = !U.menu_open; knob_acc[0] = knob_acc[1] = 0; if (!U.menu_open) settings_touch(); break;
    case BTN_BANK:                                   /* = REC */
        if (U.shift) { U.rec_clear = 1; break; }     /* SHIFT + REC: + pad = clear pattern, alone = clear channel */
        if (!LOOP.playing) {                         /* stopped: arm, the first note played starts the loop */
            U.rec_armed = !U.rec_armed;
            popup("LOOPER", U.rec_armed ? "Armed: play!" : "Disarmed", -1);
            break;
        }
        looper_record(&LOOP, !LOOP.recording);
        if (LOOP.recording) U.can_revert = false;
        popup("LOOPER", LOOP.recording ? "REC" : "Rec off", -1);
        break;
    case BTN_CC:                                     /* = PLAY */
        if (U.shift) {                               /* SHIFT + PLAY = undo the latest part (or revert a generate) */
            if (undo_repeat(UNDO_UNDO, 0)) { restore_current(); U.undo_ready = false; popup("REDO", "restored", -1); break; }
            if (U.can_revert) {
                snapshot_current();
                bool playing = LOOP.playing; U.can_revert = false;
                pattern_apply(U.slot); if (playing && !LOOP.playing) loop_start();
                popup("REVERT", "saved pattern", -1);
            } else if (looper_empty(&LOOP)) { popup("UNDO", "nothing to undo", -1); break; }
            else { snapshot_current(); looper_undo(&LOOP); popup("UNDO", "last part", -1); }
            undo_arm(UNDO_UNDO, 0);
            break;
        }
        if (LOOP.playing) {
            uint16_t chans = loop_channels();
            looper_play(&LOOP, false); U.pending_slot = -1; stutter = 0;
            fx_panic(); midi_panic(chans);           /* stop = silence, whatever got stuck (a latched arp keeps going) */
            for (int n = 0; n < 128; n++) U.note_count[n] = 0;
            pattern_persist(); popup("LOOPER", "Stop", -1);
        }
        else { U.rec_armed = false; U.loop_resume = false; loop_start(); popup("LOOPER", looper_empty(&LOOP) ? "Play (empty)" : "Play", -1); }
        break;
    case BTN_TAP:
        if (U.shift) { do_mutate(15, true); break; }               /* SHIFT + TAP = mutate */
        tempo_tap(); snprintf(v, sizeof v, "%d BPM", S.bpm); popup("TAP TEMPO", v, -1); break;
    case BTN_OCT_UP: case BTN_OCT_DOWN:
        if (b == BTN_OCT_UP) oct_up_held = true; else oct_dn_held = true;
        if (U.shift) {                               /* SHIFT + OCT = pad octave */
            for (int i = 0; i < 8; i++) voice_stop(&pv[i]);
            if (oct_up_held && oct_dn_held) S.pad_octave = 0;
            else S.pad_octave = (int8_t)clampi(S.pad_octave + (b == BTN_OCT_UP ? 1 : -1), -3, 3);
            snprintf(v, sizeof v, "%+d", S.pad_octave); popup("PAD OCTAVE", v, (S.pad_octave + 3) * 1000 / 6);
            settings_touch(); break;
        }
        if (oct_up_held && oct_dn_held) {             /* both octave buttons = reset octave + panic, all channels */
            S.octave = 0; S.pad_octave = 0;
            for (int i = 0; i < 25; i++) kv[i].held = false;
            for (int i = 0; i < 8; i++) pv[i].held = false;
            arp_clear(&ARP); fx_panic(); midi_panic(0xFFFF);
            for (int n = 0; n < 128; n++) U.note_count[n] = 0;
            popup("ALL NOTES OFF", "octaves reset", -1); settings_touch(); break;
        }
        else S.octave = (int8_t)clampi(S.octave + (b == BTN_OCT_UP ? 1 : -1), -3, 3);
        snprintf(v, sizeof v, "%+d", S.octave); popup("OCTAVE", v, (S.octave + 3) * 1000 / 6);
        settings_touch(); break;
    case BTN_FULL_LEVEL:
        if (U.shift) { S.fx_accent ^= 1; sync_all(); settings_touch(); popup("ACCENT", ONOFF[S.fx_accent], -1); break; }
        S.full_level ^= 1; popup("FULL LEVEL", ONOFF[S.full_level], -1); break;
    case BTN_NOTE_REPEAT:
        if (U.shift) { U.evolve = !U.evolve; U.evolve_laps = 0; popup("EVOLVE", U.evolve ? "On" : "Off", -1); break; }
        S.note_repeat ^= 1; snprintf(v, sizeof v, "%s %s", ONOFF[S.note_repeat], ARP_RATE_NAMES[S.repeat_rate]); popup("NOTE REPEAT", v, -1); break;
    }
}

static void button_up(int b)
{
    switch (b) {
    case BTN_PROG_SELECT: U.shift = false; break;
    case BTN_BANK:
        if (U.rec_clear == 1) {                      /* SHIFT + REC released without a pad: clear this channel */
            char v[16];
            U.can_revert = false;
            if (undo_repeat(UNDO_CLEAR_CH, 0)) {
                restore_current(); U.undo_ready = false;
                snprintf(v, sizeof v, "Ch %d", S.midi_ch + 1); popup("RESTORED", v, -1);
            } else {
                snapshot_current(); looper_clear_channel(&LOOP, S.midi_ch); undo_arm(UNDO_CLEAR_CH, 0);
                snprintf(v, sizeof v, "Ch %d", S.midi_ch + 1); popup("CLEARED", v, -1);
            }
        }
        U.rec_clear = 0; break;
    case BTN_OCT_UP: oct_up_held = false; break;
    case BTN_OCT_DOWN: oct_dn_held = false; break;
    }
}

/* ------------------------------------------------------------------ events */
void app_event(const input_ev_t *e)
{
    U.last_input_ms = g_ms;
    switch (e->type) {                               /* anything else touched: no more undo of the last shortcut */
    case EV_KEY_ON: case EV_KNOB: U.undo_ready = false; break;
    case EV_PAD_ON: if (!U.rec_clear) U.undo_ready = false; break;
    case EV_BTN_DOWN:
        if (e->a == BTN_PROG_SELECT) break;
        if ((e->a == BTN_BANK || e->a == BTN_CC) && U.shift) break;
        U.undo_ready = false; break;
    default: break;
    }
    switch (e->type) {
    case EV_KEY_ON: key_on(e->a, e->b); break;
    case EV_KEY_OFF: voice_stop(&kv[e->a]); break;
    case EV_PAD_ON: pad_on(e->a, e->b); break;
    case EV_PAD_OFF: voice_stop(&pv[e->a]); U.chord_until = g_ms + 1200; break;
    case EV_PAD_PRESSURE: break;
    case EV_KNOB: knob(e->a, e->d); break;
    case EV_JOY:
        if (e->a == 0) { int d = S.joy_x_invert ? -e->d : e->d; uint16_t pb = (uint16_t)clampi(8192 + d, 0, 16383); midi3((uint8_t)(0xE0 | S.midi_ch), pb & 0x7F, (uint8_t)(pb >> 7)); }
        else { int d = S.joy_y_invert ? -e->d : e->d; midi3((uint8_t)(0xB0 | S.midi_ch), 1, (uint8_t)clampi((d < 0 ? -d : d) >> 6, 0, 127)); }
        break;
    case EV_PEDAL: pedal_is_down = e->b; midi3((uint8_t)(0xB0 | S.midi_ch), 64, e->b ? 127 : 0); break;
    case EV_BTN_DOWN: button_down(e->a); break;
    case EV_BTN_UP: button_up(e->a); break;
    default: break;
    }
}

/* ------------------------------------------------------------------ MIDI in: clock + debug SysEx */
static uint8_t sx[16]; static uint8_t sxn;
static bool key_debug;
static uint32_t loop_max_us;

static void sysex_send(const uint8_t *d, int n) { midi_flush(); tud_midi_stream_write(0, d, (uint32_t)n); }
static void key_dbg(uint8_t key, uint32_t dt, uint8_t first, uint32_t lat)
{
    if (!key_debug) return;
    uint8_t m[] = { 0xF0, 0x7D, 0x4F, 0x10, key, (uint8_t)(dt & 0x7F), (uint8_t)(dt >> 7 & 0x7F), (uint8_t)(dt >> 14 & 0x7F),
                    first, (uint8_t)(lat & 0x7F), (uint8_t)(lat >> 7 & 0x7F), 0xF7 };
    sysex_send(m, sizeof m);
}
void app_report_loop(uint32_t us) { if (us > loop_max_us) loop_max_us = us; }

static void sysex_done(void)
{
    if (sxn < 4 || sx[1] != 0x7D || sx[2] != 0x4F) return;   /* F0 7D 'O' cmd ... */
    switch (sx[3]) {
    case 0x01: {
        uint16_t bpm = tempo_bpm_display();
        uint8_t m[] = { 0xF0, 0x7D, 0x4F, 0x01, (uint8_t)(loop_max_us & 0x7F), (uint8_t)(loop_max_us >> 7 & 0x7F),
                        (uint8_t)(loop_max_us >> 14 & 0x7F), 0x05, 0x00,
                        (uint8_t)(tempo_clockin() ? 2 : tempo_external() ? 1 : 0), (uint8_t)(bpm & 0x7F), (uint8_t)(bpm >> 7),
                        S.clk_in, S.clk_in_ppqn, S.clk_in_direct, (uint8_t)(LOOP.n & 0x7F), (uint8_t)(LOOP.n >> 7), 0xF7 };
        sysex_send(m, sizeof m); loop_max_us = 0; break; }
    case 0x02: key_debug = sxn > 4 && sx[4]; break;
    case 0x20: {
        uint8_t m[24] = { 0xF0, 0x7D, 0x4F, 0x20 }; int n = board_probe_pins(m + 4);
        m[4 + n] = 0xF7; sysex_send(m, 5 + n); break; }
    case 0x7F: settings_defaults(); sync_all(); settings_save_now(); popup("RESET", "factory", -1); break;
    }
}

void app_midi_in(const uint8_t p[4])
{
    uint8_t cin = p[0] & 0x0F;
    if (cin == 0x0F) {
        if (p[1] == 0xF8) tempo_midi_clock();
        else if (p[1] == 0xFA || p[1] == 0xFB) {     /* Start / Continue */
            if (p[1] == 0xFA) { tempo_ext_start(); arp_reset_phase(&ARP); loop_acc = (uint32_t)0 - loop_step(); }
            if (LOOP.playing || U.loop_resume) { U.loop_resume = false; loop_start(); }
        } else if (p[1] == 0xFC) {                   /* Stop: pause the loop, resume on the next Start */
            if (LOOP.playing) { uint16_t chans = loop_channels(); looper_play(&LOOP, false); U.loop_resume = true; fx_panic(); midi_panic(chans); }
        }
        return;
    }
    if (cin == 0x3 && p[1] == 0xF2) {               /* Song Position Pointer (in 16ths) */
        uint16_t spp = (uint16_t)(p[2] | p[3] << 7);
        tempo_song_position(spp); loop_acc = (uint32_t)spp * 24u - loop_step();
        return;
    }
    if (cin == 0x4 || (cin >= 0x5 && cin <= 0x7)) {
        int n = cin == 0x4 ? 3 : cin - 4;
        for (int i = 0; i < n; i++) {
            uint8_t b = p[1 + i];
            if (b == 0xF0) sxn = 0;
            if (sxn < sizeof sx) sx[sxn++] = b;
            if (b == 0xF7) sysex_done();
        }
    }
}

/* ------------------------------------------------------------------ periodic: clock, arp, looper, note repeat, LEDs */
static uint32_t repeat_tick;

static void leds(void)
{
    const uint8_t DIM = 3;
    uint32_t beat_phase = g_ms - U.beat_ms;
    bool loop_top = LOOP.playing && LOOP.pos < 24;
    led_set(LED_BANK_A, LOOP.recording ? (beat_phase < 250 ? 32 : 8) :                 /* REC */
                        U.rec_armed ? (((g_ms / 120) & 1) ? 32 : 0) : DIM);
    led_set(LED_BANK_B, looper_empty(&LOOP) ? 0 : (loop_top ? 32 : 6));                 /* loop has content */
    led_set(LED_CC, !LOOP.playing ? DIM :                                                /* PLAY: blinks on the beat, */
                    beat_phase < 120 ? 32 : (loop_top ? 12 : 0));                        /* glow on the loop's first beat */
    led_set(LED_PROG_CHANGE, U.menu_open ? 32 : DIM);
    led_set(LED_PROG_SELECT, U.shift ? 32 : DIM);
    led_set(LED_ARP, S.arp_on ? 32 : DIM);
    led_set(LED_TAP, beat_phase < 60 ? 32 : DIM);
    int8_t oct = U.shift ? S.pad_octave : S.octave;                                     /* SHIFT shows pad octave */
    led_set(LED_OCT_UP, oct > 0 ? (uint8_t)(10 + 7 * oct) : DIM);
    led_set(LED_OCT_DOWN, oct < 0 ? (uint8_t)(10 - 7 * oct) : DIM);
    led_set(LED_FULL_LEVEL, S.full_level ? 32 : S.fx_accent ? 12 : DIM);
    led_set(LED_NOTE_REPEAT, U.evolve ? (((g_ms / 400) & 1) ? 32 : 6) : S.note_repeat ? 32 : DIM);
    if (U.shift || U.save_armed) {                   /* pattern slots: stored = dim, loaded = bright, save = blink */
        static uint32_t exist_ms; static uint8_t exist;
        if (g_ms - exist_ms > 250) { exist_ms = g_ms; exist = 0; for (int s = 0; s < 8; s++) if (pattern_exists(s)) exist |= (uint8_t)(1u << s); }
        if (U.slot >= 0 && !looper_empty(&LOOP)) exist |= (uint8_t)(1u << U.slot);
        for (int p = 0; p < 8; p++) {
            uint8_t l = (exist >> p & 1) ? 5 : 0;
            if (p == U.slot) l = LOOP.playing ? (beat_phase < 120 ? 32 : 14) : 24;   /* current: bright, pulses */
            if (p == U.pending_slot) l = ((g_ms / 100) & 1) ? 32 : 0;               /* queued: fast blink */
            if (U.save_armed && p != U.slot && ((g_ms / 150) & 1)) l = (uint8_t)(l + 8);
            led_set(LED_PAD1 + p, l);
        }
        return;
    }
    for (int p = 0; p < 8; p++) {
        uint8_t l = pv[p].held ? (uint8_t)(8 + pv[p].vel / 5) : 2;
        if (S.arp_on && ARP.nheld && ARP.nseq && !pv[p].held && (ARP.step % 8) == p) l = 10;
        led_set(LED_PAD1 + p, l);
    }
}

static uint32_t clkout_until; static bool clkout_high;

void app_poll(void)
{
    if (clkout_high && (int32_t)(g_ms - clkout_until) >= 0) { clockout_set(false); clkout_high = false; }   /* 5 ms pulse */
    uint32_t cts;
    while (clockin_pop(&cts)) tempo_clockin_pulse(cts);
    if (clockin_reset_pop()) { tempo_clockin_reset(); arp_reset_phase(&ARP); repeat_tick = 0; loop_acc = (uint32_t)0 - loop_step(); }
    int ticks = tempo_poll();
    ARP.tick_ms = tempo_ms_per_tick();
    for (int t = 0; t < ticks; t++) {
        uint32_t tc = tempo_tick_count() - (uint32_t)(ticks - 1 - t);
        if (tc % 24 == 0) U.beat_ms = g_ms;
        if (S.clock_out && (!tempo_external() || tempo_clockin())) midi1(0xF8);
        loop_acc += loop_step(); loop_acc_ms = g_ms;
        if (tc % 6 == 0) fx_step16((uint8_t)((tc / 6) & 15), g_ms);   /* trance gate + euclid grid */
        if (S.te_pin2 == 1 && tc % PPQN_DIV[S.clk_out_ppqn & 3] == 0) { clockout_set(true); clkout_until = g_ms + 5; clkout_high = true; }
        loop_update();
        if (S.arp_on) arp_tick(&ARP, g_ms);
        if (S.note_repeat && ++repeat_tick >= ARP_RATE_TICKS[S.repeat_rate]) {
            repeat_tick = 0;
            for (int p = 0; p < 8; p++) if (pv[p].held && !pv[p].to_arp)
                for (int i = 0; i < pv[p].n; i++) { live_note(pv[p].ch, pv[p].notes[i], 0); live_note(pv[p].ch, pv[p].notes[i], pv[p].vel); }
        }
    }
    loop_update();
    arp_poll(&ARP, g_ms);
    FX.echo_ms = tempo_ms_per_tick() * 18;           /* dotted 1/8 */
    FX.ratchet_ms = tempo_ms_per_tick() * 3;         /* 1/32 */
    fx_poll(g_ms);
    static uint32_t last_led;
    if (g_ms - last_led >= 10) { last_led = g_ms; leds(); }
    if (!LOOP.playing && pattern_dirty() && g_ms - U.last_input_ms > 3000) pattern_persist();
    settings_poll(!app_notes_held());
}

static void arp_clear_wrap(void) { arp_clear(&ARP); }

void app_init(void)
{
    settings_load();
    if (S.sound >= sound_count()) S.sound = 0;
    fx_init(&FX);
    arp_init(&ARP, arp_out, NULL);
    ARP.theory = &T;
    looper_init(&LOOP, loop_out, NULL);
    sync_all();
    g_key_debug = key_dbg;
    U.last_input_ms = g_ms;
    U.pending_slot = -1;
    U.slot = (int8_t)(S.cur_pattern & 7);
    pattern_apply(U.slot);                           /* boot into the last pattern (stopped) */
}

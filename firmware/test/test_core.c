/* Host tests for the portable music core: see Makefile target "test" */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "theory.h"
#include "arp.h"
#include "packs.h"
#include "looper.h"
#include "fx.h"
#include "gen.h"

static int lon[128], loff[128];
static void lout(uint8_t ch, uint8_t n, uint8_t v, void *c) { (void)ch; (void)c; if (v) lon[n]++; else loff[n]++; }
static looper_t LP;
static int fx_bal[128], fx_ons, fx_on_t[64], fx_on_n[64]; static uint32_t fx_now;
/* fx_bal[n] = is note n currently sounding at the synth (on sets, off clears); chbal per channel */
static int chbal[16][128];
static int ch_sounding(void) { int s = 0; for (int c = 0; c < 16; c++) for (int n = 0; n < 128; n++) s += chbal[c][n]; return s; }
static void fxout(uint8_t ch, uint8_t n, uint8_t v) { chbal[ch & 15][n] = v ? 1 : 0; if (v) { fx_bal[n] = 1; if (fx_ons < 64) { fx_on_t[fx_ons] = (int)fx_now; fx_on_n[fx_ons] = n; } fx_ons++; } else fx_bal[n] = 0; }
static theory_cfg_t gen_t = { .root = 2, .scale = SC_DORIAN };
static int gen_chord(int pad, uint8_t *out) { return scale_chord(SC_DORIAN, 2, pad < 7 ? pad : 0, 0, 0, out, NULL); }
static uint32_t lfx_now;
static void lfx(uint8_t ch, uint8_t n, uint8_t v, void *c) { (void)c; fx_note(ch, n, v, lfx_now, FXS_LOOP); }
static void fx_run(uint32_t from, uint32_t to) { for (fx_now = from; fx_now <= to; fx_now++) fx_poll(fx_now); }
static void run(looper_t *l, int from, int to) { for (int p = from; p <= to; p++) looper_advance(l, (uint16_t)p); }

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void name_of(const theory_cfg_t *c, int key, char *buf, uint8_t *notes, int *n)
{ *n = key_chord(c, key, notes, 8); chord_name(notes, *n, buf); }

static uint8_t log_notes[256]; static int nlog;
static void out(uint8_t note, uint8_t vel, bool on, void *ctx) { (void)ctx; (void)vel; if (on && nlog < 256) log_notes[nlog++] = note; }

int main(void)
{
    theory_cfg_t c = { .root = 0, .scale = 0, .keymap = KEYMAP_SPREAD, .chord_type = CHORD_TRIAD };
    CHECK(scale_size(0) == 7 && scale_size(9) == 5 && scale_size(SCALE_COUNT - 1) == 12);
    CHECK(quantize(&c, 61) == 60);           /* C# -> C (tie goes down) */
    CHECK(quantize(&c, 66) == 65);           /* F# -> F */
    c.root = 9; c.scale = 1;                 /* A minor */
    CHECK(quantize(&c, 61) == 60);           /* C# -> C */
    CHECK(in_scale(&c, 67) && !in_scale(&c, 68));
    /* white-key map: C4 plays root, D4 next degree */
    c.keymap = KEYMAP_WHITE;
    CHECK(map_key_note(&c, 60) == 69);       /* A4 */
    CHECK(map_key_note(&c, 62) == 71);       /* B4 */
    CHECK(map_key_note(&c, 72) == 81);       /* one white-octave up = A5 */
    CHECK(map_key_note(&c, 61) == 72);       /* black key = 3rd above root: C5 */
    /* chord names in C major */
    char nm[16]; uint8_t ns[8]; int n;
    c.root = 0; c.scale = 0; c.keymap = KEYMAP_WHITE;
    const char *want[7] = { "C", "Dm", "Em", "F", "G", "Am", "Bdim" };
    int wk[7] = { 60, 62, 64, 65, 67, 69, 71 };
    for (int i = 0; i < 7; i++) { name_of(&c, wk[i], nm, ns, &n); CHECK(n == 3 && strcmp(nm, want[i]) == 0); if (strcmp(nm, want[i])) printf("  got %s want %s\n", nm, want[i]); }
    c.chord_type = CHORD_SEVENTH;
    const char *want7[7] = { "Cmaj7", "Dm7", "Em7", "Fmaj7", "G7", "Am7", "Bm7b5" };
    for (int i = 0; i < 7; i++) { name_of(&c, wk[i], nm, ns, &n); CHECK(strcmp(nm, want7[i]) == 0); if (strcmp(nm, want7[i])) printf("  got %s want %s\n", nm, want7[i]); }
    c.chord_type = CHORD_TRIAD; c.inversion = 1;
    name_of(&c, 60, nm, ns, &n); CHECK(strcmp(nm, "C/E") == 0); printf("  inv1: %s\n", nm);
    c.inversion = 0; c.chord_type = CHORD_POWER; name_of(&c, 67, nm, ns, &n); CHECK(strcmp(nm, "G5") == 0);
    c.chord_type = CHORD_SUS4; name_of(&c, 67, nm, ns, &n); CHECK(strcmp(nm, "Gsus4") == 0);
    /* snap-mode chord from a black key */
    c.keymap = KEYMAP_SPREAD; c.chord_type = CHORD_TRIAD; name_of(&c, 70, nm, ns, &n); CHECK(strcmp(nm, "Am") == 0);
    /* every key in every scale yields notes in scale (snap mode) */
    for (int s = 0; s < SCALE_COUNT; s++) for (int r = 0; r < 12; r++) for (int k = 36; k < 96; k++) {
        theory_cfg_t t = { .root = (uint8_t)r, .scale = (uint8_t)s, .keymap = KEYMAP_SPREAD };
        CHECK(in_scale(&t, map_key_note(&t, k)));
        t.keymap = KEYMAP_WHITE; CHECK(in_scale(&t, map_key_note(&t, k)));
        uint8_t cn[8]; int m = key_chord(&t, k, cn, 8);
        for (int i = 0; i < m; i++) CHECK(in_scale(&t, cn[i]));
    }
    /* arp */
    arp_t a; arp_init(&a, out, NULL); a.rate = 4; /* 1/16 = 6 ticks */
    arp_note_on(&a, 64, 100); arp_note_on(&a, 60, 100); arp_note_on(&a, 67, 100);
    nlog = 0; for (int t = 0; t < 6 * 6; t++) { arp_tick(&a, (uint32_t)t * 20); arp_poll(&a, (uint32_t)t * 20 + 10); }
    CHECK(nlog == 6 && log_notes[0] == 60 && log_notes[1] == 64 && log_notes[2] == 67 && log_notes[3] == 60);
    a.mode = ARP_UPDOWN; a.dirty = true; a.step = 0; nlog = 0;
    for (int t = 0; t < 6 * 4; t++) { arp_tick(&a, 1000 + (uint32_t)t * 20); arp_poll(&a, 1000 + (uint32_t)t * 20 + 10); }
    CHECK(nlog == 4 && log_notes[0] == 60 && log_notes[1] == 64 && log_notes[2] == 67 && log_notes[3] == 64);
    a.mode = ARP_SCALEWALK; a.theory = &c; a.dirty = true; a.step = 0; nlog = 0;
    for (int t = 0; t < 6 * 5; t++) { arp_tick(&a, 2000 + (uint32_t)t * 20); arp_poll(&a, 2000 + (uint32_t)t * 20 + 10); }
    CHECK(nlog == 5 && log_notes[0] == 60 && log_notes[1] == 62 && log_notes[2] == 64 && log_notes[3] == 65 && log_notes[4] == 67);
    CHECK(arp_euclid_hit(3, 8, 0) && !arp_euclid_hit(3, 8, 1));
    int hits = 0; for (int s = 0; s < 8; s++) hits += arp_euclid_hit(3, 8, (uint16_t)s); CHECK(hits == 3);
    /* styles: every pad note is in the scale the keys play, chords voice smoothly, names resolve */
    {
        uint8_t cn[10]; char nm2[16]; int unnamed = 0, maxjump = 0;
        for (int p = 0; p < PACK_COUNT; p++) for (int r = 0; r < 12; r++) for (int vo = 0; vo < VOICE_COUNT; vo++) {
            theory_cfg_t kt = { .root = (uint8_t)r, .scale = PACKS[p].scale };
            int prev_mid = -1;
            for (int pad = 0; pad < 8; pad++) {
                int k = pack_chord(p, pad, r, vo, cn, nm2);
                CHECK(k >= 2 && k <= 8);
                for (int i = 0; i < k; i++) { CHECK(in_scale(&kt, cn[i])); if (!in_scale(&kt, cn[i])) printf("  %s pad%d note %d\n", PACKS[p].name, pad, cn[i]); }
                for (int i = 1; i < k; i++) CHECK(cn[i] > cn[i - 1]);
                CHECK(cn[0] >= 28 && cn[k - 1] <= 100);
                if (vo == VOICE_SMOOTH) {
                    int mid = 0; for (int i = 0; i < k; i++) mid += cn[i]; mid /= k;
                    CHECK(mid >= 54 && mid <= 68);
                    if (prev_mid >= 0 && abs(mid - prev_mid) > maxjump) maxjump = abs(mid - prev_mid);
                    prev_mid = mid;
                }
                if (r == 0 && vo == 0 && strchr(nm2, '*')) { unnamed++; printf("  unnamed: %s pad%d %s\n", PACKS[p].name, pad + 1, nm2); }
            }
        }
        int k = pack_chord(13, 0, 0, VOICE_SMOOTH, cn, nm2);       /* Jazz 1, pad 1 in C = Dm9 */
        CHECK(!strcmp(nm2, "Dm9")); CHECK(k == 5);
        CHECK(unnamed == 0);
        for (int p = 0; p < PACK_COUNT; p++) {
            CHECK(strlen(PACKS[p].name) <= 12 && PACKS[p].voicing < VOICE_COUNT);
            for (int q = p + 1; q < PACK_COUNT; q++) CHECK(strcmp(PACKS[p].name, PACKS[q].name) != 0);
        }
        for (int s = 0; s < SCALE_COUNT; s++) CHECK(SCALE_VOICING[s] < VOICE_COUNT);
        printf("  %d styles, %d scales, smooth voicing max centre jump %d semitones, %d unnamed\n", PACK_COUNT, SCALE_COUNT, maxjump, unnamed);
        /* spread keymap: neighbouring keys never repeat and always rise */
        for (int s = 0; s < SCALE_COUNT; s++) for (int r = 0; r < 12; r++) {
            theory_cfg_t kt = { .root = (uint8_t)r, .scale = (uint8_t)s, .keymap = KEYMAP_SPREAD };
            for (int key = 1; key < 25; key++) CHECK(spread_key_note(&kt, key, 0) > spread_key_note(&kt, key - 1, 0));
            CHECK(spread_key_note(&kt, 12, 0) % 12 == r);
        }
    }
    /* looper: record a note in lap 1, hear it in lap 2, overdub, undo, erase, wrap-around note */
    {
        looper_t *l = &LP; looper_init(l, lout, NULL); looper_set_length(l, LOOP_BAR);   /* 1 bar = 384 */
        looper_record(l, true);
        run(l, 1, 100); looper_input(l, 0, 60, 100); run(l, 101, 150); looper_input(l, 0, 60, 0);
        run(l, 151, 383); looper_record(l, false);
        memset(lon, 0, sizeof lon); memset(loff, 0, sizeof loff);
        run(l, 0, 383);                                   /* lap 2 */
        CHECK(lon[60] == 1 && loff[60] == 1);
        looper_record(l, true);                           /* overdub a second note */
        run(l, 1, 200); looper_input(l, 0, 64, 90); run(l, 201, 250); looper_input(l, 0, 64, 0); run(l, 251, 383);
        looper_record(l, false);
        memset(lon, 0, sizeof lon); memset(loff, 0, sizeof loff);
        run(l, 0, 383); CHECK(lon[60] == 1 && lon[64] == 1);
        looper_undo(l);
        memset(lon, 0, sizeof lon); run(l, 0, 383); CHECK(lon[60] == 1 && lon[64] == 0);
        looper_erase_note(l, 60, LOOP_ANY_CH); memset(lon, 0, sizeof lon); run(l, 0, 383); CHECK(lon[60] == 0 && looper_empty(l));
        /* channel-aware erase and clear */
        looper_record(l, true); run(l, 1, 50); looper_input(l, 0, 50, 90); looper_input(l, 3, 50, 90); run(l, 51, 80);
        looper_input(l, 0, 50, 0); looper_input(l, 3, 50, 0); run(l, 81, 383); looper_record(l, false);
        looper_erase_note(l, 50, 3); CHECK(l->n == 2 && l->ev[0].ch == 0 && l->ev[1].ch == 0);
        looper_record(l, true); run(l, 0, 60); looper_input(l, 3, 52, 90); run(l, 61, 90); looper_input(l, 3, 52, 0); looper_record(l, false);
        looper_clear_channel(l, 3); CHECK(l->n == 2); looper_clear_channel(l, 0); CHECK(looper_empty(l));
        /* note held across the loop end */
        looper_record(l, true); run(l, 1, 350); looper_input(l, 0, 67, 80); run(l, 351, 383); run(l, 0, 30);
        looper_input(l, 0, 67, 0); run(l, 31, 383); looper_record(l, false);
        memset(lon, 0, sizeof lon); memset(loff, 0, sizeof loff);
        run(l, 0, 383); run(l, 0, 40);   /* off@30 first closes the note played during the overdub lap */
        CHECK(lon[67] == 1 && loff[67] == 2);
        /* stop releases sounding notes */
        memset(loff, 0, sizeof loff); run(l, 41, 360); looper_play(l, false); CHECK(loff[67] == 1);
        /* quantize to 1/16 (24 units): a note at 100 lands on 96 */
        looper_clear(l); l->quant = 24; looper_record(l, true); run(l, 1, 100); looper_input(l, 0, 72, 100);
        run(l, 101, 120); looper_input(l, 0, 72, 0); looper_record(l, false);
        CHECK(l->n == 2 && l->ev[0].pos == 96 && l->ev[1].pos == 116);
    }
    /* generative: every note-on has its note-off, everything in key, playback leaves nothing stuck */
    for (int len = 192; len <= 16 * 384; len *= 2) for (uint32_t seed = 1; seed < 40; seed++) {
        looper_t *l = &LP; looper_init(l, lout, NULL); looper_set_length(l, (uint16_t)len);
        gen_generate(l, &gen_t, gen_chord, 0, seed);
        CHECK(l->n > 0 && l->n <= LOOP_MAX_EVENTS);
        for (int rounds = 0; rounds < 3; rounds++) {
            int bal[128] = { 0 };
            for (int i = 0; i < l->n; i++) { bal[l->ev[i].note] += l->ev[i].vel ? 1 : -1; if (l->ev[i].vel) CHECK(in_scale(&gen_t, l->ev[i].note)); }
            for (int n = 0; n < 128; n++) CHECK(bal[n] == 0);
            memset(lon, 0, sizeof lon); memset(loff, 0, sizeof loff);
            looper_play(l, true); for (int lap = 0; lap < 2; lap++) run(l, 1, len - 1), run(l, 0, 0);
            looper_play(l, false);
            CHECK(l->nsnd == 0);
            gen_mutate(l, &gen_t, 30, seed * 7 + (uint32_t)rounds);
        }
    }

    /* regression: generate never duplicates notes, and long loops repeat whole blocks */
    for (int len = 384; len <= 16 * 384; len *= 2) {
        looper_t *l = &LP; looper_init(l, lout, NULL); looper_set_length(l, (uint16_t)len);
        gen_generate(l, &gen_t, gen_chord, 0, 777);
        int dups = 0;
        for (int i = 0; i < l->n; i++) for (int j = i + 1; j < l->n; j++)
            if (l->ev[i].pos == l->ev[j].pos && l->ev[i].note == l->ev[j].note && l->ev[i].vel == l->ev[j].vel && l->ev[i].ch == l->ev[j].ch) dups++;
        CHECK(dups == 0); if (dups) printf("  %d bars: %d duplicates\n", len / 384, dups);
        int bal[128] = { 0 }; for (int i = 0; i < l->n; i++) bal[l->ev[i].note] += l->ev[i].vel ? 1 : -1;
        for (int n = 0; n < 128; n++) CHECK(bal[n] == 0);
    }
    /* regression: more than 32 sounding notes -> oldest released, the looper never loses track */
    {
        looper_t *l = &LP; looper_init(l, lout, NULL); looper_set_length(l, 384);
        for (int n = 0; n < 40; n++) looper_insert(l, 10, 0, (uint8_t)(40 + n), 90);      /* 40 ons, no offs */
        memset(lon, 0, sizeof lon); memset(loff, 0, sizeof loff);
        looper_play(l, true); run(l, 1, 20); CHECK(l->nsnd == 32);
        looper_play(l, false);
        int sounding = 0; for (int n = 0; n < 128; n++) sounding += lon[n] - loff[n];
        CHECK(sounding == 0);
    }

    /* MIDI effects */
    {
        theory_cfg_t kt = { .root = 0, .scale = SC_MAJOR };
        fx_cfg_t fc = { .theory = &kt, .echo_ms = 100, .ratchet_ms = 30, .out = fxout };
        fx_init(&fc);
#define LIVE FXS_LIVE
        /* spiral echo: 3 repeats climbing C major from C4 */
        fc.echo = 3; memset(fx_bal, 0, sizeof fx_bal); fx_ons = 0;
        fx_now = 0; fx_note(0, 60, 100, 0, LIVE); fx_run(1, 50); fx_note(0, 60, 0, 50, LIVE); fx_run(51, 600);
        CHECK(fx_ons == 4 && fx_on_n[1] == 62 && fx_on_n[2] == 64 && fx_on_n[3] == 65 && fx_on_t[1] == 100 && fx_on_t[3] == 300);
        fc.echo = 0;
        /* ratchet: 3 fast repeats, 30 ms apart */
        fc.ratchet = 3; fx_ons = 0; fx_now = 700; fx_note(0, 60, 100, 700, LIVE); fx_run(700, 900); fx_note(0, 60, 0, 900, LIVE); fx_run(900, 1000);
        CHECK(fx_ons == 4 && fx_on_t[1] == 730 && fx_on_t[3] == 790);
        fc.ratchet = 0;
        /* strum: 3-note chord, 20 ms apart, direction alternates */
        fc.strum_ms = 20; fx_ons = 0; fx_now = 1000;
        fx_group_begin(); fx_note(0, 60, 90, 1000, LIVE); fx_note(0, 64, 90, 1000, LIVE); fx_note(0, 67, 90, 1000, LIVE); fx_group_end(1000);
        fx_run(1000, 1100);
        CHECK(fx_ons == 3 && fx_on_t[1] - fx_on_t[0] == 20 && fx_on_t[2] - fx_on_t[1] == 20);
        int dir1 = fx_on_n[0] < fx_on_n[2];
        fx_note(0, 60, 0, 1100, LIVE); fx_note(0, 64, 0, 1100, LIVE); fx_note(0, 67, 0, 1100, LIVE); fx_ons = 0; fx_now = 1200;
        fx_group_begin(); fx_note(0, 60, 90, 1200, LIVE); fx_note(0, 64, 90, 1200, LIVE); fx_note(0, 67, 90, 1200, LIVE); fx_group_end(1200);
        fx_run(1200, 1300); CHECK((fx_on_n[0] < fx_on_n[2]) != dir1);
        fx_note(0, 60, 0, 1300, LIVE); fx_note(0, 64, 0, 1300, LIVE); fx_note(0, 67, 0, 1300, LIVE);
        fc.strum_ms = 0;
        /* octaver */
        fc.octaver = OCT_BOTH; fx_ons = 0; fx_now = 1400; fx_note(0, 60, 100, 1400, LIVE);
        CHECK(fx_ons == 3 && fx_on_n[0] == 60 && fx_on_n[1] == 72 && fx_on_n[2] == 48);
        fx_note(0, 60, 0, 1401, LIVE); fc.octaver = OCT_OFF;
        /* scale shift: loop notes move 2 scale steps, live notes don't */
        fc.shift = 2; fx_ons = 0;
        fx_note(0, 60, 100, 1500, FXS_LOOP); fx_note(0, 62, 100, 1500, LIVE);
        CHECK(fx_on_n[0] == 64 && fx_on_n[1] == 62);
        fx_note(0, 60, 0, 1501, FXS_LOOP); fx_note(0, 62, 0, 1501, LIVE); fc.shift = 0;
        /* pitch drift stays in key */
        fc.drift = 5;
        for (int i = 0; i < 200; i++) { fx_ons = 0; fx_note(0, 64, 100, 1600, LIVE); CHECK(in_scale(&kt, fx_on_n[0])); fx_note(0, 64, 0, 1600, LIVE); }
        fc.drift = 0;
        /* shadow harmony stays in key */
        for (int hm = 0; hm < HARM_COUNT; hm++) {
            fc.harmony = (uint8_t)hm; fx_ons = 0; fx_note(0, 64, 100, 1650, LIVE);
            for (int i = 0; i < fx_ons && i < 64; i++) CHECK(in_scale(&kt, fx_on_n[i]));
            CHECK(fx_ons == (hm == 0 ? 1 : hm == HARM_TRIAD ? 3 : 2));
            fx_note(0, 64, 0, 1650, LIVE);
        }
        fc.harmony = 0;
        /* density 100% removes loop/arp notes but never live ones */
        fc.thin = 100; fx_ons = 0;
        fx_note(0, 60, 100, 1700, FXS_LOOP); fx_note(0, 62, 100, 1700, FXS_ARP); fx_note(0, 64, 100, 1700, LIVE);
        CHECK(fx_ons == 1 && fx_on_n[0] == 64);
        fx_note(0, 60, 0, 1701, FXS_LOOP); fx_note(0, 62, 0, 1701, FXS_ARP); fx_note(0, 64, 0, 1701, LIVE); fc.thin = 0;
        /* euclidean filter 3 of 8: loop notes pass on hit steps only */
        fc.euclid = 1; int passed = 0;
        for (int st = 0; st < 8; st++) { fx_step16((uint8_t)st, 1800); fx_ons = 0; fx_note(0, 60, 100, 1800, FXS_LOOP); passed += fx_ons; fx_note(0, 60, 0, 1800, FXS_LOOP); }
        CHECK(passed == 3); fc.euclid = 0;
        for (int n = 0; n < 128; n++) CHECK(fx_bal[n] == 0);
        /* trance gate chops a held note on the pattern, releases cleanly mid-gate */
        fc.gate = 1; fx_ons = 0; fx_note(0, 60, 100, 1900, LIVE);
        int chops = 0; for (int st = 0; st < 16; st++) { fx_step16((uint8_t)st, 1900); chops += fx_bal[60] == 0; }
        CHECK(chops == 8);
        fx_step16(1, 1900); fx_note(0, 60, 0, 1900, LIVE); CHECK(fx_bal[60] == 0);
        fc.gate = 0; fx_step16(0, 1900);
        /* stress: everything on, random overlapping notes from all sources -> nothing left sounding */
        fc.echo = 5; fc.strum_ms = 15; fc.octaver = OCT_BOTH; fc.harmony = HARM_TRIAD; fc.drift = 3; fc.ratchet = 2; fc.gate = 8; fc.thin = 30; fc.euclid = 4; fc.shift = 1;
        uint32_t r = 1; int held[8] = { 0 };
        for (uint32_t tt = 3000; tt < 9000; tt += 7) {
            r = r * 1103515245u + 12345u; int k = (int)(r >> 16) % 8;
            int note = 48 + k * 3; uint8_t from = (uint8_t)(k % 3);
            if (held[k]) { fx_note(0, (uint8_t)note, 0, tt, from); held[k] = 0; }
            else { if (k & 1) fx_group_begin(); fx_note(0, (uint8_t)note, 80, tt, from); if (k & 1) fx_group_end(tt); held[k] = 1; }
            if ((tt / 7) % 9 == 0) fx_step16((uint8_t)((tt / 63) & 15), tt);
            fx_run(tt, tt + 6);
        }
        for (int k = 0; k < 8; k++) if (held[k]) fx_note(0, (uint8_t)(48 + k * 3), 0, 9000, (uint8_t)(k % 3));
        fc.gate = 0; fx_step16(0, 9000);
        fx_run(9000, 14000);
        int stuck = 0; for (int n = 0; n < 128; n++) stuck += fx_bal[n] != 0;
        CHECK(stuck == 0);
        fx_note(0, 60, 100, 14000, LIVE); fx_panic(); for (int n = 0; n < 128; n++) CHECK(fx_bal[n] == 0);

        /* multi-channel: the same pitch on two channels is two notes */
        memset(fc.echo ? &fc : &fc, 0, 0); fc.echo = 0; fc.strum_ms = 0; fc.octaver = 0; fc.harmony = 0; fc.drift = 0; fc.ratchet = 0;
        fc.gate = 0; fc.thin = 0; fc.euclid = 0; fc.shift = 0; fx_step16(0, 15000); memset(chbal, 0, sizeof chbal);
        fx_note(0, 60, 100, 15000, FXS_LOOP); fx_note(1, 60, 100, 15000, LIVE);           /* loop ch1 + live ch2 */
        fx_note(0, 60, 0, 15010, FXS_LOOP); CHECK(chbal[0][60] == 0 && chbal[1][60] == 1);   /* loop's off reaches ch1 */
        fx_note(1, 60, 0, 15020, LIVE); CHECK(ch_sounding() == 0);
        fx_note(0, 64, 100, 15030, LIVE); fx_note(3, 64, 100, 15030, LIVE);                 /* hold ch1, switch, play ch4 */
        fx_note(0, 64, 0, 15040, LIVE); CHECK(chbal[0][64] == 0 && chbal[3][64] == 1);
        fx_note(3, 64, 0, 15050, LIVE); CHECK(ch_sounding() == 0);
        /* same channel, live and loop on one pitch: each off releases its own source */
        fx_note(2, 67, 100, 15060, FXS_LOOP); fx_note(2, 67, 100, 15060, LIVE);
        fx_note(2, 67, 0, 15070, LIVE); CHECK(chbal[2][67] == 1);                            /* loop still holds it */
        fx_note(2, 67, 0, 15080, FXS_LOOP); CHECK(ch_sounding() == 0);
    }

    /* the reported scenario: overdub channel 1, then 2, then 3 with the same chords over a playing loop */
    {
        theory_cfg_t kt = { .root = 0, .scale = SC_MINOR };
        fx_cfg_t fc = { .theory = &kt, .echo_ms = 90, .ratchet_ms = 25, .out = fxout, .octaver = OCT_UP1, .strum_ms = 8, .echo = 2 };
        fx_init(&fc); memset(chbal, 0, sizeof chbal);
        looper_t *l = &LP; looper_init(l, lfx, NULL); looper_set_length(l, 384);
        uint8_t chord[4]; int nc = scale_chord(SC_MINOR, 0, 0, F_7, 0, chord, NULL);
        uint32_t now = 20000; int p = 0;
        looper_play(l, true);
        for (int ch = 0; ch < 3; ch++) {
            looper_record(l, true);
            for (int lap = 0; lap < 2; lap++) for (int pos = 0; pos < 384; pos++, now++) {
                if (pos == 0) p = 0;
                bool press = lap == 0 && (pos == 40 || pos == 200), release = lap == 0 && (pos == 120 || pos == 300);
                if (press || release) {
                    if (press) fx_group_begin();
                    for (int i = 0; i < nc; i++) { fx_note((uint8_t)ch, chord[i], press ? 100 : 0, now, LIVE); looper_input(l, (uint8_t)ch, chord[i], press ? 100 : 0); }
                    if (press) fx_group_end(now);
                }
                lfx_now = now; looper_advance(l, (uint16_t)pos); fx_poll(now);
                if (pos == 100 && lap == 0) fx_step16((uint8_t)(p++ & 15), now);
            }
            looper_record(l, false);
        }
        for (int lap = 0; lap < 3; lap++) for (int pos = 0; pos < 384; pos++, now++) { lfx_now = now; looper_advance(l, (uint16_t)pos); fx_poll(now); }
        looper_play(l, false);
        for (int i = 0; i < 400; i++, now++) fx_poll(now);                                  /* let echoes finish */
        int s = ch_sounding(); CHECK(s == 0);
        if (s) for (int c = 0; c < 16; c++) for (int n = 0; n < 128; n++) if (chbal[c][n]) printf("  stuck ch%d note %d\n", c + 1, n);
    }
    printf(fails ? "%d FAILURES\n" : "all core tests passed\n", fails);
    return fails != 0;
}

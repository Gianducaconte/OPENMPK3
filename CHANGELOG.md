# Changelog

## v0.8 (2026-10-08)
- **Releases:** prebuilt firmware files contain only OPENMPK3's code. `tools/mpk_flash.py` adds Akai's bootloader
  from `tools/get_stock_firmware.py`, so no compiler is needed and no Akai code is redistributed.
- **Fix: stuck notes when overdubbing several MIDI channels.** The effects stage counted sounding notes per pitch
  instead of per (channel, pitch). When the loop played a C on channel 1 while you played a C on channel 2, the
  channel 1 note-off was swallowed and the note hung. Each channel now has its own count, and a note-off releases
  the note it belongs to (same channel, same origin: live, arp or loop).
- **Channel switching without the looper:** changing channel (K5 or the menu) recentres pitch bend and the mod wheel
  on the old channel and moves a held sustain pedal to the new one.
- **Fix: stuck notes after Generate.** On 8- and 16-bar loops Generate copied the composed bars several times
  onto the same positions (180 duplicate events). That filled the loop memory, and a note-on could be copied without
  its note-off.
  - Generate now writes whole blocks once, and composes sparser material when a long loop wouldn't fit.
  - The looper releases its oldest sounding note instead of losing track of one when it's tracking 32 at once.
  - MIDI output goes through a queue that never drops note-offs during a burst.
  - Stopping the loop sends All Notes Off on every channel the loop uses.
  - **OCT- + OCT+** together is now a panic on all 16 channels.
- **9 new scales:** Mixolydian b6, Harmonic major, Dorian b2, Ukrainian Dorian, Minor/Major hexatonic, Bebop minor,
  Lydian augmented, Whole tone.
- **50 new styles** (103 in total). New moods: Horror, Spy Movie, Noir, Sci-Fi, Dream, Ethereal, Heroic, Fantasy,
  Fairytale, Post-Rock, Shoegaze, Math Rock, Punk, Reggae, Country, Tropical, Hyperpop, 8-Bit Game, Lullaby,
  Romantic, R&B, Soul 70s, Funk, Latin, Tango, Chill Hop, Afro House, UK Garage, Drum & Bass, Dubstep, Klezmer,
  Balkan, Bollywood, China. New variations: Pop 4, Sad 4, Jazz 5-6, Bossa 2, Neo Soul 3, Gospel 2, Lo-Fi 3,
  City Pop 2, Vaporwave 3, Synthwave 4, Techno 3, House 2, Cinematic 4, Celtic 2, Ballad.
- **Voicing presets:** every style, and some of the scales, loads its own voicing; K3 still changes it. Patterns
  remember the voicing. "Dark Thriller" is renamed "Thriller".

## v0.7 (2026-10-07, flashed on the unit)
- Knobs are now performance controls. **K1** key, **K2** scale/style, **K3** voicing, **K4** octaver,
  **K5** MIDI channel, **K6** shadow harmony, **K7** loop length, **K8** tempo.
- The SHIFT layer holds the effects: pitch drift, scale shift (loop), spiral echo, strum, trance gate, ratchet,
  stutter (loop slice) and half/double time (loop).
- **PROG SELECT + key** sets the arp from the labels printed above the keys (rate, mode plus alternative modes, latch,
  arp octave, swing). **ARP** toggles on press.
- Generative tools: **SHIFT + ARP** generates, **SHIFT + TAP** mutates, **SHIFT + FULL LEVEL** toggles accent,
  **SHIFT + NOTE REPEAT** toggles evolve.
- Looper:
  - **SHIFT + REC** clears the current MIDI channel.
  - **SHIFT + REC + pad** empties that pattern.
  - **SHIFT + PLAY** undoes the last part.
  - Repeating any of these right away restores what it removed, using a hidden undo slot in flash.
- Each part keeps its MIDI channel, so several instruments can share one loop.
- Clock out on TE1 pin 2 (PA14): 1, 2, 4 or 24 PPQN, 5 ms pulses.
- Fixes:
  - the OCT-/OCT+ LEDs were swapped;
  - the PLAY LED now blinks on the beat;
  - the screen no longer draws labels over the progress bar;
  - the idle screensaver is gone.

## v0.6
- Pads and keys are always in tune: every style chord is built from its own scale, and tests enforce this.
- Smooth voicing engine; Spread key map; scale list trimmed to 26.
- Patterns P1-P8 are independent, auto-save, and switch at the loop boundary. Copy-to was added.
- Cleaner display: two text sizes, a one-item menu, `PATTERN n`, and the loop length under the PLAY/REC badge.
- The looper locks to the external bar grid and handles MIDI Start, Stop and SPP. Recording can be quantized.

## v0.5
- One list on K2: scales and 53 chord styles. Kaossilator-style looper. Armed recording.

## v0.4
- Analog clock in on the TE1 debug header (PA13) and a spare-pin probe.

## v0.3
- First fully working firmware on the hardware: scale/chord modes, arp, OLED UI, low-latency keys, musical velocity.

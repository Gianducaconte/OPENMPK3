# Changelog

## v0.9 (2026-11-26)
Looper fixes for recording, overdubbing, undo and clear, mostly with several MIDI channels.
- **Clearing or undoing one channel no longer silences the others.** Undo and clear used to copy the whole loop
  to flash first. That froze the device for about 0.15 s, cut every note the loop was playing on all channels (long
  chords stayed silent until they came round again, so they sounded deleted) and turned recording off. Undo and
  clear are now instant and touch only what they remove. Repeating the shortcut still brings it back.
- **Each channel is its own part.** Changing MIDI channel while recording starts a new part, so undo removes
  only the last channel's part. Before, a whole recording pass across several channels was undone at once.
- **Fix: stuck notes when the loop plays the same note twice at the same moment.** This happens when you overdub
  the same chord with quantize on, use two pads that share a note, or use Generate. The strum stage turned the note
  on twice but off only once, so it hung until Stop. This was the main source of stuck notes while overdubbing.
- **Fix: notes that never stopped.** All of these could store a note-on without a note-off, or play a note
  without ever ending it:
  - a very short note: a quick tap, or the arp with a short gate at a fast rate
  - running out of loop memory
  - holding more than 16 notes while recording
  - shortening the loop with K7 while notes lay past the new end
  - Mutate/Evolve on a pattern where two parts had the same note on the same channel, followed by undo
- **A note held all the way round the loop** now sustains for the whole loop. Before, it was cut to whatever was
  left past one loop (or became a short blip if released on exactly the same spot).
- **Fix: queued pattern switch.** Changes made after queueing a switch (SHIFT + pad while playing) are now saved
  before the switch, instead of being lost. Repeating a clear/undo shortcut after the switch no longer restores
  into the wrong pattern.
- **Fix: jumps in the loop position.** A MIDI Start, Song Position Pointer or TE1 reset while the loop was running,
  or a longer loop length on K7, could fire many loop notes at once or leave one hanging. The loop now jumps cleanly.
- **Sync:** after MIDI Start the loop is exactly on Ableton's first clock (it was one 1/24-beat tick early). MIDI
  Continue resumes from the Song Position Pointer, also at half/double speed. A Song Position Pointer that matches
  where the loop already is (a DAW loop wrapping, a reset pulse every bar) leaves it alone. One sent while Ableton
  is stopped no longer moves a loop that's playing on its own clock.
- Turning the arp on/off or changing a menu setting no longer cuts the loop's notes.
- `LOOP FULL` shows when the pattern memory is full and new notes aren't being recorded.
- **Styles are grouped by family** (Pop, Ballads and sad, Soul, Lo-Fi, Jazz and Latin, Electronic, Rock, Cinematic,
  World), with variants side by side: Jazz 1-6, Synthwave 1-4, and so on. City Pop, Gospel, Bossa, House and Celtic
  are now "... 1" next to their "... 2". The K2 popup shows the style number (001-103) top right. The numbered list
  is in the manual.

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

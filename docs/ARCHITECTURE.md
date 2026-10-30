# OPENMPK3 architecture

## Hardware

- **MCU:** STM32F070RB (Cortex-M0, 48 MHz from an 8 MHz HSE, 128 KiB flash, 16 KiB RAM).
- **USB:** full speed. The firmware enumerates as "OPENMPK3" with the Akai VID/PID and bcdDevice 3.00.

Full pin map: [hardware-map.md](hardware-map.md).

| Block | Hardware | Driver (`firmware/src/`) |
|---|---|---|
| Keybed + buttons | 10 x 8 diode matrix (PC0-7, PB8/9 drive; PC8-15 sense), 2 contacts per key | `board.c` matrix scan at 50 kHz, `inputs.c` decode |
| Pads, knobs, joystick | 3x 74HC4051 (PB0-2) into ADC ch0-4, DMA | `board.c` ADC/mux, `inputs.c` (endless dual-wiper knobs) |
| LEDs | 3x 74HC595 on SPI1 (PB3/5, latch PB4, enable PB7), 32-level PWM | `board.c` |
| OLED | SSD1306 128x32 on SPI2 (PB10/12/13/14/15), DMA | `board.c`, `gfx.c`, `ui.c` |
| Pedal | PA5 | `inputs.c` |
| TE1 clock | PA13 clock in, PA14 reset in / clock out | `board.c`, `tempo.c` |

## Software layers

```
inputs (keys, pads, knobs, buttons) ──► app.c ──► voices ──► live_note ─┬─► looper (records dry notes)
                                          │                    │         └─► fx stage ──► USB MIDI out
                                          │                    └─► arp ──► live_note (source = arp)
                                          └─► ui.c (OLED), LEDs
looper playback ──► fx stage (source = loop) ──► USB MIDI out
tempo.c (internal / USB clock / TE1 clock, 24 PPQN) drives arp, looper position, fx grid, clock out
```

- **`core/`** is hardware-independent and covered by host tests (`make test`):
  - `theory.c`: scales, spread key map, voicing
  - `packs.c`: 53 diatonic chord styles
  - `arp.c`
  - `looper.c`: events in 96 PPQN, overdub layers, per-channel clear
  - `fx.c`: effects stage with per-source output tracking, refcounted outputs and a stuck-note stress test
  - `gen.c`: generate and mutate
- **`app.c`** connects inputs, harmony, looper, arp, patterns, effects and the menu.
- **Loop position** is an accumulator of 96 PPQN units, advanced 2, 4 or 8 per clock tick (half, normal, double
  time). Stutter remaps it onto a slice. MIDI Start, SPP and the TE1 reset re-anchor it, so the loop stays on the
  external bar grid.

## Flash map

| Range | Use |
|---|---|
| 0x08000000-0x08003FFF | Akai bootloader (never touched) |
| 0x08004000-0x08007FFF | Staging copy of the bootloader (must equal stock in every image) |
| 0x08008000-0x08014FFF | OPENMPK3 application (52 KiB max; ~46 KiB used) |
| 0x08015000-0x0801CFFF | Patterns P1-P8, 4 KiB each (header written last, checksummed) |
| 0x0801D000-0x0801DFFF | Hidden undo slot (repeat-to-undo of clear / undo) |
| 0x0801E800-0x0801EFFF | Settings |
| 0x0801F800-0x0801FFFF | Akai settings page + image tail (VID/PID/version/checksum). The bootloader checks the version bytes |

`flash.c` refuses any erase or program outside 0x08015000-0x0801F7FF.

## RAM

About 14 KiB of 16 KiB is used: the vector table copy at 0x20000000, the looper with 600 events, effect queues,
the OLED framebuffer and a 1.5 KiB stack. The app remaps its vectors to SRAM (SYSCFG MEM_MODE) and sets its own stack
pointer at reset, because the bootloader doesn't reload MSP.

## Timing

- **TIM3 at 50 kHz:** matrix step, plus the LED PWM and ADC/mux steps every 100 µs.
- **SysTick at 1 kHz:** `g_ms`.
- **Main loop:** worst case about 1.5 ms. Key contact to USB measured under 0.12 ms.

## Debug SysEx (manufacturer ID 0x7D, `F0 7D 4F cmd ... F7`)

| cmd | Function |
|---|---|
| 0x01 | Stats: main-loop worst case, tempo, source, clock-in settings, loop event count |
| 0x02 n | Per-key contact timing stream on/off |
| 0x20 | Probe spare pins (pull-up / pull-down test) |
| 0x7F | Factory reset of settings |

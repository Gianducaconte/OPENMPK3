> Reverse-engineered from the stock v1.26 firmware (read-only static analysis). Corrections found on real hardware:
> OCT-/OCT+ buttons and their LEDs are swapped versus this map (button idx 6 = OCT+, 7 = OCT-; LED 18 = OCT+, 19 = OCT-),
> joystick X (PA3) is only valid at mux position 0, and the even matrix line is confirmed as the first key contact.

# MPK mini mk3 (PID 0x0049, STM32F070RB) — hardware map from stock fw v1.26

Source image: Akai's official v1.26 image `STM.bin` (`python3 tools/get_stock_firmware.py`), base 0x08000000.
Method: static RE only (capstone recursive-descent + literal-pool resolution). No device touched.
Tools used: a capstone-based recursive-descent disassembler that annotates literal-pool values, and a decompressor
for the RW-data init image (not included in this repository).

Confidence marks: **[H]** read directly from code/registers, **[M]** strong inference, **[L]** guess / needs HW check.
Function addresses are the evidence (Thumb, no +1).

Net names come from public notes (MPKill8) plus what the firmware does. Every pin assignment here comes from the code.

---------------------------------------------------------------------------------------------------

## 0. Pin map, at a glance

| Pin | Mode (stock init) | Function | Evidence | Conf |
|---|---|---|---|---|
| PA0 | analog | ADC_IN0, VR-OUT1, encoder phase A (via 74HC4051 #1) | 0800f3b4, 0800f5d8 | H |
| PA1 | analog | ADC_IN1, VR-OUT2, encoder phase B (via 74HC4051 #2) | same | H |
| PA2 | analog | ADC_IN2, pad FSR mux output (8 pads via 74HC4051 #3) | same + 0800e840 | H |
| PA3 | analog | ADC_IN3, joystick axis 0 (horizontal/X, pitch-bend default) | 0801261c | H (X/Y: M) |
| PA4 | analog | ADC_IN4, joystick axis 1 (vertical/Y) | 0801261c | H (X/Y: M) |
| PA5 | input, pull-up | Sustain pedal, active low (CC64) | 0800f154, 08009f5c | H |
| PA11/PA12 | USB | USB FS DM/DP | — | H |
| PA13/PA14 | SWD | SWDIO/SWCLK (not reconfigured) | — | H |
| PB0 | out PP | 4051 ADDR0 (S0) for all three muxes | 0800f800 | H |
| PB1 | out PP | 4051 ADDR1 (S1) | 0800f800 | H |
| PB2 | out PP | 4051 ADDR2 (S2) | 0800f800 | H |
| PB3 | AF0 | SPI1_SCK, 74HC595 SRCLK (LED_CLK) | 0800f990 | H |
| PB4 | out PP, PU | 74HC595 RCLK latch (LED_RC). Low while shifting, rising edge after DMA | 0800b88c | H |
| PB5 | AF0 | SPI1_MOSI, 74HC595 SER (LED_DATA) | 0800f990 | H |
| PB7 | out PP, PU | LED power enable (PW_EN), driven high 1 ms after LED init | 0800f990, 0800b838 | M |
| PB8 | out PP | Matrix drive line 8: buttons PROG SEL, PROG CH, CC, BANK, ARP, TAP, OCT-, OCT+ | 080082d8, 0800f2ae | H |
| PB9 | out PP | Matrix drive line 9: buttons FULL LEVEL, NOTE REPEAT | same | H |
| PB10 | out PP, PU | OLED RST (active low) | 08010baa | H |
| PB12 | out PP, PU | OLED CS (active low) | 08010b60 | H |
| PB13 | AF0 | SPI2_SCK, OLED CLK | 0800f1aa | H |
| PB14 | out PP, PU | OLED D/C (0 = command, 1 = data) | 08010b60, 08010cea | H |
| PB15 | AF0 | SPI2_MOSI, OLED DATA | 0800f1aa | H |
| PC0..PC7 | out PP | Keybed matrix drive lines 0..7, active low | 080082d8, 0800f2ae | H |
| PC8..PC15 | input, no pull in app (bootloader turns on internal PU) | Matrix sense lines, active low (external pull-ups presumed) | same, boot 08003408 | H |
| PF0/PF1 | OSC | 8 MHz HSE crystal | 08011396 | H |
| unused | — | PA6-PA10, PA15, PB6, PB11, PD2: the app never configures them | — | M |

Correction to PLAN.md: the sustain pedal is on **PA5**, not PD2.

---------------------------------------------------------------------------------------------------

## 1. Clocks, SysTick, timers, IRQs

### Reset path [H]
- `Reset_Handler` 08015384: loads MSP=0x20003AD8. If `(*(u32*)0x4)>>24 == 0x1F`, the part booted with system memory
  aliased at 0. In that case it sets RCC_APB2ENR=1 (SYSCFG clock) and SYSCFG_CFGR1=0 (MEM_MODE = main flash).
  Then it calls `SystemInit` 080112ac and Keil `__main` 080154c4. `__main` runs scatter-load 08015430 and then `main` 08014ba8.
- Scatter-load: 1) zero-init 0x20000220..0x200032D7 (len 0x30B8, routine 080153c0);
  2) LZ-decompress 0xEA bytes at 0x080158AE into RW 0x20000100..0x2000021F (routine 0800a47e; `rwinit.py` reproduces it).

### System clock [H] (`SetSysClock` 08011396)
- HSEON, wait HSERDY (timeout 0x5000). If HSE fails, the code just returns and stays on HSI 8 MHz (no PLL).
- FLASH_ACR = 0x11 (prefetch on, 1 wait state).
- RCC_CFGR: HPRE=/1, PPRE=/1, PLLSRC=HSE/PREDIV(/1), PLLMUL=×6 (CFGR |= 0x00110000). **SYSCLK = HCLK = PCLK = 48 MHz.**
- SystemCoreClock variable at 0x20000144 = 48 000 000 (from the RW init).
- **USB clock** (08014214): RCC_APB1ENR.USBEN, HSE on, PLL on, RCC_CFGR3.USBSW=1 → USB clocked from PLL 48 MHz (crystal-based, no CRS).
- ADC clock: CKMODE left at default, so it runs on the asynchronous HSI14 (14 MHz), which the ADC requests itself. [M]

### SysTick [H] (0800bbac / 0800bbde)
- LOAD = SystemCoreClock/50000 − 1 = 959 → **20 µs tick (50 kHz)**. CTRL=7. Priority set to 0, the highest (0800bb20).
- Handler 0800bc0a: increments the 64-bit tick counter at 0x20003210 (`now()` = 0800bc40, `elapsed(t)` = 0800bc46).
  Once main sets 0x200032b0=1:
  - **every tick:** keybed matrix scan step `080082d8`
  - **every 5th tick (100 µs):** LED 74HC595 PWM refresh step `0800b88c`
- All firmware timeouts count in 20 µs ticks: 501 = 10 ms, 1001 = 20 ms, 12501 = 250 ms, 50000 = 1 s, 100001 = 2 s, 250001 = 5 s.

### TIM3 [H] (init 0800f2ae, IRQ 0800f368)
- PSC=0, ARR=4799, up-counting, update IRQ → **10 kHz (100 µs)**. NVIC: TIM3 IRQ16, priority 2.
- The ISR runs, in order: `0800f5d8` ADC/DMA block and mux advance → `08010cea` OLED DMA state machine →
  `0800a6ae` USB-MIDI RX parse and TX flush → `08009380` MIDI clock/arp engine → `0800e840` one pad per tick
  (skipped in test mode and in the snake game).

### Other IRQs [H]
- USB IRQ31 → 08014d86 → 080146ac (ST USBD-style stack, handle at 0x20002480), priority 1 (0801424a).
- Every other vector is a `b .` stub (WWDG, RTC, FLASH, RCC, EXTI*, DMA*, ADC, TIM1, TIM14-17, SPI, USART…). There is no EXTI,
  no watchdog, and no DMA interrupts; DMA completion is polled.
- `08011648` (TIM_TimeBaseInit) contains compares against the TIM1/6/15/16/17 base addresses. That is library code; only TIM3 is used.

---------------------------------------------------------------------------------------------------

## 2. GPIO and peripherals in detail

GPIO helpers (StdPeriph): `GPIO_Init` 0800a318 (struct {u32 pins; u8 mode; u8 speed; u8 otype; u8 pupd}),
`ReadInputData` 0800a400, `SetBits`(BSRR) 0800a404, `ResetBits`(BRR) 0800a40c, `Write`(ODR) 0800a410, `PinAFConfig` 0800a414,
`ReadInputDataBit` 0800a3e4. RCC: AHBENR 080114c4, APB2ENR 080114e8, APB1ENR 08011508.
Init order in main: `0800fb70` settings load → `0800f154` HW init (`0800f3b4` ADC, `0800f990` LED SPI1, `0800f2ae` matrix+TIM3,
`0800f1aa` OLED SPI2, PA5) → `0800b838` LED init → `08010baa` OLED init.

### 2.1 Keybed + button matrix [H] (scan 080082d8, init 0800f2ae / 080080c0, decode 08008428)
- 10 drive lines, active low: idx 0..7 = PC0..PC7, idx 8 = PB8, idx 9 = PB9. 8 sense lines: PC8..PC15 (IDR>>8), active low.
- Scan step, every 20 µs:
  1. Read PC[15:8].
  2. XOR with the last state for the current line (table 0x2000316c[10]). If anything changed, push
     {line, newbits, changedbits, ts64} into a 32-entry ring at 0x20001d00 (16 B each, write index 0x20003296).
  3. Drive every line high (ODR=0x00FF, PB8=PB9=1).
  4. line = (line+1) % 10.
  5. Pull that line low (lines 0..7: ODR = ~(1<<line); 8: PB8 low; 9: PB9 low).
     The sense port is read on the next tick, giving 20 µs of settle time.
- Full matrix period = 10 × 20 µs = **200 µs**. Events are timestamped at 20 µs resolution.
- **Keys (25):** drive lines 0..7 come in pairs (0,1), (2,3), (4,5), (6,7).
  - With k = (line>>1)*8 + bit, **key index = 27 − k**. Key index 0 is the leftmost key (the ARP-shift code uses keys 0..7 for
    time division, matching the panel). k = 3..27 are used; k = 0..2 and 28..31 are empty.
  - Inverse: k = 27 − key; drive pair = k>>3; sense bit = k&7 (PC8+bit).
  - Example: key 0 = PC6/PC7 × PC11; key 24 = PC0/PC1 × PC11.
  - **Even line = first contact** (closes first on the way down). Closing only stores timestamp t1 in key state +8
    (key state table 0x200016c0, 32 B per key).
  - **Odd line = second contact.** When it closes (and the key is idle): dt = t2 − t1 in ticks → velocity → note-on.
  - Release: opening the odd contact only stores a timestamp at +0x10. **Opening the even contact sends the note-off.**
- **Key velocity** (08008428, parameters loaded in 080080c0 from the settings page):
  - T = {T0,T1,T2,T3} = {1200, 300, 200, 120} ticks (24 ms, 6 ms, 4 ms, 2.4 ms). Settings bytes 0x2C..0x33, u16 LE.
  - V = {120, 64, 32, 0} (RW constants at 0x20000140).
  - For keys whose bit is set in mask 0x00544AA5 (const at 0x08015880; keys 0,2,5,7,9,11,14,18,20,22), every T is first
    multiplied by S/10, where S = settings byte 0x34 = 7 (0.7×). The purpose of this subset is unknown.
  - v = V0 if dt > T0
  - v = V1 + (dt−T1)(V0−V1)/(T0−T1) if dt > T1
  - v = V2 + (dt−T2)(V1−V2)/(T1−T2) if dt > T2
  - v = V3 + (dt−T3)(V2−V3)/(T2−T3) if dt > T3; if that gives 0, v = 1
  - v = 1 otherwise
  - **MIDI velocity = 128 − v**, so the range is 8..127.
- **Buttons:** drive lines 8/9, button idx = (line−8)*8 + bit. State table 0x200025e8, 16 B each. Polled one button per main-loop pass:
  - press: state 1
  - ≥10 ms held → "press" (0800c7e0)
  - ≥2 s → "long" (0800c8d0)
  - ≥5 s → "very long" (0800c926)
  - release → 0800c93e

  | idx | drive × sense | Button | Press handler effect | LED |
  |---|---|---|---|---|
  | 0 | PB8 × PC8 | **PROG SELECT** (shift; "To load Program / Press a pad") | 0800c7e0 case 0 | 4 |
  | 1 | PB8 × PC9 | **PROG CHANGE** (pad mode 1 → Program Change 0xC0) | 0800efdc(1) | 3 |
  | 2 | PB8 × PC10 | **CC** (pad mode 2 → CC 0xB0) | 0800efdc(2) | 2 |
  | 3 | PB8 × PC11 | **BANK A/B** toggle (0x200032bf) | 0800eefc | 0 = A, 1 = B |
  | 4 | PB8 × PC12 | **ARP** (hold = arp edit via keys/knobs; tap = on/off) | 0800df9c / 08009e54 | 16 |
  | 5 | PB8 × PC13 | **TAP TEMPO** (250 ms..1 s window) | 08009c38 | 17 (flashes on beat) |
  | 6 | PB8 × PC14 | **OCT −** (OCT− + OCT+ together = reset) | 08009a84 / 08009b70 | 18 |
  | 7 | PB8 × PC15 | **OCT +** | 08009afa | 19 |
  | 8 | PB9 × PC8 | **FULL LEVEL** (toggles on release; 2 s hold with joystick-Y = 0 starts the "Snake Game" easter egg) | 0800ef76 / 0800ca2a | 20 |
  | 9 | PB9 × PC9 | **NOTE REPEAT** (hold = rate screen; toggle on release) | 0800e134 / 0800efa4 | 21 |
  | 10..15 | PB9 × PC10..15 | unused | — | — |

- **Boot-time combos.** The code drives PB8 low with PC0-7 and PB9 high, then reads PC[15:8]:
  - App main 08014ba8: `0xF300` (CC + BANK) → factory test mode (flag 0x200032b1). `0xF500` (PROG CHANGE + BANK) →
    OLED/LED test pattern 0801115c.
  - Bootloader 08003494: `0xF600` (**PROG SELECT + BANK**) → stay in the updater. See §5.
- **Sustain pedal [H]:** PA5 input with pull-up, polled (08009f5c). Low → CC64=127 plus a "SUST" popup; high → CC64=0.
  Polarity is fixed, and the debounce compare is effectively a no-op (it compares the stored timestamp instead of the elapsed time).
  At init (080080c0) the current pedal state is sent once.

### 2.2 ADC / analog mux [H] (0800f3b4 init, 0800f5d8 per-TIM3 processing, getter 0800f7e4)
- ADC: 12-bit, continuous, scan upward, channels 0..4 (PA0..PA4), sample time code 2 (13.5 cycles), calibrated.
  DMA1 Ch1: ADC_DR → 0x20002e98, half-word, 40 transfers, high priority, two 80-byte buffers used ping-pong.
- Each TIM3 tick, once DMA1 TC1 is set:
  1. Stop DMA.
  2. Remember the current mux address.
  3. Advance the mux, addr = (addr+1)&7, on PB0..PB2.
  4. Swap buffers, re-arm 40 transfers, clear OVR, start the ADC.
  5. Process the finished buffer: 8 sequences × 5 channels.
- Per channel: average of the 8 samples (>>3), then hysteresis on the filtered value. Change < 36 → ignored; 36..50 → step ±1;
  ≥ 51 → jump to the new value.
- Analog slots: 0x200029e0 + id*8, struct {u16 out; u16 filt; u16 min; u16 max}.
  - id 0..7 = PA0 at mux 0..7
  - id 8..15 = PA1 at mux 0..7
  - id 16..23 = PA2 at mux 0..7
  - id 24 = PA3 (updated only when mux = 0)
  - id 25 = PA4 (every block)
  - Joystick ids 24/25 are calibrated: out = (filt−min)*4095/(max−min), clamped to 0 / 4095, with min/max auto-expanding by 60 counts.
    min/max/centre come from settings bytes 0x20..0x2B (defaults: max 0xDFF, min 0x19A, centre 0x800).
- Mux timing: one mux position per DMA block (~100 µs) → a full 8-position sweep every ~800 µs.
- **Mux channel ↔ control** (const tables):
  - Knob A ids @0x08015790 = `03 00 01 02 05 07 06 04`; knob B ids @0x08015798 = A + 8.
    K1..K8 → 4051 Y3, Y0, Y1, Y2, Y5, Y7, Y6, Y4. **K8 = Y4 on both phase muxes, ADDR = 100b (PB2=1, PB1=0, PB0=0).**
  - Pad ids @0x08015840 = `13 10 11 12 15 17 16 14` → pad 1..8 = PA2-mux Y3, Y0, Y1, Y2, Y5, Y7, Y6, Y4. Pad n uses LED 8+n−1.
    (Pad 1 = table index 0 is assumed. [M])
- **"Encoders" are endless dual-wiper pots, not digital quadrature** [H] (08011c10; per-knob state 0x20002dd8 + k*0x18).
  There is no 0/1 thresholding of the phases. Each pass reads A = slot(Aid), B = slot(Bid) and compares with the previous A/B:
  - if A ≥ 0xE66 (3686): Δ = (Bprev − B)·180/4095
  - elif A < 0x19A (410): Δ = (B − Bprev)·180/4095
  - elif B ≥ 0x801 (2049): Δ = (A − Aprev)·180/4095
  - else: Δ = (Aprev − A)·180/4095
  - Dead band: if the knob has been idle ≥ 200 ms (10001 ticks), |Δ| must reach 6 and the first step is forced to ±2;
    otherwise |Δ| must reach 2. Then Δ is handed to the mode/CC logic (abs/rel, 20-byte knob records).
  - The best place for a K8-ignore patch is this loop at index 7 (0x200032a9 == 7), i.e. ids 4/12.
- **Pads** (0800e840, one pad per TIM3 tick; state 0x20002300 + pad*0x30):
  - trigger when raw ≥ **0x1C3 (451)**; track the peak
  - note-on once 20 ms (1001 ticks) have passed **or** raw falls below **250**
  - velocity = clamp((peak − 450)·127/3450, 1..127); FULL LEVEL forces 127; then the curve LUT **0x08015304[128]**
    (gentle up to ~0x2A, then steeper)
  - note-off when raw < 250 and ≥ 20 ms after note-on
  - aftertouch: mode at 0x200032c2 (1 = channel pressure 0xD0, 2 = poly 0xA0). Starts 250 ms after note-on, updated every 10 ms,
    same scaling and LUT, sent only on change; AT 0 is sent on release.
- Joystick (0801261c): the axis alternates each call. Deadzone ±300 counts around the centre. Axis mode byte = current program +0x1E
  (axis 0) / +0x21 (axis 1). Mode 0 = pitch bend (0x2000 ± …, 0x800a670), otherwise CC (0x800a5ac).

### 2.3 LEDs — 3 × 74HC595 on SPI1 [H] (0800f990, 0800b88c, 0800b91a)
- SPI1: 1-line TX master, 8-bit, CPOL 0 / CPHA 0, MSB first, soft NSS, prescaler /16 = **3 MHz**. DMA1 Ch3 mem→SPI1_DR, 3 bytes.
- Software PWM with 32 brightness levels:
  - frame table 0x20002fc8 = 32 frames × 3 bytes
  - LED n = byte n>>3, bit n&7; byte 0 is sent first
  - **bit 0 = LED on** (active low); frames 0..b−1 have the bit cleared
- Every 100 µs, alternating: state 1 = PB4 low, start a 3-byte DMA for frame f; state 2 = wait for DMA TC3 and !BSY, PB4 high (latch),
  f = (f+1)&31. That is 200 µs per frame and **6.4 ms per PWM period (156 Hz)**.
- `led_set(n, b)` 0800b91a: b = 0..32 (0x20 = full).
  - Rule: LEDs other than 0, 1 and 8..15 never drop below brightness 4. This produces the dim "off" glow on the function buttons.
  - Init: all 24 off, wait 1 ms, PB7 high.
- **LED index map** [H from button handlers]:

  | idx | LED | | idx | LED |
  |---|---|---|---|---|
  | 0 | BANK A | | 16 | ARP |
  | 1 | BANK B | | 17 | TAP TEMPO |
  | 2 | CC | | 18 | OCT − |
  | 3 | PROG CHANGE | | 19 | OCT + |
  | 4 | PROG SELECT | | 20 | FULL LEVEL |
  | 5..7 | not used by UI [M] | | 21 | NOTE REPEAT |
  | 8..15 | Pads 1..8 | | 22, 23 | not used [M] |

- Which physical 595 holds byte 0 is not determinable from software. Byte 0 is shifted first, so it ends up in the last chip of the chain.

### 2.4 OLED — SPI2 [H] (0800f1aa, 08010baa, 08010b60, 08010cea)
- SPI2: 1-line TX master, 8-bit, mode 0, MSB first, prescaler /8 = **6 MHz**. DMA1 Ch5 mem→SPI2_DR.
- Command byte write (08010b60): PB14 low (cmd), PB12 low (CS), wait TXE, write DR, wait !BSY, PB12 high.
- Reset: PB10 low, wait 101 ticks (~2 ms), PB10 high, wait ~2 ms.
- See §3 for the controller and init sequence.

---------------------------------------------------------------------------------------------------

## 3. OLED controller, framebuffer, fonts

- **Controller: SSD1306-compatible [H]** (command set and charge-pump command 8D). **Framebuffer 512 B = 128×32**
  at 0x20002100 (cleared by 08010e04; DMA length 0x200).
- **Init sequence after reset** (08010baa). Build constant 0x2000021B = 2, so the second value of each pair is the one used:
  ```
  AE            display off
  00 10         column low/high = 0
  40            start line 0
  81 CF         contrast (alt build: 9F)
  A6            normal (not inverted)
  A4            resume from RAM
  A8 3F         multiplex 64   (!)
  D3 00         display offset 0
  D5 80         clock div
  D9 F1         pre-charge (alt: 22)
  DA 02         COM pins: sequential, no L/R remap
  DB 20         VCOMH
  20 00         horizontal addressing mode
  A1            segment remap (col 127 = SEG0)
  C8            COM scan reversed
  8D 14         charge pump ON (alt: 8D 10 = external VCC)
  AF            display on
  ```
- **Refresh state machine** (TIM3, 08010cea):
  - State 1: if 2 s have passed since the last preamble → state 2; else if 10 ms since the last frame → state 3.
  - State 2: DC low, CS low, DMA 5 bytes from 0x08015838 = `D3 00 00 10 40`.
  - State 3: DC high, CS low, DMA the 512-byte framebuffer.
  - State 4: wait TC5 and !BSY, CS high → state 1.
  - Result: at most **100 fps**, with the offset/start-line preamble re-sent every 2 s.
- [M] The stock code never sets a page/column window (no 21/22 commands) and only ever sends 512 B in horizontal mode, with MUX 64.
  The RAM pointer therefore alternates between pages 0-3 and 4-7 on successive frames, and the panel is most likely a 128×32 glass.
  **For new firmware: copy the init exactly, and either send the 512 B buffer twice (1024 B) per refresh or check on HW
  which page half is visible before setting a 21/22 window.**
- Framebuffer layout: SSD1306 page format, byte = FB[page*128 + x], bit0 = top row of the page.
- **Fonts / bitmaps (app)** [H]. Descriptor {u8 w, u8 h, u8 first=0x20, u8 last=0x7E, u32 ptr}:
  - desc @0x08015770: **5×8 font** @0x080150A0 (5 B/char, 95 chars, column bytes)
  - desc @0x08015778: **8×8 font** @0x080148B0 (8 B/char)
  - desc @0x08015780: **12×16 font** @0x08013034 (24 B/char: 12 columns × 2 pages)
  - **128×32 logo bitmap** @0x08014E9C (512 B; drawn by 080110e8(x, y, ptr, invert), used at boot/idle 0800e284 and in 0801115c)
  - UI strings @0x080154E0..0x08015848. The bootloader has its own 5×7 font (~0x2F64).

---------------------------------------------------------------------------------------------------

## 4. USB

- Descriptors in the app [H]:
  - device desc @0x08015574: bcdUSB 2.00, class 0, **bMaxPacketSize0 = 32**, VID 0x09E8, PID 0x0049, bcdDevice 0x0200,
    iManufacturer 1, iProduct 2, 1 config
  - config desc @0x0801527C, 133 B, 3 interfaces, bus-powered 500 mA:
    - IF0 HID, EP **0x81** IN interrupt 32 B, EP **0x01** OUT interrupt 32 B, interval 1
    - IF1 Audio Control (bcdADC 1.00)
    - IF2 MIDIStreaming, 1 port (embedded IN jack 1 / external IN 2 / embedded OUT 3 / external OUT 4),
      EP **0x02** bulk OUT 32 B, EP **0x83** bulk IN 32 B. The bootloader's copy uses 0x82 instead.
  - HID report desc @0x080153FC (52 B): usage page 0xFFA0, 32-byte input + 32-byte output reports
  - LANGID @0x08015884; strings are built at runtime ("AKAI" @0x08015848, "MPK mini 3" @0x080156DC)
- **MIDI is standard USB-MIDI 1.0** [H]. Senders (0800a518 note, 0800a562/0800a5ac CC, 0800a5dc PC, 0800a60e poly AT, 0800a63e CP,
  0800a670 PB) push 4-byte event packets (CIN|cable0, status, d1, d2; e.g. `09 9n kk vv`) into a 512 B ring at 0x20001f00.
  Every 100 µs (0800a6ba) up to 32 B are sent on EP 0x83. TX-busy flag 0x2000021C, cleared in DataIn (0801017c path); 5 ms timeout.
  OUT EP2 → DataOut callback 08010136 → buffer and length → parsed in 0800a748 (SysEx program dump/load, clock, etc.).
  There is no DIN MIDI (no USART in use).
- **HID in the app accepts exactly one command** [H] (08014e34; OUT buffer 0x20003108, flag 0x200032d4 set in 08010136):
  OUT `55 4F C4 …` → IN `55 49 E4 00…` (32 B). This is an "app alive / are you the app" ping.
  **There is no jump-to-bootloader command over HID** (and no SysEx path found that resets into the bootloader).
  The only software reset (0800ca18, AIRCR=0x05FA0004) is reached from the snake-game exit.
- Bootloader HID protocol (boot 080018dc, shown for completeness):
  - OUT reports start `55 4F <cmd>`; replies start `55 49 <status>`.
  - `C1`: erase 0x08004000..0x0801F7FF → `F1` (or `E2` on error).
  - `C2`: write 16 B.
    - bytes[3..6] = big-endian offset, OR'd with 0x08000000
    - bytes[9..24] = data
    - bytes[30..31] = BE 16-bit sum of the 16 data bytes
    - addresses < 0x08004000 are rejected; ≥ 0x0801F800 go to a RAM page buffer
    - reply `F2` = ok, `E2` = bad checksum or write failure
  - `C3`: finish → `F3`, "Upgrade - Completed", then system reset.
  - `C4`: version query → `F4 <0x1FFF6> <0x1FFF7>`.

---------------------------------------------------------------------------------------------------

## 5. Flash usage, vectors, bootloader handshake

### Flash map [H]
| Range | Use | Evidence |
|---|---|---|
| 0x08000000-0x08003FFF | Akai bootloader (vectors at 0, SP 0x20001118, reset 0x08003671) | — |
| 0x08004000-0x08007FFF | Bootloader **staging copy**. App main copies it over 0x0000 when byte 0x08003FFF ≠ 0x08007FFF and 0x08007FFF ≠ 0xFF (then shows "Upgrade - Completed") | 08014ba8, 08014d8e |
| 0x08008000-~0x08015997 | App vectors, code, rodata, compressed RW init @0x080158AE | — |
| ~0x08015998-0x0801E7FF | free (zero-filled in the updater image) | — |
| 0x0801E800-0x0801EFFF | **Last-selected-program journal**: append-only u16 entries, page erased when full; last non-0xFFFF entry = program index (≥ 8 → 0) | 0800ff24 (write), 0800ff9c (read) |
| 0x0801F000-0x0801F7FF | **Backup copy of the settings page.** Restored when the main page is blank or older (byte 0x7F5 compared) | 0800fb70, 0800fe60 |
| 0x0801F800-0x0801FFFF | **Settings page (2 KB)**, mirrored to RAM 0x20000220[0x800] at boot. Saved by erasing and writing both pages | 0800fb70 / 0800fe60 |

Settings page layout [H/M]:

| Offset | Contents |
|---|---|
| 0x00 | ProductSN (16 B) |
| 0x10 | PCBSN (16 B) |
| 0x20..0x2B | Joystick cal: Xmax, Ymax, Xmin, Ymin, Xctr, Yctr (u16) |
| 0x2C..0x33 | Key velocity T0..T3 |
| 0x34 | Key threshold scale (7) |
| 0x35 + n·0xF5 | **8 programs × 245 B**, same layout as the SysEx program payload (name at +0, knob records at +0x54, 20 B each) |
| 0x7F0 | VID BE 09 E8 |
| 0x7F2 | PID BE 00 49 |
| 0x7F5 | Settings-format version (0x09) |
| 0x7F6/0x7F7 | Firmware version (01 26) |
| 0x7FE | Checksum (BE byte-sum, checked by the updater) |

Option-byte user data (0x1FFFF804/806) holds factory-test status flags (0800fac4/0800faf6, factory parser 0800b138).

### App vector handling [H] (main 08014ba8)
- Copies **0x100 bytes from 0x08008000 to 0x20000000**, enables the SYSCFG clock, and sets **SYSCFG_CFGR1 |= 3 (MEM_MODE = SRAM at 0)**.
  The RAM vector table lives at 0x20000000-0x200000FF, so the app's RW data starts at 0x20000100.
- Interrupts stay disabled until HW init (0800f154 runs `cpsie i` at its end).
- RAM map:

  | Range | Use |
  |---|---|
  | 0x20000000 | Vectors |
  | 0x20000100-0x2000021F | RW |
  | 0x20000220-0x200032D7 | ZI |
  | 0x20000220 | Settings mirror (2 KB) |
  | 0x20001d00 | Key event ring |
  | 0x20001f00 | MIDI TX ring |
  | 0x20002100 | OLED framebuffer |
  | 0x200028e8 | Current program |
  | 0x200029e0 | Analog slots |
  | 0x20002e98 | ADC DMA buffers |
  | 0x20002fc8 | LED PWM frames |
  | ~0x200032D8-0x20003AD8 | Stack (initial MSP 0x20003AD8) |

### Bootloader → app decision [H] (boot main 08003494)
1. Init (clock, SysTick at **100 µs**, matrix GPIO with **internal pull-ups on PC8-15**, OLED), then wait 200 ms.
2. If byte **0x0801FFF6 == 0xFF or 0x0801FFF7 == 0xFF** (no firmware version) → stay in the updater.
3. Drive PC0-7 high, PB9 high, PB8 low; wait 5 ms; read PC[15:8]. If **== 0xF6** (PC8 = PROG SELECT and PC11 = BANK pressed,
   nothing else) → stay in the updater.
4. Otherwise: `cpsid i`; jump to `*(u32*)0x08008004`.
   - **MSP is not loaded** (the app reset handler sets it).
   - **No checksum, CRC or magic check** on the app.
   - SysTick is left running with the bootloader's vectors active, so the app must remap or copy vectors before enabling IRQs.
5. The updater's final step `C3` causes a system reset. On that boot the app installs the staged bootloader if needed.
6. Writes to the settings page: the bootloader always keeps the first 32 B (serials) of the existing page. If the incoming
   page's 0x7F5 byte equals the installed one, it also keeps bytes 0x20..0x7EF (user programs and calibration).
   Otherwise it writes the defaults from the image.

---------------------------------------------------------------------------------------------------

## 6. Implications for a from-scratch firmware (checklist)
- Link the app at 0x08008000 (vectors there). In `Reset_Handler`: set MSP; copy vectors to 0x20000000; set
  SYSCFG MEM_MODE = 3; reserve RAM 0x20000000-0x200000BF (0xC0 B minimum; stock reserves 0x100).
  Reconfigure SysTick before `cpsie i`.
- Image tail **must** carry non-0xFF bytes at 0x0801FFF6/7 (version), plus VID/PID @0x1FFF0, 0x09 @0x1FFF5 and the updater checksum.
  Keep the stock settings page layout, or bump 0x7F5 to force-write your own defaults (serials are kept either way).
  Keep 0x08004000-0x08007FFF = an exact copy of the bootloader, or 0xFF at 0x08007FFF. This avoids any self-copy logic
  (only the stock app has it), and the bootloader erases that range on update anyway.
- Clocks: HSE 8 MHz × 6 = 48 MHz, flash 1 WS, USBSW = PLL.
- Copy the stock timing for a known-good start: 20 µs matrix step, 100 µs LED latch step, 100 µs ADC/mux step.
- Enable the internal pull-ups on PC8-15 (the bootloader does; it is harmless with external pull-ups).

## 7. Open items / not verified
- Physical OLED page window (see §3) [M].
- PB7 polarity and exact role (PW_EN through FET vs 595 /OE) [M].
- Which 74HC595 receives byte 0 [L].
- Joystick axis 0 = horizontal is inferred from program field order [M].
- Purpose of the key-mask 0x00544AA5 subset (scaled velocity thresholds) [L].
- ADC clock source assumed to be HSI14 auto [M].

> Static analysis of the Akai bootloader and updater. Verified on hardware: tools/mpk_flash.py flashes with this protocol
> (stock v1.26 and OPENMPK3 images, many times). Note: OPENMPK3 also moved its own flash data, see ARCHITECTURE.md.

# Akai MPK mini mk3 (STM32F070RB, PID 0x0049): firmware update protocol

This was worked out by static analysis only. Nothing was sent to any device, and the updater apps were never run.

Sources:

- The device image: the updater's `STM.bin` (`python3 tools/get_stock_firmware.py` downloads it from Akai).
- The Thumb disassembly of the bootloader (0x0000-0x3FFF) and of the application (0x8000-).
- The macOS `Updater` binary (x86_64, symbols present: `UpdaterDlg::Connect`, `UpdaterDlg::Update`, `CHIDDevice::*`).

The disassembly was done with a capstone-based recursive-descent disassembler that annotates literal-pool values
(not included in this repository).

All addresses below are absolute (0x08xxxxxx).

---

## 0. Summary

| Item | Finding |
|---|---|
| Transport | **USB HID**, interface 0, vendor usage page 0xFFA0 / usage 0x01. 32-byte reports with no report ID, interrupt IN EP 0x81 and interrupt OUT EP 0x01. Not SysEx. |
| Enumeration in bootloader mode | Same as the application: VID **0x09E8**, PID **0x0049**, bcdDevice 0x0200, manufacturer "AKAI", product "MPK mini 3", no serial number. HID and USB-MIDI interfaces are both present. You cannot tell the two modes apart from the descriptors. |
| How the mode is told apart | Send `55 4F C4 ...`. The bootloader replies `55 49 F4 vH vL`. The application replies `55 49 E4 00 ...`. |
| Commands | C1 = erase, C2 = write 16 bytes, C3 = finish/reboot, C4 = get version (C0 is ignored). |
| Payload per packet | 16 bytes, with a big-endian absolute offset and a 16-bit additive checksum. 0x20000 bytes / 16 = **8192 packets**. The XML's `blockCount=64` is only used for the progress bar. |
| Protection of 0x0000-0x3FFF | **Yes.** C2 writes below 0x08004000 are acknowledged (F2) but **not written**, and C1 erases only 0x08004000-0x0801F7FF. |
| 0x4000-0x7FFF | **This region is erased and rewritten by the host.** It holds the staged copy of the bootloader. The **application** (not the bootloader) copies it over 0x0000-0x3FFF at boot if byte 0x08003FFF != byte 0x08007FFF and 0x08007FFF != 0xFF. **This is the main brick risk.** |
| When the bootloader stays in update mode | Byte 0x0801FFF6 == 0xFF or byte 0x0801FFF7 == 0xFF (the version bytes are erased), **or** BANK + PROG SELECT are held at power-up. The checksum at 0x1FFFE and the VID/PID tail are **not** checked by the device. |
| Software command to enter the bootloader | **None in the stock application** (HID handles only C4 → E4). The only reset in the application is the snake-game exit. |

---

## 1. Transport and USB identity

### 1.1 Bootloader USB descriptors (in 0x0000-0x3FFF)

**Device descriptor @0x08003824**
```
12 01 00 02 00 00 00 20 E8 09 49 00 00 02 01 02 00 01
```
- USB 2.0, EP0 packet size 32
- VID 0x09E8, PID 0x0049, bcdDevice 0x0200
- iManufacturer = 1, iProduct = 2, iSerialNumber = **0**, one configuration

The application's device descriptor @0x08015574 is **byte-identical**.

**String descriptors.** These are the ST USBD getters at 0x08001538-0x08001598:

| Index | String | Source |
|---|---|---|
| Product | "MPK mini 3" | @0x08003848 |
| Manufacturer | "AKAI" | @0x08003874 |
| Configuration | "Device Configuration" | |
| Interface | "Device Interface" | |
| Serial (built from the UID at 0x1FFFF7AC) | not exposed | iSerialNumber = 0 |

**Configuration descriptor @0x080035E8.** wTotalLength 0x85, 3 interfaces, bus-powered, 500 mA.

- **IF0, HID** (class 3, subclass 0, protocol 0):
  - HID descriptor: bcdHID 1.11, report descriptor length 0x34
  - `07 05 81 03 20 00 01`: EP 0x81, interrupt IN, 32 bytes, interval 1 ms
  - `07 05 01 03 20 00 01`: EP 0x01, interrupt OUT, 32 bytes, interval 1 ms
- **IF1** Audio Control and **IF2** MIDI Streaming: bulk EP 0x02 / 0x82, 32 bytes.

**HID report descriptor @0x080036E8** (52 bytes):
```
06 A0 FF  09 01  A1 01        Usage Page 0xFFA0, Usage 1, Collection(App)
  09 02  A1 00                  Usage 2, Collection(Phys)
    06 A1 FF 09 03 09 04 15 18 25 7F 35 00 45 FF 75 08 95 20 81 02   Input  32 x 8 bit
    09 05 09 06 15 80 25 7F 35 00 45 FF 75 08 95 20 91 02            Output 32 x 8 bit
  C0 C0
```
There are no report IDs and no feature reports. The class Setup handler at 0x080023B8 only answers GET_DESCRIPTOR 0x21 and 0x22. All traffic goes over the interrupt endpoints. The same descriptor is in the application @0x080153FC.

### 1.2 Receive path (bootloader)

The HID DataOut callback is at 0x0800242C:

- If `epnum == 1`, advance the write index 0x20000D0E by 32 (mod 0x80).
- Re-arm EP1 OUT into `ring[0x20000C10 + wr]`.

The ring holds 4 reports and has **no overflow check and no flow control**. MIDI OUT (EP 0x02) is ignored. The host **must** use stop-and-wait: one command, then wait for its reply.

Replies are sent with `USBD_HID_Send(0x81, buf, 32)` (0x08002460). A reply is only sent while the device is in the CONFIGURED state.

### 1.3 How the official updater talks to the device

The updater uses the PJRC-style `rawhid` (`CHIDDevice`):

- `open(max=1, vid=0x09E8, pid=0x0049, usage_page=0, usage=0)`, using values from STM.xml.
- `send()` = `IOHIDDeviceSetReport(kIOHIDReportTypeOutput, reportID 0, buf, 32)` (0x100004B80).
- `receive()` = input-report callback queue, with the timeout in ms (0x1000049B0).

The timeout is 3000 ms and the retry count is 3 (STM.xml `<debug>`).

On Windows the same code uses SetupDi plus WriteFile, which means a 33-byte write with report ID 0 prepended.

---

## 2. Packet formats (byte-level)

Every packet is exactly 32 bytes.

- Host → device: `55 4F` ('U','O') + command.
- Device → host: `55 49` ('U','I') + status.

Bytes that are not listed below are 0x00. The device zeroes its reply buffer (0x20000CD0) before each reply. The updater compares the first **4** bytes of a reply as a little-endian dword, for example `0x00F14955`, so byte 3 must be 0x00 except in the C4 reply.

Command dispatch is in `FUNC_080018DC` (the bootloader main poll).

### C4: get version / mode probe
```
TX: 55 4F C4 00 00 ... 00
RX (bootloader): 55 49 F4 VH VL 00 ...   VH,VL = bytes @0x0801FFF6, 0x0801FFF7 (v1.26 -> 01 26)
RX (application): 55 49 E4 00 00 ...      (app handler FUNC_08014E34: only C4 is implemented)
```
`UpdaterDlg::Connect` (0x10000DD40) sends exactly this. It treats E4 as "Can't enter upgrade mode" and F4 as success. It reads the version from bytes [3..3+verLen).

### C1: start (erase)
```
TX: 55 4F C1 00 ... 00
```
Device actions (0x08001960):

1. Show "Upgrade - Start".
2. Save `*(u8*)0x0801FFF5`. This is the "settings layout" byte, 0x09 in v1.26.
3. Unlock flash and clear flags 0x34.
4. Erase pages for `addr = 0x08004000; addr < 0x0801F800; addr += 0x800`. That is 108 pages, covering **0x4000 through 0x1F7FF**. It does **not** erase 0x0000-0x3FFF or the last page 0x1F800.
5. Lock flash.

```
RX: 55 49 F1 00 ...       success
```
If a page erase fails, the device sends `55 49 E2 ...` **and then also** `55 49 F1 ...` (firmware quirk). The updater accepts F1 and also checks for E1, but the bootloader never sends E1.

Erase takes 108 × (20-40 ms), roughly **2.2-4.3 s**. The stock updater's 3 s timeout is marginal. Use 10-15 s.

### C2: write 16 bytes
```
off  0  1  2  3  4  5  6  7  8  9 ....... 24  25..29  30  31
    55 4F C2 A3 A2 A1 A0 S1 S0 D0 ....... D15  00..   CH  CL
```
- `A3..A0`: big-endian 32-bit offset. The device computes `addr = 0x08000000 | (A3<<24|A2<<16|A1<<8|A0)`. The updater sends `mcuStartAddr + offset` = offset 0x00000..0x1FFF0.
- `S1 S0`: big-endian 16-bit packet counter set by the updater (it increments per packet and wraps at the section's packet count). **The device ignores it.**
- `D0..D15`: data in memory order. The device programs 4 little-endian words.
- `CH CL`: big-endian 16-bit sum of D0..D15. The updater computes it at 0x10000F61A-0x10000F68F; the device checks it at 0x08001A30-0x08001A68.

Device behaviour, from 0x080019DE:

| Condition | Action | Reply |
|---|---|---|
| addr < 0x08004000 | **nothing written** | `55 49 F2` |
| addr >= 0x08020000 | nothing | `55 49 00` (status 0; the updater treats this as an unknown reply) |
| checksum mismatch | nothing | `55 49 E2` |
| 0x08004000 <= addr < 0x0801F800 | program 4 words immediately | `55 49 F2`. On a program error: `E2` **then also** `F2`. |
| 0x0801F800 <= addr | copy the 16 bytes into a RAM staging page (0x200000A8 + (addr - 0x0801F800)) | `55 49 F2` |
| addr == 0x0801FFF0 (last packet) | commits the last page, see below | `F2`, or `E2` on erase error, or `E2` + `F2` on program error |

**Last-page commit**, triggered only by the packet at 0x0801FFF0 (0x08001B34-0x08001C2A):

1. `staging[0x000..0x01F]` is replaced by the device's current flash 0x0801F800..0x0801F81F. This preserves the per-unit 32-byte header, which looks like a serial or calibration value ("A00000000000001" ×2 in the stock image).
2. If `staging[0x7F5]` (the new layout byte) == current flash 0x0801FFF5, then `staging[0x020..0x7EF]` is replaced by current flash 0x0801F820..0x0801FFEF. This preserves the user's programs and settings across updates of the same layout.
3. Erase page 0x0801F800 and program 512 words from staging.

Only `staging[0x7F0..0x7FF]` comes from the host in every case: VID, PID, layout byte, version and checksum. `staging[0x20..0x7EF]` comes from the host only when the layout byte changes.

=> **The packet at 0x1FFF0 must be the last C2.** All packets for 0x1F800-0x1FFE0 must be sent before it, because staging RAM is not initialised otherwise.

### C3: finish
```
TX: 55 4F C3 00 ... 00
RX: 55 49 F3 00 ...  (sent immediately)
```
Device actions (0x08001C50):

1. Lock flash and wait 10 ms.
2. If byte 0x08003FFF == byte 0x08007FFF (the staged bootloader matches the live one), show "Upgrade - Completed" and "vXX.XX" and wait 1 s.
3. Set flag 0x20000D12 = 1. The main loop then disables interrupts and calls NVIC_SystemReset (0x08003480: AIRCR = 0x05FA0004).

The updater checks for F3 and E3. E3 is never sent.

### Verify / readback
**There is none.** The device has no read-memory command. The only integrity checks are:

- the per-packet 16-bit additive sum,
- the C4 version afterwards.

### Official updater sequence (`UpdaterDlg::Update`, 0x10000EA10)
```
open HID (VID/PID from XML)
C1 -> expect F1 (or E1)              [3000 ms, 3 retries]
for section (binStart 0, size 0x20000, mcuStart 0):
    for off in 0..0x1FFF0 step 16:   (ALL 8192 packets, incl. bootloader region and 0xFF runs)
        C2(off) -> expect F2 (E2 = error)   [3000 ms, 3 retries, same packet resent]
C4 -> expect F4, log version
C3 -> expect F3
```
Timing units in the bootloader: SysTick = SystemCoreClock/10000, so one tick = **0.1 ms** (0x08002566).

---

## 3. Safety analysis (critical)

### 3.1 Does the bootloader write or erase its own region?

**For 0x08000000-0x08003FFF: no.**

- C1's erase loop starts at **0x08004000** (literal at 0x08001986: `ldr r5,=0x08004000`; loop bound 0x0801F800).
- C2 rejects addr < 0x08004000 and returns F2 without writing (0x08001A04-0x08001A1A).
- Nothing else in the bootloader writes flash.

**For 0x08004000-0x08007FFF: yes, it is erased and rewritten from the host image.** Blocks 8..15 of the 64 × 2 KiB blocks land here.

### 3.2 Is 0x4000 a backup bootloader?

It is a **staging copy used by the application to update the bootloader**. The bootloader never boots from it, never checks it, and never copies from it. Its only use of it is cosmetic: C3 compares bytes 0x3FFF and 0x7FFF to decide whether to display "Completed".

The **application** does the copy. In app `main` at 0x08014BA8:
```
08014be4  ldr r0,=0x08007fff ; ldr r1,=0x08003fff
08014be8  if (*0x08003FFF != *0x08007FFF && *0x08007FFF != 0xFF) {
08014bf8      cpsid i
08014bfa      FUNC_08014D8E():  unlock; erase pages 0x08000000..0x08003FFF (8 pages);
                                copy words 0x08004000..0x08007FFF -> 0x08000000..;
                                on any flash error: `b .` (hang forever)
08014bfe      show "Upgrade - Completed" + version, wait 5 s, continue
          }
```
Byte 0x3FFF is the bootloader version marker (0x03 in the stock image, at the end of the zero padding). In stock v1.26, 0x0000-0x3FFF == 0x4000-0x7FFF and 0x7FFF == 0x3FFF == 0x03, so this never fires.

**Brick scenarios:**

1. A custom image whose 0x4000-0x7FFF is not the genuine bootloader, with byte 0x7FFF ∉ {0x03, 0xFF}. The application would overwrite the real bootloader on its next boot, which means a **hard brick (SWD needed)**.
2. Power loss during that app-side copy, which takes about 8 page erases + 4096 word programs, roughly 0.3 s.

**Rule for our flasher:** refuse any image unless `img[0x0000:0x4000] == img[0x4000:0x8000] == known-good stock bootloader` (SHA-256 of the 16 KiB block from v1.26). Equivalently, never change 0x0000-0x7FFF. A patched application must keep, or deliberately remove, this copy logic.

### 3.3 What decides app vs update mode (bootloader `main`, 0x08003494)

```
init, wait 200 ms (2000 ticks)
if (*(u8*)0x0801FFF6 == 0xFF || *(u8*)0x0801FFF7 == 0xFF)  -> UPDATE MODE
// key-matrix check: drive PB8/PB9, wait 5 ms, read GPIOC->IDR
if ((GPIOC->IDR & 0xFF00) == 0xF600)                       -> UPDATE MODE   (BANK + PROG SELECT held,
                                                                              per STM.xml instructions)
cpsid i; pc = *(u32*)0x08008004   (MSP is NOT reloaded; the app's reset handler sets MSP itself)
```
- There is **no** checksum, CRC, VID/PID or vector-table validity test.
- There is no RAM magic or backup-register flag.
- UPDATE MODE shows "Upgrade" and "vXX.XX" on the OLED, starts USB and polls forever. After C3 it resets.

### 3.4 Interrupted transfer

| Interrupted during | Last page (0x1F800, version bytes) | Next power-up |
|---|---|---|
| C1 erase or any C2 below 0x1F800 | **untouched, still valid** | The bootloader **jumps to the half-erased or half-written app**. If 0x8004 = 0xFFFFFFFF, this HardFaults, and the bootloader's handler at 0x08002722 is `b .`. The device looks dead: no USB, blank OLED. **Recovery: hold BANK + PROG SELECT while plugging in.** The key check runs before the jump, so update mode is always reachable. Then re-flash. |
| The ~20-40 ms last-page erase/program window | version = 0xFF | Stays in update mode automatically |
| After the 0x1FFF0 packet, before C3 | new, valid | Boots the new app normally |

The first 16 KiB is never touched, so a bootloader-driven update cannot brick the unit (§3.2 is the exception). Worst case, the device needs the button combo.

Side note: the app's preset save (0x0800FE60) erases and rewrites page 0x1F800 and mirrors it to 0x1F000. A power cut there leaves 0x1FFF6 = 0xFF, so the unit then sits in update mode until re-flashed. This is recoverable.

---

## 4. Entering the bootloader from the running application

The stock firmware has **no software path**.

- The app HID handler (0x08014E34) only replies to C4, with E4.
- The only NVIC_SystemReset in the app (0x0800CA18) is called from the snake-game exit (0x0800CADC). That reset re-enters the bootloader, which immediately jumps back to the app.
- No code path erases the version bytes and then resets.

I did not fully audit the SysEx handler, but these flash and reset call sites are the complete list of AIRCR and version-page writers in the app. Confidence: high.

The documented method is: **hold BANK + PROG SELECT while connecting USB**. Alternatively, the version bytes must already be erased.

Idea for a custom app (untested): erase page 0x1F800, then call NVIC_SystemReset. The bootloader then sees 0xFF and enters update mode. Caveat: this loses the per-unit header at 0x1F800-0x1F81F and the user settings, because the bootloader preserves them from (now-erased) flash. Back them up first, and have the flasher re-send them in the image.

---

## 5. Draft Python flasher (hidapi). Untested; follow the safety gates.

```python
#!/usr/bin/env python3
"""mpk3flash.py - DRAFT, untested. MPK mini mk3 (STM32F070, PID 0x0049) HID bootloader flasher."""
import hashlib, struct, sys, time
import hid  # pip install hidapi

VID, PID = 0x09E8, 0x0049
IMG_SIZE = 0x20000
PKT = 16
STOCK_BL_SHA256 = "f5503cee0bc42c1412fddca36198f8b9736d09a60ee5a41ace3ee1a270cb8c38"   # compute once from the backup

def sum16(b): return sum(b) & 0xFFFF

def validate_image(img: bytes):
    assert len(img) == IMG_SIZE, "image must be exactly 128 KiB"
    bl = img[0x0000:0x4000]
    assert hashlib.sha256(bl).hexdigest() == STOCK_BL_SHA256, "bootloader region != stock"
    assert img[0x4000:0x8000] == bl, "0x4000-0x7FFF MUST equal stock bootloader (app copies it to 0x0!)"
    assert img[0x1FFF0:0x1FFF4] == bytes([0x09, 0xE8, 0x00, 0x49]), "VID/PID tail"
    assert img[0x1FFF6] != 0xFF and img[0x1FFF7] != 0xFF, "version bytes must not be 0xFF"
    msp, rst = struct.unpack_from("<II", img, 0x8000)
    assert 0x20000000 < msp <= 0x20004000 and 0x08008000 <= (rst & ~1) < 0x08020000 and rst & 1
    ck = sum16(img[:0x1FFFE])                     # updater-side checksum (device ignores it)
    if struct.unpack_from(">H", img, 0x1FFFE)[0] != ck:
        img = img[:0x1FFFE] + struct.pack(">H", ck)  # or refuse; Akai's updater checks it
    return img

class Dev:
    def __init__(self):
        cands = [d for d in hid.enumerate(VID, PID) if d.get("usage_page") in (0xFFA0, None, 0)
                 and d.get("interface_number") in (0, -1)]
        # macOS reports usage_page; on Linux/hidraw usage_page may be 0 -> pick interface 0
        if not cands: raise SystemExit("MPK mini 3 HID interface not found")
        self.h = hid.device(); self.h.open_path(cands[0]["path"])
    def drain(self):
        while self.h.read(64, timeout_ms=10): pass
    def xfer(self, pkt32: bytes, timeout_ms: int) -> bytes:
        assert len(pkt32) == 32
        self.drain()                                  # drop stray extra replies (E2+F2 quirk)
        if self.h.write(b"\x00" + pkt32) < 0: raise IOError("HID write failed")
        r = bytes(self.h.read(32, timeout_ms=timeout_ms))
        if len(r) < 4: raise TimeoutError
        if r[0:2] != b"UI": raise IOError(f"bad reply {r.hex()}")
        return r

def cmd(c): return bytes([0x55, 0x4F, c]) + bytes(29)

def c2(off, data, seq):
    p = bytearray(32)
    p[0:3] = b"\x55\x4F\xC2"
    p[3:7] = struct.pack(">I", off)            # offset; device ORs 0x08000000
    p[7:9] = struct.pack(">H", seq & 0xFFFF)   # ignored by device; mimic updater
    p[9:25] = data
    p[30:32] = struct.pack(">H", sum16(data))
    return bytes(p)

def flash(img: bytes, dry_run=True):
    img = validate_image(img)
    d = Dev()
    r = d.xfer(cmd(0xC4), 3000)
    if r[2] == 0xE4: raise SystemExit("Device is in APPLICATION mode. Unplug, hold BANK+PROG SELECT, replug.")
    if r[2] != 0xF4: raise SystemExit(f"unexpected C4 reply {r.hex()}")
    print(f"bootloader OK, current fw v{r[3]:02X}.{r[4]:02X}")
    if dry_run: return
    r = d.xfer(cmd(0xC1), 15000)                # erase 0x4000-0x1F7FF, ~2-4 s
    if r[2] != 0xF1: raise SystemExit(f"erase failed {r.hex()}")
    # Write ascending; 0x0000-0x3FFF is ignored by device (F2) -> may be skipped, but harmless to send.
    for i, off in enumerate(range(0, IMG_SIZE, PKT)):
        if off < 0x4000: continue               # optional: skip, device would not write anyway
        r = d.xfer(c2(off, img[off:off+PKT], i), 3000)
        if r[2] != 0xF2:
            # E2 after a resend of an already-programmed packet is expected (STM32 PGERR) -> do NOT
            # blindly retry; restart the whole sequence from C1 instead.
            raise SystemExit(f"write failed at 0x{off:05X}: {r.hex()}  -> re-run (device stays recoverable)")
    r = d.xfer(cmd(0xC4), 3000)
    assert r[2] == 0xF4 and (r[3], r[4]) == (img[0x1FFF6], img[0x1FFF7]), "version mismatch after write"
    r = d.xfer(cmd(0xC3), 3000)                 # device resets ~1 s later
    assert r[2] == 0xF3
    print("done; device rebooting")
```

### Notes and uncertainties
- **Retries:** retrying a C2 below 0x1F800 after a lost reply makes the device try to reprogram non-erased flash, which gives PGERR → E2, even though the data may already be correct. The stock updater does exactly this (inference: possible spurious failures). Safer policy: on any C2 failure, restart from C1. C2 packets at or above 0x1F800 only go to RAM, so they are safe to resend. Resending 0x1FFF0 re-erases and reprograms the last page, which is idempotent.
- **Skipping 0xFF-only packets** below 0x1F800 is safe, because those pages were just erased. Never skip packets at or above 0x1F800, because staging RAM is uninitialised.
- **hidapi platform behaviour:**
  - macOS hidapi `write()` with a leading 0x00 maps to SetReport(Output, ID 0). This matches the official updater.
  - Linux hidraw sends via the interrupt OUT endpoint.
  - The bootloader has no SET_REPORT handler, so a control-pipe-only fallback would fail. This was not tested on Linux or Windows.
- **AT32 variant:** units with PID **0x1049** use `AT.bin` (a different MCU and vector layout). This analysis covers only the STM32 bootloader. Refuse PID 0x1049.
- **Button-to-GPIO mapping:** the bootloader key mask is `GPIOC[15:8] == 0xF6` (PC8 and PC11 low). The button names (BANK + PROG SELECT) come from the updater's own instructions in STM.xml. The app checks the same row at boot for 0xF3 and 0xF5 (other service modes: 0xF3 sets flag 0x200032B1 and alters startup, 0xF5 calls 0x0801115C; purpose not analysed).
- **The checksum at 0x1FFFE** is the big-endian 16-bit byte sum of 0x00000-0x1FFFD (verified: 0xF38A). Only the host updater cares about it.

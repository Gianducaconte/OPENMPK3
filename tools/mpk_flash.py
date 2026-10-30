#!/usr/bin/env python3
"""OPENMPK3 flasher for the Akai MPK mini mk3 (STM32F070, USB 09E8:0049) via Akai's HID bootloader.

  python3 tools/mpk_flash.py probe              # ask the device which mode it is in (safe)
  python3 tools/mpk_flash.py flash IMAGE.bin    # dry run: validate image + check bootloader
  python3 tools/mpk_flash.py flash IMAGE.bin --write

Enter update mode: unplug, hold BANK + PROG SELECT, plug USB in (screen shows "Upgrade").
Protocol + safety analysis: .codex/UPDATE_PROTOCOL.md. The bootloader never writes 0x0000-0x3FFF;
images are refused unless 0x0000-0x7FFF equals the stock bootloader (the stock app copies 0x4000->0x0).
"""
import argparse, hashlib, struct, sys, time
import hid

VID, PID = 0x09E8, 0x0049
IMG_SIZE, PKT = 0x20000, 16
LAST_PAGE, TAIL_PKT = 0x1F800, 0x1FFF0
STOCK_BL_SHA256 = "f5503cee0bc42c1412fddca36198f8b9736d09a60ee5a41ace3ee1a270cb8c38"

def sum16(b): return sum(b) & 0xFFFF

def validate(img: bytes) -> bytes:
    if len(img) != IMG_SIZE: sys.exit("image must be exactly 128 KiB")
    bl = img[:0x4000]
    if hashlib.sha256(bl).hexdigest() != STOCK_BL_SHA256: sys.exit("REFUSED: 0x0000-0x3FFF is not the stock bootloader")
    if img[0x4000:0x8000] != bl: sys.exit("REFUSED: 0x4000-0x7FFF must equal the stock bootloader (brick risk)")
    if img[0x1FFF0:0x1FFF4] != bytes([0x09, 0xE8, 0x00, 0x49]): sys.exit("REFUSED: VID/PID tail is not 09E8:0049")
    if 0xFF in img[0x1FFF6:0x1FFF8]: sys.exit("REFUSED: version bytes must not be 0xFF")
    sp, rv = struct.unpack_from("<II", img, 0x8000)
    if not (0x20000000 < sp <= 0x20004000 and rv & 1 and 0x08008000 <= rv < 0x08020000):
        sys.exit(f"REFUSED: app vector table invalid (sp={sp:#x} reset={rv:#x})")
    if struct.unpack_from(">H", img, 0x1FFFE)[0] != sum16(img[:0x1FFFE]): sys.exit("REFUSED: image checksum mismatch")
    return img

class Dev:
    def __init__(self):
        c = [d for d in hid.enumerate(VID, PID) if d.get("usage_page") == 0xFFA0 or d.get("interface_number") == 0]
        if not c: sys.exit("MPK mini 3 HID interface not found (is it plugged in?)")
        self.h = hid.device(); self.h.open_path(c[0]["path"])
    def drain(self):
        while self.h.read(64, timeout_ms=5): pass
    def xfer(self, pkt: bytes, timeout_ms=3000) -> bytes:
        assert len(pkt) == 32
        self.drain()
        if self.h.write(b"\x00" + pkt) < 0: raise IOError("HID write failed")
        r = bytes(self.h.read(32, timeout_ms=timeout_ms))
        if len(r) < 3: raise TimeoutError("no reply from device")
        if r[:2] != b"UI": raise IOError(f"unexpected reply {r.hex()}")
        return r

def cmd(c): return bytes([0x55, 0x4F, c]) + bytes(29)

def c2(off, data, seq):
    p = bytearray(32); p[0:3] = b"\x55\x4F\xC2"
    p[3:7] = struct.pack(">I", off); p[7:9] = struct.pack(">H", seq & 0xFFFF)
    p[9:25] = data; p[30:32] = struct.pack(">H", sum16(data))
    return bytes(p)

def probe(d):
    r = d.xfer(cmd(0xC4))
    if r[2] == 0xE4: return "app", None
    if r[2] == 0xF4: return "bootloader", f"{r[3]:02X}.{r[4]:02X}"
    return "unknown", r.hex()

def flash(path, write):
    img = validate(open(path, "rb").read())
    print(f"image OK: {path}  version {img[0x1FFF6]:02X}.{img[0x1FFF7]:02X}  sha256 {hashlib.sha256(img).hexdigest()[:16]}")
    d = Dev(); mode, ver = probe(d)
    if mode != "bootloader":
        sys.exit("device is running its application. Unplug, hold BANK + PROG SELECT, plug back in, retry.")
    print(f"bootloader ready (installed firmware v{ver})")
    if not write: print("dry run only - add --write to flash"); return
    t0 = time.time()
    r = d.xfer(cmd(0xC1), 15000)
    if r[2] != 0xF1: sys.exit(f"erase failed: {r.hex()}")
    print(f"erased in {time.time() - t0:.1f}s")
    offs = [o for o in range(0x4000, IMG_SIZE, PKT)
            if o >= LAST_PAGE or img[o:o + PKT] != b"\xff" * PKT]    # erased flash is already 0xFF
    assert offs[-1] == TAIL_PKT
    for i, off in enumerate(offs):
        r = d.xfer(c2(off, img[off:off + PKT], off // PKT))
        if r[2] != 0xF2:
            sys.exit(f"\nwrite failed at {off:#07x}: {r.hex()}\nDevice is still recoverable: replug in update mode and re-run.")
        if i % 256 == 0 or i == len(offs) - 1:
            print(f"\r  writing {100 * (i + 1) // len(offs):3d}%  ({off:#07x})", end="", flush=True)
    print()
    mode, ver = probe(d)
    want = f"{img[0x1FFF6]:02X}.{img[0x1FFF7]:02X}"
    if ver != want: sys.exit(f"version readback mismatch: got {ver}, want {want}")
    r = d.xfer(cmd(0xC3))
    if r[2] != 0xF3: sys.exit(f"finish failed: {r.hex()}")
    print(f"done in {time.time() - t0:.1f}s - device is rebooting into v{ver}")

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    sp.add_parser("probe")
    f = sp.add_parser("flash"); f.add_argument("image"); f.add_argument("--write", action="store_true")
    a = ap.parse_args()
    if a.cmd == "probe":
        mode, ver = probe(Dev()); print(mode, ver or "")
    else:
        flash(a.image, a.write)

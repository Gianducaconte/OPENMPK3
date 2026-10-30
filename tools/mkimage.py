#!/usr/bin/env python3
"""Pack an OPENMPK3 app binary into a full 128 KiB image accepted by Akai's updater/bootloader.

  bootloader (0x0000-0x7FFF) copied byte-for-byte from the official stock image
  app        at 0x8000
  tail       at 0x1FFF0: VID(BE) PID(BE) FF 09 VER(2) FF.. CHECKSUM(BE16 = sum of bytes[0:0x1FFFE])
"""
import argparse, os, sys, struct, hashlib
SIZE, APP_OFF, APP_MAX, TAIL = 0x20000, 0x8000, 0x15000 - 0x8000, 0x1FFF0

def checksum(img): return sum(img[:SIZE - 2]) & 0xFFFF

def build(stock, app, version):
    if len(stock) != SIZE: sys.exit('stock image must be 128 KiB')
    if checksum(stock) != struct.unpack('>H', stock[-2:])[0]: sys.exit('stock image checksum mismatch')
    if len(app) > APP_MAX: sys.exit(f'app too big: {len(app)} > {APP_MAX}')
    sp, rv = struct.unpack('<II', app[:8])
    if not (0x20000000 < sp <= 0x20004000 and rv & 1 and 0x08008000 <= rv < 0x08015000):
        sys.exit(f'app vector table looks wrong: sp={sp:#x} reset={rv:#x}')
    img = bytearray(b'\xff' * SIZE)
    img[:APP_OFF] = stock[:APP_OFF]                       # Akai bootloader, untouched
    img[APP_OFF:APP_OFF + len(app)] = app
    img[TAIL:TAIL + 16] = stock[TAIL:TAIL + 16]          # VID/PID/marker from stock
    img[0x1FFF6:0x1FFF8] = bytes(version)
    img[-2:] = struct.pack('>H', checksum(img))
    # Safety: stock app restores 0x0000-0x3FFF from 0x4000-0x7FFF when bytes 0x3FFF/0x7FFF differ, and the
    # bootloader stays in update mode only if version bytes are 0xFF. Never emit an image that breaks these.
    assert img[:0x4000] == img[0x4000:0x8000] == stock[:0x4000], 'bootloader area must equal stock'
    assert hashlib.sha256(img[:0x4000]).hexdigest().startswith('f5503cee'), 'unexpected bootloader hash'
    assert 0xFF not in img[0x1FFF6:0x1FFF8], 'version bytes must not be 0xFF'
    return bytes(img)

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('app'); ap.add_argument('out')
    ap.add_argument('--stock', default='stock/MPKmini3_v1.26_STM32F070_pid0049.bin',
                    help="Akai's official image (get it with: python3 tools/get_stock_firmware.py)")
    ap.add_argument('--version', default='02.00', help='major.minor shown by the updater, e.g. 02.00')
    a = ap.parse_args()
    maj, mnr = (int(x, 16) for x in a.version.split('.'))
    if not os.path.exists(a.stock):
        sys.exit(f"missing {a.stock}\nRun first:  python3 tools/get_stock_firmware.py   (downloads Akai's official firmware)")
    img = build(open(a.stock, 'rb').read(), open(a.app, 'rb').read(), (maj, mnr))
    open(a.out, 'wb').write(img)
    print(f'{a.out}: app {len(open(a.app,"rb").read())} bytes, version {maj:02x}.{mnr:02x}, '
          f'checksum {checksum(img):#06x}, sha256 {hashlib.sha256(img).hexdigest()[:16]}')

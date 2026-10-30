#!/usr/bin/env python3
"""Fetch Akai's official MPK mini mk3 firmware v1.26 from Akai's own server and extract the device image.

The image is Akai's copyrighted code, so it is not part of this repository. It is needed to
  - build OPENMPK3 (the image packer copies Akai's bootloader into every image, byte for byte), and
  - go back to the stock firmware at any time.

  python3 tools/get_stock_firmware.py              # downloads the official macOS updater (~10 MB) from Akai
  python3 tools/get_stock_firmware.py --zip FILE   # or use an updater zip you already downloaded

Result: stock/MPKmini3_v1.26_STM32F070_pid0049.bin (checked against its known SHA-256).
"""
import argparse, hashlib, io, os, sys, urllib.request, zipfile

URL = 'https://cdn.inmusicbrands.com/akai/mpk3mini/1_26/MPKmini3_Updater_v1.26.app.zip'
MEMBER = 'MPKmini3_Updater_v1.26.app/Contents/Resources/STM.bin'
SHA256 = '2c006142134bcc66116f25f04c091f4c62e2b97984beecfdbffc8a93db602a19'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'stock', 'MPKmini3_v1.26_STM32F070_pid0049.bin')

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--zip', help='use a local copy of MPKmini3_Updater_v1.26.app.zip instead of downloading')
    a = ap.parse_args()
    if os.path.exists(OUT) and hashlib.sha256(open(OUT, 'rb').read()).hexdigest() == SHA256:
        print('already there:', os.path.relpath(OUT)); return
    if a.zip:
        data = open(a.zip, 'rb').read()
    else:
        print('downloading Akai MPK mini mk3 updater v1.26 from', URL)
        with urllib.request.urlopen(URL, timeout=60) as r:
            data = r.read()
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        try:
            img = z.read(MEMBER)
        except KeyError:
            sys.exit('STM.bin not found in the zip - is this MPKmini3_Updater_v1.26.app.zip?')
    if hashlib.sha256(img).hexdigest() != SHA256:
        sys.exit('unexpected image (SHA-256 mismatch) - refusing to use it')
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, 'wb').write(img)
    print('saved', os.path.relpath(OUT), f'({len(img)} bytes, SHA-256 verified)')

if __name__ == '__main__':
    main()

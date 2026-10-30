#!/usr/bin/env python3
"""Back up identity + all 8 user programs of an Akai MPK mini mk3 via SysEx.
Writes backup/<timestamp>/ with .syx files (re-sendable via the program-send command)."""
import mido, time, os, sys, datetime, json
OUT = os.path.join(os.path.dirname(__file__), '..', 'backup', datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
port = next(n for n in mido.get_input_names() if 'MPK mini 3' in n)
i, o = mido.open_input(port), mido.open_output(next(n for n in mido.get_output_names() if 'MPK mini 3' in n))
def tx(data, wait=0.8):
    for _ in i.iter_pending(): pass
    o.send(mido.Message('sysex', data=data)); t = time.time(); got = []
    while time.time() - t < wait:
        got += [m for m in i.iter_pending() if m.type == 'sysex']; time.sleep(0.01)
    return got
os.makedirs(OUT, exist_ok=True); meta = {}
ident = tx([0x7e, 0x7f, 0x06, 0x01])
if ident:
    open(f'{OUT}/identity.syx', 'wb').write(bytes(ident[0].bytes())); meta['identity'] = ident[0].hex()
for p in range(0, 9):   # 0 = RAM/current, 1..8 = stored programs
    r = tx([0x47, 0x7f, 0x49, 0x66, 0x00, 0x01, p])
    if r:
        open(f'{OUT}/program_{p}.syx', 'wb').write(bytes(r[0].bytes()))
        name = bytes(r[0].data[7:23]).split(b'\0')[0].decode(errors='replace')
        meta[f'program_{p}'] = name; print(p, len(r[0].bytes()), name)
json.dump(meta, open(f'{OUT}/meta.json', 'w'), indent=2); print('saved', os.path.abspath(OUT))

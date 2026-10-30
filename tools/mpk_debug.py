#!/usr/bin/env python3
"""OPENMPK3 live diagnostics over SysEx (F0 7D 4F ...).
  python3 tools/mpk_debug.py [seconds]   per-key contact timing + main-loop stats
  python3 tools/mpk_debug.py probe       check which spare MCU pins are free (inputs only, harmless)"""
import mido, sys, time
name = next(n for n in mido.get_input_names() if 'OPENMPK3' in n or 'MPK' in n)
i, o = mido.open_input(name), mido.open_output(name)
if len(sys.argv) > 1 and sys.argv[1] == 'probe':
    PINS = ['PA13 (TE1 SWDIO) clock in', 'PA14 (TE1 SWCLK) reset in', 'PA8', 'PA9 (UART1 TX)', 'PA10 (UART1 RX)',
            'PA15', 'PB6', 'PB11', 'PD2', 'PA6 (ADC6, 3.3V only)', 'PA7 (ADC7, 3.3V only)']
    CODES = ['free / floating', 'tied LOW (do not drive!)', 'tied HIGH (do not drive!)', 'unclear']
    o.send(mido.Message('sysex', data=[0x7D, 0x4F, 0x20])); t = time.time()
    while time.time() - t < 2:
        m = i.poll()
        if m and m.type == 'sysex' and m.data[:3] == (0x7D, 0x4F, 0x20):
            for p, c in zip(PINS, m.data[3:]): print(f'{p:28s} {CODES[c]}')
            sys.exit(0)
    sys.exit('no reply (is OPENMPK3 >= v0.4 running?)')
dur = float(sys.argv[1]) if len(sys.argv) > 1 else 20
o.send(mido.Message('sysex', data=[0x7D, 0x4F, 0x02, 1]))       # key debug on
o.send(mido.Message('sysex', data=[0x7D, 0x4F, 0x01]))          # reset loop stats
t0 = time.time(); last_stats = t0
print('key  dt(ms)  first  detect-latency(us)  -> velocity')
pending = {}
while time.time() - t0 < dur:
    for m in i.iter_pending():
        if m.type == 'sysex' and m.data[:3] == (0x7D, 0x4F, 0x10):
            d = m.data; dt = d[4] | d[5] << 7 | d[6] << 14; lat = d[8] | d[9] << 7
            pending[d[3]] = (dt * 0.02, 'even' if d[7] == 1 else 'odd', lat * 20)
        elif m.type == 'note_on' and pending:
            k, (dt, first, lat) = pending.popitem()
            print(f'{k:3d}  {dt:6.2f}  {first:5s}  {lat:6d}              -> {m.velocity}')
        elif m.type == 'sysex' and m.data[:3] == (0x7D, 0x4F, 0x01):
            us = m.data[3] | m.data[4] << 7 | m.data[5] << 14
            extra = ''
            if len(m.data) >= 13:
                src = ['internal', 'USB MIDI clock', 'CLOCK IN (TE1)'][m.data[8]]
                extra = (f'  | tempo {m.data[9] | m.data[10] << 7} BPM from {src}'
                         f'  | clock-in {"on" if m.data[11] else "off"}, {["4","1","2","24"][m.data[12]]} PPQN, {"direct" if m.data[13] else "transistor"}')
            print(f'-- main loop worst case: {us} us{extra}')
    if time.time() - last_stats > 5:
        last_stats = time.time(); o.send(mido.Message('sysex', data=[0x7D, 0x4F, 0x01]))
    time.sleep(0.002)
o.send(mido.Message('sysex', data=[0x7D, 0x4F, 0x02, 0]))

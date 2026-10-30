#!/usr/bin/env python3
"""Decode an MPK mini mk3 program dump (.syx, 254 bytes: F0 47 00 49 67 01 76 ... F7)."""
import sys
def decode(b):
    d = list(b[1:-1]); assert d[:4] == [0x47, 0x00, 0x49, 0x67], d[:6]
    p = d[6:]   # payload after 47 00 49 67 01 76 (len 0x76<<? -> 246 bytes)
    r = {'program': p[0], 'name': bytes(p[1:17]).split(b'\0')[0].decode()}
    k = ['pad_ch','aftertouch','key_ch','octave','arp_on','arp_mode','arp_div','arp_clock_ext','arp_latch','arp_swing',
         'tempo_taps','tempo_msb','tempo_lsb','arp_octave','joyx_mode','joyx_neg','joyx_pos','joyy_mode','joyy_neg','joyy_pos']
    for n, v in zip(k, p[17:37]): r[n] = v
    r['tempo'] = (r.pop('tempo_msb') << 7) | r.pop('tempo_lsb')
    pads = p[37:37+48]; r['pads'] = [tuple(pads[j:j+3]) for j in range(0, 48, 3)]  # (note, pc, cc) x16 (bank A+B)
    kn = p[85:85+8*20]; r['knobs'] = [dict(mode=kn[j], cc=kn[j+1], min=kn[j+2], max=kn[j+3],
          name=bytes(kn[j+4:j+20]).split(b'\0')[0].decode()) for j in range(0, 160, 20)]
    r['transpose'] = p[245] if len(p) > 245 else None
    return r
if __name__ == '__main__':
    import pprint
    for f in sys.argv[1:]: pprint.pprint(decode(open(f, 'rb').read()), sort_dicts=False, width=120)

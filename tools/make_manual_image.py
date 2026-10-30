#!/usr/bin/env python3
"""Render docs/img/openmpk3-layout.png: a drawn top view of the MPK mini mk3 annotated with the OPENMPK3 functions.
   python3 tools/make_manual_image.py
   The controller is drawn from simple shapes (no photo), at the same coordinates the labels point to."""
from PIL import Image, ImageDraw, ImageFont
import os

ROOT = os.path.join(os.path.dirname(__file__), '..')
OUT = os.path.join(ROOT, 'docs', 'img', 'openmpk3-layout.png')

INK, SHIFT, MUTED, LINE, BG = (28, 28, 30), (200, 30, 40), (110, 110, 115), (150, 150, 155), (255, 255, 255)
FONT = '/System/Library/Fonts/HelveticaNeue.ttc'
def font(size, bold=False):
    return ImageFont.truetype(FONT, size, index=1 if bold else 0)

# drawn controller; coordinates are in the "device space" the labels use (x 30..1170, y 265..1000)
CX, CY = 30, 265
DW, DH = 1140, 735
W, H = 2400, 1750
OX, OY = (W - DW) // 2, 330
img = Image.new('RGB', (W, H), BG)
d = ImageDraw.Draw(img)

BODY, PANEL, DARK, KEYW, KEYB, RED, LABEL = (246, 246, 247), (232, 232, 235), (40, 40, 44), (255, 255, 255), (30, 30, 34), (200, 30, 40), (120, 120, 126)
def R(x0, y0, x1, y1, **kw): d.rounded_rectangle([P(x0, y0), P(x1, y1)], **kw)
def T(x, y, s, size=13, fill=LABEL, bold=False, anchor='mm'): d.text(P(x, y), s, font=font(size, bold), fill=fill, anchor=anchor)

def draw_controller():
    R(40, 278, 1160, 995, radius=26, fill=BODY, outline=(205, 205, 210), width=3)                 # body
    # joystick
    jx, jy = P(120, 350); d.ellipse([jx - 46, jy - 46, jx + 46, jy + 46], fill=PANEL); d.ellipse([jx - 30, jy - 30, jx + 30, jy + 30], fill=DARK)
    # arpeggiator block + function buttons
    R(66, 402, 192, 460, radius=6, outline=DARK, width=2); T(129, 410, 'ARPEGGIATOR', 11)
    for (x, y, t) in [(100, 436, 'ON/OFF'), (161, 436, 'TAP'), (100, 481, 'OCT -'), (161, 481, 'OCT +'), (100, 526, 'FULL LVL'), (161, 526, 'NOTE RPT')]:
        R(x - 27, y - 16, x + 27, y + 16, radius=5, fill=DARK); T(x, y, t, 10, fill=RED, bold=True)
    # pads 5-8 top, 1-4 bottom
    for row, (y, first) in enumerate([(365, 5), (490, 1)]):
        for c in range(4):
            x = 280 + c * 123
            R(x - 50, y - 50, x + 50, y + 50, radius=8, fill=DARK, outline=RED if (first + c) % 2 == 0 else DARK, width=3)
            T(x - 50, y - 60, f'PAD {first + c}', 11, anchor='lm')
    # display + pad-control buttons
    R(727, 318, 857, 372, radius=5, fill=(15, 15, 18)); T(792, 338, 'C# Dorian', 12, fill=(230, 230, 235)); T(792, 356, 'Jazz 1', 10, fill=(160, 160, 166))
    T(1005, 318, 'PAD CONTROLS', 11)
    for (x, t) in [(912, 'BANK'), (970, 'CC'), (1036, 'PROG CHG'), (1099, 'PROG SEL')]:
        R(x - 27, 330, x + 27, 362, radius=5, fill=DARK); T(x, 346, t, 10, fill=RED, bold=True)
    # knobs K1-K8
    for i in range(8):
        x, y = [771, 877, 980, 1085][i % 4], [430, 527][i // 4]
        cx, cy = P(x, y); d.ellipse([cx - 30, cy - 30, cx + 30, cy + 30], fill=DARK, outline=(90, 90, 96), width=3)
        d.line([(cx, cy - 26), (cx, cy - 12)], fill=(220, 220, 225), width=3)
        T(x - 38, y - 38, f'K{i + 1}', 12, bold=True, fill=DARK, anchor='lm')
    # keyboard: 25 keys (15 white), black keys on top
    kx0, kx1, ky0, ky1 = 110, 1130, 610, 985
    ww = (kx1 - kx0) / 15
    for k in range(15):
        R(kx0 + k * ww + 2, ky0, kx0 + (k + 1) * ww - 2, ky1, radius=4, fill=KEYW, outline=(190, 190, 196), width=2)
    for k in range(14):
        if k % 7 in (2, 6): continue                     # no black key after E and B
        cx = kx0 + (k + 1) * ww
        R(cx - ww * 0.32, ky0, cx + ww * 0.32, ky0 + 230, radius=3, fill=KEYB)
    T(600, 590, 'arp: rate · mode · latch · octave · swing (printed above the keys)', 12)

def P(x, y):                                                              # original photo coords -> canvas
    return (x - CX + OX, y - CY + OY)

draw_controller()

def block(at, title, primary, shift=None):
    """draw a text block, return its bounding box"""
    tx, ty = at; y = ty; w = d.textlength(title, font=font(26, True))
    d.text((tx, y), title, font=font(26, True), fill=INK); y += 34
    for line in primary.split('\n'):
        if line: d.text((tx, y), line, font=font(22), fill=INK); w = max(w, d.textlength(line, font=font(22))); y += 28
    if shift:
        for line in shift.split('\n'):
            d.text((tx, y), line, font=font(22), fill=SHIFT); w = max(w, d.textlength(line, font=font(22))); y += 28
    return (tx, ty, tx + w, y)

def label(anchor, at, title, primary, shift=None, side='right'):
    """text block + a leader line from the block's `side` to a control (photo coords)"""
    x0, y0, x1, y1 = block(at, title, primary, shift)
    ax, ay = P(*anchor)
    if side == 'right':  start = (x1 + 14, y0 + 16)
    elif side == 'left': start = (x0 - 14, y0 + 16)
    else:                start = (x0 + 20, y1 + 6)            # bottom
    d.line([start, (ax, ay)], fill=LINE, width=2)
    d.ellipse([ax - 7, ay - 7, ax + 7, ay + 7], outline=SHIFT, width=3)

# ---- header ----
d.text((60, 40), 'OPENMPK3', font=font(64, True), fill=INK)
d.text((60, 118), 'Open firmware for the Akai MPK mini mk3: scales, chords, loops and MIDI effects', font=font(28), fill=MUTED)
d.text((60, 170), 'Black = normal function', font=font(24, True), fill=INK)
d.text((400, 170), 'Red = with SHIFT (hold PROG SELECT)', font=font(24, True), fill=SHIFT)

# ---- left column: joystick + arpeggiator block (lines leave from the right edge) ----
L = 60
label((120, 350), (L, 300), 'JOYSTICK', 'X: pitch bend\nY: mod wheel (CC1)')
label((100, 435), (L, 440), 'ARP ON/OFF', 'Arp on/off', 'Generate new pattern')
label((161, 435), (L, 600), 'TAP TEMPO', 'Tap tempo', 'Mutate pattern')
label((130, 480), (L, 720), 'OCT- / OCT+', 'Key octave (both = reset)', 'Pad octave')
label((100, 525), (L, 840), 'FULL LEVEL', 'Velocity 127', 'Accent on/off')
label((161, 525), (L, 960), 'NOTE REPEAT', 'Held pads repeat in time', 'Evolve on/off (autopilot)')

# ---- top row (lines leave from the bottom of each block) ----
label((465, 425), (560, 205), 'PADS 1-8', 'The 8 chords of the scale / style', 'Switch pattern P1-P8', side='bottom')
label((792, 345), (1010, 205), 'DISPLAY', 'Key · scale/style · pattern · loop', side='bottom')
label((912, 345), (1420, 205), 'BANK A/B = REC', 'Stopped: arm · playing: overdub', 'Clear channel · + pad: clear pattern', side='bottom')

# ---- right column (lines leave from the left edge) ----
R = 1700
label((970, 345), (R + 160, 330), 'CC = PLAY', 'Start / stop the loop', 'Undo last part (again = redo)', side='left')
label((1036, 345), (R + 160, 470), 'PROG CHANGE', 'Menu (K1 select, K2 edit)', 'Copy pattern to a pad', side='left')
label((1099, 345), (R + 160, 610), 'PROG SELECT = SHIFT', 'Hold for the red functions', '+ key: arp settings (key labels)', side='left')

# ---- knobs: a grid laid out like the knobs themselves ----
knobs = [('K1', 'Key (root)', 'Pitch drift'), ('K2', 'Scale / style', 'Scale shift (loop)'),
         ('K3', 'Voicing', 'Spiral echo'), ('K4', 'Octaver', 'Strum'),
         ('K5', 'MIDI channel', 'Trance gate'), ('K6', 'Shadow harmony', 'Ratchet'),
         ('K7', 'Loop length', 'Stutter'), ('K8', 'Tempo', 'Half / double time')]
gx, gy = OX + 20, OY + 735 + 60
d.text((gx, gy), 'KNOBS', font=font(30, True), fill=INK)
d.text((gx + 130, gy + 6), '(same arrangement as on the device)', font=font(22), fill=MUTED)
cw, ch = 280, 120
for i, (k, a, b) in enumerate(knobs):
    x, y = gx + (i % 4) * cw, gy + 50 + (i // 4) * ch
    d.rounded_rectangle([x, y, x + cw - 20, y + ch - 16], radius=14, outline=(220, 220, 224), width=2)
    d.text((x + 16, y + 10), k, font=font(26, True), fill=INK)
    d.text((x + 16, y + 44), a, font=font(22), fill=INK)
    d.text((x + 16, y + 72), b, font=font(22), fill=SHIFT)

# ---- keys ----
ky = gy + 50 + 2 * ch + 20
d.text((gx, ky), 'KEYS', font=font(30, True), fill=INK)
d.text((gx, ky + 42), 'Play the scale (Spread: every key is the next scale note, middle key = root)', font=font(22), fill=INK)
d.text((gx, ky + 72), 'SHIFT + key: arp rate · mode · latch · arp octave · swing (the labels printed above the keys)', font=font(22), fill=SHIFT)

os.makedirs(os.path.dirname(OUT), exist_ok=True)
img.save(OUT, optimize=True)
print('wrote', os.path.relpath(OUT, ROOT), img.size)

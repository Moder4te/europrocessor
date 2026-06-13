#!/usr/bin/env python3
# Nyan Cat GIF → RGB565 프레임 헤더 생성 (런타임 GIF 디코드 우회)
# 출력: src/nyan_frames.h  (160x120 × 12프레임, PROGMEM)
from PIL import Image

SRC = r'C:\Users\rladb\.claude\uploads\ea778163-94e1-4938-b505-4f1fe6b8f307\0263daa8-IMG_3482.gif'
OUT = r'F:\developer\v2\software\rotary_processor\src\nyan_frames.h'
W, H = 160, 120   # 런타임 2× 확대 → 320x240

im = Image.open(SRC)
n = im.n_frames
delay = im.info.get('duration', 70) or 70

frames = []
for i in range(n):
    im.seek(i)
    full = im.convert('RGB')                 # 원본 해상도
    fpx = full.load()
    fw, fh = full.size
    for yy in range(fh):                     # 흰 픽셀(R,G,B≥230) → 검정 (리사이즈 전)
        for xx in range(fw):
            r, g, b = fpx[xx, yy]
            if r >= 230 and g >= 230 and b >= 230:
                fpx[xx, yy] = (0, 0, 0)
    fr = full.resize((W, H), Image.LANCZOS)
    px = fr.load()
    vals = []
    for y in range(H):
        for x in range(W):
            r, g, b = px[x, y]
            vals.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    frames.append(vals)

with open(OUT, 'w') as f:
    f.write('// Auto-generated — Nyan Cat RGB565 프레임 (런타임 GIF 디코드 우회)\n')
    f.write('// gen: tools/gen_nyan.py · 160x120 → 런타임 2x → 320x240\n')
    f.write('#pragma once\n#include <Arduino.h>\n\n')
    f.write(f'#define NYAN_W {W}\n#define NYAN_H {H}\n')
    f.write(f'#define NYAN_FRAMES {n}\n#define NYAN_DELAY_MS {delay}\n\n')
    f.write('static const uint16_t NYAN[NYAN_FRAMES][NYAN_W * NYAN_H] = {\n')
    for vals in frames:
        f.write('{' + ','.join('0x%04X' % v for v in vals) + '},\n')
    f.write('};\n')

print(f'wrote {OUT}: {n} frames, {W}x{H}, {W*H*n*2} bytes flash')

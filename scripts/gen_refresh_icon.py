#!/usr/bin/env python3
"""Génère refresh.png antialiasé 40x40 pour bouton Vita 48x48 (960x544)."""
import math
from pathlib import Path
from PIL import Image, ImageDraw

OUT = 40
SCALE = 8
S = OUT * SCALE
PI = math.pi

im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(im)
cx = cy = S / 2.0
R = 13.0 * SCALE
thick = 2.6 * SCALE


def stamp_arc(a0, a1, n=80):
    for i in range(n + 1):
        t = i / n
        a = a0 + (a1 - a0) * t
        x = cx + math.cos(a) * R
        y = cy + math.sin(a) * R
        d.ellipse(
            [x - thick, y - thick, x + thick, y + thick],
            fill=(255, 255, 255, 255),
        )


def arrow(ang):
    tip_x = cx + math.cos(ang) * R
    tip_y = cy + math.sin(ang) * R
    tx, ty = -math.sin(ang), math.cos(ang)
    nx, ny = math.cos(ang), math.sin(ang)
    length = 6.5 * SCALE
    width = 4.2 * SCALE
    tip_x += tx * 1.0 * SCALE
    tip_y += ty * 1.0 * SCALE
    base_x = tip_x - tx * length
    base_y = tip_y - ty * length
    pts = [
        (tip_x, tip_y),
        (base_x + nx * width, base_y + ny * width),
        (base_x - nx * width * 0.2, base_y - ny * width * 0.2),
    ]
    d.polygon(pts, fill=(255, 255, 255, 255))


a0, a1 = -0.15 * PI, 0.72 * PI
b0, b1 = 0.85 * PI, 1.72 * PI
stamp_arc(a0, a1)
arrow(a1)
stamp_arc(b0, b1)
arrow(b1)

out = im.resize((OUT, OUT), Image.Resampling.LANCZOS)
root = Path(__file__).resolve().parents[1]
dest = root / "assets" / "refresh.png"
dest.parent.mkdir(parents=True, exist_ok=True)
out.save(dest, optimize=True)
print("OK", dest, out.size)

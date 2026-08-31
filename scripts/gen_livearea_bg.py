#!/usr/bin/env python3
"""Génère bg.png LiveArea 840x500 avec version bien visible bas-gauche."""
from PIL import Image, ImageDraw, ImageFont
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
cm = (root / "CMakeLists.txt").read_text(encoding="utf-8")
m = re.search(r'set\(VITA_VERSION\s+"([^"]+)"\)', cm)
version = m.group(1) if m else "00.00"
text = "Ver. " + version

im = Image.new("RGB", (840, 500), (236, 242, 248))
draw = ImageDraw.Draw(im)
for y in range(500):
    c = int(236 - y * 0.025)
    draw.line([(0, y), (839, y)], fill=(c, min(255, c + 4), min(255, c + 10)))

try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 36)
except Exception:
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 36)
    except Exception:
        font = ImageFont.load_default()

# Pas collé au bord bas : la feuille LiveArea coupe souvent ~40–60 px
x, y = 28, 400
# Pastille claire derrière pour contraste
bbox = draw.textbbox((x, y), text, font=font)
pad = 10
draw.rounded_rectangle(
    [bbox[0] - pad, bbox[1] - pad, bbox[2] + pad, bbox[3] + pad],
    radius=12,
    fill=(255, 255, 255),
)
draw.text((x, y), text, fill=(0, 0, 0), font=font)

dest = root / "sce_sys" / "livearea" / "contents" / "bg.png"
im.save(dest, optimize=True)
print("OK", text, "->", dest)

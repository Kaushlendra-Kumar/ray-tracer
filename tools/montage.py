#!/usr/bin/env python3
"""Builds docs/images/convergence.png from the per-pass snapshots written by
`rt --snapshot-dir` (see tools/render_gallery.sh). Requires Pillow."""
import os
from PIL import Image, ImageDraw

d = os.path.join(os.path.dirname(__file__), "..", "docs", "images", "convergence")
spps = [1, 4, 16, 64, 256, 1024]
tiles = [Image.open(os.path.join(d, f"spp_{s:05d}.png")).convert("RGB") for s in spps]
w, h = tiles[0].size
label_h = 22
sheet = Image.new("RGB", (w * 3, (h + label_h) * 2), (230, 232, 234))
draw = ImageDraw.Draw(sheet)
for i, (s, t) in enumerate(zip(spps, tiles)):
    x, y = (i % 3) * w, (i // 3) * (h + label_h)
    sheet.paste(t, (x, y))
    draw.text((x + 8, y + h + 5), f"{s} sample{'s' if s > 1 else ''} per pixel", fill=(28, 32, 36))
sheet.save(os.path.join(d, "..", "convergence.png"))
print("wrote convergence.png", sheet.size)

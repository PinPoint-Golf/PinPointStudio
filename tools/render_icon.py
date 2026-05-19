#!/usr/bin/env python3
"""
tools/render_icon.py
Render assets/icons/pinpoint.svg into all platform icon formats.

Usage:
    pip install cairosvg Pillow
    python3 tools/render_icon.py          # from project root
"""

import io
import os
import struct
import sys

try:
    import cairosvg
    from PIL import Image
except ImportError:
    sys.exit("Missing dependencies. Run: pip install cairosvg Pillow")

ROOT    = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SVG     = os.path.join(ROOT, "src", "Resources", "icons", "pinpoint.svg")
OUT_DIR = os.path.join(ROOT, "src", "Resources", "icons")


def render_png(size: int) -> Image.Image:
    data = cairosvg.svg2png(url=SVG, output_width=size, output_height=size)
    return Image.open(io.BytesIO(data)).convert("RGBA")


def save_pngs(sizes):
    images = {}
    for s in sizes:
        img = render_png(s)
        path = os.path.join(OUT_DIR, f"pinpoint_{s}.png")
        img.save(path)
        images[s] = img
        print(f"  PNG {s:4}x{s} -> {path}")
    return images


def save_ico(images, sizes):
    """Write a proper ICO with each size embedded as a PNG chunk."""
    path = os.path.join(OUT_DIR, "pinpoint.ico")
    blobs = []
    for s in sizes:
        buf = io.BytesIO()
        images[s].save(buf, format="PNG")
        blobs.append((s, buf.getvalue()))

    count      = len(blobs)
    hdr_size   = 6 + count * 16
    offset     = hdr_size
    ico_header = struct.pack("<HHH", 0, 1, count)
    entries    = []
    for sz, blob in blobs:
        w = sz if sz < 256 else 0
        entries.append(struct.pack("<BBBBHHII", w, w, 0, 0, 1, 32, len(blob), offset))
        offset += len(blob)

    with open(path, "wb") as f:
        f.write(ico_header)
        for e in entries:
            f.write(e)
        for _, blob in blobs:
            f.write(blob)
    print(f"  ICO       -> {path}  ({os.path.getsize(path)} bytes, {count} sizes)")


def save_icns(images):
    """Write a macOS .icns using Pillow."""
    path  = os.path.join(OUT_DIR, "PinPoint.icns")
    sizes = [16, 32, 64, 128, 256, 512, 1024]
    imgs  = []
    for s in sizes:
        if s in images:
            imgs.append(images[s])
        else:
            imgs.append(render_png(s))
    imgs[-1].save(path, format="ICNS", append_images=imgs[:-1])
    print(f"  ICNS      -> {path}  ({os.path.getsize(path)} bytes)")


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    if not os.path.exists(SVG):
        sys.exit(f"SVG not found: {SVG}")

    print(f"Source: {SVG}")
    print("Rendering PNGs...")

    png_sizes  = [16, 32, 48, 64, 128, 256, 512, 1024]
    ico_sizes  = [16, 32, 48, 64, 128, 256]
    images     = save_pngs(png_sizes)

    # Render extra sizes needed for ICNS but not in png_sizes
    for s in [64, 1024]:
        if s not in images:
            images[s] = render_png(s)

    print("Building ICO (Windows)...")
    save_ico(images, ico_sizes)

    print("Building ICNS (macOS)...")
    save_icns(images)

    print("Done.")


if __name__ == "__main__":
    main()

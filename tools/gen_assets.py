#!/usr/bin/env python3
"""Generates the LiveArea images (8-bit indexed PNG, as the Vita expects)."""
from PIL import Image, ImageDraw, ImageFont
import os

OUT = os.path.join(os.path.dirname(__file__), "..", "app", "sce_sys")
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
BG, PANEL, ACCENT, TEXT, OK, WARN = (14, 18, 28), (26, 32, 48), (90, 170, 255), (220, 226, 236), (90, 210, 120), (240, 190, 60)


def font(sz):
    try:
        return ImageFont.truetype(FONT, sz)
    except OSError:
        return ImageFont.load_default()


def pulse(d, x0, y, w, col, width=6):
    """little 'boot heartbeat' line"""
    pts = [(x0, y), (x0 + w * .35, y), (x0 + w * .42, y - w * .18), (x0 + w * .5, y + w * .14),
           (x0 + w * .58, y - w * .06), (x0 + w * .63, y), (x0 + w, y)]
    d.line(pts, fill=col, width=width, joint="curve")


def save(img, path):
    img.convert("RGB").quantize(colors=256).save(path, optimize=True)


def icon():
    im = Image.new("RGB", (128, 128), BG)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((6, 6, 121, 121), 18, fill=PANEL, outline=ACCENT, width=4)
    pulse(d, 16, 66, 96, OK, 7)
    d.text((64, 100), "BOOT", font=font(18), fill=TEXT, anchor="mm")
    d.text((64, 30), "DIAG", font=font(18), fill=ACCENT, anchor="mm")
    save(im, os.path.join(OUT, "icon0.png"))


def bg():
    im = Image.new("RGB", (840, 500), BG)
    d = ImageDraw.Draw(im)
    for x in range(0, 840, 40):
        d.line([(x, 0), (x, 500)], fill=(20, 26, 40))
    for y in range(0, 500, 40):
        d.line([(0, y), (840, y)], fill=(20, 26, 40))
    pulse(d, 60, 300, 720, OK, 8)
    d.text((420, 120), "VitaBootDiag", font=font(56), fill=TEXT, anchor="mm")
    d.text((420, 180), "boot rescue & diagnostics", font=font(24), fill=ACCENT, anchor="mm")
    d.text((420, 440), "ur0:tai  ·  SD2Vita  ·  plugins  ·  crash dumps", font=font(18), fill=WARN, anchor="mm")
    save(im, os.path.join(OUT, "livearea", "contents", "bg.png"))


def startup():
    im = Image.new("RGB", (280, 158), PANEL)
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, 279, 157), outline=ACCENT, width=3)
    pulse(d, 20, 100, 240, OK, 5)
    d.text((140, 40), "VitaBootDiag", font=font(26), fill=TEXT, anchor="mm")
    save(im, os.path.join(OUT, "livearea", "contents", "startup.png"))


if __name__ == "__main__":
    os.makedirs(os.path.join(OUT, "livearea", "contents"), exist_ok=True)
    icon(); bg(); startup()
    print("assets written")

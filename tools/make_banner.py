"""Make the README banner (docs/images/banner.png): original artwork for this
project, using no game assets. A dark ink-wash background with film grain,
a blood-red heartbeat trace that ends in a flatline, worn title lettering and
a few blood specks. Random details use fixed seeds (reproducible).

Usage: python tools/make_banner.py
"""
import math
import random
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
W, H = 1600, 500
SS = 2  # supersampling for the line work
BLOOD = (176, 16, 34)
INK = (232, 224, 222)


def font(names, size, variation=None):
    for name in names:
        try:
            f = ImageFont.truetype(name, size)
            if variation:
                try:
                    f.set_variation_by_name(variation)
                except (OSError, ValueError):
                    pass
            return f
        except OSError:
            continue
    return ImageFont.load_default()


def noise(size, scale, seed):
    """Smooth value noise in 0..255, `scale` pixels per cell."""
    rng = random.Random(seed)
    w, h = max(2, size[0] // scale + 2), max(2, size[1] // scale + 2)
    small = Image.new("L", (w, h))
    small.putdata([rng.randint(0, 255) for _ in range(w * h)])
    return small.resize((w * scale, h * scale), Image.BICUBIC).crop((0, 0, size[0], size[1]))


def background():
    img = Image.new("RGB", (W, H), (6, 3, 6))
    # Ink wash: large soft blots of dark crimson and violet.
    wash = noise((W, H), 220, 1)
    wash2 = noise((W, H), 90, 2)
    tint = Image.merge("RGB", (wash.point(lambda v: int(v * 0.16)), Image.new("L", (W, H), 4),
                               wash2.point(lambda v: int(v * 0.10))))
    img = ImageChops.add(img, tint)
    # Vignette.
    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).ellipse([-W * 0.1, -H * 0.55, W * 1.1, H * 1.55], fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(110))
    img = Image.composite(img, Image.new("RGB", (W, H), (0, 0, 0)), mask)
    # Film grain.
    rng = random.Random(3)
    grain = Image.new("L", (W, H))
    grain.putdata([rng.randint(0, 22) for _ in range(W * H)])
    img = ImageChops.add(img, Image.merge("RGB", (grain, grain, grain)))
    return img


def heartbeat(img):
    """A heartbeat trace across the banner that ends in a flatline."""
    w, h = W * SS, H * SS
    base = 0.70 * h
    pts = []
    beats = [0.10, 0.27, 0.44]          # beat positions (fractions of the width)
    x = 0
    while x <= w:
        t = x / w
        y = base
        for i, b in enumerate(beats):
            d = (t - b) * w / SS
            amp = 1.0 - 0.28 * i       # weaker each beat
            if -14 < d < -4:
                y -= amp * 18 * SS * math.sin((d + 14) / 10 * math.pi)          # P wave
            elif -2 < d < 2:
                y += amp * 22 * SS * (1 - abs(d) / 2)                           # Q
            elif 2 <= d < 9:
                y -= amp * 120 * SS * (1 - abs(d - 5.5) / 3.5)                  # R spike
            elif 9 <= d < 14:
                y += amp * 34 * SS * (1 - abs(d - 11.5) / 2.5)                  # S
            elif 24 < d < 46:
                y -= amp * 24 * SS * math.sin((d - 24) / 22 * math.pi)          # T wave
        pts.append((x, y))
        x += 2
    line = Image.new("L", (w, h), 0)
    ImageDraw.Draw(line).line(pts, fill=255, width=4 * SS, joint="curve")
    # Fade the trace in from the left.
    fade = Image.linear_gradient("L").rotate(90).resize((w, h))
    fade = ImageChops.invert(fade).point(lambda v: min(255, int(v * 3)))
    line = ImageChops.multiply(line, fade).resize((W, H), Image.LANCZOS)
    glow = line.filter(ImageFilter.GaussianBlur(9))
    out = img.convert("RGBA")
    for layer, color, alpha in ((glow, BLOOD, 1.0), (line, (226, 40, 58), 1.0)):
        c = Image.new("RGBA", (W, H), color + (0,))
        c.putalpha(layer.point(lambda v: int(v * alpha)))
        out = Image.alpha_composite(out, c)
    return out


def worn_text(img, text, f, center, fill, spacing, seed, wear=0.38):
    """Letter-spaced text whose coverage is eaten away by noise (worn print)."""
    layer = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(layer)
    widths = [d.textlength(ch, font=f) for ch in text]
    total = sum(widths) + spacing * (len(text) - 1)
    x = center[0] - total / 2
    for ch, cw in zip(text, widths):
        d.text((x, center[1]), ch, font=f, fill=255, anchor="lm")
        x += cw + spacing
    wear_mask = noise((W, H), 3, seed).point(lambda v: 0 if v < 255 * wear * 0.55 else 255)
    scratches = noise((W, H), 26, seed + 1).point(lambda v: 0 if v < 255 * wear * 0.35 else 255)
    layer = ImageChops.multiply(layer, ImageChops.multiply(wear_mask, scratches))
    c = Image.new("RGBA", (W, H), fill + (0,))
    c.putalpha(layer)
    return Image.alpha_composite(img, c)


def specks(img, seed):
    rng = random.Random(seed)
    layer = Image.new("L", (W * SS, H * SS), 0)
    d = ImageDraw.Draw(layer)
    for cx, cy, n, spread in ((1330, 120, 26, 70), (250, 410, 16, 50), (1180, 360, 10, 40)):
        for _ in range(n):
            a = rng.uniform(0, 2 * math.pi)
            r = spread * rng.random() ** 0.6
            s = rng.uniform(1.0, 6.5) * (1.4 if r < spread * 0.3 else 1.0)
            x, y = (cx + r * math.cos(a)) * SS, (cy + r * math.sin(a)) * SS
            d.ellipse([x - s * SS, y - s * SS, x + s * SS, y + s * SS], fill=255)
    layer = layer.resize((W, H), Image.LANCZOS)
    c = Image.new("RGBA", (W, H), BLOOD + (0,))
    c.putalpha(layer.point(lambda v: int(v * 0.85)))
    return Image.alpha_composite(img, c)


def main():
    img = heartbeat(background())
    img = specks(img, 7)
    title = font(["bahnschrift.ttf", "arialbd.ttf", "DejaVuSans-Bold.ttf"], 132, "Bold Condensed")
    small = font(["bahnschrift.ttf", "arial.ttf", "DejaVuSans.ttf"], 34, "SemiLight SemiCondensed")
    img = worn_text(img, "CORPSE PARTY 2", small, (W / 2, 118), (200, 184, 186), 14, 11, wear=0.0)
    img = worn_text(img, "DEAD PATIENT", title, (W / 2, 205), INK, 10, 12)
    img = worn_text(img, "iOS  ·  native arm64 recompilation", small, (W / 2, 430), (186, 172, 176), 3, 13,
                    wear=0.0)
    out = ROOT / "docs/images/banner.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    img.convert("RGB").save(out, optimize=True)
    print(out, out.stat().st_size)


if __name__ == "__main__":
    main()

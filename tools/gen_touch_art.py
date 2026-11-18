"""Draw the on-screen touch-control artwork and embed it as PNG bytes.

Original artwork for this port, styled after the game's title lettering:
rough white dry-brush strokes with ink flecks and a few blood-red specks,
on dark translucent discs. The joystick base has an open brush circle
(enso) with four direction ticks; its knob is a smaller ink disc. Buttons
are round, with brushed icons. Everything is drawn with Pillow at 4x and
downsampled for clean edges; random details use fixed seeds, so the output
is reproducible.

Geometry shared with the drawing code (Classes/iOSBridge.mm), as fractions
of each texture's side:
  DISC = 0.36   button, knob and base disc radius

Outputs:
  build/touch_art/*.png                    previews
  direct/runtime/touch_art_data.hpp        PNG bytes for the iOS bridge
Usage: python tools/gen_touch_art.py
"""
import io
import math
import random
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

SS = 4                       # supersampling
DISC = 0.36                  # disc radius / texture side
INK = (238, 230, 226)        # the title's off-white brush colour
BLOOD = (150, 14, 30)
FILL_INNER = (34, 12, 18, 175)
FILL_OUTER = (12, 4, 8, 205)


def disc(draw, c, r, fill):
    draw.ellipse([c[0] - r, c[1] - r, c[0] + r, c[1] + r], fill=fill)


def smooth_noise(rng, n, scale):
    """n values of 1D value noise with features about `scale` samples wide."""
    knots = [rng.uniform(-1, 1) for _ in range(n // max(1, scale) + 3)]
    out = []
    for i in range(n):
        f = i / max(1, scale)
        k = int(f)
        t = f - k
        t = t * t * (3 - 2 * t)
        out.append(knots[k] * (1 - t) + knots[k + 1] * t)
    return out


def brush(mask, path, width, seed, dry=0.35, taper=0.18):
    """A dry-brush stroke along `path` (a list of points) into an L mask.

    The stroke is a bundle of bristles across its width. Each bristle runs
    the length of the stroke with gaps that grow towards the end (the brush
    runs dry), the width swells and tapers, and the edges wobble.
    """
    rng = random.Random(seed)
    draw = ImageDraw.Draw(mask)
    # Resample the path evenly.
    pts = [path[0]]
    for a, b in zip(path, path[1:]):
        seg = math.dist(a, b)
        steps = max(1, int(seg / (width * 0.06)))
        for s in range(1, steps + 1):
            t = s / steps
            pts.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
    n = len(pts)
    bristles = 22
    wobble = smooth_noise(rng, n, max(4, n // 6))
    for k in range(bristles):
        across = (k + 0.5) / bristles - 0.5            # -0.5 .. 0.5
        edge = abs(across) * 2                          # 0 centre, 1 edge
        r = width / bristles * rng.uniform(0.9, 1.6)
        gaps = smooth_noise(rng, n, max(3, n // rng.randint(6, 14)))
        start = rng.uniform(0, 0.06) + edge * 0.05
        for i, p in enumerate(pts):
            t = i / max(1, n - 1)
            if t < start:
                continue
            # Dry brush: more gaps near the end and on the outer bristles.
            dryness = dry * (0.25 + 1.6 * t ** 1.6) * (0.6 + edge)
            if gaps[i] < -1 + 2 * min(0.95, dryness) and t > 0.12:
                continue
            swell = min(1.0, t / taper) ** 0.5 * min(1.0, (1 - t) / taper) ** 0.35
            if i + 1 < n:
                dx, dy = pts[i + 1][0] - p[0], pts[i + 1][1] - p[1]
            else:
                dx, dy = p[0] - pts[i - 1][0], p[1] - pts[i - 1][1]
            ln = math.hypot(dx, dy) or 1.0
            nx, ny = -dy / ln, dx / ln
            off = across * width * (0.55 + 0.45 * swell) + wobble[i] * width * 0.07
            q = (p[0] + nx * off, p[1] + ny * off)
            disc(draw, q, r * (0.6 + 0.4 * swell), 255)


def flecks(mask, center, radius, count, size, seed, spread=(0.95, 1.35)):
    """Ink flecks scattered around a ring, like the title's splatter."""
    rng = random.Random(seed)
    draw = ImageDraw.Draw(mask)
    for _ in range(count):
        a = rng.uniform(0, 2 * math.pi)
        d = radius * rng.uniform(*spread)
        r = size * rng.uniform(0.25, 1.0) ** 1.5
        disc(draw, (center[0] + d * math.cos(a), center[1] + d * math.sin(a)), r, 255)


def arc_path(c, r, start, sweep, steps=180, jitter=0.0, seed=0):
    rng = random.Random(seed)
    out = []
    for i in range(steps + 1):
        a = start + sweep * i / steps
        rr = r * (1 + jitter * math.sin(a * 3 + rng.uniform(0, 0.2)))
        out.append((c[0] + rr * math.cos(a), c[1] + rr * math.sin(a)))
    return out


def paint(canvas, mask, color, alpha=255):
    layer = Image.new("RGBA", canvas.size, color + (0,))
    layer.putalpha(mask.point(lambda v: v * alpha // 255))
    return Image.alpha_composite(canvas, layer)


def ink_disc(N, c, R, seed):
    """The dark translucent disc behind a control, with a soft ragged rim."""
    canvas = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    small = 96
    g = Image.new("RGBA", (small, small), (0, 0, 0, 0))
    px = g.load()
    s = N / small
    for y in range(small):
        for x in range(small):
            t = min(1.0, math.hypot(x - c[0] / s, y - c[1] / s) / (R / s))
            px[x, y] = tuple(int(FILL_INNER[i] + (FILL_OUTER[i] - FILL_INNER[i]) * t) for i in range(4))
    g = g.resize((N, N), Image.BICUBIC)
    mask = Image.new("L", (N, N), 0)
    disc(ImageDraw.Draw(mask), c, R, 255)
    mask = mask.filter(ImageFilter.GaussianBlur(N * 0.006))
    canvas.paste(g, (0, 0), mask)
    return canvas


def icon_paths(kind, c, r):
    """Brush paths (lists of points) for an icon of radius r around c."""
    if kind == "ring":
        return [arc_path(c, r * 0.78, math.radians(-70), math.radians(330), jitter=0.03, seed=3)]
    if kind == "cross":
        d = r * 0.68
        return [[(c[0] - d, c[1] - d), (c[0] + d * 1.02, c[1] + d * 0.96)],
                [(c[0] + d * 0.95, c[1] - d * 1.02), (c[0] - d * 0.98, c[1] + d)]]
    if kind == "triangle":
        d = r * 0.78
        top = (c[0], c[1] - d * 0.92)
        left, right = (c[0] - d * 0.9, c[1] + d * 0.62), (c[0] + d * 0.92, c[1] + d * 0.6)
        return [[left, top, right], [(right[0] + d * 0.05, right[1] + d * 0.02), (left[0] - d * 0.04, left[1])]]
    if kind == "bars":
        out = []
        for k in (-1, 0, 1):
            y = c[1] + k * r * 0.56
            out.append([(c[0] - r * 0.72, y + r * 0.03 * k), (c[0] + r * 0.74, y - r * 0.04)])
        return out
    if kind == "chevrons":
        out = []
        for k in (-0.42, 0.42):
            x = c[0] + k * r
            out.append([(x - r * 0.36, c[1] - r * 0.66), (x + r * 0.34, c[1]), (x - r * 0.36, c[1] + r * 0.66)])
        return out
    raise ValueError(kind)


ICON_WIDTH = {"ring": 0.30, "cross": 0.30, "triangle": 0.24, "bars": 0.22, "chevrons": 0.24}


def button(kind, size, seed):
    N = size * SS
    c = (N / 2, N / 2)
    R = DISC * N
    canvas = ink_disc(N, c, R, seed)
    # The rim: an open brush circle around the disc.
    rim = Image.new("L", (N, N), 0)
    brush(rim, arc_path(c, R * 0.92, math.radians(rng_angle(seed)), math.radians(318), jitter=0.015, seed=seed),
          R * 0.15, seed + 1, dry=0.85, taper=0.3)
    flecks(rim, c, R, 14, R * 0.035, seed + 2)
    canvas = paint(canvas, rim, INK, 225)
    # A few blood specks.
    blood = Image.new("L", (N, N), 0)
    flecks(blood, c, R, 6, R * 0.04, seed + 3, spread=(0.9, 1.2))
    canvas = paint(canvas, blood, BLOOD, 235)
    # The icon.
    ic = Image.new("L", (N, N), 0)
    for i, path in enumerate(icon_paths(kind, c, R * 0.62)):
        brush(ic, path, R * ICON_WIDTH[kind], seed + 10 + i, dry=0.3, taper=0.12)
    canvas = paint(canvas, ic, INK, 255)
    return canvas.resize((size, size), Image.LANCZOS)


def rng_angle(seed):
    return random.Random(seed * 7919).uniform(0, 360)


def press_glow(size):
    """A blood-red halo drawn behind a pressed button or the held knob."""
    N = size * SS
    c = (N / 2, N / 2)
    R = DISC * N
    mask = Image.new("L", (N, N), 0)
    d = ImageDraw.Draw(mask)
    disc(d, c, R * 1.2, 190)
    disc(d, c, R * 0.8, 0)
    mask = mask.filter(ImageFilter.GaussianBlur(N * 0.035))
    glow = Image.new("RGBA", (N, N), (196, 22, 44, 0))
    glow.putalpha(mask)
    return glow.resize((size, size), Image.LANCZOS)


def stick_base(size, seed=40):
    N = size * SS
    c = (N / 2, N / 2)
    R = DISC * N
    canvas = ink_disc(N, c, R, seed)
    lines = Image.new("L", (N, N), 0)
    # Enso: one open brush circle.
    brush(lines, arc_path(c, R * 0.9, math.radians(205), math.radians(325), jitter=0.02, seed=seed),
          R * 0.11, seed + 1, dry=0.85, taper=0.3)
    # A faint inner ring where the knob rests.
    faint = Image.new("L", (N, N), 0)
    brush(faint, arc_path(c, R * 0.42, math.radians(30), math.radians(300), seed=seed + 2), R * 0.03, seed + 3,
          dry=0.7)
    # Four direction ticks: short brushed arrowheads.
    for k in range(4):
        a = k * math.pi / 2
        ca, sa = math.cos(a), math.sin(a)
        tip, back, side = R * 0.78, R * 0.64, R * 0.11
        pts = [(c[0] + back * ca - side * sa, c[1] + back * sa + side * ca), (c[0] + tip * ca, c[1] + tip * sa),
               (c[0] + back * ca + side * sa, c[1] + back * sa - side * ca)]
        brush(lines, pts, R * 0.06, seed + 10 + k, dry=0.25, taper=0.2)
    flecks(lines, c, R, 22, R * 0.03, seed + 5)
    canvas = paint(canvas, faint, INK, 110)
    canvas = paint(canvas, lines, INK, 215)
    blood = Image.new("L", (N, N), 0)
    flecks(blood, c, R, 8, R * 0.035, seed + 6, spread=(0.92, 1.25))
    canvas = paint(canvas, blood, BLOOD, 230)
    return canvas.resize((size, size), Image.LANCZOS)


def stick_knob(size, seed=60):
    N = size * SS
    c = (N / 2, N / 2)
    R = DISC * N
    canvas = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    # Drop shadow.
    sh = Image.new("L", (N, N), 0)
    disc(ImageDraw.Draw(sh), (c[0], c[1] + R * 0.08), R * 1.02, 150)
    sh = sh.filter(ImageFilter.GaussianBlur(N * 0.03))
    canvas = paint(canvas, sh, (0, 0, 0), 255)
    # A solid ink body with a dark crimson core.
    body = Image.new("L", (N, N), 0)
    disc(ImageDraw.Draw(body), c, R, 255)
    g = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    gd = ImageDraw.Draw(g)
    for i in range(40, 0, -1):
        t = i / 40
        col = tuple(int(a + (b - a) * t) for a, b in zip((92, 14, 30), (22, 6, 10))) + (235,)
        disc(gd, (c[0] - R * 0.18 * (1 - t), c[1] - R * 0.22 * (1 - t)), R * t, col)
    canvas.paste(g, (0, 0), body)
    rim = Image.new("L", (N, N), 0)
    brush(rim, arc_path(c, R * 0.86, math.radians(rng_angle(seed)), math.radians(335), jitter=0.02, seed=seed),
          R * 0.14, seed + 1, dry=0.45)
    canvas = paint(canvas, rim, INK, 235)
    return canvas.resize((size, size), Image.LANCZOS)


def stick_glow(size):
    """A blood-red wedge glow towards the right (rotated for other directions)."""
    N = size * SS
    c = (N / 2, N / 2)
    R = DISC * N
    mask = Image.new("L", (N, N), 0)
    d = ImageDraw.Draw(mask)
    d.pieslice([c[0] - R * 0.98, c[1] - R * 0.98, c[0] + R * 0.98, c[1] + R * 0.98], -24, 24, fill=200)
    disc(d, c, R * 0.45, 0)
    mask = mask.filter(ImageFilter.GaussianBlur(N * 0.02))
    glow = Image.new("RGBA", (N, N), (200, 24, 46, 0))
    glow.putalpha(mask)
    return glow.resize((size, size), Image.LANCZOS)


BUTTONS = {
    # name: (icon, seed)
    "ok": ("ring", 11),
    "cancel": ("cross", 22),
    "action": ("triangle", 33),
    "menu": ("bars", 44),
    "run": ("chevrons", 55),
}


def main():
    root = Path(__file__).resolve().parent.parent
    out = root / "build" / "touch_art"
    out.mkdir(parents=True, exist_ok=True)
    images = {"stick_base": stick_base(384), "stick_knob": stick_knob(192), "stick_glow": stick_glow(384),
              "press_glow": press_glow(192)}
    for name, (kind, seed) in BUTTONS.items():
        images[name] = button(kind, 192, seed)
    blobs = {}
    for name, img in images.items():
        img.save(out / (name + ".png"))
        buf = io.BytesIO()
        img.save(buf, "PNG", optimize=True)
        blobs[name] = buf.getvalue()
    # A preview of the layout over a game-like background, at
    # iPhone 16 Pro landscape proportions (half size).
    w, h = 1311, 603
    sheet = Image.new("RGBA", (w, h), (52, 26, 44, 255))
    shade = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shade)
    for i in range(0, h, 3):
        sd.line([(0, i), (w, i)], fill=(90 + (i * 37) % 40, 40, 80, 40))
    sheet = Image.alpha_composite(sheet, shade)
    u = min(w, h)

    def place(img, x, y, radius, alpha=1.0):
        side = int(radius / DISC)
        im = img.resize((side, side), Image.LANCZOS)
        if alpha < 1.0:
            im.putalpha(im.getchannel("A").point(lambda v: int(v * alpha)))
        sheet.alpha_composite(im, (int(x - side / 2), int(y - side / 2)))

    margin, sr = 0.045 * u + 88, 0.15 * u
    sx, sy = margin + sr, h - 0.045 * u - sr
    place(images["stick_base"], sx, sy, sr, 0.85)
    place(images["stick_glow"], sx, sy, sr)
    place(images["press_glow"], sx + sr * 0.5, sy, sr * 0.38)
    place(images["stick_knob"], sx + sr * 0.5, sy, sr * 0.38)
    r, spread = 0.06 * u, 0.105 * u
    cx, cy = w - 88 - 0.045 * u - r - spread, h - 0.045 * u - r - spread
    for name, (x, y) in {"ok": (cx + spread, cy), "cancel": (cx, cy + spread), "action": (cx - spread, cy),
                         "menu": (cx, cy - spread)}.items():
        if name == "ok":
            place(images["press_glow"], x, y, r * 1.06)
        place(images[name], x, y, r * (1.06 if name == "ok" else 1.0), 1.0 if name == "ok" else 0.8)
    small = 0.045 * u
    place(images["run"], w - 88 - 0.045 * u - small, 0.045 * u + small, small, 0.8)
    sheet.save(out / "preview.png")

    lines = ["#pragma once",
             "// Touch-control artwork (PNG bytes), generated by tools/gen_touch_art.py.",
             "// Original artwork for this port. Do not edit by hand.",
             "#include <cstddef>",
             "",
             "namespace cp2 {",
             "namespace touch_art {",
             "",
             "// Disc radius as a fraction of each texture's side.",
             "static const float kDisc = %.2ff;" % DISC,
             ""]
    for name, data in blobs.items():
        lines.append("static const unsigned char %s_png[] = {" % name)
        for i in range(0, len(data), 24):
            lines.append("    " + ", ".join("0x%02x" % b for b in data[i:i + 24]) + ",")
        lines.append("};")
        lines.append("static const std::size_t %s_png_size = sizeof(%s_png);" % (name, name))
        lines.append("")
    lines += ["}  // namespace touch_art", "}  // namespace cp2", ""]
    (root / "direct" / "runtime" / "touch_art_data.hpp").write_text("\n".join(lines), encoding="ascii", newline="\n")
    print({k: len(v) for k, v in blobs.items()})


if __name__ == "__main__":
    main()

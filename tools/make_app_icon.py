"""Make the app icons from direct/ios/art/app-icon-source.png (the game's key
art, supplied by the user for this private port).

Icons are opaque squares (iOS rounds the corners). The template's files
(icon57/58/120, part of the Xcode project) are replaced in place; the other
sizes go to direct/ios/art/AppIcon/ and are copied into the app by
tools/build_ios.py, so the Xcode project needs no new file references.
Usage: python tools/make_app_icon.py
"""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "direct/ios/art/app-icon-source.png"
PROJECT = ROOT / "direct/ios/hspproj"
EXTRA = ROOT / "direct/ios/art/AppIcon"

# Files in the Xcode project (replaced in place).
PROJECT_ICONS = {"icon57.png": 57, "icon58.png": 58, "icon120.png": 120}
# Copied into the bundle at build time: 60pt @3x, 29pt @3x, 40pt @2x/@3x,
# and the 1024 marketing size.
EXTRA_ICONS = {"icon180.png": 180, "icon87.png": 87, "icon80.png": 80, "icon1024.png": 1024}


def main():
    image = Image.open(SOURCE).convert("RGB")
    side = min(image.size)
    left, top = (image.width - side) // 2, (image.height - side) // 2
    image = image.crop((left, top, left + side, top + side))
    EXTRA.mkdir(parents=True, exist_ok=True)
    for name, size in PROJECT_ICONS.items():
        image.resize((size, size), Image.LANCZOS).save(PROJECT / name, optimize=True)
    for name, size in EXTRA_ICONS.items():
        image.resize((size, size), Image.LANCZOS).save(EXTRA / name, optimize=True)
    print("icons:", sorted(list(PROJECT_ICONS) + list(EXTRA_ICONS)))


if __name__ == "__main__":
    main()

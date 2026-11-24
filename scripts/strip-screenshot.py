# Makes docs/screenshot.png: the taskbar strip beside a few tray icons, from the demo devices.
#   python scripts/strip-screenshot.py [KJ-BatteryHub.exe]
# Runs `KJ-BatteryHub.exe --demo --strip-shot <temp file>` (it works beside a running copy and quits by itself),
# then puts the strip on a dark Windows 11 taskbar at 200% with the tray's chevron, network and volume icons
# drawn from Segoe Fluent Icons. Needs Pillow.
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(ROOT, "KJ-BatteryHub.exe")
OUT = os.path.join(ROOT, "docs", "screenshot.png")
BAR = (28, 28, 28, 255)       # dark Windows 11 taskbar
HEIGHT = 96                   # 48 px taskbar at 200%
TRAY = ["\uE70E", "\uE839", "\uE995"]   # chevron up, network, volume
ICON_FONT = os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "Fonts", "SegoeIcons.ttf")


def load_bmp(path):
    """32-bit top-down BMP with premultiplied alpha (see widget::SaveStripShot) -> RGBA image."""
    data = open(path, "rb").read()
    offset = int.from_bytes(data[10:14], "little")
    width = int.from_bytes(data[18:22], "little", signed=True)
    height = abs(int.from_bytes(data[22:26], "little", signed=True))
    return Image.frombuffer("RGBa", (width, height), data[offset:offset + width * height * 4], "raw", "BGRa", 0, 1).convert("RGBA")


def main():
    bmp = os.path.join(tempfile.mkdtemp(prefix="bh-strip-"), "strip.bmp")
    subprocess.run([EXE, "--demo", "--strip-shot", bmp], timeout=60)
    strip = load_bmp(bmp)
    font = ImageFont.truetype(ICON_FONT, 32)
    slot = 72                                  # one tray icon's width at 200%
    width = 24 + strip.width + 24 + slot * len(TRAY) + 12
    sheet = Image.new("RGBA", (width, HEIGHT), BAR)
    sheet.alpha_composite(strip, (24, (HEIGHT - strip.height) // 2))
    draw = ImageDraw.Draw(sheet)
    x = 24 + strip.width + 24
    for glyph in TRAY:
        l, t, r, b = draw.textbbox((0, 0), glyph, font=font)
        draw.text((x + (slot - (r - l)) / 2 - l, (HEIGHT - (b - t)) / 2 - t), glyph, font=font, fill=(255, 255, 255, 255))
        x += slot
    sheet.convert("RGB").save(OUT, optimize=True)
    print(OUT, sheet.size, os.path.getsize(OUT), "bytes")


if __name__ == "__main__":
    main()

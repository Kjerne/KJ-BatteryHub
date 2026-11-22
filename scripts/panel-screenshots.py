# Makes docs/screenshots/panel.png: the panel's tabs side by side in one picture, from the demo devices.
#   python scripts/panel-screenshots.py [BatteryHub.exe]
# Runs `BatteryHub.exe --demo --panel-shots <temp folder>` (it works beside a running copy and quits by
# itself), then joins the pictures on a transparent background. Needs Pillow.
import os
import subprocess
import sys
import tempfile
import time

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(ROOT, "BatteryHub.exe")
OUT = os.path.join(ROOT, "docs", "screenshots", "panel.png")
SHOTS = ["panel-devices", "panel-device-actions", "panel-history", "panel-settings-2"]
SCALE = 0.6   # the shots are drawn at 150%; 0.6 of that is 90%, sharp and still small
GAP = 24


def load_bmp(path):
    """32-bit top-down BMP with premultiplied alpha (see panel::SaveShots) -> RGBA image."""
    data = open(path, "rb").read()
    offset = int.from_bytes(data[10:14], "little")
    width = int.from_bytes(data[18:22], "little", signed=True)
    height = abs(int.from_bytes(data[22:26], "little", signed=True))
    return Image.frombuffer("RGBa", (width, height), data[offset:offset + width * height * 4], "raw", "BGRa", 0, 1).convert("RGBA")


def main():
    folder = tempfile.mkdtemp(prefix="bh-shots-")
    subprocess.Popen([EXE, "--demo", "--panel-shots", folder])
    last = os.path.join(folder, "panel-settings-4.bmp")
    for _ in range(60):
        if os.path.exists(last):
            break
        time.sleep(0.5)
    time.sleep(1)
    images = [load_bmp(os.path.join(folder, name + ".bmp")) for name in SHOTS]
    images = [im.resize((round(im.width * SCALE), round(im.height * SCALE)), Image.LANCZOS) for im in images]
    width = sum(im.width for im in images) + GAP * (len(images) - 1)
    height = max(im.height for im in images)
    sheet = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    x = 0
    for im in images:
        sheet.alpha_composite(im, (x, 0))
        x += im.width + GAP
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    sheet.save(OUT, optimize=True)
    print(OUT, sheet.size, os.path.getsize(OUT), "bytes")


if __name__ == "__main__":
    main()

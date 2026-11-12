"""Generate the Proxmark3GUI application icon (SVG source + PNG + ICO).

One geometry definition drives the SVG (maintainable source) and the raster
sizes, so the two cannot drift apart.  Sizes <= 24 px use a simplified variant
(thicker USB stub, one thick antenna ring, solid chips) so the silhouette stays
readable at 16x16.

Usage:
    python scripts/make-icons.py
"""

from __future__ import annotations

import os
import struct
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "resources" / "icons"
S = 1024  # master canvas

# Palette
BG_TOP = (36, 48, 63)
BG_BOTTOM = (12, 16, 22)
BORDER = (58, 74, 94)
PCB = (26, 92, 66)
PCB_EDGE = (72, 152, 112)
COPPER = (214, 146, 54)
COPPER_DARK = (168, 108, 36)
CHIP = (12, 16, 22)
CHIP_PIN = (198, 170, 96)
USB = (200, 208, 218)
USB_EDGE = (118, 126, 136)
USB_SLOT = (84, 92, 102)
SILK = (208, 224, 216)


def _background() -> Image.Image:
    top, bottom = BG_TOP, BG_BOTTOM
    grad = Image.new("RGB", (S, S))
    px = grad.load()
    for y in range(S):
        t = y / (S - 1)
        color = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(S):
            px[x, y] = color
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([24, 24, S - 25, S - 25], radius=210, fill=255)
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    out.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(out)
    d.rounded_rectangle([24, 24, S - 25, S - 25], radius=210, outline=BORDER, width=6)
    return out


def _draw(compact: bool) -> Image.Image:
    img = _background()
    d = ImageDraw.Draw(img)

    if compact:
        # Small sizes: fewer, bolder shapes so the silhouette survives 16x16.
        d.rounded_rectangle([96, 282, 892, 742], radius=70, fill=(46, 139, 87),
                            outline=(124, 200, 154), width=18)
        d.rounded_rectangle([38, 412, 216, 612], radius=30, fill=USB,
                            outline=USB_EDGE, width=12)
        d.rounded_rectangle([516, 328, 852, 696], radius=96,
                            outline=(232, 163, 60), width=84)
        d.rounded_rectangle([248, 370, 452, 502], radius=14, fill=CHIP)
        d.rounded_rectangle([248, 550, 396, 652], radius=14, fill=CHIP)
        return img

    # Main PCB with a light edge.
    d.rounded_rectangle([168, 298, 856, 726], radius=46, fill=PCB,
                        outline=PCB_EDGE, width=8)
    # USB-C style connector on the left.
    d.rounded_rectangle([92, 440, 214, 584], radius=22, fill=USB,
                        outline=USB_EDGE, width=6)
    d.rounded_rectangle([116, 486, 172, 538], radius=9, fill=USB_SLOT)
    # Antenna coil: three concentric copper loops on the right half.
    for inset, width in ((0, 20), (44, 18), (88, 16)):
        d.rounded_rectangle([556 + inset, 356 + inset, 802 - inset, 668 - inset],
                            radius=max(30, 66 - inset // 2),
                            outline=COPPER, width=width)
    # Two chips with pins.
    d.rounded_rectangle([278, 392, 414, 494], radius=10, fill=CHIP)
    for x in range(300, 404, 26):
        d.line([x, 384, x, 400], fill=CHIP_PIN, width=6)
        d.line([x, 486, x, 502], fill=CHIP_PIN, width=6)
    d.rounded_rectangle([278, 548, 372, 634], radius=10, fill=CHIP)
    for x in range(296, 360, 24):
        d.line([x, 540, x, 552], fill=CHIP_PIN, width=5)
    # Traces to the coil.
    d.line([414, 444, 520, 444], fill=COPPER_DARK, width=9)
    d.line([520, 444, 556, 480], fill=COPPER_DARK, width=9)
    d.line([372, 590, 500, 590], fill=COPPER_DARK, width=9)
    d.line([500, 590, 556, 620], fill=COPPER_DARK, width=9)
    # Status LED and silkscreen marks.
    d.ellipse([786, 322, 818, 354], fill=SILK)
    d.rounded_rectangle([236, 640, 330, 652], radius=6, fill=SILK)
    return img


def _svg() -> str:
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<!-- Proxmark3GUI application icon. Original artwork, no third-party logo. -->
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {S} {S}" width="256" height="256">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#24303F"/>
      <stop offset="1" stop-color="#0C1016"/>
    </linearGradient>
  </defs>
  <rect x="24" y="24" width="{S - 48}" height="{S - 48}" rx="210" fill="url(#bg)"
        stroke="#3A4A5E" stroke-width="6"/>
  <rect x="168" y="298" width="688" height="428" rx="46" fill="#1A5C42"
        stroke="#489870" stroke-width="8"/>
  <rect x="92" y="440" width="122" height="144" rx="22" fill="#C8D0DA"
        stroke="#767E88" stroke-width="6"/>
  <rect x="116" y="486" width="56" height="52" rx="9" fill="#545C66"/>
  <g fill="none" stroke="#D69236">
    <rect x="556" y="356" width="246" height="312" rx="66" stroke-width="20"/>
    <rect x="600" y="400" width="158" height="224" rx="48" stroke-width="18"/>
    <rect x="644" y="444" width="70" height="136" rx="30" stroke-width="16"/>
  </g>
  <g fill="#0C1016">
    <rect x="278" y="392" width="136" height="102" rx="10"/>
    <rect x="278" y="548" width="94" height="86" rx="10"/>
  </g>
  <g stroke="#C6AA60" stroke-width="6">
    <path d="M300 384V400 M326 384V400 M352 384V400 M378 384V400"/>
    <path d="M300 486V502 M326 486V502 M352 486V502 M378 486V502"/>
    <path d="M296 540V552 M320 540V552 M344 540V552"/>
  </g>
  <g stroke="#A86C24" stroke-width="9" stroke-linecap="round">
    <path d="M414 444H520L556 480"/>
    <path d="M372 590H500L556 620"/>
  </g>
  <circle cx="802" cy="338" r="16" fill="#D0E0D8"/>
  <rect x="236" y="640" width="94" height="12" rx="6" fill="#D0E0D8"/>
</svg>
"""


def _ico(png_paths: list[tuple[int, Path]], target: Path) -> None:
    """Write an ICO with one PNG-compressed frame per size (Vista+)."""
    frames = []
    for size, path in png_paths:
        data = path.read_bytes()
        frames.append((size, data))

    header = struct.pack("<HHH", 0, 1, len(frames))
    offset = 6 + 16 * len(frames)
    directory = b""
    payload = b""
    for size, data in frames:
        width = 0 if size >= 256 else size
        height = 0 if size >= 256 else size
        directory += struct.pack("<BBBBHHII", width, height, 0, 0, 1, 32,
                                 len(data), offset + len(payload))
        payload += data
    target.write_bytes(header + directory + payload)


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "app-icon.svg").write_text(_svg(), encoding="utf-8")

    master = _draw(compact=False)
    compact = _draw(compact=True)

    sizes = [16, 24, 32, 48, 64, 128, 256]
    ico_frames = []
    for size in sizes:
        source = compact if size <= 32 else master
        image = source.resize((size, size), Image.LANCZOS)
        path = OUT / ("app-icon-%d.png" % size)
        image.save(path)
        ico_frames.append((size, path))

    master.resize((512, 512), Image.LANCZOS).save(OUT / "app-icon.png")
    _ico(ico_frames, OUT / "app-icon.ico")
    print("written:", ", ".join(sorted(p.name for p in OUT.iterdir())))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


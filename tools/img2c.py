#!/usr/bin/env python3
"""Convert PNG/JPG to ESP32 RGB565 C array.

Usage:
  python img2c.py input.png var_name [width height] [--bgr] [--no-swap]

Options:
  --bgr      Use BGR565 instead of RGB565 (swap R and B channels)
  --no-swap  Disable byte-swap (default: swap bytes for ESP32 SPI LCD)

Examples:
  python img2c.py fail_bg.png capture_fail_bg 240 240
  python img2c.py paw.png asset_icon_paw 32 32
"""
import sys
from PIL import Image
import numpy as np

def convert(input_path, var_name, width, height, bgr=False, swap_bytes=True):
    img = Image.open(input_path).resize((width, height)).convert("RGB")
    arr = np.array(img, dtype=np.uint16)
    r = arr[:,:,0] >> 3
    g = arr[:,:,1] >> 2
    b = arr[:,:,2] >> 3
    if bgr:
        rgb565 = (b << 11) | (g << 5) | r
    else:
        rgb565 = (r << 11) | (g << 5) | b
    if swap_bytes:
        rgb565 = ((rgb565 & 0x00FF) << 8) | ((rgb565 & 0xFF00) >> 8)

    out_path = f"{var_name}.c"
    with open(out_path, "w") as f:
        f.write(f'#include "assets/assets.h"\n\n')
        f.write(f"const uint16_t {var_name}[{width} * {height}] = {{\n    ")
        flat = rgb565.flatten()
        for i, v in enumerate(flat):
            f.write(f"0x{v:04X},")
            if (i + 1) % 16 == 0:
                f.write("\n    ")
        f.write("\n};\n")
    print(f"Written {out_path} ({width}x{height}, swap={swap_bytes}, bgr={bgr})")

if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = [a for a in sys.argv[1:] if a.startswith("--")]
    if len(args) < 2:
        print(__doc__)
        sys.exit(1)
    w = int(args[2]) if len(args) > 2 else 240
    h = int(args[3]) if len(args) > 3 else 240
    convert(args[0], args[1], w, h,
            bgr="--bgr" in flags,
            swap_bytes="--no-swap" not in flags)

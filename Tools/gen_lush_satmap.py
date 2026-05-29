"""Generate a lush green satmap as a PNG.

Layered value noise blended across a small palette of green tones, with a
warm/cool large-scale gradient and a faint detail layer to break up flatness.
"""

import argparse
import numpy as np
from PIL import Image


def value_noise(size: int, cell: int, rng: np.random.Generator) -> np.ndarray:
    """Upscaled-bilinear value noise. Returns float32 in [0,1]."""
    low = max(2, size // cell)
    grid = rng.random((low, low), dtype=np.float32)
    img = Image.fromarray((grid * 255).astype(np.uint8))
    img = img.resize((size, size), Image.BILINEAR)
    return np.asarray(img, dtype=np.float32) / 255.0


def fbm(size: int, octaves: int, rng: np.random.Generator) -> np.ndarray:
    out = np.zeros((size, size), dtype=np.float32)
    amp = 1.0
    cell = 8
    total = 0.0
    for _ in range(octaves):
        out += value_noise(size, cell, rng) * amp
        total += amp
        amp *= 0.5
        cell = max(2, cell // 2)
    out /= total
    # gentle contrast stretch
    lo, hi = np.percentile(out, [2, 98])
    return np.clip((out - lo) / max(1e-6, hi - lo), 0.0, 1.0)


def blend_palette(t: np.ndarray, stops):
    """t in [0,1] -> RGB float [0,1]. stops = list of (pos, (r,g,b))."""
    stops = sorted(stops, key=lambda s: s[0])
    h, w = t.shape
    out = np.zeros((h, w, 3), dtype=np.float32)
    for i in range(len(stops) - 1):
        p0, c0 = stops[i]
        p1, c1 = stops[i + 1]
        mask = (t >= p0) & (t <= p1)
        if not mask.any():
            continue
        local = (t[mask] - p0) / max(1e-6, p1 - p0)
        for ch in range(3):
            out[..., ch][mask] = c0[ch] + (c1[ch] - c0[ch]) * local
    out[t <= stops[0][0]] = stops[0][1]
    out[t >= stops[-1][0]] = stops[-1][1]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=4096)
    ap.add_argument("--seed", type=int, default=0xC0FFEE)
    ap.add_argument("--out", default="Assets/Terrain/satmap_lush.png")
    args = ap.parse_args()

    rng = np.random.default_rng(args.seed)
    size = args.size

    base = fbm(size, octaves=6, rng=rng)
    detail = fbm(size, octaves=3, rng=rng)
    warmth = fbm(size, octaves=2, rng=rng)  # slow regional shift

    # mix: dominant base + 25% detail bump
    t = np.clip(base * 0.8 + detail * 0.2, 0.0, 1.0)

    # lush green palette (sRGB-ish 0..1)
    palette = [
        (0.00, (0.02, 0.05, 0.02)),  # deep shadow under canopy
        (0.25, (0.04, 0.10, 0.03)),  # dark forest
        (0.55, (0.09, 0.18, 0.06)),  # healthy grass
        (0.80, (0.15, 0.24, 0.09)),  # bright meadow
        (1.00, (0.21, 0.30, 0.12)),  # sun-bleached highlight
    ]
    rgb = blend_palette(t, palette)

    # warm/cool large-scale tint: yellow-green vs blue-green
    tint_warm = np.array([0.05, 0.03, -0.04], dtype=np.float32)
    tint_cool = np.array([-0.04, 0.00, 0.04], dtype=np.float32)
    w3 = warmth[..., None]
    rgb = rgb + (tint_warm * w3 + tint_cool * (1.0 - w3)) * 0.25

    # subtle high-freq grain so the result isn't slick
    grain = (rng.random((size, size), dtype=np.float32) - 0.5) * 0.015
    rgb = rgb + grain[..., None]

    rgb = np.clip(rgb, 0.0, 1.0)
    out = (rgb * 255.0 + 0.5).astype(np.uint8)
    Image.fromarray(out, "RGB").save(args.out, optimize=True)
    print(f"wrote {args.out} ({size}x{size})")


if __name__ == "__main__":
    main()

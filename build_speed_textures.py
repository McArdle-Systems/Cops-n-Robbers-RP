"""
Generate the radar screen's speed-readout textures.

Produces 23 PNGs (one per speed bucket) into Assets/Models/SpeedRadarV2/Textures/:
  speed_dash.png         — "—"   (no target)
  speed_000.png          — "0"
  speed_010.png          — "10"
  ... through ...
  speed_200.png          — "200"
  speed_over.png         — "OVER"

Each PNG is 256x128, black background, bright orange digits centered.
After running:
  1. In Workbench, drag-drop the Textures/ folder into Resource Browser.
     Each PNG is converted to .edds with a fresh GUID.
  2. Tell Claude the texture GUIDs (or just paste the .meta files) so
     the matching .emat variants can be generated.

Requires Python 3 and Pillow:
  pip install pillow
"""

import os
from PIL import Image, ImageDraw, ImageFont

OUT_DIR = r"C:\Users\bluej\Documents\My Games\ArmaReforgerWorkbench\addons\cops-n-robbers-rp\Assets\Models\SpeedRadarV2\Textures"
SIZE = (256, 128)
BG_COLOR = (0, 0, 0, 255)         # black
FG_COLOR = (255, 80, 0, 255)      # bright orange (matches existing screen emissive tint)
FONT_PT = 92                       # tuned to roughly fill 256x128

# Try to find a monospaced "digital" looking font; fall back to default.
FONT_CANDIDATES = [
    r"C:\Windows\Fonts\consolab.ttf",   # Consolas Bold
    r"C:\Windows\Fonts\cour.ttf",        # Courier
    r"C:\Windows\Fonts\arialbd.ttf",     # Arial Bold (fallback)
]

def get_font():
    for path in FONT_CANDIDATES:
        if os.path.exists(path):
            return ImageFont.truetype(path, FONT_PT)
    print("[warn] No system font found, using PIL default (very small).")
    return ImageFont.load_default()

def render(label, filename, font):
    img = Image.new("RGBA", SIZE, BG_COLOR)
    draw = ImageDraw.Draw(img)
    # Measure & center
    bbox = draw.textbbox((0, 0), label, font=font)
    w = bbox[2] - bbox[0]
    h = bbox[3] - bbox[1]
    x = (SIZE[0] - w) // 2 - bbox[0]
    y = (SIZE[1] - h) // 2 - bbox[1]
    draw.text((x, y), label, fill=FG_COLOR, font=font)
    out_path = os.path.join(OUT_DIR, filename)
    img.save(out_path)
    print(f"  wrote {out_path}")

def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    font = get_font()

    print(f"=== Generating speed textures into {OUT_DIR} ===")
    render("—",     "speed_dash.png", font)
    for kmh in range(0, 210, 10):
        render(str(kmh), f"speed_{kmh:03d}.png", font)
    render("OVER",  "speed_over.png", font)

    print("\nDone. 23 files written.")
    print("Next: in Workbench, drag Textures/ into Resource Browser to import.")

if __name__ == "__main__":
    main()

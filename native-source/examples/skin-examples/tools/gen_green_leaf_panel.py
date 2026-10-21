"""Generate the light-theme nine-slice panel of the bundled "green_leaf" (绿叶) skin.

The dark panel (assets/panel.png) is the original canvas sample; this draws its light counterpart
with the same geometry -- 64x64 2x asset, 20 px slices, a 3 px rim on a 16 px corner radius and a
small diamond in each corner slice -- so both share one [candidate_window.border_image] table.

Requires Pillow (pip install pillow). From the repository root:
    python examples/skin-examples/tools/gen_green_leaf_panel.py skins/green_leaf/assets
"""

import sys
from pathlib import Path

from PIL import Image, ImageDraw

SS = 4  # supersampling factor
W = H = 64
RADIUS = 16


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def panel_light(out):
    s = SS
    img = Image.new("RGBA", (W * s, H * s), (0, 0, 0, 0))
    # rim: the whole rounded rectangle in leaf green, the fill is drawn over it 3 px in
    rim = (52, 178, 136, 255)
    ImageDraw.Draw(img).rounded_rectangle([0, 0, W * s - 1, H * s - 1], radius=RADIUS * s, fill=rim)
    # pale mint glass, a touch lighter at the top
    grad = Image.new("RGBA", img.size)
    gd = ImageDraw.Draw(grad)
    top, bottom = (250, 254, 251, 238), (238, 249, 243, 238)
    for y in range(img.size[1]):
        gd.line([(0, y), (img.size[0], y)], fill=lerp(top, bottom, y / img.size[1]))
    inset = 3
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [inset * s, inset * s, (W - inset) * s - 1, (H - inset) * s - 1], radius=(RADIUS - inset) * s, fill=255
    )
    img.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(img)
    # soft inner band under the rim, like the dark panel's glow
    d.rounded_rectangle(
        [inset * s, inset * s, (W - inset) * s - 1, (H - inset) * s - 1],
        radius=(RADIUS - inset) * s,
        outline=(168, 226, 200, 200),
        width=1 * s,
    )
    # corner diamonds, centred at (9, 9) like the dark panel's, inside the 20 px slices
    gold = (226, 172, 46, 255)
    for cx, cy in [(9, 9), (W - 9, 9), (9, H - 9), (W - 9, H - 9)]:
        r = 3.2
        d.polygon(
            [((cx) * s, (cy - r) * s), ((cx + r) * s, cy * s), (cx * s, (cy + r) * s), ((cx - r) * s, cy * s)],
            fill=gold,
        )
    img.resize((W, H), Image.LANCZOS).save(out / "panel-light.png", optimize=True)


if __name__ == "__main__":
    target = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    target.mkdir(parents=True, exist_ok=True)
    panel_light(target)

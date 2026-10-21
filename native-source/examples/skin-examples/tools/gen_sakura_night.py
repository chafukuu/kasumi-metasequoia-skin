"""Generate the "sakura-night" schema_version 2 demo skin for Metasequoia IME.

All artwork is procedural (Pillow), drawn at 4x and downsampled for anti-aliasing.
Every bitmap is a 2x asset: the manifest's *_dip sizes are half the pixel sizes.

Requires Pillow (pip install pillow). From examples/skin-examples:
    python tools/gen_sakura_night.py skins/sakura-night
"""

import math
import random
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

SS = 4  # supersampling factor


def canvas(w, h):
    return Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))


def finish(img):
    w, h = img.size
    return img.resize((w // SS, h // SS), Image.LANCZOS)


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def sakura_flower(draw, cx, cy, r, petal, center, rot=0.0):
    """Five notched petals around a small center, in supersampled coordinates."""
    for k in range(5):
        ang = rot + k * 2 * math.pi / 5
        px = cx + math.cos(ang) * r * 0.55
        py = cy + math.sin(ang) * r * 0.55
        pr = r * 0.48
        draw.ellipse([px - pr, py - pr, px + pr, py + pr], fill=petal)
        # notch at the petal tip
        nx = cx + math.cos(ang) * r * 1.02
        ny = cy + math.sin(ang) * r * 1.02
        nr = r * 0.14
        draw.ellipse([nx - nr, ny - nr, nx + nr, ny + nr], fill=(0, 0, 0, 0))
    cr = r * 0.22
    draw.ellipse([cx - cr, cy - cr, cx + cr, cy + cr], fill=center)


def panel(out):
    """96x96 nine-slice panel; slices are 24 px (12 DIP), corner flowers live inside the slices."""
    W = H = 96
    img = canvas(W, H)
    s = SS
    # vertical gradient fill clipped to a rounded rectangle
    grad = Image.new("RGBA", img.size)
    gd = ImageDraw.Draw(grad)
    top, bottom = (34, 22, 58, 240), (22, 16, 44, 240)
    for y in range(img.size[1]):
        gd.line([(0, y), (img.size[0], y)], fill=lerp(top, bottom, y / img.size[1]))
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, W * s - 1, H * s - 1], radius=20 * s, fill=255)
    img.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(img)
    # outer rim and a faint inner highlight
    d.rounded_rectangle([1 * s, 1 * s, W * s - 1 * s - 1, H * s - 1 * s - 1], radius=19 * s,
                        outline=(255, 170, 205, 150), width=2 * s)
    d.rounded_rectangle([4 * s, 4 * s, W * s - 4 * s - 1, H * s - 4 * s - 1], radius=16 * s,
                        outline=(255, 255, 255, 28), width=1 * s)
    # corner flowers: entirely within the 24 px corner slices, so they never stretch
    gold, pink = (255, 214, 140, 235), (255, 160, 200, 235)
    for (cx, cy, col, rot) in [(15, 15, gold, -0.3), (W - 15, 15, pink, 0.2),
                                (15, H - 15, pink, 0.5), (W - 15, H - 15, gold, 0.9)]:
        sakura_flower(d, cx * s, cy * s, 6.5 * s, col, (255, 245, 220, 255), rot)
    finish(img).save(out / "panel.png", optimize=True)


def moon(out):
    """144x144 moon with a soft halo; drawn behind the panel (z_index < 1)."""
    W = H = 144
    s = SS
    halo = canvas(W, H)
    hd = ImageDraw.Draw(halo)
    c = W * s / 2
    for i in range(28, 0, -1):
        r = (40 + i * 1.1) * s
        hd.ellipse([c - r, c - r, c + r, c + r], fill=(255, 210, 120, int(10 + (28 - i) * 2.2)))
    body = canvas(W, H)
    bd = ImageDraw.Draw(body)
    r = 40 * s
    bd.ellipse([c - r, c - r, c + r, c + r], fill=(255, 226, 140, 255))
    for (dx, dy, rr, a) in [(-12, -8, 8, 70), (10, 12, 11, 60), (14, -15, 5, 70), (-8, 17, 5, 55)]:
        bd.ellipse([c + dx * s - rr * s, c + dy * s - rr * s, c + dx * s + rr * s, c + dy * s + rr * s],
                   fill=(214, 168, 90, a))
    img = Image.alpha_composite(halo.filter(ImageFilter.GaussianBlur(6 * s)), body)
    finish(img).save(out / "moon.png", optimize=True)


def petal_sprite(size, color):
    s = SS
    p = Image.new("RGBA", (size * s, size * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(p)
    d.ellipse([size * 0.18 * s, 0, size * 0.82 * s, size * s - 1], fill=color)
    d.ellipse([size * 0.42 * s, -size * 0.12 * s, size * 0.58 * s, size * 0.12 * s], fill=(0, 0, 0, 0))
    return p


def petals(out):
    """96x160, 24 frames x 70 ms looping APNG of falling petals (front layer)."""
    W, H, N = 96, 160, 24
    rnd = random.Random(11)
    specs = []
    for _ in range(10):
        specs.append(dict(x0=rnd.uniform(10, W - 14), y0=rnd.uniform(0, H), size=rnd.randint(12, 18),
                          sway=rnd.uniform(6, 14), phase=rnd.uniform(0, 2 * math.pi),
                          spin=rnd.choice([-1, 1]) * rnd.uniform(0.6, 1.4),
                          color=lerp((255, 190, 215, 230), (255, 140, 185, 230), rnd.random())))
    frames = []
    for f in range(N):
        t = f / N  # one full fall per loop keeps the animation seamless
        img = canvas(W, H)
        for sp in specs:
            y = (sp["y0"] + t * (H + 20)) % (H + 20) - 20
            x = sp["x0"] + math.sin(sp["phase"] + t * 2 * math.pi) * sp["sway"]
            angle = math.degrees(sp["phase"] + sp["spin"] * t * 2 * math.pi)
            sprite = petal_sprite(sp["size"], sp["color"]).rotate(angle, resample=Image.BICUBIC, expand=True)
            fade = min(1.0, (y + 20) / 30, (H - y) / 30)
            if fade <= 0:
                continue
            if fade < 1:
                a = sprite.getchannel("A").point(lambda v: int(v * fade))
                sprite.putalpha(a)
            img.alpha_composite(sprite, (int(x * SS - sprite.size[0] / 2), int(y * SS - sprite.size[1] / 2)))
        frames.append(finish(img))
    frames[0].save(out / "petals.apng", format="PNG", save_all=True, append_images=frames[1:], duration=70,
                   loop=0, disposal=1, blend=0, optimize=True)


def stars(out):
    """80x40, 8 frames x 150 ms looping GIF of twinkling stars (front layer, shows GIF support)."""
    W, H, N = 80, 40, 8
    rnd = random.Random(3)
    pts = [(rnd.uniform(6, W - 6), rnd.uniform(6, H - 6), rnd.uniform(2.5, 5), rnd.uniform(0, 2 * math.pi))
           for _ in range(6)]
    frames = []
    for f in range(N):
        img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        for (x, y, r, ph) in pts:
            k = 0.35 + 0.65 * (0.5 + 0.5 * math.sin(ph + f * 2 * math.pi / N))
            rr = r * k
            # four-point star
            d.polygon([(x, y - rr * 2), (x + rr * 0.45, y - rr * 0.45), (x + rr * 2, y), (x + rr * 0.45, y + rr * 0.45),
                       (x, y + rr * 2), (x - rr * 0.45, y + rr * 0.45), (x - rr * 2, y), (x - rr * 0.45, y - rr * 0.45)],
                      fill=(255, 226, 150, 255))
        # GIF has 1-bit transparency: threshold alpha, then quantize with a reserved transparent index
        alpha = img.getchannel("A").point(lambda v: 255 if v >= 128 else 0)
        rgb = Image.new("RGB", (W, H), (0, 0, 0))
        rgb.paste(img.convert("RGB"), mask=alpha)
        p = rgb.quantize(colors=255)
        p.paste(255, mask=alpha.point(lambda v: 255 if v == 0 else 0))
        frames.append(p)
    frames[0].save(out / "stars.gif", save_all=True, append_images=frames[1:], duration=150, loop=0,
                   transparency=255, disposal=2, optimize=False)


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    out = Path(sys.argv[1]) / "assets"
    out.mkdir(parents=True, exist_ok=True)
    panel(out)
    moon(out)
    petals(out)
    stars(out)
    for f in sorted(out.iterdir()):
        print(f"{f.name}\t{f.stat().st_size} bytes")


if __name__ == "__main__":
    main()

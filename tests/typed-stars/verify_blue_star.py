"""Verify that the blue asset changes only the existing magenta star's RGB."""

import hashlib
import json
import sys
from pathlib import Path

from PIL import Image


def verify(skin_root):
    root = Path(skin_root)
    source = root / "assets/star_m.png"
    target = root / "assets/star_b.png"
    with Image.open(source) as original, Image.open(target) as blue:
        if original.format != "PNG" or blue.format != "PNG":
            raise ValueError("Both star images must be PNG")
        a, b = original.convert("RGBA"), blue.convert("RGBA")
        if a.size != b.size or a.size != (114, 136):
            raise ValueError("Star dimensions changed")
        red, green, blue_channel, alpha = a.split()
        expected = Image.merge("RGBA", (green, blue_channel, red, alpha))
        if b.tobytes() != expected.tobytes():
            raise ValueError("Blue star differs outside the exact RGB recolor")
        if b.tobytes() == a.tobytes():
            raise ValueError("Blue star was not recolored")
        return {
            "passed": True,
            "size": list(a.size),
            "alphaByteIdentical": a.getchannel("A").tobytes() == b.getchannel("A").tobytes(),
            "recolor": "(R,G,B,A) -> (G,B,R,A)",
            "sourceSha256": hashlib.sha256(source.read_bytes()).hexdigest().upper(),
            "blueSha256": hashlib.sha256(target.read_bytes()).hexdigest().upper(),
        }


if __name__ == "__main__":
    print(json.dumps(verify(sys.argv[1]), ensure_ascii=False))

"""Validate the documented skin package contract and all shipped asset references."""
from pathlib import Path
import os
import re
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
# MSIME_SKINS_DIR points the same checks at another skin collection, e.g. the repository's skins/.
SKINS_DIR = Path(os.environ.get("MSIME_SKINS_DIR", ROOT / "skins")).resolve()
BUILTIN_SKINS = ("fluent", "wechat", "wechat_scroll", "graphite", "willow_green", "autumn_osmanthus", "microsoft")
# Mirrors the Server catalog: community bases shared with msime, adapted onto fluent at load time.
SHARED_BASES = ("system", "shuishan", "light", "paper", "night", "ink")
# The full-texture base: only a base for schema_version 2 skins, never a skin of its own.
CANVAS_BASE = "canvas"
# Images v2 tables may reference (border_image.source, decorations.image).
V2_IMAGE_SUFFIXES = (".png", ".apng", ".gif", ".webp")


class ManifestTests(unittest.TestCase):
    def test_shipped_skins(self):
        manifests = list(SKINS_DIR.glob("*/skin.toml"))
        self.assertTrue(manifests, "No skin packages found")
        for path in manifests:
            with self.subTest(skin=path.parent.name):
                data = tomllib.loads(path.read_text(encoding="utf-8"))
                self.assertIn(data["schema_version"], (1, 2))
                self.assertEqual(data["id"], path.parent.name)
                self.assertRegex(data["id"], r"^[a-z0-9._-]{1,64}$")
                # Mirrors the Server catalog: name, version and base are required, author is not.
                for key in ("name", "version", "base"):
                    self.assertTrue(data[key].strip(), key)
                self.assertIn(data["base"], BUILTIN_SKINS + SHARED_BASES + (CANVAS_BASE,))
                # Mirrors the Server catalog: canvas needs schema 2, and schema 2 needs canvas (otherwise the
                # catalog loads the skin as schema 1 and ignores every v2 table).
                self.assertEqual(data["schema_version"] == 2, data["base"] == CANVAS_BASE, "schema 2 <=> canvas")
                window = data.get("candidate_window", {})
                assets = {key: data.get(key) for key in ("toolbar_stylesheet", "preview")}
                for table in ("decoration", "background"):
                    if table in window:
                        assets[f"candidate_window.{table}.image"] = window[table]["image"]
                v2_images = []
                if "border_image" in window:
                    assets["candidate_window.border_image.source"] = window["border_image"]["source"]
                    v2_images.append(window["border_image"]["source"])
                    if "light_source" in window["border_image"]:
                        light_source = window["border_image"]["light_source"]
                        assets["candidate_window.border_image.light_source"] = light_source
                        v2_images.append(light_source)
                    for key in ("slice_dip", "stretch_dip", "width_dip"):
                        if key in window["border_image"]:
                            values = window["border_image"][key]
                            self.assertIn(len(values), (1, 2, 4), key)
                            self.assertTrue(all(0 <= v <= 500 for v in values), key)
                    self.assertIsInstance(window["border_image"].get("fill", True), bool, "fill")
                decorations = window.get("decorations", [])
                self.assertLessEqual(len(decorations), 32, "decorations")
                for i, dec in enumerate(decorations):
                    if "image" in dec:
                        assets[f"candidate_window.decorations[{i}].image"] = dec["image"]
                        v2_images.append(dec["image"])
                    self.assertIn(dec.get("anchor", "right"), ("top-left", "top-right", "bottom-left", "bottom-right",
                                                               "left", "right", "top", "bottom", "center"))
                    self.assertIsInstance(dec.get("z_index", 2), int, "z_index")
                    self.assertTrue(-1000 <= dec.get("z_index", 2) <= 1000, "z_index")
                    for key in ("offset_x_dip", "offset_y_dip"):
                        self.assertTrue(-2000 <= dec.get(key, 0) <= 2000, key)
                for image in v2_images:
                    self.assertTrue(image.lower().endswith(V2_IMAGE_SUFFIXES), image)
                if "decoration" in window:
                    for key in ("top_inset_dip", "width_dip"):
                        self.assertGreater(window["decoration"][key], 0, key)
                for key, rel in assets.items():
                    if rel is None:
                        continue
                    asset = (path.parent / rel).resolve()
                    self.assertTrue(asset.is_relative_to(path.parent.resolve()), key)
                    self.assertTrue(asset.is_file(), f"Missing {key}: {asset}")
                    if asset.suffix.lower() in V2_IMAGE_SUFFIXES:
                        self.assertLessEqual(asset.stat().st_size, 500 * 1024, key)
                for scope in ("candidate_window", "toolbar"):
                    if "corner_radius_dip" in data.get(scope, {}):
                        self.assertTrue(0 <= data[scope]["corner_radius_dip"] <= 32, scope)
                if "background" in window:
                    self.assertTrue(0 <= window["background"].get("opacity", 1) <= 1)
                for key, limit in (("border_width_dip", 4), ("item_corner_radius_dip", 16)):
                    if key in window:
                        self.assertTrue(0 <= window[key] <= limit, key)
                if "shadow" in window:
                    self.assertIn(window["shadow"], ("none", "soft", "strong"))
                if "backdrop" in window:
                    self.assertIn(window["backdrop"], ("none", "acrylic", "mica"))
                if "page_arrows" in window:
                    self.assertIsInstance(window["page_arrows"], bool, "page_arrows")
                if "fit_content" in window:
                    self.assertIsInstance(window["fit_content"], bool, "fit_content")
                if "font_family" in window:
                    # Mirrors the Server catalog: the name is pasted into a quoted CSS font-family value.
                    self.assertRegex(window["font_family"], r"^[^\x00-\x1f\x7f\"'\\,;{}<>`]+$")
                    self.assertLessEqual(len(window["font_family"].encode("utf-8")), 64)
                for theme, colors in data.get("candidate", {}).items():
                    if "menu" in colors:
                        self.assertIsInstance(colors["menu"], dict, f"candidate.{theme}.menu")
                    for key in ("backdrop_opacity", "backdrop_luminosity"):
                        if key in colors:
                            self.assertTrue(0 <= colors[key] <= 1, f"candidate.{theme}.{key}")
                for theme in data["supports"]["themes"]:
                    self.assertIn(theme, data["candidate"])
                    if "toolbar" in data:
                        self.assertIn(theme, data["toolbar"])
                    # The toolbar's drag handle is the theme's accent, like the selection bar and caret; the
                    # catalog fills it in from accent when a skin leaves it out.
                    accent = data["candidate"][theme].get("accent")
                    handle = data.get("toolbar", {}).get(theme, {}).get("handle")
                    if accent and handle:
                        self.assertEqual(handle.lower(), accent.lower(), f"toolbar.{theme}.handle")

    def test_default_skin_settings(self):
        # skins/default/<id>/skin.toml is the factory manifest of each built-in skin and of the canvas base: the
        # switches plus the whole look. Only collections that ship it are checked.
        default_dir = SKINS_DIR / "default"
        if not default_dir.is_dir():
            self.skipTest("No default skin settings in this collection")
        manifests = {path.parent.name: path for path in default_dir.glob("*/skin.toml")}
        self.assertEqual(sorted(manifests), sorted(BUILTIN_SKINS + (CANVAS_BASE,)))
        for skin_id, path in manifests.items():
            with self.subTest(skin=skin_id):
                data = tomllib.loads(path.read_text(encoding="utf-8"))
                self.assertEqual(data["schema_version"], 1)
                self.assertEqual(data["id"], skin_id)
                window = data.get("candidate_window", {})
                for switch in ("page_arrows", "fit_content", "expand_panel"):
                    self.assertIsInstance(window.get(switch, True), bool, switch)
                # The renderer has no built-in look left to fall back on, so every factory manifest spells out
                # both themes, the menu and the toolbar.
                for theme in ("dark", "light"):
                    colors = data["candidate"][theme]
                    for key in ("surface", "border", "text", "number", "accent", "selected", "hover"):
                        self.assertIn(key, colors, f"candidate.{theme}.{key}")
                    self.assertIn("menu", colors, f"candidate.{theme}.menu")
                    self.assertIn(theme, data["toolbar"], f"toolbar.{theme}")
                    accent = colors["accent"]
                    self.assertEqual(data["toolbar"][theme]["handle"].lower(), accent.lower(), f"toolbar.{theme}.handle")


if __name__ == "__main__":
    unittest.main()

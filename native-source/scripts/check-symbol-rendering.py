#!/usr/bin/env python3
"""用 Windows 自带字体检查 msime-dictionary 的符号源，把显示不了的符号写进 sources/symbols/wanxiang-unrenderable.txt。

build-dictionary.py 构建 others.db 时按这份清单剔除符号，构建本身不读字体，结果与机器无关；
换了符号源或想按新的 Windows 字体重新判定时，在 Windows 上重跑本脚本并评审清单的变化。

判定规则（逐条，命中第一条即记下）：
    invisible    只有格式字符、空白或组合附加符号，上屏后看不见或浮在前一个字上
    private-use  含私用区码位，显示什么取决于碰巧装了哪种字体
    no-glyph     C:\\Windows\\Fonts 下（不算 Noto 系列这类非系统自带的字体）没有任何字体有它的字形；
                 emoji 序列改用 Segoe UI Emoji 排版判定
    split        Segoe UI Emoji 不能把序列合成一个字形（国旗、部分 ZWJ 组合），会显示成几个字符

需要 fonttools 与 uharfbuzz：python -m pip install fonttools uharfbuzz

用法：
    python scripts/check-symbol-rendering.py                      # 默认读写与本仓同级的 msime-dictionary
    python scripts/check-symbol-rendering.py --dictionary D:/msime-dictionary
"""

from __future__ import annotations

import argparse
import os
import platform
import subprocess
import sys
import unicodedata
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from dictionary_build import build, symbols_db, textutil  # noqa: E402

FONTS = Path(os.environ.get("WINDIR", r"C:\Windows")) / "Fonts"
# 不随 Windows 安装的字体：只出现在装过它们的机器上，不能拿来证明「Windows 能显示」。
NON_SYSTEM_PREFIXES = ("noto",)
IGNORABLE = {0x200D, 0xFE0E, 0xFE0F}
EMOJI_MARKERS = {0x200D, 0xFE0F, 0x20E3}


def load_fonts():
    try:
        import uharfbuzz as hb
        from fontTools.ttLib import TTCollection, TTFont
    except ImportError as error:
        raise SystemExit("需要 fonttools 与 uharfbuzz：python -m pip install fonttools uharfbuzz") from error
    coverage: set[int] = set()
    used = 0
    for path in sorted(FONTS.iterdir()):
        suffix = path.suffix.lower()
        if suffix not in (".ttf", ".otf", ".ttc") or path.name.lower().startswith(NON_SYSTEM_PREFIXES):
            continue
        try:
            fonts = TTCollection(path, lazy=True).fonts if suffix == ".ttc" else [TTFont(path, lazy=True)]
            for font in fonts:
                coverage.update((font.getBestCmap() or {}).keys())
            used += 1
        except Exception:  # noqa: BLE001 - 坏字体文件跳过，不影响其余判定
            continue
    emoji_path = FONTS / "seguiemj.ttf"
    emoji_ttf = TTFont(emoji_path)
    emoji_font = hb.Font(hb.Face(hb.Blob.from_file_path(str(emoji_path))))
    version = emoji_ttf["head"].fontRevision
    return hb, coverage, used, emoji_ttf.getBestCmap(), emoji_font, f"{version:.2f}"


def classify(symbol: str, hb, coverage: set[int], emoji_cmap: dict, emoji_font) -> str | None:
    categories = [unicodedata.category(c) for c in symbol]
    if all(cat in ("Cf", "Zs", "Zl", "Zp", "Cc", "Mn", "Me") for cat in categories):
        return "invisible"
    if "Co" in categories:
        return "private-use"
    codepoints = [ord(c) for c in symbol]
    emoji = any(cp in EMOJI_MARKERS or 0x1F1E6 <= cp <= 0x1F1FF for cp in codepoints) or (
        any(cp >= 0x1F000 for cp in codepoints) and all(cp in emoji_cmap or cp in IGNORABLE for cp in codepoints))
    if emoji:
        buffer = hb.Buffer()
        buffer.add_str(symbol)
        buffer.guess_segment_properties()
        hb.shape(emoji_font, buffer, {})
        if any(info.codepoint == 0 for info in buffer.glyph_infos):
            return "no-glyph"
        visible = sum(1 for position in buffer.glyph_positions if position.x_advance > 0)
        return "split" if visible > 1 else None
    if any(cp not in coverage and cp not in IGNORABLE for cp in codepoints):
        return "no-glyph"
    return None


def git_head(path: Path) -> str:
    try:
        commit = subprocess.run(["git", "-C", str(path), "rev-parse", "HEAD"], capture_output=True, text=True,
                                check=True).stdout.strip()
        dirty = subprocess.run(["git", "-C", str(path), "status", "--porcelain", "--", "scripts"], capture_output=True,
                               text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"
    return commit + ("-dirty" if dirty else "")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dictionary", type=Path, default=build.ROOT.parent / "msime-dictionary")
    args = parser.parse_args()
    dictionary = args.dictionary.resolve()

    groups = symbols_db.parse_wanxiang(textutil.read(dictionary / symbols_db.WANXIANG))
    codex = symbols_db.parse_codex(textutil.read(dictionary / symbols_db.CODEX))
    sources: dict[str, list[str]] = {}
    for group in groups:
        for symbol in group.items:
            sources.setdefault(symbol, []).append(f"/{group.code}")
    for name, symbol in codex:
        sources.setdefault(symbol, []).append(f"codex:{name}")

    hb, coverage, used, emoji_cmap, emoji_font, emoji_version = load_fonts()
    lines = []
    counts: dict[str, int] = {}
    for symbol, where in sources.items():
        reason = classify(symbol, hb, coverage, emoji_cmap, emoji_font)
        if reason is None:
            continue
        counts[reason] = counts.get(reason, 0) + 1
        codepoints = " ".join(f"U+{ord(c):04X}" for c in symbol)
        shown = symbol if reason != "invisible" else ""
        lines.append(f"{codepoints}\t{shown}\t{reason}\t{' '.join(dict.fromkeys(where))}")

    header = [
        f"# 由 MSIME-Windows 仓库提交 {git_head(build.ROOT)} 的 scripts/check-symbol-rendering.py 生成，不要手工编辑。",
        f"# 判定环境：Windows {platform.version()}，{used} 个 C:\\Windows\\Fonts 字体（不含 Noto 系列），Segoe UI Emoji {emoji_version}。",
        f"# 检查对象：{symbols_db.WANXIANG} 中入选的符号分组与 {symbols_db.CODEX} 的全部符号，共 {len(sources)} 个不同的符号。",
        "# 判定规则见生成脚本的说明：invisible 看不见，private-use 私用区，no-glyph 系统字体没有字形，split 序列不能合成一个字形。",
        "# build-dictionary.py 构建 others.db 时剔除这里列出的全部符号，它们不进入表情面板，也不进入 S 模式和符号混输。",
        f"# 共 {len(lines)} 个：" + "，".join(f"{name} {count}" for name, count in sorted(counts.items())),
        "# 格式：码位<TAB>符号（invisible 留空）<TAB>原因<TAB>出处（万象分组编码或 codex:名称）",
    ]
    target = dictionary / symbols_db.UNRENDERABLE
    target.write_bytes(("\n".join(header + lines) + "\n").encode("utf-8"))
    print(f"{len(lines)} 个显示不了的符号（{counts}）写入 {target}")


if __name__ == "__main__":
    main()

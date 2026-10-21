"""把构建结果与 msime-dictionary 的某个 dict-v* Release 逐表对比，确认 Python 构建与 Rust 构建器的结果一致。

Release 由 msime 构建器从某个 msime-dictionary 提交（manifest 的 custom_dictionary_commit）构建；
本地 checkout 在同一提交、inputs.lock.json 的 msime 提交与 Release 的 source.commit 相同时，下面每一项都应一致。
"""

from __future__ import annotations

import json
import sqlite3
from contextlib import closing
from pathlib import Path

from . import inputs

REPOSITORY = "metasequoiaime/msime-dictionary"
ASSETS = ("msime-pinyin.db", "msime-wubi.db", "msime-english.db", "msime-others.db", "msime-japanese.dat",
          "msime-mozc_dictionary_oss_README.txt", "msime-mozc_LICENSE.txt", "msime-scowl_Copyright.txt",
          "msime-dictionary-manifest.json")


def fetch_release(tag: str, cache: Path) -> tuple[Path, dict]:
    directory = cache / tag
    directory.mkdir(parents=True, exist_ok=True)
    base = f"https://github.com/{REPOSITORY}/releases/download/{tag}"
    sums_path = directory / "msime-SHA256SUMS.txt"
    inputs.download(f"{base}/msime-SHA256SUMS.txt", sums_path)
    sums = {}
    for line in sums_path.read_text(encoding="utf-8").splitlines():
        if line.strip():
            digest, name = line.split(maxsplit=1)
            sums[name.strip().lstrip("*")] = digest.lower()
    for name in ASSETS:
        path = directory / name
        if path.is_file() and inputs.sha256(path) == sums[name]:
            continue
        print(f"下载 {tag}/{name} ...")
        inputs.download(f"{base}/{name}", path)
        if inputs.sha256(path) != sums[name]:
            raise ValueError(f"{tag}/{name} 的 SHA-256 与 msime-SHA256SUMS.txt 不符")
    return directory, json.loads((directory / "msime-dictionary-manifest.json").read_text(encoding="utf-8"))


def _rows(connection: sqlite3.Connection, schema: str, table: str, order: str) -> list[tuple]:
    columns = [row[1] for row in connection.execute(f'PRAGMA {schema}.table_info("{table}")')]
    return connection.execute(f'SELECT {", ".join(columns)} FROM {schema}."{table}" ORDER BY {order}').fetchall()


def _compare_tables(built: Path, release: Path, tables: list[tuple[str, str]], problems: list[str]) -> int:
    """tables 是 (表名, 排序方式)：有 rowid 的表按 rowid（行序也算内容，五笔同权重按 rowid 排），WITHOUT ROWID 表按主键。"""
    checked = 0
    with closing(sqlite3.connect(f"file:{built}?mode=ro", uri=True)) as connection:
        connection.execute("ATTACH DATABASE ? AS release", (str(release),))
        for table, order in tables:
            ours = _rows(connection, "main", table, order)
            theirs = _rows(connection, "release", table, order)
            checked += 1
            if ours == theirs:
                continue
            ours_set, theirs_set = set(ours), set(theirs)
            only_ours, only_theirs = ours_set - theirs_set, theirs_set - ours_set
            if not only_ours and not only_theirs:
                problems.append(f"{built.name}:{table}: 行内容一致但顺序不同（{len(ours)} 行）")
                continue
            sample = lambda rows: ", ".join(repr(row) for row in sorted(rows)[:3])
            problems.append(f"{built.name}:{table}: 本地 {len(ours)} 行 / Release {len(theirs)} 行；"
                            f"只在本地 {len(only_ours)}（{sample(only_ours)}），只在 Release {len(only_theirs)}（{sample(only_theirs)}）")
    return checked


def compare(built: Path, tag: str, cache: Path, checkout_commit: str, msime_commit: str) -> list[str]:
    release, manifest = fetch_release(tag, cache)
    notes = []
    if manifest.get("custom_dictionary_commit") != checkout_commit:
        notes.append(f"注意：{tag} 读的 msime-dictionary 提交是 {manifest.get('custom_dictionary_commit', '?')[:12]}，"
                     f"本地 checkout 是 {checkout_commit[:12] or '?'}，源数据不同时表不一致是预期的")
    if manifest.get("source", {}).get("commit") != msime_commit:
        notes.append(f"注意：{tag} 由 msime {manifest.get('source', {}).get('commit', '?')[:12]} 构建，"
                     f"inputs.lock.json 固定的是 {msime_commit[:12]}")
    for note in notes:
        print(note)

    problems: list[str] = []
    with closing(sqlite3.connect(f"file:{release / 'msime-pinyin.db'}?mode=ro", uri=True)) as pinyin:
        quanpin = [row[0] for row in pinyin.execute("SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'tbl#_%' ESCAPE '#' ORDER BY name")]
    checked = _compare_tables(built / "msime.db", release / "msime-pinyin.db",
                              [(table, "rowid") for table in quanpin], problems)
    # quick_parases 不比：本地构建有意不带 Release 里的内置快捷短语。
    checked += _compare_tables(built / "msime.db", release / "msime-wubi.db", [("wubi86", "rowid")], problems)
    checked += _compare_tables(built / "english.db", release / "msime-english.db",
                               [("english_words", "word, display"), ("en_zh_glosses", "english"),
                                ("zh_en_glosses", "chinese"), ("source_notices", "source")], problems)
    checked += _compare_tables(built / "others.db", release / "msime-others.db",
                               [("emoji", "emoji"), ("emoji_pinyin", "key, emoji"), ("kaomoji", "pinyin, jianpin, kaomoji"),
                                ("kaomoji_catalog", "kaomoji"), ("symbol_catalog", "symbol, category")], problems)
    with closing(sqlite3.connect(f"file:{built / 'english.db'}?mode=ro", uri=True)) as ours, \
            closing(sqlite3.connect(f"file:{release / 'msime-english.db'}?mode=ro", uri=True)) as theirs:
        if ours.execute("PRAGMA user_version").fetchone() != theirs.execute("PRAGMA user_version").fetchone():
            problems.append("english.db: user_version 不同")
    for ours, theirs in (("dict_japanese.dat", "msime-japanese.dat"),
                         ("mozc_dictionary_oss_README.txt", "msime-mozc_dictionary_oss_README.txt"),
                         ("mozc_LICENSE.txt", "msime-mozc_LICENSE.txt"),
                         ("scowl_Copyright.txt", "msime-scowl_Copyright.txt")):
        checked += 1
        if inputs.sha256(built / ours) != inputs.sha256(release / theirs):
            problems.append(f"{ours} 与 {tag}/{theirs} 字节不同")
    print(f"对比 {tag}：检查了 {checked} 项，{len(problems)} 项不一致")
    return problems

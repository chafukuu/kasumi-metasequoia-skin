"""others.db：表情面板与 E/M 模式读的 emoji 和颜文字（符号在 symbols_db.py）。

对应 Rust 构建器 others.rs / pinyin.rs 的 emoji / kaomoji 阶段。关键词的拼音逐字取：
Rust 用 pinyin crate，这里用 pypinyin 的单字表（同出 pinyin-data），多音字短语读音由 pinyin-overrides.txt 指定。
"""

from __future__ import annotations

import sqlite3

from . import textutil

VS16 = "\N{VARIATION SELECTOR-16}"
# 没有检索意义的英文虚词；有方向意义的（up、down、no、not、on、off、out）故意不在里面。
EN_STOPWORDS = frozenset("""a an the of in at by for with to from into onto upon via and or but nor yet so as if then than
that this these those be is are was were am been being it its all any both each few more most other some such only own
same too very can will just should would could do does did have has had there here which who whom whose""".split())
WORD_PUNCTUATION = ".,:;!?()[]{}<>-/\"'`"


class Pinyin:
    """无声调拼音，形状同 pypinyin 的 lazy_pinyin：每个汉字一项，连续的其他字符合成一项。"""

    def __init__(self, overrides_text: str, name: str):
        try:
            from pypinyin.pinyin_dict import pinyin_dict
            from pypinyin.style._utils import replace_symbol_to_no_symbol
        except ImportError as error:
            raise SystemExit("构建 others.db 需要 pypinyin：python -m pip install pypinyin") from error
        self._readings = pinyin_dict
        self._plain = replace_symbol_to_no_symbol
        self._overrides: dict[str, list[str]] = {}
        for number, line in enumerate(textutil.universal_lines(overrides_text), start=1):
            if not line or line.startswith("#"):
                continue
            keyword, *items = line.split("\t")
            if not keyword or not items or any(not item for item in items):
                raise ValueError(f"{name}:{number}: expected keyword<TAB>item[<TAB>item...]")
            if keyword in self._overrides:
                raise ValueError(f"{name}:{number}: {keyword!r} is listed twice")
            self._overrides[keyword] = items

    def _reading(self, character: str) -> str | None:
        readings = self._readings.get(ord(character))
        if readings is None:
            return None
        return self._plain(readings.split(",")[0]).replace("ü", "v")

    def lazy(self, keyword: str) -> list[str]:
        if keyword in self._overrides:
            return list(self._overrides[keyword])
        items: list[str] = []
        other = ""
        for character in keyword:
            reading = self._reading(character)
            if reading is None:
                other += character
                continue
            if other:
                items.append(other)
                other = ""
            items.append(reading)
        if other:
            items.append(other)
        return items

    def full_and_initials(self, keyword: str) -> tuple[str, str]:
        items = self.lazy(keyword)
        return "".join(items), "".join(item[0] for item in items if item)


def _is_cjk(text: str) -> bool:
    return any(0x4E00 <= ord(c) <= 0x9FFF for c in text)


def _english_word(token: str) -> str | None:
    word = token.strip(WORD_PUNCTUATION)
    return word if word.isalpha() and word not in EN_STOPWORDS else None


def load_keyword_map(text: str, name: str) -> dict[str, list[str]]:
    """`关键词<TAB>条目` 行，按条目首次出现的顺序分组，每个条目下关键词不重复。"""
    mapping: dict[str, list[str]] = {}
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if "\t" not in stripped:
            raise ValueError(f"{name}: expected keyword<TAB>item: {line!r}")
        keyword, item = (part.strip() for part in stripped.split("\t", 1))
        if not keyword or not item:
            continue
        keywords = mapping.setdefault(item, [])
        if keyword not in keywords:
            keywords.append(keyword)
    return mapping


# ---- emoji ----

def read_emoji_catalog(text: str, name: str) -> dict[str, tuple[str, int]]:
    catalog: dict[str, tuple[str, int]] = {}
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        fields = stripped.split("\t")
        if len(fields) != 3:
            raise ValueError(f"{name}: expected emoji, category and order: {line!r}")
        order = textutil.parse_int(fields[2].strip())
        if order is None:
            raise ValueError(f"{name}: bad order {fields[2]!r}")
        catalog[fields[0]] = (fields[1], order)
    return catalog


class _KeywordSource:
    def __init__(self, mapping: dict[str, list[str]]):
        self.mapping = mapping
        self.stripped: dict[str, list[str]] = {}
        for emoji, keywords in mapping.items():
            known = self.stripped.setdefault(emoji.replace(VS16, ""), [])
            known.extend(keyword for keyword in keywords if keyword not in known)

    def keywords(self, emoji: str) -> list[str]:
        """这个 emoji 自己的关键词，再加只差 VS16 的其他写法的关键词。"""
        return self.mapping.get(emoji, []) + self.stripped.get(emoji.replace(VS16, ""), [])


def _dedup(keywords: list[str]) -> list[str]:
    return list(dict.fromkeys(keywords))


def _emoji_search_keys(pinyin: Pinyin, merged: list[str]) -> list[str]:
    keys = []
    for keyword in merged:
        if _is_cjk(keyword):
            full, initials = pinyin.full_and_initials(keyword)
            if full:
                keys.append(full)
            if initials and initials != full:
                keys.append(initials)
        else:
            keys.extend(word for word in map(_english_word, keyword.lower().split()) if word is not None)
    return keys


def emoji_rows(pinyin: Pinyin, catalog: dict[str, tuple[str, int]], zh: dict[str, list[str]],
               en: dict[str, list[str]]) -> tuple[list[tuple], list[list[str]]]:
    """目录里的 emoji 按目录顺序，再加关键词文件里有、目录没有的 emoji，归入 Symbols。"""
    zh_source, en_source = _KeywordSource(zh), _KeywordSource(en)
    rows, keys = [], []

    def add(emoji: str, category: str, order: int) -> None:
        merged = _dedup(zh_source.keywords(emoji) + en_source.keywords(emoji))
        pinyin_column = " ".join(full for full in ("".join(pinyin.lazy(k)) for k in merged if _is_cjk(k)) if full)
        rows.append((emoji, category, order, " ".join(merged), pinyin_column))
        keys.append(_emoji_search_keys(pinyin, merged))

    known = {emoji.replace(VS16, "") for emoji in catalog}
    for emoji, (category, order) in catalog.items():
        add(emoji, category, order)
    extra_order = max((order for _, order in catalog.values()), default=-1) + 1
    for emoji in [*zh, *en]:
        stripped = emoji.replace(VS16, "")
        if stripped in known:
            continue
        known.add(stripped)
        add(emoji, "Symbols", extra_order)
        extra_order += 1
    return rows, keys


def build_emoji(connection: sqlite3.Connection, rows: list[tuple], keys: list[list[str]]) -> int:
    count = 0
    with connection:
        connection.execute("DROP TABLE IF EXISTS emoji")
        connection.execute("DROP INDEX IF EXISTS idx_emoji_category_order")
        connection.execute("\n            CREATE TABLE emoji (\n                emoji TEXT NOT NULL,\n"
                           "                category TEXT NOT NULL,\n                sort_order INTEGER NOT NULL,\n"
                           "                keywords TEXT NOT NULL,\n                pinyin TEXT NOT NULL,\n"
                           "                PRIMARY KEY (emoji)\n            ) WITHOUT ROWID\n            ")
        connection.execute("CREATE INDEX idx_emoji_category_order ON emoji(category, sort_order)")
        connection.executemany("INSERT INTO emoji (emoji, category, sort_order, keywords, pinyin) VALUES (?, ?, ?, ?, ?)", rows)
        connection.execute("DROP TABLE IF EXISTS emoji_pinyin")
        connection.execute("\n            CREATE TABLE emoji_pinyin (\n                key TEXT NOT NULL,\n"
                           "                emoji TEXT NOT NULL,\n                sort_order INTEGER NOT NULL,\n"
                           "                PRIMARY KEY (key, emoji)\n            ) WITHOUT ROWID\n            ")
        seen = set()
        # 每个检索键一行，输入的编码能前缀匹配 emoji 的任何一个关键词。
        for row, row_keys in zip(rows, keys):
            for key in row_keys:
                if key and (key, row[0]) not in seen:
                    seen.add((key, row[0]))
                    connection.execute("INSERT INTO emoji_pinyin (key, emoji, sort_order) VALUES (?, ?, ?)",
                                       (key, row[0], row[2]))
                    count += 1
        textutil.analyze(connection, optimize=False)
    return count


# ---- 颜文字 ----

def _kaomoji_columns(pinyin: Pinyin, keyword: str) -> tuple[str, str]:
    if _is_cjk(keyword):
        full, initials = pinyin.full_and_initials(keyword)
        full, initials = full.lower(), initials.lower()
        return full, "" if initials == full else initials
    lowered = keyword.lower()
    # 本身就是空格分隔拼音的 ASCII 关键词（"zai xiang"）。
    if " " in keyword and all(c == " " or ("a" <= c <= "z") for c in lowered):
        syllables = lowered.split()
        full = "".join(syllables)
        initials = "".join(syllable[0] for syllable in syllables)
        return full, "" if initials == full else initials
    word = _english_word(lowered)
    return (word, "") if word is not None else ("", "")


def build_kaomoji(connection: sqlite3.Connection, pinyin: Pinyin, mapping: dict[str, list[str]]) -> tuple[int, int]:
    rows = 0
    with connection:
        connection.executescript("DROP TABLE IF EXISTS kaomoji; DROP TABLE IF EXISTS kaomoji_pinyin; "
                                 "DROP INDEX IF EXISTS idx_kaomoji_jianpin; DROP TABLE IF EXISTS kaomoji_catalog;")
        connection.execute("\n            CREATE TABLE kaomoji (\n                pinyin TEXT NOT NULL,\n"
                           "                jianpin TEXT NOT NULL,\n                kaomoji TEXT NOT NULL,\n"
                           "                sort_order INTEGER NOT NULL,\n"
                           "                PRIMARY KEY (pinyin, jianpin, kaomoji)\n            ) WITHOUT ROWID\n            ")
        connection.execute("CREATE INDEX idx_kaomoji_jianpin ON kaomoji(jianpin)")
        seen = set()
        for order, (kaomoji, keywords) in enumerate(mapping.items()):
            for keyword in keywords:
                full, initials = _kaomoji_columns(pinyin, keyword)
                if not full or (full, initials, kaomoji) in seen:
                    continue
                seen.add((full, initials, kaomoji))
                connection.execute("INSERT INTO kaomoji (pinyin, jianpin, kaomoji, sort_order) VALUES (?, ?, ?, ?)",
                                   (full, initials, kaomoji, order))
                rows += 1
        connection.execute("\n            CREATE TABLE kaomoji_catalog (\n                kaomoji TEXT NOT NULL,\n"
                           "                sort_order INTEGER NOT NULL,\n                keywords TEXT NOT NULL,\n"
                           "                PRIMARY KEY (kaomoji)\n            ) WITHOUT ROWID\n            ")
        connection.executemany("INSERT INTO kaomoji_catalog (kaomoji, sort_order, keywords) VALUES (?, ?, ?)",
                               [(kaomoji, order, " ".join(keywords)) for order, (kaomoji, keywords) in enumerate(mapping.items())])
        textutil.analyze(connection, optimize=False)
    return rows, len(mapping)

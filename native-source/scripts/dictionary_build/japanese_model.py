"""dict_japanese.dat：日文整句解码器内存映射的只读 Viterbi 模型（MSJPDT1），由 sources/japanese/ 下的 Mozc OSS 词库打包。

对应 Rust 构建器 japanese.rs。布局（小端）：56 字节文件头（MSJPDT1\\0、版本、词条数、连接矩阵边长、保留、
词条/连接矩阵/字符串的偏移、字符串字节数），每条 20 字节的词条记录（读音偏移 u32、读音长度 u16、表层偏移 u32、
表层长度 u16、左 id u16、右 id u16、代价 i32），size*size 的 i16 连接矩阵，最后是去重后的 UTF-8 字符串。
"""

from __future__ import annotations

import array
import math
import re
import struct
import sys
from pathlib import Path

from . import textutil

MAGIC = b"MSJPDT1\0"
HEADER_SIZE = 56
DICTIONARY_FILES = tuple(f"sources/japanese/dictionary{index:02d}.txt" for index in range(10))
ID_DEF = "sources/japanese/id.def"
AUX_DICTIONARY = "sources/japanese/aux_dictionary.tsv"
DICTIONARY_FILTER = "sources/japanese/dictionary_filter.tsv"
# Mozc src/data/dictionary_manual/ 的词表，按该目录 dictionary_manual filegroup 的顺序。
MANUAL_WORDS = ("sources/japanese/places.tsv", "sources/japanese/words.tsv")
CONNECTION = "sources/japanese/connection_single_column.txt"
NOTICE = "sources/japanese/README.txt"
LICENSE = "sources/japanese/LICENSE"

# gen_aux_dictionary.py 在词表里接受的词性别名及其 id.def 名称。
_POS_ALIASES = (
    ("名詞", "名詞,一般,*,*,*,*,*"), ("固有名詞", "名詞,固有名詞,一般,*,*,*,*"), ("人名", "名詞,固有名詞,人名,一般,*,*,*"),
    ("姓", "名詞,固有名詞,人名,姓,*,*,*"), ("名", "名詞,固有名詞,人名,名,*,*,*"), ("組織", "名詞,固有名詞,組織,*,*,*,*"),
    ("地名", "名詞,固有名詞,地域,一般,*,*,*"), ("名詞サ変", "名詞,サ変接続,*,*,*,*,*"),
    ("名詞形動", "名詞,形容動詞語幹,*,*,*,*,*"), ("副詞", "副詞,一般,*,*,*,*,*"), ("連体詞", "連体詞,*,*,*,*,*,*"),
    ("接続詞", "接続詞,*,*,*,*,*,*"), ("感動詞", "感動詞,*,*,*,*,*,*"), ("接頭語", "接頭詞,名詞接続,*,*,*,*,*"),
    ("助数詞", "名詞,接尾,助数詞,*,*,*,*"), ("接尾一般", "名詞,接尾,一般,*,*,*,*"), ("接尾人名", "名詞,接尾,人名,*,*,*,*"),
    ("接尾地名", "名詞,接尾,地域,*,*,*,*"),
)

# (读音, 表层, 左 id, 右 id, 代价)
Token = tuple[str, str, int, int, int]

# Rust 的 str::parse::<i64> 接受的写法，与 textutil.parse_int 相同。
_is_integer = re.compile(r"[+-]?[0-9]+\Z").match


def _u16(value: int) -> int:
    if not 0 <= value <= 0xFFFF:
        raise ValueError(f"context id {value} outside u16")
    return value


def _i32(value: int) -> int:
    if not -(1 << 31) <= value < 1 << 31:
        raise ValueError(f"cost {value} outside i32")
    return value


class DictionaryFilter:
    """gen_filtered_dictionary.py 的过滤：每行 key/value 是两段正则，整行完整匹配 `{key}\\t\\d+\\t\\d+\\t\\d+\\t{value}(\\t.*)?` 就删掉。"""

    def __init__(self, text: str, name: str):
        patterns = []
        for number, line in enumerate(textutil.universal_lines(text), start=1):
            if line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) != 2:
                raise ValueError(f"{name}:{number}: expected key<TAB>value")
            key, value = parts
            pattern = rf"(?:{key}\t\d+\t\d+\t\d+\t{value}(?:\t.*)?)"
            try:
                re.compile(pattern)
            except re.error as error:
                raise ValueError(f"{name}:{number}: bad pattern") from error
            patterns.append(pattern)
        # 合成一个交替式：「任一条完整匹配」与逐条试等价，百万行时快得多。
        self._pattern = re.compile("|".join(patterns)) if patterns else None

    def removes(self, line: str) -> bool:
        return self._pattern is not None and self._pattern.fullmatch(line) is not None


def _manual_pos_ids(id_def: str) -> dict[str, str]:
    ids = {}
    for number, line in enumerate(textutil.universal_lines(id_def), start=1):
        parts = line.rstrip().split(" ")
        if len(parts) != 2:
            raise ValueError(f"id.def:{number}: expected <id> <name>")
        ids[parts[1]] = parts[0]
    result = {}
    for alias, name in _POS_ALIASES:
        if name not in ids:
            raise ValueError(f"id.def has no {name}")
        result[alias] = ids[name]
    return result


def aux_tokens(dictionaries: list[tuple[str, str]], id_def: str, aux: tuple[str, str],
               word_lists: list[tuple[str, str]]) -> list[Token]:
    """gen_aux_dictionary.py --strict 写进 aux_dictionary.txt 的词条：aux 行复制基准词条的上下文 id 与代价再加偏移，
    词表行按词性取左右 id 都是该词性的基准词条代价的中位数。基准是未过滤的词库，与 Bazel 规则一致。"""
    bases = []
    for name, text in dictionaries:
        for number, line in enumerate(textutil.universal_lines(text), start=1):
            columns = line.rstrip().split("\t")
            if len(columns) < 5:
                raise ValueError(f"{name}:{number}: expected at least five tab-separated columns")
            cost = textutil.parse_int(columns[3].strip())
            if cost is None:
                raise ValueError(f"{name}:{number}: {columns[3]!r} is not an integer")
            bases.append((columns[0], columns[1], columns[2], cost, columns[4]))
    by_word: dict[tuple[str, str], list[int]] = {}
    existing = set()
    costs_by_pos: dict[str, list[int]] = {}
    for index, (key, left, right, cost, value) in enumerate(bases):
        by_word.setdefault((key, value), []).append(index)
        existing.add((left, right, key, value))
        if left == right:
            costs_by_pos.setdefault(left, []).append(cost)

    def token(key: str, left: str, right: str, cost: int, value: str) -> Token:
        return key, value, _u16(int(left)), _u16(int(right)), _i32(cost)

    tokens: list[Token] = []
    added = set()
    aux_name, aux_text = aux
    for number, line in enumerate(textutil.universal_lines(aux_text), start=1):
        if line.startswith("#"):
            continue
        columns = line.rstrip().split("\t")
        if len(columns) != 5:
            raise ValueError(f"{aux_name}:{number}: expected five tab-separated columns")
        key, value, base_key, base_value, offset_text = columns
        offset = textutil.parse_int(offset_text.strip())
        if offset is None:
            raise ValueError(f"{aux_name}:{number}: bad cost offset")
        indices = by_word.get((base_key, base_value))
        if indices is None:
            raise ValueError(f"{aux_name}:{number}: {base_key} and {base_value} are not in the dictionary")
        for index in indices:
            _, left, right, cost, _ = bases[index]
            if (left, right, key, value) in existing:
                continue
            tokens.append(token(key, left, right, cost + offset, value))
            added.add((left, right, key, value))

    pos_ids = _manual_pos_ids(id_def)
    medians: dict[str, int] = {}
    for name, text in word_lists:
        for number, line in enumerate(textutil.universal_lines(text), start=1):
            if line.startswith("#") or not line.rstrip():
                continue
            columns = line.rstrip().split("\t")
            if len(columns) != 3:
                raise ValueError(f"{name}:{number}: expected key<TAB>value<TAB>pos")
            key, value, pos = columns
            if pos not in pos_ids:
                raise ValueError(f"{name}:{number}: {pos} is an invalid pos")
            pos_id = pos_ids[pos]
            if (pos_id, pos_id, key, value) in existing or (pos_id, pos_id, key, value) in added:
                continue
            if pos_id not in medians:
                if pos_id not in costs_by_pos:
                    raise ValueError(f"no dictionary entry has context ids {pos_id}/{pos_id}")
                costs = sorted(costs_by_pos[pos_id])
                # EntryList.AtRatio(0.5)：升序代价里下标 int((n - 1) * 0.5) 的那个。
                medians[pos_id] = costs[(len(costs) - 1) // 2]
            tokens.append(token(key, pos_id, pos_id, medians[pos_id], value))
    return tokens


def read_tokens(dictionaries: list[tuple[str, str]], dictionary_filter: DictionaryFilter,
                extra: list[Token]) -> tuple[list[Token], int]:
    """过滤后留下的词库行加上 extra，去重，按读音、表层、左 id、右 id、代价排序。"""
    seen = set(extra)
    filtered = 0
    for name, text in dictionaries:
        for number, line in enumerate(textutil.universal_lines(text), start=1):
            if not line or line.startswith("#"):
                continue
            if dictionary_filter.removes(line):
                filtered += 1
                continue
            columns = line.split("\t")
            if len(columns) < 5:
                raise ValueError(f"{name}:{number}: expected at least five tab-separated columns")
            numbers = [textutil.parse_int(value.strip()) for value in columns[1:4]]
            if None in numbers:
                raise ValueError(f"{name}:{number}: context ids and cost must be integers")
            seen.add((columns[0], columns[4], _u16(numbers[0]), _u16(numbers[1]), _i32(numbers[2])))
    # Rust 的 String 按 UTF-8 字节序比较，与 Python 按码位比较结果相同。
    return sorted(seen), filtered


def read_connection(id_def: str, connection: str) -> tuple[int, array.array]:
    """连接矩阵，代价截到 i16。单列文件可能以矩阵边长开头。"""
    largest = None
    for line in textutil.universal_lines(id_def):
        if not line.strip() or line.startswith("#"):
            continue
        identifier = textutil.parse_int(line.split()[0])
        if identifier is None:
            raise ValueError("id.def: bad id")
        largest = identifier if largest is None else max(largest, identifier)
    if largest is None:
        raise ValueError("id.def lists no ids")
    size = largest + 1
    lines = [line.strip() for line in textutil.universal_lines(connection)]
    values = [line.split()[0] for line in lines if line and not line.startswith("#")]
    if not all(map(_is_integer, values)):
        raise ValueError("connection: bad cost")
    costs = array.array("h", (min(max(int(value), -32768), 32767) for value in values))
    if len(costs) == size * size + 1 and costs[0] == size:
        del costs[0]
    if len(costs) != size * size:
        inferred = math.isqrt(len(costs))
        if inferred * inferred != len(costs):
            raise ValueError(f"connection cost count {len(costs)} is not a square matrix")
        print(f"warning: id.def size {size}; using inferred connection size {inferred}", file=sys.stderr)
        size = inferred
    return size, costs


def pack(tokens: list[Token], size: int, costs: array.array) -> bytes:
    strings = bytearray()
    locations: dict[str, tuple[int, int]] = {}

    def intern(value: str) -> tuple[int, int]:
        if value in locations:
            return locations[value]
        encoded = value.encode("utf-8")
        if len(encoded) > 0xFFFF:
            raise ValueError("dictionary string exceeds u16 length")
        location = (len(strings), len(encoded))
        strings.extend(encoded)
        locations[value] = location
        return location

    records = bytearray()
    record = struct.Struct("<IHIHHHi")
    for reading, surface, left, right, cost in tokens:
        if left >= size or right >= size:
            raise ValueError(f"context id outside connection matrix: {left}, {right}")
        reading_at, reading_length = intern(reading)
        surface_at, surface_length = intern(surface)
        records += record.pack(reading_at, reading_length, surface_at, surface_length, left, right, cost)
    if sys.byteorder != "little":
        costs = array.array("h", costs)
        costs.byteswap()
    token_offset = HEADER_SIZE
    connection_offset = token_offset + len(records)
    string_offset = connection_offset + len(costs) * 2
    header = MAGIC + struct.pack("<IIII", 1, len(tokens), size, 0) + struct.pack(
        "<QQQQ", token_offset, connection_offset, string_offset, len(strings))
    return header + bytes(records) + costs.tobytes() + bytes(strings)


def build(dictionary: Path) -> tuple[bytes, int, int, int]:
    """返回 (模型字节, 词条数, aux 与人工词表加入的词条数, 被过滤的基础行数)。"""
    def read(name: str) -> str:
        return textutil.read(dictionary / name)

    dictionaries = [(name, read(name)) for name in DICTIONARY_FILES]
    id_def = read(ID_DEF)
    word_lists = [(name, read(name)) for name in MANUAL_WORDS]
    dictionary_filter = DictionaryFilter(read(DICTIONARY_FILTER), DICTIONARY_FILTER)
    aux = aux_tokens(dictionaries, id_def, (AUX_DICTIONARY, read(AUX_DICTIONARY)), word_lists)
    tokens, filtered = read_tokens(dictionaries, dictionary_filter, aux)
    size, costs = read_connection(id_def, read(CONNECTION))
    return pack(tokens, size, costs), len(tokens), len(aux), filtered

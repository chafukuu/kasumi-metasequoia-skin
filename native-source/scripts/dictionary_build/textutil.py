"""源文件的读取与切分规则，与 Rust 构建器的 text.rs 一致。

Rust 侧照搬的是 Python 的语义（str.strip、str.splitlines、str.isalpha），所以这里大多直接用内置方法；
唯一要自己写的是「文本模式迭代文件」的行切分：只认 \\r\\n、\\r、\\n，和 str.splitlines 不同。
"""

from __future__ import annotations

import re
import sqlite3
from pathlib import Path

_UNIVERSAL = re.compile(r"\r\n|\r|\n")
_INTEGER = re.compile(r"[+-]?[0-9]+\Z")


def read(path: Path) -> str:
    """严格按 UTF-8 解码，保留 BOM（只有部分读取方去掉它，见 without_bom）。"""
    return path.read_bytes().decode("utf-8")


BOM = "\N{ZERO WIDTH NO-BREAK SPACE}"


def without_bom(text: str) -> str:
    return text[len(BOM):] if text.startswith(BOM) else text


def universal_lines(text: str) -> list[str]:
    """只按 \\r\\n、\\r、\\n 切行，去掉行尾，最后一个换行之后的空串不算一行。"""
    lines = _UNIVERSAL.split(text)
    if lines and lines[-1] == "":
        lines.pop()
    return lines


def newline_lines(text: str) -> list[str]:
    """只按 \\n 切行（Rust 的 split_terminator('\\n')），\\r 留在行里，等 strip 去掉。"""
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    return lines


def parse_int(value: str) -> int | None:
    """Rust 的 str::parse::<i64>：只认 ASCII 数字和可选的正负号，不像 int() 那样接受空白、下划线和全角数字。"""
    return int(value) if _INTEGER.match(value) else None


def is_ascii_lower_word(value: str) -> bool:
    return bool(value) and value.isascii() and value.isalpha() and value.islower()


def is_ascii_alpha(value: str) -> bool:
    return bool(value) and value.isascii() and value.isalpha()


def integrity_check(connection: sqlite3.Connection) -> None:
    result = connection.execute("PRAGMA integrity_check").fetchone()[0]
    if result != "ok":
        raise ValueError(f"SQLite integrity check failed: {result}")


def analyze(connection: sqlite3.Connection, optimize: bool) -> None:
    """ANALYZE 后只留 sqlite_stat1，与发布的库一致。"""
    connection.execute("ANALYZE")
    if optimize:
        connection.execute("PRAGMA optimize")
    connection.execute("DROP TABLE IF EXISTS sqlite_stat4")


def freeze(path: Path) -> None:
    """发布前收尾：回滚日志模式、没有空闲页，不需要 -wal/-shm 旁路文件。"""
    connection = sqlite3.connect(path, isolation_level=None)
    try:
        connection.execute("PRAGMA wal_checkpoint(TRUNCATE)")
        connection.execute("VACUUM")
        mode = connection.execute("PRAGMA journal_mode=DELETE").fetchone()[0]
        if mode.lower() != "delete":
            raise ValueError(f"{path}: journal mode is {mode}, expected delete")
    finally:
        connection.close()

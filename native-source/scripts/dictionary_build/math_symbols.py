"""将随交接包维护的数学符号加入 others.db。可单独运行，参数为已有词库路径。"""

from __future__ import annotations

import argparse
import re
import sqlite3
from contextlib import closing
from pathlib import Path

SOURCE = Path(__file__).with_name("math_symbols.tsv")


def apply(connection: sqlite3.Connection, excluded: frozenset[str] | set[str] = frozenset()) -> tuple[int, int]:
    """按词表顺序补充编码和面板分组，保留已有候选的优先级。"""
    codes = {}
    headings = {}
    with SOURCE.open(encoding="utf-8") as stream:
        if stream.readline().rstrip("\n") != "分类\t分类码\t符号\t名称\t输入码":
            raise ValueError(f"{SOURCE}: 表头不匹配")
        for number, line in enumerate(stream, 2):
            category, group, symbol, names, readings = line.rstrip("\n").split("\t")
            aliases = readings.split()
            if not category or not symbol.strip() or not names.strip() or not aliases or not all(
                re.fullmatch("[a-z]+", code) for code in [group, *aliases]
            ):
                raise ValueError(f"{SOURCE}:{number}: 分类、符号、名称或编码无效")
            if symbol in excluded:
                continue
            section = headings.setdefault(category, {})
            if symbol in section:
                raise ValueError(f"{SOURCE}:{number}: {category} 中重复的符号 {symbol}")
            section[symbol] = f"数学 shuxue sx {category} {group} {names} {readings}"
            for code in [*aliases, group, "shuxue", "sx"]:
                codes[code, symbol] = None

    with connection:
        connection.execute("BEGIN IMMEDIATE")
        first = connection.execute("SELECT COALESCE(MAX(sort_order), -1) + 1 FROM symbol_code").fetchone()[0]
        before = connection.total_changes
        connection.executemany(
            "INSERT INTO symbol_code(code, symbol, sort_order, mixed) VALUES (?, ?, ?, 1) "
            "ON CONFLICT(code, symbol) DO NOTHING",
            ((code, symbol, order) for order, (code, symbol) in enumerate(codes, first)),
        )
        new_codes = connection.total_changes - before
        new_items = 0
        for category, section in headings.items():
            existing = {row[0] for row in connection.execute("SELECT symbol FROM symbol_catalog WHERE category = ?",
                                                             (category,))}
            pending = [(symbol, keywords) for symbol, keywords in section.items() if symbol not in existing]
            if not pending:
                continue
            # 面板按 sort_order 顺序遇到新父类或小标题就另开一组，同一组的行必须相邻。
            after = connection.execute(
                "SELECT COALESCE(MAX(CASE WHEN parent_category = '数学' AND category = ? THEN sort_order END), "
                "MAX(CASE WHEN parent_category = '数学' THEN sort_order END), MAX(sort_order), -1) FROM symbol_catalog",
                (category,),
            ).fetchone()[0]
            connection.execute("UPDATE symbol_catalog SET sort_order = sort_order + ? WHERE sort_order > ?",
                               (len(pending), after))
            connection.executemany(
                "INSERT INTO symbol_catalog(symbol, category, parent_category, sort_order, keywords) "
                "VALUES (?, ?, '数学', ?, ?)",
                ((symbol, category, order, keywords) for order, (symbol, keywords) in enumerate(pending, after + 1)),
            )
            new_items += len(pending)
    return new_codes, new_items


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("database", type=Path, help="目标 others.db")
    path = parser.parse_args().database.resolve(strict=True)
    with closing(sqlite3.connect(path.as_uri() + "?mode=rw", uri=True)) as connection:
        codes, items = apply(connection)
    print(f"新增 {codes} 个编码与符号的对应关系，新增 {items} 个面板条目。")

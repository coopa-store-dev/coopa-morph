#!/usr/bin/env python3
"""把 levels/*.txt 生成 src/morph_levels.{h,c}(同样的输入永远生成同样的文件)。
关卡文本:8 行一样宽;. 空  # 实心  = 单向平台  S 出生点(格子本身是空,出生点在这一格底边中点);
「# 」(井号加空格)开头的行是注释(关卡行里不会有空格,所以和实心格 # 不冲突)。行宽不一样、没有 S、有不认识的字符 → 报错退出,不生成。"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ROWS, TILE = 8, 32
CODE = {".": 0, "#": 1, "=": 2, "S": 0}


def parse(path):
    lines = [l.rstrip("\r\n") for l in path.read_text(encoding="utf-8").splitlines()]
    lines = [l for l in lines if l and not l.startswith("# ")]
    if len(lines) != ROWS:
        raise ValueError(f"{path.name}: 需要 {ROWS} 行,实际 {len(lines)} 行")
    cols = len(lines[0])
    spawns = []
    for r, l in enumerate(lines):
        if len(l) != cols:
            raise ValueError(f"{path.name}: 第 {r + 1} 行宽 {len(l)},应为 {cols}")
        for c, ch in enumerate(l):
            if ch not in CODE:
                raise ValueError(f"{path.name}: 第 {r + 1} 行第 {c + 1} 列有不认识的字符 {ch!r}")
            if ch == "S":
                spawns.append((c, r))
    if len(spawns) != 1:
        raise ValueError(f"{path.name}: 需要恰好一个 S,实际 {len(spawns)} 个")
    c, r = spawns[0]
    return cols, [CODE[ch] for l in lines for ch in l], c * TILE + TILE // 2, (r + 1) * TILE


def main():
    levels = []
    try:
        for p in sorted((ROOT / "levels").glob("*.txt")):
            levels.append((p.stem, *parse(p)))
    except ValueError as e:
        sys.exit(f"gen_levels: {e}")
    if not levels:
        sys.exit("gen_levels: levels/ 里没有关卡")
    h = ["// 生成的文件(tools/gen_levels.py),别手改", "#pragma once", '#include "morph_level.h"', ""]
    c = ["// 生成的文件(tools/gen_levels.py),别手改", '#include "morph_levels.h"', ""]
    for name, cols, tiles, sx, sy in levels:
        h.append(f"extern const morph_level_t morph_level_{name};")
        c.append(f"static const uint8_t tiles_{name}[{ROWS * cols}] = {{")
        for r in range(ROWS):
            c.append("    " + ",".join(str(t) for t in tiles[r * cols:(r + 1) * cols]) + ",")
        c.append("};")
        c.append(f'const morph_level_t morph_level_{name} = {{ "{name}", {cols}, tiles_{name}, {sx}, {sy} }};')
        c.append("")
    (ROOT / "src" / "morph_levels.h").write_text("\n".join(h) + "\n", encoding="utf-8")
    (ROOT / "src" / "morph_levels.c").write_text("\n".join(c), encoding="utf-8")


main()

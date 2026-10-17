#!/usr/bin/env python3
"""生成一份带库巴变变变(14 号,local:本仓库)的商店目录,给外壳的模拟器 / 卡上构建用(照 coopa-fight)。
  tests/make_catalog.py SHELL OUT.toml
SHELL = 外壳仓库目录;目录里原有的游戏照抄。"""
import sys
from pathlib import Path

GAME = Path(__file__).resolve().parents[1]
shell, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2])
src = (shell / "store" / "catalog.toml").read_text(encoding="utf-8")
src = src.replace('icons = ["icons/', f'icons = ["{shell}/store/icons/').replace('", "icons/', f'", "{shell}/store/icons/')
src = src.replace('repo = "local:', f'repo = "local:{shell}/')
out.write_text(src + f'''
[[game]]
no = 14
id = "morph"
author = "库巴老爸"
blurb = "两张卡一起闯四关:变身、合体、打倒玩具魔王"
repo = "local:{GAME}"
commit = ""
price = 0
starter = false
listed = true
stars = [2, 3, 4, 5, 6, 10]
size_kb = 200
tags = []
icons = []
''', encoding="utf-8")
print(out)

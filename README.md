# 库巴变变变

两张卡一起玩的横版闯关小游戏:两个人各拿一张库巴卡,一路往前走,过坑、翻墙、变身,一起打倒关底的发条铁皮熊。

开发:`make -C tests` 跑宿主测试(纯 C 的关卡格子等,带 ASan/UBSan);`python3 tools/gen_levels.py` 把 `levels/*.txt` 重新生成 `src/morph_levels.{h,c}`;`python3 tests/make_catalog.py 外壳目录 OUT.toml` 生成带本游戏的商店目录。

版权所有。

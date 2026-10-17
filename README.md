# 库巴变变变

两张卡一起玩的横版闯关小游戏:两个人各拿一张游戏卡,一路往前走,过坑、翻墙、变身,一起打倒关底的发条铁皮熊。

开发:`make -C tests` 跑宿主测试(纯 C 的关卡格子等,带 ASan/UBSan);`python3 tools/gen_levels.py` 把 `levels/*.txt` 重新生成 `src/morph_levels.{h,c}`;`python3 tests/make_catalog.py 外壳目录 OUT.toml` 生成带本游戏的商店目录。

调试通道(实验用,只在游戏在前台时认):devlink `BOS1` 一个人进试验关、`BOS2` 摄像机步长 1 → 2 → 4 → 8、`BOS3` 云和草丛开 / 关、`BOS4` 摆 / 收 6 个怪的替身(放大两倍的人物图,跟着关卡卷)。状态行(`MO …`,格式见 `src/morph_app.c` 的 `morph_status`)最后几项是上一段 2 秒的刷屏次数、刷屏合计微秒、联机状态消息从该发到交出去的平均 / 最长毫秒;对象数含那 6 个替身。`tests/sim_walk.py`、`tests/sim_duo.py` 用模拟器跑(`--shell 外壳仓库目录`)。

版权所有。

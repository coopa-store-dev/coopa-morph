#!/usr/bin/env python3
"""桌面模拟器(外壳的 tools/sim,step 时钟)装上库巴变变变,按真人手速一个人打一趟,按键全走真的按键组件。
地图上分叉就随便选,战斗里每 250–450 ms 按一下(1/5 是按住),和 tests/test_run.c 的乱按一样。
截图:首页、地图、分叉、战斗、大招特写(BOS2 给满能量再按 ▲)、超必杀(▼▲)、boss、结束页。
  tests/sim_play.py --shell 外壳仓库目录 --fonts 字库目录 --catalog 目录.toml --out 截图目录 [--minutes 25]
字库:coopa _fonts --out DIR;目录:tests/make_catalog.py SHELL OUT.toml。"""
import argparse
import os
import random
import subprocess
import sys
import time

KEYS = ("page phase nh row walking hp en form st x foes boss result cine objs dt_avg dt_max draw_max draw_n "
        "net me rtt delay desync step100").split()
PG_TITLE, PG_RUN, PG_END, PG_LINK, PG_PICK = range(5)
RUN_MAP, RUN_EVENT, RUN_BATTLE, RUN_WIN, RUN_OVER = range(5)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shell", required=True)
    ap.add_argument("--fonts", required=True)
    ap.add_argument("--catalog", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--minutes", type=float, default=25)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    sys.path.insert(0, os.path.join(a.shell, "tools", "sim"))
    from simctl import Sim  # noqa: E402
    sock = os.path.join(a.out, "ctl.sock")
    if os.path.exists(sock):
        os.unlink(sock)
    proc = subprocess.Popen(
        [os.path.join(a.shell, "tools/sim/run.sh"), "--headless", "--wipe", "--clock", "step", "--seed", "5",
         "--catalog", a.catalog, "--games", "14", "--font-dir", a.fonts, "--ctl", sock,
         "--state", os.path.join(a.out, "state"), "--out", os.path.join(a.out, "simout")],
        stdout=open(os.path.join(a.out, "stdout.log"), "w"), stderr=subprocess.STDOUT)
    rng = random.Random(7)
    try:
        end = time.time() + 900
        while not os.path.exists(sock):
            if proc.poll() is not None or time.time() > end:
                sys.exit(f"模拟器没起来,看 {a.out}/stdout.log")
            time.sleep(0.5)
        c = Sim(sock)

        def stat():
            f = c.cmd("stat").split()
            if len(f) < 2 or f[1] != "MO":
                return None
            if len(f) != len(KEYS) + 2:
                sys.exit(f"状态行字段数不对:{f}")
            return dict(zip(KEYS, map(int, f[2:])))

        shots = set()

        def shot(name):
            if name not in shots:
                shots.add(name)
                c.ok(f"shot {a.out}/{name}.png")

        c.ok("wait 1500")
        for _ in range(12):   # 开机菜单上找到本游戏(菜单光标位置不定,一路往下按 ●)
            if stat():
                break
            c.ok("press ok")
            c.ok("wait 800")
            if not stat():
                c.ok("press down")
                c.ok("wait 300")
        s = stat()
        assert s and s["page"] == PG_TITLE, s
        shot("title")
        c.ok("press ok")
        c.ok("wait 500")
        s = stat()
        assert s["page"] == PG_RUN, s
        sim_ms, limit = 0, a.minutes * 60 * 1000
        did_ult = did_super = False
        battles = 0
        last_phase = None
        while sim_ms < limit:
            s = stat()
            if s["page"] == PG_END:
                shot("end")
                print("结束:", "通关" if s["phase"] == RUN_WIN else "全倒", s)
                break
            ph = s["phase"]
            if ph != last_phase:
                if ph == RUN_BATTLE:
                    battles += 1
                    print(f"[{sim_ms / 1000:.0f}s] 第 {battles} 场战斗,第 {s['row']} 排", s)
                last_phase = ph
            if ph == RUN_MAP:
                shot("map")
                if not s["walking"]:
                    shot("fork")
                    c.ok(rng.choice(["press up", "press down", "press ok"]))
                c.ok("wait 300")
                sim_ms += 400
                continue
            if ph == RUN_EVENT:
                shot("event")
                c.ok("wait 800")
                c.ok("press ok")
                sim_ms += 900
                continue
            # 战斗
            shot("battle" if s["boss"] == 0 else "boss")
            if s["cine"]:
                shot({1: "cine-ult", 2: "cine-super", 3: "cine-combo"}[s["cine"]])
            if battles == 2 and not did_ult and s["foes"]:
                c.ok("serial BOS2")
                c.ok("wait 100")
                c.ok("press up")
                c.ok("wait 400")
                shot("cine-ult")
                did_ult = True
            elif battles == 3 and not did_super and s["foes"]:
                c.ok("serial BOS2")
                c.ok("wait 100")
                c.ok("press down")
                c.ok("wait 120")
                c.ok("press up")
                c.ok("wait 600")
                shot("cine-super")
                did_super = True
            k = rng.choice(["up", "ok", "down", "ok", "ok"])
            if k != "up" and rng.random() < 0.2:
                c.ok(f"hold {k} 700")
            else:
                c.ok(f"press {k}")
            gap = rng.randint(250, 450)
            c.ok(f"wait {gap}")
            sim_ms += gap + 80
        s = stat()
        print("最后:", s)
        print("lvmem:", c.cmd("lvmem"))
        print("mem:", c.cmd("mem"))
        print("截图:", sorted(shots))
    finally:
        proc.terminate()
        proc.wait(10)


if __name__ == "__main__":
    main()

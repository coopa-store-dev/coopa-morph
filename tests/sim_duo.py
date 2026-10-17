#!/usr/bin/env python3
"""两个桌面模拟器(外壳的 tools/sim,真时钟,假链路走本机 UDP)装上库巴变变变,两人联机打一趟。按键走真的按键组件。
两边首页 ▼ 到「两人联机」→ 连上 → a 选库巴(▲)、b 自动当爸爸 → 营地:a 先按「出发」(要等 b)、b 再按 → 开打;
两边按真人手速乱按(地图上分叉随便选、弹出来的 ● 关掉);中途把 b 停 3 秒(断线),两边都该停住、放开后接着打。
一直查:两边状态行的「对不上」一直是 0;最后两边都到这关的结果,● 一起回营地。
  tests/sim_duo.py --shell 外壳 --fonts 字库 --catalog 目录 --out 截图目录 [--minutes 12]"""
import argparse
import os
import random
import subprocess
import sys
import time

KEYS = ("page phase nh row walking hp en form st x foes boss result cine objs dt_avg dt_max draw_max draw_n "
        "net me rtt delay desync step100 ch parts").split()
PG_TITLE, PG_RUN, PG_LINK, PG_PICK = range(4)
RUN_MAP, RUN_EVENT, RUN_BATTLE, RUN_WIN, RUN_OVER, RUN_HUB = range(6)
MN_IDLE, MN_PICK, MN_PLAY, MN_PAUSED, MN_BAD = range(5)


class Card:
    def __init__(self, a, name, seed):
        sys.path.insert(0, os.path.join(a.shell, "tools", "sim"))
        from simctl import Sim  # noqa: E402
        d = os.path.join(a.out, name)
        os.makedirs(d, exist_ok=True)
        sock = os.path.join(d, "ctl.sock")
        if os.path.exists(sock):
            os.unlink(sock)
        self.name, self.dir = name, d
        self.proc = subprocess.Popen(
            [os.path.join(a.shell, "tools/sim/run.sh"), "--headless", "--wipe", "--seed", str(seed), "--catalog", a.catalog,
             "--games", "14", "--font-dir", a.fonts, "--ctl", sock, "--state", os.path.join(d, "state"),
             "--out", os.path.join(d, "simout")],
            stdout=open(os.path.join(d, "stdout.log"), "w"), stderr=subprocess.STDOUT)
        end = time.time() + 900
        while not os.path.exists(sock):
            if self.proc.poll() is not None or time.time() > end:
                sys.exit(f"模拟器 {name} 没起来,看 {d}/stdout.log")
            time.sleep(0.3)
        self.c = Sim(sock)
        self.shots = set()

    def stat(self):
        f = self.c.cmd("stat").split()
        if len(f) < 2 or f[1] != "MO":
            return None
        if len(f) != len(KEYS) + 2:
            sys.exit(f"{self.name} 状态行字段数不对:{f}")
        return dict(zip(KEYS, map(int, f[2:])))

    def press(self, k, ms=60):
        self.c.ok(f"press {k} {ms}")

    def shot(self, tag):
        if tag not in self.shots:
            self.shots.add(tag)
            self.c.ok(f"shot {self.dir}/{tag}.png")

    def enter_link(self):
        end = time.time() + 90
        while self.stat() is None:
            if time.time() > end:
                sys.exit(f"{self.name} 进不了游戏")
            self.press("ok")
            time.sleep(1.2)
            if self.stat() is None:
                self.press("down")
                time.sleep(0.3)
        assert self.stat()["page"] == PG_TITLE
        self.press("down")
        time.sleep(0.3)
        self.press("ok")
        time.sleep(0.5)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shell", required=True)
    ap.add_argument("--fonts", required=True)
    ap.add_argument("--catalog", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--minutes", type=float, default=12)
    a = ap.parse_args()
    rng = random.Random(11)
    x = Card(a, "a", 5)
    y = Card(a, "b", 9)
    try:
        x.enter_link()
        y.enter_link()
        end = time.time() + 60
        while not (x.stat()["page"] == PG_PICK and y.stat()["page"] == PG_PICK):
            if time.time() > end:
                sys.exit(f"连不上:{x.stat()} {y.stat()}")
            time.sleep(0.5)
        x.shot("pick")
        x.press("up")   # a 当库巴
        end = time.time() + 20
        while not (x.stat()["net"] == MN_PLAY and y.stat()["net"] == MN_PLAY):
            if time.time() > end:
                sys.exit(f"开不了:{x.stat()} {y.stat()}")
            time.sleep(0.3)
        sx, sy = x.stat(), y.stat()
        assert sx["me"] == 0 and sy["me"] == 1, (sx, sy)
        end = time.time() + 10
        while not (x.stat()["phase"] == RUN_HUB and y.stat()["phase"] == RUN_HUB):
            if time.time() > end:
                sys.exit(f"没到营地:{x.stat()} {y.stat()}")
            time.sleep(0.3)
        x.shot("hub")
        x.press("ok")   # a 先按出发:等 b
        time.sleep(1.0)
        assert x.stat()["phase"] == RUN_HUB and y.stat()["phase"] == RUN_HUB, (x.stat(), y.stat())
        x.shot("hub-wait")
        y.press("ok")
        end = time.time() + 10
        while not (x.stat()["phase"] == RUN_MAP and y.stat()["phase"] == RUN_MAP):
            if time.time() > end:
                sys.exit(f"出发不了:{x.stat()} {y.stat()}")
            time.sleep(0.3)
        print("开打:", x.stat(), y.stat())
        t0 = time.time()
        paused = False
        checks = 0
        while time.time() - t0 < a.minutes * 60:
            for card in (x, y):
                s = card.stat()
                if s["phase"] in (RUN_WIN, RUN_OVER, RUN_HUB):
                    continue
                if s["phase"] == 1:   # RUN_EVENT
                    card.press("ok")
                elif s["phase"] == 0 and not s["walking"]:
                    card.press(rng.choice(["up", "down", "ok"]))
                else:
                    k = rng.choice(["up", "ok", "down", "ok", "ok"])
                    if k != "up" and rng.random() < 0.2:
                        card.c.ok(f"hold {k} 650")
                    else:
                        card.press(k)
                tag = {0: "map", 2: "battle"}.get(s["phase"])
                if tag:
                    card.shot(tag)
                if s["cine"]:
                    card.shot(f"cine{s['cine']}")
            time.sleep(rng.uniform(0.15, 0.3))
            sx, sy = x.stat(), y.stat()
            assert sx["desync"] == 0 and sy["desync"] == 0, ("对不上", sx, sy)
            checks += 1
            if sx["phase"] in (RUN_WIN, RUN_OVER) and sy["phase"] in (RUN_WIN, RUN_OVER):
                break
            if not paused and time.time() - t0 > 60:   # 断线 3 秒
                y.c.ok("pause")
                time.sleep(3.5)
                sx = x.stat()
                print("b 停住时 a:", sx)
                assert sx["net"] == MN_PAUSED, sx
                x.shot("paused")
                y.c.ok("resume")
                end = time.time() + 15
                while not (x.stat()["net"] == MN_PLAY and y.stat()["net"] == MN_PLAY):
                    if time.time() > end:
                        sys.exit(f"连回来接不上:{x.stat()} {y.stat()}")
                    time.sleep(0.3)
                print("连回来:", x.stat(), y.stat())
                paused = True
        sx, sy = x.stat(), y.stat()
        print(f"查了 {checks} 次;最后 a:", sx)
        print("最后 b:", sy)
        assert paused and sx["desync"] == 0 and sy["desync"] == 0
        if sx["phase"] in (RUN_WIN, RUN_OVER) and sy["phase"] in (RUN_WIN, RUN_OVER):
            time.sleep(1.5)
            x.shot("result")
            x.press("ok")
            end = time.time() + 10
            while not (x.stat()["phase"] == RUN_HUB and y.stat()["phase"] == RUN_HUB):
                if time.time() > end:
                    sys.exit(f"回不了营地:{x.stat()} {y.stat()}")
                time.sleep(0.3)
            x.shot("hub-after")
            print("两边都打完这关、回了营地;零件", x.stat()["parts"], y.stat()["parts"])
        print("sim_duo: ok")
    finally:
        for card in (x, y):
            card.proc.terminate()
            card.proc.wait(10)


if __name__ == "__main__":
    main()

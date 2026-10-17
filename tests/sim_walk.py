#!/usr/bin/env python3
"""一个桌面模拟器(外壳的 tools/sim,step 时钟)装上库巴变变变,一个人在试验关里试按键手感。
按键全用 press / hold / down / up(真的按键组件),每一步读状态行判定,不对就打印状态行、退出码 1:
  1. serial BOS1 进试验关,走到第一个坑边:st == MB_EDGE,停 2 秒 x 不变;
  2. ▲ 过一格坑;到两格坑边 ▲,再每 250 ms ▲ 共 4 下 → 过去;
  3. 按住 ▼ 700 ms:方向只变一次(按下一次、长按被吞);
  4. 两个键叠着按:按住 ▲ 再按 ▼、先松开 ▲、再松开 ▼:方向最多变一次、最多一次跳;
  5. 一秒连按 ▲ 5 下(间隔 200 ms):一次跳 + 4 次扑腾都算到(再连按,多出来的不起作用);
  6. 走到关卡头上的墙 → MB_WALL;BOS2 四次 → cam 依次 2 4 8 1;BOS3 → 截图里没有云(再 BOS3 云回来)。
  tests/sim_walk.py --shell 外壳仓库目录(或环境变量 COOPA_SHELL) [--font-dir DIR] [--keep] [--shots DIR]"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time

GAME = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PG_TITLE, PG_LINK, PG_GAME = range(3)
MP_WAIT, MP_PLAY, MP_PAUSED = range(3)
MB_WALK, MB_AIR, MB_EDGE, MB_WALL = range(4)
ML_TILE = 32
KEYS = ("page phase solo init x y st dir peer_x peer_y peer_st clock rtt avg_ms max_ms objs cam tx rx refused "
        "draw_us draw_max_us").split()


class Fail(Exception):
    pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shell", default=os.environ.get("COOPA_SHELL"))
    ap.add_argument("--font-dir")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--shots", help="把各个画面截图存到这个目录")
    a = ap.parse_args()
    if not a.shell:
        ap.error("要给外壳仓库目录:--shell DIR 或环境变量 COOPA_SHELL")
    shell = os.path.abspath(a.shell)
    sys.path.insert(0, os.path.join(shell, "tools", "sim"))
    from simctl import Sim  # noqa: E402
    root = tempfile.mkdtemp(prefix="morph-walk.")
    catalog = os.path.join(root, "catalog.toml")
    subprocess.run([sys.executable, os.path.join(GAME, "tests", "make_catalog.py"), shell, catalog], check=True,
                   stdout=subprocess.DEVNULL)
    sock = os.path.join(root, "ctl.sock")
    proc = subprocess.Popen(
        [os.path.join(shell, "tools/sim/run.sh"), "--headless", "--wipe", "--clock", "step", "--seed", "5",
         "--catalog", catalog, "--games", "14", *(["--font-dir", a.font_dir] if a.font_dir else []), "--ctl", sock,
         "--state", os.path.join(root, "state"), "--out", os.path.join(root, "out")],
        stdout=open(os.path.join(root, "stdout.log"), "w"), stderr=subprocess.STDOUT)
    end = time.time() + 900
    while not os.path.exists(sock):
        if proc.poll() is not None or time.time() > end:
            sys.exit(f"模拟器没起来,看 {root}/stdout.log")
        time.sleep(0.2)
    c = Sim(sock)
    if a.shots:
        os.makedirs(a.shots, exist_ok=True)

    def stat():
        f = c.cmd("stat").split()
        if len(f) != len(KEYS) + 2 or f[:2] != ["ok", "MO"]:
            return None
        return dict(zip(KEYS, map(int, f[2:])))

    def fail(msg, *states):
        print(f"失败:{msg}", file=sys.stderr)
        for s in states or (stat(),):
            print("  MO " + " ".join(str(s[k]) for k in KEYS) if s else "  (没有状态行)", file=sys.stderr)
        raise Fail(msg)

    def check(cond, msg, *states):
        if not cond:
            fail(msg, *states)

    def shot(name):
        if a.shots:
            c.ok(f"shot {os.path.join(a.shots, name)}.png")

    def watch(ms, step=10):
        """模拟时间走 ms,每 step 毫秒读一次状态行,返回所有读数。"""
        out = []
        for _ in range(ms // step):
            out.append(stat())
            c.ok(f"wait {step}")
        out.append(stat())
        return out

    def until(pred, ms, what, step=20):
        for _ in range(ms // step):
            s = stat()
            if pred(s):
                return s
            c.ok(f"wait {step}")
        fail(f"{ms} ms 内等不到:{what}")

    def solo():
        """serial BOS1:一个人从出生点(x 80)重新开始。"""
        c.ok("serial BOS1")
        c.ok("wait 40")
        s = stat()
        check(s and s["page"] == PG_GAME and s["solo"] == 1 and s["phase"] == MP_PLAY and s["x"] <= 82,
              "BOS1 没把人放回出生点", s)

    def takeoffs(rows):
        return sum(1 for p, q in zip(rows, rows[1:]) if p["st"] != MB_AIR and q["st"] == MB_AIR)

    def turns(rows):
        return sum(1 for p, q in zip(rows, rows[1:]) if p["dir"] != q["dir"])

    def y_at(rows, t):
        """关卡时钟 t 那一刻(或最近的前一个读数)的 y。"""
        return ([r["y"] for r in rows if r["clock"] <= t] or [rows[0]["y"]])[-1]

    def kick_effect(rows, t):
        """第 t(关卡时钟)按的 ▲ 有没有往上推:按前 100 ms 和按后 40..140 ms 的垂直速率(像素 / 毫秒)之差。
        重力让速率每 100 ms 增加约 0.12,所以没推的是正数,推了的接近 0 或负数。"""
        before = (y_at(rows, t) - y_at(rows, t - 100)) / 100
        after = (y_at(rows, t + 140) - y_at(rows, t + 40)) / 100
        return after - before

    def press_up_times(n, gap=200):
        """每 gap 毫秒按一下 ▲(按 50 ms),每 10 ms 读一次状态行;返回 (读数, 每一下按的时刻)。"""
        rows, ts = [stat()], []
        for _ in range(n):
            ts.append(rows[-1]["clock"])
            c.ok("press up 50")
            rows += watch(gap - 50, 10)
        return rows, ts

    try:
        c.ok("wait 1500")
        for _ in range(10):  # 开机菜单光标在游戏上:● 进游戏(状态行是 MO 才算进了)
            if stat():
                break
            c.ok("press ok")
            c.ok("wait 800")
        s = stat()
        check(s and s["page"] == PG_TITLE, "进不了游戏首页", s)
        shot("1-title")

        # 1. 走到第一个坑边
        c.ok("serial BOS1")
        c.ok("wait 100")
        s = until(lambda s: s["st"] == MB_EDGE, 6000, "第一个坑边(MB_EDGE)")
        check(s["dir"] == 1 and s["page"] == PG_GAME and s["solo"] == 1 and s["refused"] == 0, "出生后方向 / 页不对", s)
        x0 = s["x"]
        check(x0 == 10 * ML_TILE - 10, f"坑边应在 x={10 * ML_TILE - 10}(第 10 列左边,身子半宽 10)", s)
        shot("2-pit1-edge")
        for s in watch(2000, 100):
            check(s["x"] == x0 and s["st"] == MB_EDGE, "停在坑边 2 秒内 x / st 变了", s)
        print(f"1. 第一个坑边 x={x0} 停了 2 秒没动")

        # 2. 一格坑 ▲ 一下;两格坑 ▲ + 4 下
        rows = [stat()]
        c.ok("press up")
        rows += watch(1500, 20)
        check(takeoffs(rows) == 1, "▲ 没起跳(或跳了不止一次)", rows[-1])
        s = until(lambda s: s["st"] == MB_EDGE, 8000, "两格坑的坑边(MB_EDGE)")
        check(s["x"] == 15 * ML_TILE - 10 and s["y"] == 224, "没过一格坑 / 位置不对", s)
        shot("3-pit2-edge")
        c.ok("press up")
        for _ in range(4):
            c.ok("wait 200")
            c.ok("press up")  # press 本身 50 ms:合起来 250 ms 一下
        rows = watch(2500, 20)
        s = until(lambda s: s["st"] == MB_EDGE, 8000, "三格坑(21-23 列)的坑边")
        check(s["x"] == 21 * ML_TILE - 10 and s["y"] == 224, "没过两格坑 / 过去后位置不对", s)
        print(f"2. 过了一格坑、两格坑(▲ + 4 下),现在在第三个坑边 x={s['x']}")
        shot("4-pit3-edge")

        # 3. 按住 ▼ 700 ms:方向只变一次
        solo()
        until(lambda s: s["x"] >= 140, 3000, "走到 x>=140")
        d0 = stat()["dir"]
        c.ok("down down")  # 手动展开 hold:按下 → 700 ms → 松开(长按被吞)
        rows = watch(700, 10)
        c.ok("up down")
        rows += watch(300, 10)
        check(turns(rows) == 1 and rows[-1]["dir"] == -d0, "按住 ▼ 700 ms 方向没有只变一次", *rows[-1:])
        # 再用 hold 命令走一遍
        c.ok("hold down 700")
        rows = watch(300, 10)
        check(turns(rows) == 0 and rows[-1]["dir"] == d0, "hold down 700 以后应该回到原方向(又变一次)", rows[-1])
        print("3. 按住 ▼ 700 ms:方向只变一次(下一次 hold 再变一次)")

        # 4. 叠着按
        for name, seq in (("▲ 先按住,▼ 叠上,先松 ▲", ["down up", "down down", "up up", "up down"]),
                          ("▼ 先按住,▲ 叠上,先松 ▼", ["down down", "down up", "up down", "up up"])):
            solo()
            until(lambda s: s["x"] >= 120, 3000, "走到 x>=120")
            rows = [stat()]
            for k in seq:
                c.ok(k)
                rows += watch(90, 10)
            rows += watch(1500, 10)
            nt, nj = turns(rows), takeoffs(rows)
            check(nt <= 1 and nj <= 1, f"叠着按({name}):方向变了 {nt} 次、跳了 {nj} 次(最多各 1 次)", rows[-1])
            print(f"4. 叠着按({name}):方向变 {nt} 次、跳 {nj} 次")

        # 5. 一秒 5 下(间隔 200 ms)
        solo()
        until(lambda s: s["x"] >= 120, 3000, "走到 x>=120")
        rows, ts = press_up_times(5)
        rows += watch(1000, 10)
        shot("5-flap")
        eff = [round(kick_effect(rows, t), 2) for t in ts]
        check(takeoffs(rows) == 1, "连按 5 下应该只起跳一次", rows[-1])
        check(all(e < -0.05 for e in eff[2:]), f"第 3、4、5 下应该各推一次(推的效果 < -0.05),读到 {eff}", rows[-1])
        print(f"5. 连按 5 下:起跳 1 次,扑腾的推力(第 1 下是起跳;第 2 下起跳正猛,可以被吞)={eff[1:]},y 最高 "
              f"{min(r['y'] for r in rows)}")
        solo()
        until(lambda s: s["x"] >= 120, 3000, "走到 x>=120")
        rows, ts = press_up_times(9)  # 再多按:扑腾上限 MB_FLAPS 次,多出来的不起作用
        rows += watch(300, 10)
        eff = [round(kick_effect(rows, t), 2) for t in ts]
        check(takeoffs(rows) == 1 and rows[-1]["st"] == MB_AIR, "9 下应该在空中、只起跳一次", rows[-1])
        check(all(e < -0.05 for e in eff[2:6]), f"第 3..6 下应该各推一次,读到 {eff}", rows[-1])
        check(all(e > -0.05 for e in eff[6:]), f"第 7 下以后(扑腾已满 {5} 次)不该再推,读到 {eff}", rows[-1])
        print(f"   按 9 下:各下的推力 {eff[1:]}:第 6 下以前有推,第 7 下以后没有(第 2 下虽然被吞,次数照算)")
        until(lambda s: s["st"] != MB_AIR, 8000, "落地")

        # 6. 关卡头上的墙;摄像机步长;云和草丛
        solo()
        c.ok("press down")  # 掉头往左
        s = until(lambda s: s["st"] == MB_WALL, 4000, "左墙(MB_WALL)")
        check(s["x"] == ML_TILE + 10 and s["dir"] == -1, "左墙应在 x=42,朝左", s)
        c.ok("wait 1500")
        shot("6-wall")
        print(f"6. 走到关卡头上的墙 x={s['x']} MB_WALL")
        cams = []
        for _ in range(4):
            c.ok("serial BOS2")
            c.ok("wait 60")
            cams.append(stat()["cam"])
        check(cams == [2, 4, 8, 1], f"BOS2 四次 cam 应依次 2 4 8 1,读到 {cams}")
        print(f"   BOS2 x4 → cam {cams}")
        if a.shots:
            from PIL import Image, ImageChops
            sh = [os.path.join(a.shots, n + ".png") for n in ("6-decor-on", "6-decor-off", "6-decor-on-again")]
            c.ok(f"shot {sh[0]}")
            c.ok("serial BOS3")
            c.ok("wait 100")
            c.ok(f"shot {sh[1]}")
            c.ok("serial BOS3")
            c.ok("wait 100")
            c.ok(f"shot {sh[2]}")
            nonzero = lambda d: sum(d.convert("L").histogram()[1:])  # noqa: E731  不是全黑的像素数
            im = [Image.open(p).convert("RGB") for p in sh]
            d_off = nonzero(ImageChops.difference(im[0], im[1]))
            d_back = nonzero(ImageChops.difference(im[0], im[2]))
            check(d_off > 200, f"BOS3 以后画面应该少了云和草丛,不同的像素只有 {d_off}")
            check(d_back == 0, f"再 BOS3 云和草丛应该回来,仍有 {d_back} 个像素不同")
            print(f"   BOS3:云和草丛没了(差 {d_off} 个像素);再 BOS3 回来(差 {d_back})")
        else:
            c.ok("serial BOS3")
            c.ok("wait 100")
            print("   (没给 --shots,云有没有只看不到截图)")
        s = stat()
        check(s["refused"] == 0 and s["max_ms"] <= 40, "refused != 0 或 dt 最大 > 40 ms", s)
        print("sim_walk: ok")
        return 0
    except Fail:
        return 1
    finally:
        proc.terminate()
        proc.wait(10)
        if a.keep:
            print("留着:", root)
        else:
            shutil.rmtree(root)


if __name__ == "__main__":
    sys.exit(main())

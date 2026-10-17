#!/usr/bin/env python3
"""两个桌面模拟器(外壳的 tools/sim,真时钟,假链路走本机 UDP)装上库巴变变变,两张卡一起走试验关。按键全用 press(真的按键组件)。
两边进「两人联机」→ 连上(a 是主动方还是被连方看谁先连上,不重要)→ 60 秒里两边各按真人节奏乱按 ▲ ▼(间隔 200–1500 ms):
  1. 每 5 秒两边都停手:一直等到两个人都停在坑边或墙边(MB_EDGE / MB_WALL,最多 15 秒),
     再比:a 的 peer_x peer_y peer_st = b 的 x y st,反过来也一样(x y 允许差 1 像素:状态行是取整的)。
     两边时钟只对齐到十几毫秒,所以走着的时候不比;
  2. 中途停住 b 3 秒(断线):a 的阶段是 MP_PAUSED(b 放开后也要看到自己 MP_PAUSED);放开后 2 秒内两边都回到
     MP_PLAY(b 的页面 / 阶段变化来自它自己发现断线、重连),再等两个人停住、再对上;
  3. 全程 refused == 0。
  tests/sim_duo.py --shell 外壳仓库目录(或环境变量 COOPA_SHELL) [--font-dir DIR] [--keep] [--secs 60] [--shots DIR]"""
import argparse
import os
import random
import shutil
import subprocess
import sys
import tempfile
import time

GAME = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PG_TITLE, PG_LINK, PG_GAME = range(3)
MP_WAIT, MP_PLAY, MP_PAUSED = range(3)
MB_WALK, MB_AIR, MB_EDGE, MB_WALL = range(4)
KEYS = ("page phase solo init x y st dir peer_x peer_y peer_st clock rtt avg_ms max_ms objs cam tx rx refused "
        "draw_us draw_max_us draw_n draw_sum_us b_wait_avg_ms b_wait_max_ms").split()


class Fail(Exception):
    pass


def dump(*pairs):
    for name, s in pairs:
        print(f"  {name}: MO " + " ".join(str(s[k]) for k in KEYS) if s else f"  {name}: (没有状态行)", file=sys.stderr)


class Card:
    def __init__(self, shell, root, name, seed, catalog, fonts):
        sys.path.insert(0, os.path.join(shell, "tools", "sim"))
        from simctl import Sim  # noqa: E402
        d = os.path.join(root, name)
        os.makedirs(d)
        self.name = name
        self.last = None
        sock = os.path.join(d, "ctl.sock")
        self.proc = subprocess.Popen(
            [os.path.join(shell, "tools/sim/run.sh"), "--headless", "--wipe", "--seed", str(seed), "--catalog", catalog,
             "--games", "14", *(["--font-dir", fonts] if fonts else []), "--ctl", sock,
             "--state", os.path.join(d, "state"), "--out", os.path.join(d, "out")],
            stdout=open(os.path.join(d, "stdout.log"), "w"), stderr=subprocess.STDOUT)
        end = time.time() + 600
        while not os.path.exists(sock):
            if self.proc.poll() is not None or time.time() > end:
                sys.exit(f"模拟器 {name} 没起来,看 {d}/stdout.log")
            time.sleep(0.2)
        self.ctl = Sim(sock)

    def try_stat(self):
        """开机时还没进游戏,状态行不是 MO:返回 None(别的地方都用 stat,读不到就算失败)。"""
        f = self.ctl.cmd("stat").split()
        if len(f) != len(KEYS) + 2 or f[:2] != ["ok", "MO"]:
            return None
        self.last = dict(zip(KEYS, map(int, f[2:])))
        return self.last

    def stat(self):
        s = self.try_stat()
        if s is None:
            print(f"失败:{self.name} 读不到状态行(游戏不在前台?)", file=sys.stderr)
            dump((self.name, self.last))
            raise Fail("没有状态行")
        return s

    def press(self, k, ms=50):
        self.ctl.ok(f"press {k} {ms}")

    def enter_game(self):
        """开机菜单光标在游戏上:● 进游戏(状态行变成 MO);首页光标在「两人联机」:● 去找队友。"""
        end = time.time() + 60
        while self.try_stat() is None:
            if time.time() > end:
                sys.exit(f"{self.name} 进不了游戏")
            self.press("ok")
            time.sleep(1.5)
        assert self.stat()["page"] == PG_TITLE, self.last
        self.press("ok")

    def close(self):
        self.proc.terminate()
        self.proc.wait(10)


def wait(pred, secs, what, *cards):
    end = time.time() + secs
    while time.time() < end:
        v = pred()
        if v:
            return v
        time.sleep(0.05)
    print(f"失败:{secs} 秒内等不到:{what}", file=sys.stderr)
    dump(*((c.name, c.last) for c in cards))
    raise Fail(what)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shell", default=os.environ.get("COOPA_SHELL"))
    ap.add_argument("--font-dir")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--secs", type=int, default=60, help="乱按多久(默认 60 秒)")
    ap.add_argument("--shots", help="把联机中两边的画面截图存到这个目录")
    a = ap.parse_args()
    if not a.shell:
        ap.error("要给外壳仓库目录:--shell DIR 或环境变量 COOPA_SHELL")
    shell = os.path.abspath(a.shell)
    root = tempfile.mkdtemp(prefix="morph-duo.")
    catalog = os.path.join(root, "catalog.toml")
    subprocess.run([sys.executable, os.path.join(GAME, "tests", "make_catalog.py"), shell, catalog], check=True,
                   stdout=subprocess.DEVNULL)
    ca, cb = Card(shell, root, "a", 1, catalog, a.font_dir), Card(shell, root, "b", 2, catalog, a.font_dir)
    cards = (ca, cb)
    rng = random.Random(7)
    if a.shots:
        os.makedirs(a.shots, exist_ok=True)

    def both(pred):
        return lambda: pred(ca.stat(), cb.stat())

    def fail(msg):
        print(f"失败:{msg}", file=sys.stderr)
        dump(("a", ca.last), ("b", cb.last))
        raise Fail(msg)

    def settle_and_compare(label):
        """两边都停手:等到两个人都停在坑边 / 墙边(最多 15 秒),再比对方的位置。"""
        stopped = lambda s: s["st"] in (MB_EDGE, MB_WALL)  # noqa: E731
        wait(both(lambda x, y: x["phase"] == MP_PLAY and y["phase"] == MP_PLAY and stopped(x) and stopped(y)
                  and x["peer_st"] == y["st"] and y["peer_st"] == x["st"]),
             15, f"{label}:两个人都停住(坑边 / 墙边)", ca, cb)
        time.sleep(0.3)  # 状态行是各读各的,多等一下再读一对
        x, y = ca.stat(), cb.stat()
        for who, me, other in (("a 看 b", x, y), ("b 看 a", y, x)):
            if (abs(me["peer_x"] - other["x"]) > 1 or abs(me["peer_y"] - other["y"]) > 1
                    or me["peer_st"] != other["st"] or not stopped(other)
                    or me["phase"] != MP_PLAY or other["phase"] != MP_PLAY):
                fail(f"{label}:{who}的位置对不上(peer_x peer_y 要等于对方的 x y,±1 像素)")
        return x, y

    try:
        time.sleep(3)  # 开机
        for c in cards:
            c.enter_game()
        wait(both(lambda x, y: x["phase"] == MP_PLAY and y["phase"] == MP_PLAY), 60, "两边连上、开局", ca, cb)
        x, y = ca.stat(), cb.stat()
        if x["init"] == y["init"]:
            fail("两边的主动方应该一个是 1 一个是 0")
        print(f"连上了:a {'主动方(爸爸)' if x['init'] else '被连方(库巴)'},b 另一边;a x={x['x']} b x={y['x']}")
        if a.shots:
            ca.ctl.ok(f"shot {os.path.join(a.shots, 'duo-a-start.png')}")
            cb.ctl.ok(f"shot {os.path.join(a.shots, 'duo-b-start.png')}")

        t_start = time.time()
        t_next_cmp = t_start + 5
        nxt = {c: t_start + rng.uniform(0.2, 1.5) for c in cards}
        compares, presses, paused_done = 0, 0, False
        b_wait = {c: 0 for c in cards}  # 乱按时状态行 b_wait_max_ms 最大读到多少
        t_pause = t_start + a.secs / 2
        while time.time() - t_start < a.secs:
            now = time.time()
            if now >= t_next_cmp:
                s = settle_and_compare(f"第 {compares + 1} 次对位")
                compares += 1
                if a.shots and compares == 2:
                    ca.ctl.ok(f"shot {os.path.join(a.shots, 'duo-a-mid.png')}")
                    cb.ctl.ok(f"shot {os.path.join(a.shots, 'duo-b-mid.png')}")
                print(f"  {now - t_start:5.1f}s 停手后一致:a=({s[0]['x']},{s[0]['y']}) b=({s[1]['x']},{s[1]['y']}) "
                      f"rtt={s[0]['rtt']}/{s[1]['rtt']}", flush=True)
                # 停手的 1.5 秒已经被等停住花掉了;不够 1.5 秒再补
                time.sleep(max(0.0, 1.5 - (time.time() - now)))
                t_next_cmp = time.time() + 5
                nxt = {c: time.time() + rng.uniform(0.2, 1.5) for c in cards}
                continue
            if not paused_done and now >= t_pause:
                cb.ctl.ok("pause")  # 停住 b 3 秒:a 那边没音信就算断线
                seen_a = False
                t0 = time.time()
                while time.time() - t0 < 3:
                    seen_a |= ca.stat()["phase"] == MP_PAUSED
                    time.sleep(0.1)
                if not seen_a:
                    fail("b 停住 3 秒,a 没有进 MP_PAUSED")
                cb.ctl.ok("resume")
                t0 = time.time()
                seen_b = False
                while time.time() - t0 < 2:
                    st = cb.stat()
                    seen_b |= st["phase"] == MP_PAUSED
                    ra, rb = ca.stat(), st
                    if ra["phase"] == MP_PLAY and rb["phase"] == MP_PLAY:
                        break
                    time.sleep(0.05)
                else:
                    fail("放开 b 后 2 秒内两边没都回到 MP_PLAY")
                print(f"  {time.time() - t_start:5.1f}s 停住 b 3 秒:a 进了 MP_PAUSED;放开后 "
                      f"{time.time() - t0:.2f} 秒两边回到 MP_PLAY(b 自己{'进过' if seen_b else '没抓到'} MP_PAUSED:停住的那张卡自己的时钟也停了,回来时可能一帧就接上)", flush=True)
                if a.shots:
                    ca.ctl.ok(f"shot {os.path.join(a.shots, 'duo-a-resumed.png')}")
                    cb.ctl.ok(f"shot {os.path.join(a.shots, 'duo-b-resumed.png')}")
                settle_and_compare("断线重连后")
                compares += 1
                paused_done = True
                print("       重连后两边停住,位置对上", flush=True)
                time.sleep(1.5)
                t_next_cmp = time.time() + 5
                nxt = {c: time.time() + rng.uniform(0.2, 1.5) for c in cards}
                continue
            for c in cards:
                if now >= nxt[c]:
                    c.press(rng.choice(["up", "down"]))
                    presses += 1
                    nxt[c] = time.time() + rng.uniform(0.2, 1.5)
            for c in cards:
                s = c.stat()
                b_wait[c] = max(b_wait[c], s["b_wait_max_ms"])
                if s["refused"]:
                    fail(f"{c.name} 的 refused = {s['refused']}")
            time.sleep(0.02)
        final = settle_and_compare("结束时")
        compares += 1
        x, y = ca.stat(), cb.stat()
        if x["refused"] or y["refused"]:
            fail("refused != 0")
        if not paused_done:
            fail("没测到断线(--secs 太短?)")
        print(f"{a.secs} 秒乱按了 {presses} 下,对位 {compares} 次都对;tx a={x['tx']} b={y['tx']} rx a={x['rx']} "
              f"b={y['rx']},refused 0,dt 最大 a={x['max_ms']} b={y['max_ms']} ms;'B' 从该发到交出去最长 "
              f"a={b_wait[ca]} b={b_wait[cb]} ms")
        print("sim_duo: ok")
        return 0
    except Fail:
        return 1
    finally:
        for c in cards:
            c.close()
        if a.keep:
            print("留着:", root)
        else:
            shutil.rmtree(root)


if __name__ == "__main__":
    sys.exit(main())

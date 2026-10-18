#!/usr/bin/env python3
"""桌面模拟器(step 时钟)里把六种变身挨个看一遍:第 4 关 Boss 战(血厚,打不完),每种变身:
BOS2 换下一个变身 + 能量加满 → 变身那一下拍 5 张 → 走两步(看身边的冰晶 / 电火花 / 铁块 / 喷火)→ ▲ 大招隔 200 ms 拍一张;
最后一种再放一次 ▼▲ 超必杀。截图 form<N>-*.png、ult<N>-*.png、super-*.png,另拼一张 <out>/forms.png(要 Pillow)。
  tests/sim_forms.py --shell 外壳仓库目录 --fonts 字库目录 --catalog 目录.toml --out 截图目录"""
import argparse
import os
import subprocess
import sys
import time

KEYS = ("page phase nh row walking hp en form st x foes boss result cine objs dt_avg dt_max draw_max draw_n "
        "net me rtt delay desync step100 ch parts").split()
RUN_MAP, RUN_EVENT, RUN_BATTLE, RUN_WIN, RUN_OVER, RUN_HUB = range(6)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shell", required=True)
    ap.add_argument("--fonts", required=True)
    ap.add_argument("--catalog", required=True)
    ap.add_argument("--out", required=True)
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
    shots = []
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
            return dict(zip(KEYS, map(int, f[2:])))

        def shot(name):
            c.ok(f"shot {a.out}/{name}.png")
            shots.append(name)

        def press(k, wait=300):
            c.ok(f"press {k}")
            c.ok(f"wait {wait}")

        c.ok("wait 1500")
        for _ in range(12):
            if stat():
                break
            c.ok("press ok")
            c.ok("wait 800")
            if not stat():
                c.ok("press down")
                c.ok("wait 300")
        press("ok", 500)                       # 首页 → 营地
        c.ok("serial BOS1")                    # 四关全开
        c.ok("wait 200")
        press("down"); press("down")
        for _ in range(3):
            press("ok")                        # 选到第 4 关
        press("up"); press("up")
        press("ok", 500)                       # 出发
        for _ in range(400):                   # 走到 Boss
            s = stat()
            if s["phase"] == RUN_BATTLE and s["boss"]:
                break
            if s["phase"] == RUN_MAP and not s["walking"]:
                c.ok("serial BOS4")
                c.ok("press ok")
            elif s["phase"] in (RUN_EVENT, RUN_BATTLE):
                c.ok("serial BOS3")            # 路上的小怪一碰就倒
                c.ok("press ok")
            c.ok("wait 400")
        assert s["phase"] == RUN_BATTLE and s["boss"], s
        c.ok("wait 2500")
        for n in range(6):
            s = stat()
            for _ in range(20):                # 等人站好(不在大招里、不在挨打)
                if not s["cine"] and s["st"] in (0, 1):
                    break
                c.ok("wait 150")
                s = stat()
            c.ok("serial BOS2")
            for i, w in enumerate((60, 120, 120, 120, 150)):
                c.ok(f"wait {w}")
                shot(f"form{n}-{i}")
            c.ok("wait 700")
            shot(f"form{n}-walk")
            c.ok("press up")
            for i in range(14):
                c.ok("wait 200")
                shot(f"ult{n}-{i}")
            c.ok("wait 1500")
        c.ok("serial BOS2")
        c.ok("wait 900")
        c.ok("press down")
        c.ok("wait 100")
        c.ok("press up")
        for i in range(14):
            c.ok("wait 200")
            shot(f"super-{i}")
        print("最后:", stat())
        print("lvmem:", c.cmd("lvmem")[:200])
    finally:
        proc.terminate()
        proc.wait(10)
    try:
        from PIL import Image
    except ImportError:
        return
    ims = [Image.open(f"{a.out}/{n}.png").convert("RGB") for n in shots]
    w, h = ims[0].size
    cols = 10
    sheet = Image.new("RGB", (cols * (w // 2 + 2), (len(ims) + cols - 1) // cols * (h // 2 + 2)), (255, 255, 255))
    for i, im in enumerate(ims):
        sheet.paste(im.resize((w // 2, h // 2)), ((i % cols) * (w // 2 + 2), (i // cols) * (h // 2 + 2)))
    sheet.save(f"{a.out}/forms.png")


if __name__ == "__main__":
    main()

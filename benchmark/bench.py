"""bench.py <cmd|rev|gen> <w8|athei|native> <fs|win> <round>: one benchmark run.

Runs one game scene in the work folder of setup.sh (BENCH_WORK, default
~/Library/Caches/mac-windows-gaming-bench) and prints one result line:
the CPU time of the game process in a fixed time window (cores: 1.00 = one
full core) and the frame rate. A failed start is retried, at most 3 tries; each
failure is printed, so the log also shows the start reliability. The line is
also added to <work>/results.txt. Game log and screenshots: <work>/runs/.

Scenes and windows (seconds from the start):
  cmd  mission of save slot 1 (scripts/commandos.txt), CPU from 30 to 90
  rev  new game in the Keep (scripts/revenant.txt), CPU from 40 to 95
  gen  3D menu scene, no input, CPU from 150 to 270, stopped at 275
Frame rate: Wine: median of the second half of the WINEDEBUG=fps values.
Native: screen updates per second from the <P>TRACE_LAG lines in the window.
Mode: fs = full screen (the game's own setting), win = a window (Generals
only: -win). The native ports always start in their default (full screen).
"""
import datetime
import os
import re
import statistics
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
W = os.environ.get("BENCH_WORK", os.path.expanduser("~/Library/Caches/mac-windows-gaming-bench"))
G = os.path.expanduser("~/Games")
ATHEI = os.path.expanduser("~/Applications/Wine athei")
game, wine, mode, rnd = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
tag = f"{game}-{wine}-{mode}"
WINDOW = {"cmd": (30, 90), "rev": (40, 95), "gen": (150, 270)}[game]
SHOTS = {"cmd": (40, 85), "rev": (50, 90), "gen": (160, 265)}[game]
END = {"gen": 275}.get(game)

if wine == "native":
    exe, prefix_env = {
        "cmd": (f"{G}/Commandos Behind Enemy Lines/Commandos Behind Enemy Lines (Native).app/Contents/MacOS/Commandos", "COMMANDOS"),
        "rev": (f"{G}/Revenant/Revenant (Native).app/Contents/MacOS/Revenant", "REVENANT"),
        "gen": (f"{G}/Command and Conquer Generals Zero Hour/Command & Conquer Generals Zero Hour (Native).app/Contents/MacOS/GeneralsZH", "GENERALSZH"),
    }[game]
    data = {"cmd": f"{W}/data/cmd-Game Data", "rev": f"{W}/data/rev-Game Data",
            "gen": f"{W}/data/gen/Game Data/Command and Conquer Generals Zero Hour"}[game]
    script = f"{HERE}/scripts/" + {"cmd": "commandos.txt", "rev": "revenant.txt", "gen": "generals.txt"}[game]
    env = dict(os.environ, **{f"{prefix_env}_DATA": data, f"{prefix_env}_SCRIPT": script, f"{prefix_env}_TRACE_LAG": "1"})
    if game == "gen":
        env["GENERALSZH_DOCUMENTS"] = f"{W}/data/gen/native-docs"
    argv, cwd, proc = [exe], "/", "MacOS/" + os.path.basename(exe)
    kill = lambda: None
else:
    wb = "/opt/homebrew/bin" if wine == "w8" else f"{ATHEI}/wine/bin"
    pfx = f"{W}/pfx-{game}-{wine}"
    env = dict(os.environ, WINEPREFIX=pfx, WINEDEBUG="fps", MVK_CONFIG_LOG_LEVEL="0", WINEDLLOVERRIDES="mscoree,mshtml=")
    if wine == "athei":
        env.update(ROSETTA_X87_PATH=f"{ATHEI}/x87sidecar", DYLD_INSERT_LIBRARIES=f"{ATHEI}/qos.dylib")
    run = {
        "cmd": ("drive_c/GOG Games/Commandos", [r"C:\GOG Games\Commandos\cinput.exe", r"C:\GOG Games\Commandos\bench-script.txt"], "comandos.exe"),
        "rev": ("drive_c/Revenant", [r"C:\Revenant\input.exe", r"C:\Revenant\bench-script.txt"], "Revenant.exe"),
        "gen": ("drive_c/EA Games/Command and Conquer Generals Zero Hour", ["game.dat"] + (["-win"] if mode == "win" else []), "game.dat"),
    }[game]
    cwd, argv, proc = f"{pfx}/{run[0]}", [f"{wb}/wine"] + run[1], run[2]
    kill = lambda: subprocess.run([f"{wb}/wineserver", "-k"], env=env, capture_output=True)


def secs(t):
    s = 0.0
    for p in t.split(":"):
        s = s * 60 + float(p)
    return s


def snap():
    """(pid, CPU seconds of the game, CPU seconds of the other Wine processes)."""
    out = subprocess.run(["ps", "-A", "-o", "pid=,time=,command="], capture_output=True, text=True).stdout
    g = other = 0.0
    pid = None
    for line in out.splitlines():
        p, t, cmd = line.split(None, 2)
        if "bench.py" in cmd:
            continue
        if (cmd.rstrip().endswith(proc) if wine == "native" else proc in cmd and "input.exe" not in cmd):
            g += secs(t)
            pid = p
        elif wine != "native" and (cmd.rstrip().endswith("wineserver") or re.search(r"C:\\windows\\|input\.exe|start\.exe|x87sidecar", cmd)):
            other += secs(t)
    return pid, g, other


def frame_rate(text):
    if wine != "native":
        fps = [float(x) for x in re.findall(r"@ approx ([\d.]+)fps", text)]
        fps = fps[len(fps) // 2:]
        return f"fps median {statistics.median(fps) if fps else 0:.1f} min {min(fps) if fps else 0:.1f}"
    pts = [(float(t), int(n)) for t, n in re.findall(r"^\s*([\d.]+) s lag: .*?presents (\d+)", text, re.M)]
    win = [(t, n) for t, n in pts if WINDOW[0] < t <= WINDOW[1]]
    rate = sum(n for _, n in win[1:]) / (win[-1][0] - win[0][0]) if len(win) > 2 else 0
    return f"presents/s {rate:.1f}"


for tries in range(1, 4):
    kill()
    D = f"{W}/runs/{tag}-r{rnd}-t{tries}"
    subprocess.run(["rm", "-rf", D])
    os.makedirs(D)
    log = open(f"{D}/game.log", "w")
    t0 = time.time()
    g = subprocess.Popen(argv, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT)
    snaps = {}
    for at in sorted(set(WINDOW) | set(SHOTS) | ({END} if END else set())):
        while time.time() - t0 < at and g.poll() is None:
            time.sleep(0.2)
        if g.poll() is not None and (game == "gen" or wine == "native"):
            break
        if at in WINDOW:
            snaps[at] = snap()
        if at in SHOTS:
            subprocess.run(["screencapture", "-x", "-t", "jpg", f"{D}/shot{at:03d}.jpg"])
    if END:
        kill() if wine != "native" else g.terminate()
    try:
        g.wait(timeout=60)
    except subprocess.TimeoutExpired:
        kill()
        g.kill()
        g.wait()
    kill()
    log.close()
    text = open(f"{D}/game.log", errors="replace").read()
    a, b = snaps.get(WINDOW[0]), snaps.get(WINDOW[1])
    if not (a and b and a[0] and a[0] == b[0]):
        crash = re.findall(r"Unhandled [^\n]*|err:seh:NtRaiseException[^\n]*|wine: [^\n]*", text)
        line = f"{tag} round {rnd} try {tries}: FAILED ({crash[0][:120] if crash else proc + ' not running through the window'})"
    else:
        span = WINDOW[1] - WINDOW[0]
        other = f", other Wine {(b[2] - a[2]) / span:.3f}" if wine != "native" else ""
        line = f"{tag} round {rnd} (try {tries}): game {(b[1] - a[1]) / span:.2f} cores{other}; {frame_rate(text)}"
    print(line, flush=True)
    with open(f"{W}/results.txt", "a") as f:
        f.write(f"{datetime.datetime.now():%Y-%m-%d %H:%M} {line}\n")
    if "FAILED" not in line:
        break

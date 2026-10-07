# Benchmark: Wine 8, athei and the native ports

These scripts measure the speed of the three games in a fixed way, so that
results of different days can be compared. They compare:

- `w8`: Wine 8 (Homebrew `wine-crossover` 23.7.1) with the `wine-8` prefix
  of the game.
- `athei`: the athei CrossOver 26.3 Wine with x87sidecar and `qos.dylib`
  (the default Wine, see [`common/wine`](../common/wine/README.md)), with the
  `wine-11-athei` prefix.
- `native`: our native ports (the installed `native-mac/<Game> (Native).app`
  of each game). The GeneralsX app is not part of the benchmark.

## Rules

- A run never writes into `~/Games`. `setup.sh` makes an APFS copy of each
  game folder (the layout of the top README) in a work folder (default
  `~/Library/Caches/mac-windows-gaming-bench`), and `relink.py` points every
  link of the copies at the copies. It stops if a link into `~/Games` is
  left.
- All versions of a game use the same game settings: `setup.sh` copies the
  `wine-11-athei` settings into the other versions of the copy (Generals:
  `Options.ini`, High, 1280x800; Revenant: `revenant.ini` with
  `Windowed=No`; Commandos: `ddraw.ini`). (On
  2026-10-07 an earlier test let athei read other options at Low detail; its
  Generals result was not valid.)
- The same input script drives the native port and the Wine version
  (`scripts/`). In Wine, small helper programs play it (`helpers/`, built by
  `setup.sh` with `i686-w64-mingw32-gcc`).
- Rounds: each round runs every chosen game once on every version, one after
  the other. Two rounds are the default (on 2026-10-07 two rounds differed
  by 0.03 cores or less).
- Saves: `setup.sh` records the checksums of the real saves and settings,
  and `run-all.sh` compares them at the end.
- The games take the screen. Run it when the Mac is not in use.

## Run

```sh
benchmark/setup.sh                    # new work folder (an old one goes to the Trash)
benchmark/run-all.sh                  # 2 rounds, w8 and athei, all games, full screen
benchmark/run-all.sh 2 "w8 athei" gen "fs win"   # Generals also in a window
benchmark/run-all.sh 2 "athei native"            # athei against the native ports
```

`bench.py <cmd|rev|gen> <w8|athei|native> <fs|win> <round>` runs one run.
The result lines also go to `<work>/results.txt`; the game logs and two
screenshots per run go to `<work>/runs/`.

Time: Commandos and Revenant about 2 minutes per run, Generals about 5. A
round of `w8` and `athei` with all games in full screen: about 18 minutes.

## What is measured

| Game | Scene | CPU window (seconds from the start) |
|---|---|---|
| Commandos (`cmd`) | mission of save slot 1 | 30 to 90 |
| Revenant (`rev`) | new game, the Keep | 40 to 95 |
| Generals (`gen`) | 3D menu scene, no input (stopped at 275 s) | 150 to 270 |

- CPU: the CPU time of the game process in the window, divided by its
  length (1.00 cores = one full core). For Wine, "other Wine" is the CPU of
  `wineserver`, the Wine system programs and x87sidecar.
- Frame rate: Wine: the median of the second half of the `WINEDEBUG=fps`
  values. Native: screen updates per second from the `<P>TRACE_LAG` lines in
  the window.
- A failed start is retried (at most 3 tries) and printed as `FAILED`.
- Mode `fs` is full screen (the games' own setting; Revenant gets
  `Windowed=No` in its data copy). Mode `win` is Generals with `-win`.

## Results of 2026-10-07 (full screen, two rounds)

| Game | Wine 8 | athei |
|---|---|---|
| Commandos | 0.25-0.28 cores, 19.5-19.8 fps | 0.20-0.22 cores, 19.5-19.8 fps |
| Revenant | 0.42 cores, 24.4 fps | 0.26 cores, 24.4 fps |
| Generals, full screen | 2.19-2.20 cores, 24.0-24.2 fps | 1.43 cores, 31.2 fps |
| Generals, window | 2.20 cores, 23.0-23.6 fps | 1.46 cores, 31.2 fps |

No start failed. On athei, Generals in full screen uses emulated display
modes (`EmulateModeset`); the full-screen and window values show that this
costs no speed.

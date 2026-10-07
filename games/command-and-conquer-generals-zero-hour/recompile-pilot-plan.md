# Plan: recompile Generals Zero Hour to native arm64 (pilot)

Status: plan only, 2026-10-06. Nothing is built yet.

## Why this pilot

The decision of 2026-10-06: native ports are the standard for 32-bit
Windows games without source code, because they do not need Rosetta. Our
method works for small, mostly 2D games (Commandos, Revenant). This pilot
tests it on a big 3D game. The result sets the standard for big games.

Generals is the pilot because it has three references:

- the EA source code (in the `GeneralsX` submodule), which shows what a
  function of the exe does;
- the community native port (GeneralsX), which shows the correct picture
  and speed;
- Wine 8, which runs the same exe.

Other big games have no source code. So the pilot records each time that
the source code helped (a "source assist", see "Records"). The list shows
how much harder a game without source will be.

## What we port

The retail exe of the EA app copy, as it is:

| Item | Value |
|---|---|
| File | `game.dat` (in the Zero Hour Wine game folder) |
| SHA-256 | `9f615bfd3910ca174d0f044cdaee8c6fa26e5fdc28b89cc0e61c52943d857df7` |
| Type | PE32, i386, linker 6.0 (Visual C++ 6), built 2005-03-10 (patch 1.04) |
| Code (`.text`) | 0x537de5 bytes (5.5 MB). Commandos: 0x2d9040 (2.9 MB) |
| Relocations | none (`gen_relocs.py` must find them, as for Commandos) |
| C runtime | imported from `MSVCRT.dll` (138 functions) and `MSVCIRT.dll` (3), not static |
| C++ exceptions | yes: imports `_CxxThrowException` and `__CxxFrameHandler`; the source has 493 `throw` and 124 `catch` |
| Direct3D 8 | loaded at run time: `LoadLibraryA("D3D8.DLL")`, then `GetProcAddress(Direct3DCreate8)` (`dx8wrapper.cpp`) |
| SIMD code | MMX, SSE and 3DNow! code is in the exe (about 11,000 instructions, linear sweep), mostly in CPU-specific paths |

Imports, by DLL: KERNEL32 90, MSVCRT 138, mss32 60 (Miles 6.5c, with 3D
samples), USER32 43, WSOCK32 28, GDI32 18, IMM32 12, binkw32 11 (Bink
video), DBGHELP 8, AVIFIL32 8, ADVAPI32 7, ole32 6, OLEAUT32 6, SHELL32 3,
WINMM 3, MSVCIRT 3, DINPUT8 1.

## Scope

- In: single player. Main menu with the 3D shell map, intro video,
  skirmish against the AI, a campaign mission, save and load, music,
  voices and effects.
- Out: multiplayer (LAN and online). The network functions fail, as in
  Commandos. The map editor (`WorldBuilder.exe`).

## Main design choices

1. **Recompiler:** SRW from M-HT/SR, the same set-up as Commandos and
   Revenant (`common/native-mac/`). Guest addresses stay 32-bit
   (`-ptrofs`).
2. **Direct3D 8:** a new COM layer in our runtime (`runtime/com/d3d8.com`,
   `WinApi-d3d8.c`). It changes the 32-bit guest structures into native
   ones and calls DXVK-native `d3d8` on MoltenVK. This is the same
   graphics path that the community port uses on this Mac. We do not
   write our own 3D renderer. The DXVK build with the Mac fixes comes from
   the DXVK fork that `GeneralsX/cmake/dx8.cmake` uses.
3. **Buffers:** a `Lock` of a vertex or index buffer gives guest memory.
   `Unlock` copies the changed range into the DXVK buffer. This is simple,
   but it costs time (the same cost as the copies of Wine 11). Make it
   faster only after the game works.
4. **SIMD:** SRW reports a CPU with no MMX, SSE or 3DNow! (`cpuid` gives 0).
   The game then uses its plain x86 paths. SRW makes a trap of each SIMD
   instruction. A trap that runs shows a path that has no CPU check; fix
   each one in `srw/`.
5. **C runtime:** the MSVCRT functions are imports, so the runtime gives
   native versions (extend `CLIB.c`). This is easier than in Commandos,
   where the C runtime was inside the exe.
6. **C++ exceptions:** new in the runtime. `_CxxThrowException` and
   `__CxxFrameHandler` read the MSVC exception tables of the exe (FuncInfo,
   unwind map, try blocks, catch types). They call the catch blocks and
   destructors in the recompiled code. The SEH chain (`fs:[0]`) already
   exists (`X86_FS_mem.c`).
7. **Video:** the 11 Bink functions use FFmpeg (libavcodec has Bink video
   and Bink audio) from the conda env of the repository.
8. **Sound:** Miles 6.5c on our mixer (`WinApi-mss32.c`). New: 3D samples
   (position to volume and pan), filter calls (stub), and the 6.5c stream
   functions.
9. **Licenses:** our runtime is MIT. DXVK (zlib) and MoltenVK (Apache 2.0)
   can go into the app. GeneralsX is GPL 3: read its code to understand
   the game, but do not copy its code into the runtime.
10. **Data and saves:** the game data comes from `Game Data`, the same as
    for the other two versions. The saves and options go to a separate
    test folder (a copy). The real saves do not change.

## Steps and checks

Each step ends with a check. After steps 1, 2 and 3, I report to you, and
we decide together to continue or to stop.

### Step 0: set-up

- `games/command-and-conquer-generals-zero-hour/native-recompile/` with
  `game.conf` (exe name and SHA-256), `runtime/game.h`, `srw/`, `Makefile`.
- Snapshot first: `git status`, the SHA-256 of `game.dat`, the checksums of
  `Old Windows Saves` and `Settings and Saves`.
- Check: `make` starts, and stops at the first recompile error.

### Step 1: recompile (stop or go 1)

- `gen_relocs.py` on the 5.5 MB `.text`: code walk from the entry point
  (0x8e0778), vtables, pointer tables, jump tables.
- SRW with `SRW_KEEP_GOING=1`. Sort the traps into SIMD paths (expected)
  and others.
- Check: all code translates to llasm, and the arm64 binary links with
  stub functions for all imports.
- Report: the number of traps, of unknown indirect jumps, and the hours
  spent, against Commandos.

### Step 2: start-up (stop or go 2)

- Native MSVCRT and MSVCIRT functions; C++ exceptions; files (`.big`
  archives through `CreateFileA` and `ReadFile`); registry values from a
  config file; `LoadLibraryA` and `GetProcAddress` for `D3D8.DLL`.
- Check: the game reads its INI files (a test with an INI error must reach
  the right `catch`), and it opens its window.

### Step 3: graphics (stop or go 3)

- The Direct3D 8 COM layer on DXVK-native (about 100 methods of
  `IDirect3DDevice8`, plus textures, surfaces, buffers and the swap
  chain).
- Check: the main menu shows the 3D shell map with the correct ground
  (sand and grass), the same as the community port and Wine 8 (screenshots
  at the same time after the start).

### Step 4: video, sound and input

- Bink, Miles 6.5c, DirectInput 8 (keyboard and mouse), IMM32 stubs.
- Check: the intro plays with sound; the menu music plays; menu buttons
  work with the mouse.

### Step 5: play

- Skirmish against the AI for 10 minutes (scripted input and screenshots).
  A campaign mission. Save and load.
- Check: no crash and no trap; a save of the recompiled game loads in Wine
  8 with the same money and units (the same exe, so the same save format).

### Step 6: speed

- Menu scene at 1280x800, the same method as the 2026-10-06 tests.
- Check: CPU cores and fps against the community port (about 70 fps, 0.46
  cores) and Wine 8 (21.8 fps, 2.22 cores).

### Step 7: app and docs

- `.app` bundle with `make-bundle.sh`, DXVK and MoltenVK inside the app.
- README for the port, and a new row in the top-level README.

## Records

`native-recompile/PILOT-LOG.md` (in the repository, no game data):

- the hours or sessions for each step;
- each "source assist": the problem, how the source code solved it, and
  how we would solve it without source code;
- each new runtime part, so that the next game can use it.

## Risks

- **Size:** 1.9 times the code of Commandos. More missing relocations and
  more SRW fixes.
- **C++ exceptions:** new work, and the start-up needs it (INI parsing
  uses `throw`).
- **SIMD code with no CPU check:** each one is a fix by hand.
- **Threads:** the game makes threads (`CreateThread`, `_beginthread`). The
  recompiled code and our runtime must be safe with more than one thread.
- **DXVK in a 32-bit guest:** structures with pointers and handles (for
  example `D3DPRESENT_PARAMETERS`, `D3DLOCKED_RECT`) need a change in each
  direction. The window handle must be the SDL window that DXVK expects.
- **Effort:** I cannot estimate it well. The reports at stop-or-go 1 to 3
  will show it.

## Not changed

- The installed community port, the Wine prefixes and the real saves.
- No dotfiles, no PATH changes, no installs outside the conda env of the
  repository. No commits until you ask, no push until you say "push".

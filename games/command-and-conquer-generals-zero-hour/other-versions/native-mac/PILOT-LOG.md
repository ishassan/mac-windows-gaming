# Pilot log: native recompile of Generals Zero Hour

The records that the pilot plan asks for (`../recompile-pilot-plan.md`,
"Records"): time per step, each "source assist", and each new runtime part.
No game data is in this file.

## Step 0: set-up (2026-10-06)

- Folder `native-recompile/` with `game.conf`, `Makefile`,
  `runtime/game.h`, `srw/` (the same layout as the other two ports).
- Snapshot before the work: `git status` (only the plan file was new), the
  SHA-256 of `game.dat` (the one in `game.conf`), the checksums of the 64
  files in `Old Windows Saves` and `Settings and Saves`, the file list of
  `Game Data`.
- Time: under one hour.

## Step 1: recompile (2026-10-06 to 2026-10-07)

Result: the whole exe translates, and the arm64 program links (40 MB).

| Item | Generals | For comparison: Commandos |
|---|---|---|
| Decoded code | 1,630,770 instructions, 95.6% of `.text` | 2.9 MB `.text` |
| Relocations found | 187,839 | |
| C++ EH FuncInfo tables | 4,991 | |
| Jump tables | 478 | |
| SRW traps (instructions that SRW cannot translate) | 16,691, almost all MMX, SSE and 3DNow! | |
| Flag entries (`instruction_flags.sci`) | 3,808 (3,805 by `fix_flags.py`, 3 by hand) | 544 |
| Hand-written replacements | 8 | 2 |
| Translated llasm | 87 MB | |
| Build time (translate + compile, 8 cores) | about 9 minutes | |

Time: about 5 hours of work.

What was new for a big exe, and how it was fixed (shared tools, so the next
game gets it too):

1. **Numbers that look like code addresses.** The `.text` range of a 5.5 MB
   exe holds many plain numbers (two 16-bit values such as `0x00450008`).
   The old rule for code pointers in data made code at about 300 false
   addresses. New option `--strict-data-code` of `gen_relocs.py`
   (`GEN_RELOCS_OPTIONS` in `game.conf`): a code pointer in data makes new
   code only at a function start; a guessed start that overlaps other code
   is rejected and the analysis runs again (13 rejected).
2. **Calls that never return.** After `call _CxxThrowException` the compiler
   puts data or the next function. `gen_relocs.py` and SRW now stop after
   calls to imports that never return (`noret_procedures.sci` for the
   thunks).
3. **Imports by ordinal** (OLEAUT32) and C++ names of MSVCRT imports
   (`gen_extern.py`, SRW loader).
4. **Missing instructions in SRW:** `cmovcc` (422 uses in the fast float
   code), `fsincos`, `fldl2e`, `ffree`, `fcomip`, and `lock bts` (two spin
   locks, by hand, with an atomic helper).
5. **llasm** dropped temporary values after each helper call. SRW puts the
   `fs:[0]` write of the MSVC 6 SEH prologue between `cmp` and `jcc`, so 18
   functions failed. Fixed in llasm (`patches/llasm.patch`).
6. The exports of the exe (two copy protection data names with `?` and `@`)
   are removed from the output.

Source assists in this step: none. The exe and the tools were enough.

## Step 2: start-up (2026-10-07)

New runtime parts (shared, in `common/native-mac/runtime/`):

- `WinApi-msvcrt.c`: the MSVCRT functions (about 140): FILE on guest
  memory, 16-bit wide strings, scanf for all conversions, time, sorting
  with x86 compare functions, start-up (`__getmainargs`, `_initterm`).
- `WinApi-msvcrt-eh.c`: C++ exceptions (`_CxxThrowException`,
  `__CxxFrameHandler` with the MSVC tables of the exe) and the MSVC 6 SEH
  handler (`_except_handler3`).
- `llasm/asm-llasm.c`: the x86 code can continue after a catch at a frame
  that an outer C entry started (longjmp to that entry).
- `WinApi-registry.c`: a read-only registry from the game's `game.h`.
- `tools/gen_glue.py`: calling conventions `c` (cdecl), `t` (thiscall),
  `f` (result on the x87 stack) and `d` (data imports).

Problems found at start-up:

1. The static initializer at `0x62a810` follows a call to
   `_CxxThrowException`, so it did not look like a function start, and its
   pointer in the initializer table was not relocated. The program jumped to
   the raw address. Fix: `gen_relocs.py` accepts a function start after a
   call that never returns.
2. "Corrupted heap": the MSVC check of a static buffer does
   `xor ecx, <buffer address>`. That address was not relocated. Fix:
   `gen_relocs.py` relocates `xor`, `and` and `sbb` operands that point to
   data, and SRW translates `and`, `or` and `xor` with a relocated
   immediate.
3. The stack pointer became a code address: a vtable entry was not
   relocated, because a jump table hid the start of the function. Fix: the
   "vtable run" rule of `gen_relocs.py` (an aligned run of three or more
   code pointers where at least two others are good).
4. Smaller ones: `FindClose` with an invalid handle, `LoadImageA` for
   icons and cursors, `DirectInput8Create` (new `com/dinput8.com`), the
   cpu structure moved out of the x86 stack area (2 MB stack with guard
   pages).

Source assists in this step:

1. The registry values that the game reads (`InstallPath`, `Language`,
   `Version`, `MapPackVersion`): seen in `Win32BIGFileSystem.cpp` and
   `registry.cpp`. Without source: the same values are in the Wine prefix
   (`wine/prefix.reg`), and a registry trace (`GENERALSZH_TRACE_REG=1`)
   lists every key that the game asks for.

Time: about 4 hours.

## Step 3: graphics (2026-10-07)

Result: the loading screen, the movies and the main menu draw through DXVK
(Direct3D 8 to Vulkan) and MoltenVK (Vulkan to Metal), all arm64.

New runtime parts:

- `tools/gen_d3d8.py`: reads `d3d8.h` of DXVK and writes the COM glue list
  (12 interfaces) and the pass-through methods.
- `d3d8/WinApi-d3d8.c`: a guest wrapper object for each DXVK object,
  shadow buffers for `Lock`, `D3DPRESENT_PARAMETERS` to a window that DXVK
  knows (SDL window with Vulkan), screenshots with `GENERALSZH_DUMP`.
- `game.mk`: `DXVK_INCLUDE` in `game.conf` turns the D3D8 part on.

Problems:

1. `CreateDevice` failed: a windowed device must have the default present
   interval.
2. The menu had no buttons in the first tests. Cause: the test stopped
   during the 65 s trailer (Escape was sent while the EA logo played, and
   the game ignores Escape then). With Escape at 12 s the menu comes.
3. The menu showed the static backdrop picture, not the 3D shell map. The
   game turns the shell map off when the CPU speed is under 600 MHz. Our
   `cpuid` reported no time stamp counter, so the speed was 0. Fix: SRW
   translates `cpuid` and `rdtsc` to runtime helpers (`x86_cpuid`: family 6
   with FPU and TSC, no MMX or SSE; `x86_rdtsc`: a 3 GHz counter).
4. The ground was black. Three faults in the Direct3D 8 layer, found with a
   call trace (`GENERALSZH_TRACE_D3D8=3` prints each call with its
   arguments) and dumps of the lock buffers:
   - The shroud (fog of war) multiplies the ground. The game locks its
     system-memory shroud surface once, keeps the pointer after
     `UnlockRect`, and writes to it in each frame before `CopyRects`.
     Direct3D allows that. The layer gave a guest copy, so the shroud stayed
     black. Fix: `CopyRects` and `UpdateTexture` first write a kept copy of
     a system-memory surface back to the native surface.
   - A lock of a write-only vertex buffer did not start with the current
     data, so the vertices that the game did not change became garbage at
     `Unlock`. Fix: only a discard lock skips the copy.
   - (Made during this work: an argument trace in `gen_d3d8.py` reused a
     variable name, and methods such as `GetDirect3D` wrote a 64-bit pointer
     into a 32-bit slot. Fixed before the result.)

Source assists in this step:

1. Why the menu did not come: `GameClient::update` (the intro state) and
   `Display::update` / `isMoviePlaying`. Without source: a breakpoint trace
   of the branches near the "Sizzle640" string shows the same thing (this
   was done too), but the source told what the flags mean.
2. Why the shell map was off: `GameLOD.cpp` (`m_memPassed`,
   `isReallyLowMHz`) and `cpudetect.cpp`. Without source: the game reads
   `GlobalMemoryStatus` and runs `cpuid` and `rdtsc`; a trace of untranslated
   or stubbed CPU instructions points there.

## Step 4: video, sound and input (2026-10-07)

- Bink movies play with sound (FFmpeg 8.0 LGPL from the conda env).
  Escape skips the trailer. The game ignores Escape while the EA logo
  plays, as on Windows.
- Menu music: the game opens `Data\Audio\Tracks\USA_11.mp3` from its
  archives through the Miles file callbacks. The effects play as samples.
- Mouse and keyboard work in the menus. On the first start the menu buttons
  come only after the mouse moves or a key is pressed (the original
  behavior, `MainMenu.cpp`).
- The skirmish player name is the host name (`gethostname`). The network
  start (`WSAStartup`) failed on purpose, so the name was empty. Now
  `GAME_WSA_STARTUP_OK` in `game.h` makes it succeed for this game only
  (sockets still fail); the name is "Player".

Source assists: `MainMenu.cpp` (when the buttons show),
`SkirmishGameOptionsMenu.cpp` and `IPEnumeration.cpp` (the player name).
Without source: a breakpoint on the menu update and a trace of the
`gethostname` call give the same facts. The shroud fault (Step 3) was found
from the call trace only, not from source.

## Step 5: play (2026-10-07)

1. Skirmish against the Easy AI: the game starts, units train, money and
   tooltips work. A crash after about 2 minutes: the x86 stack pointer
   became `0x652c20`. The vtable at `0x951838` has one entry, and its bytes
   look like text, so `gen_relocs.py` did not relocate it. New rule: a data
   word that code uses as an address (a constructor stores the vtable) and
   that points to a function start is a code pointer.
2. After that fix: 10 minutes of skirmish with no crash (GLA against the
   Easy AI; workers trained, money went down, the AI attacked at the end).
   About 31 presented frames per second (the game's limit is 30).
3. An intermittent crash in `IDirect3DIndexBuffer8::Lock` (about 1 run in
   5). DXVK frees an object when the device drops its last reference
   (`SetIndices`, `SetTexture`) without a `Release` from the game, and a new
   object of another type can get the same address. The layer then gave
   the old wrapper (with the wrong method table) for the new object. Fix:
   reuse a wrapper only for the same interface. After this fix the
   infantry drew with normal colors (before: rainbow colors). A wrong
   texture through an old wrapper is the likely cause; not proven.
4. The saves list was empty: the game lists its saves with
   `SetCurrentDirectoryA(<save folder>)` and `FindFirstFileA("*")`, and the
   runtime ignored the current directory. Now `SetCurrentDirectoryA` works
   (relative paths start at the current directory).
5. Save and load, both ways:
   - A save of the Windows version on Wine (`GLA 5`, from 2024) loads in
     this port (the GLA 5 mission, money $7,500 at the start).
   - A save of this port (`00000014.sav`, the same mission, $7,200 at
     save time) loads in Wine 8 (wine-crossover 23.7.1), on a clone of the
     Wine prefix with a scratch documents folder. Same base and buildings;
     the money showed $8,100 about 50 s after the load (the game ran
     during the wait), so an exact money match is not shown.
   - Input to Wine without macOS focus: a small helper inside Wine posts
     mouse messages to the game window (`PostMessage`).
6. Campaign: USA mission 1 starts (difficulty menu, briefing movie with
   sound, the mission scene). The original 1.04 cannot skip the briefing
   with Escape; the community code added that.
7. Trees drew with speckled wrong colors. Their texture is correct at full
   size, but its smaller mip levels were garbage. The game makes them with
   the D3DX box filter in the exe (`D3DXFilterTexture` at `0x7dca6a`),
   which rounds with `lea ecx, [ecx + edx + 0x800080]`. `0x800080` is in the
   `.text` range, so `gen_relocs.py` relocated it as an address. New strict
   rules: an `lea` with two registers and a displacement in `.text` is a
   constant; a data word that equals an instruction inside a function (not
   a function start), in a run of at most two code-like words with one such
   value, is a number. The first form of this rule removed about 180
   entries, among them a long table of code labels at `0x9bd168` that may
   be real, so it was narrowed. Result: 15 fewer relocations (the D3DX
   masks, bytes of import names such as `.dll\0`, pairs of 16-bit numbers),
   and the trees draw correctly.
8. Time: about 6 hours (with Step 4).

Debug tools made in this step: `GENERALSZH_BACKGROUND=offscreen` (SDL's
offscreen driver with Vulkan headless surfaces: tests run with no window
and take no focus), `GENERALSZH_DUMP_TEXTURES` (each texture the game
fills, as BMP files), the caller address in the level-3 Direct3D trace,
and the library name in crash reports.

Source assists in this step: `W3DTreeBuffer.cpp` (how the tree texture is
built and filtered). Without source: the texture dump showed the bad mip
levels, and the call trace showed the D3DX code that made them.

## Step 6: speed (2026-10-07)

Menu scene at 1280x800 (Escape at 12 s, mouse moved at 18 s), CPU time of
the process from 30 s to 90 s (`ps`), frames from `GENERALSZH_TRACE_LAG`:

| Version | CPU cores | Frames per second | CPU per frame |
|---|---|---|---|
| This port, before the `Sleep(0)` change | 1.04 | 31 | 33 ms |
| This port (offscreen, no window) | 0.26 | 31 | 8.4 ms |
| This port, after the menu crash fix (offscreen, shadows on) | 0.26 | 31.3 | 8.3 ms |
| This port, after the menu crash fix (window) | 0.25 | 31.3 | 8.0 ms |
| Community port (2026-10-06, window) | 0.46 | about 70 | 6.6 ms |
| Wine 8 (2026-10-06, window) | 2.22 | 21.8 | 102 ms |

- The game limits itself to 30 frames per second, and waits with a
  `Sleep(0)` loop (a profile showed 71% of the main thread samples in
  `Sleep`). Windows also spins a core there. The runtime now sleeps 0.2 ms
  on repeated `Sleep(0)` calls.
- Before the menu crash fix, the windowed CPU reading failed (the run
  crashed at 75 s, see "After the plan"). The two "after" rows are from
  the same build, 2026-10-07, while the Mac was idle.
- The comparison is not exact: the community port runs at about 70 fps
  with its own frame pacer, and the offscreen run has no window present.

## Step 7: app and docs (2026-10-07)

- `make-bundle.sh` makes `build/Command & Conquer Generals Zero Hour
  (Native).app` (about 150 MB). New in the shared script: the libraries of
  `EXTRA_DYLIBS` with all their conda dependencies (FFmpeg brings many),
  links to the conda C++ library changed to the macOS one, and DXVK, the
  Vulkan loader and MoltenVK from `DXVK_LIB_DIR` (the community port's app).
  The runtime finds the MoltenVK driver file in `Resources/`.
- The app starts from its own libraries and reaches the menu (tested with a
  scratch saves folder). Only arm64.
- `README.md` in this folder, a row in the top-level README, and the new
  runtime parts in `common/native-mac/README.md`.
- DXVK writes its shader cache to the saves folder and no log files (before,
  it wrote both into `Game Data`; the two test files went to the Trash).

## After the plan: the menu crash (2026-10-07)

1. The crash came at 75 to 78 s in every windowed run, and never
   offscreen. The difference: SDL's offscreen driver has no display modes,
   so the game found no 32-bit mode and made the device with a 16-bit
   back buffer and a depth buffer without stencil (`D3DFMT_D32`). With a
   window it takes `A8R8G8B8` and `D24S8`, and with a stencil buffer it
   draws volume shadows. Now the Direct3D 8 layer reports a list of usual
   desktop modes when the driver has none. After that, offscreen runs
   crashed at the same place (3 of 3).
2. The crash was not in the code that it showed. Logs from Python
   breakpoints in lldb (batch mode does not print the output of breakpoint
   commands) showed the last shadow volume before the crash: a mesh with
   1398 shadow polygons, 4194 indices. `W3DBufferManager::getSlot` (index
   buffer, `0x7d59f0`) rounds the size to 32 and uses `size / 32 - 1` as
   the index into a table of 128 entries, with no check. Index 131 reads
   past the table, and the "slot" pointer is a float of other data. The
   shadow code then used it (`ebp` at `0x7a6707`).
3. Fix: two entries in `srw/llasm/instruction_replacements.sci` check the
   index in the vertex buffer (`0x7d57cd`) and index buffer (`0x7d5a00`)
   functions and return no slot. The callers already handle no slot (no
   shadow for that mesh). The later source has the same check
   (TheSuperHackers, 2025-05-18: "a mesh is too complex to draw shadows
   with"). So the bug is in 1.04 itself. Why Windows does not reach it in
   this scene was not checked (the game may choose other shadow or detail
   settings there).
4. While looking for the cause, an lldb run also crashed on the Miles
   end-of-sample thread, in game code. The runtime called the game's
   callback from its own thread, at the same time as the main loop, and
   the Generals callback starts the next sound and changes the audio
   manager's lists. With `GAME_MSS_EOS_MAIN_THREAD` (in `game.h`, Generals
   only) the callbacks now run on the main thread from `Sleep` and
   `PeekMessageA`, outside the game logic. This did not fix the 75 s
   crash (3 of 3 runs still crashed), but it removes a data race. Revenant
   and Commandos keep the thread (the new hook is empty for them).
5. Result, offscreen with the new device set-up (stencil, volume
   shadows): 3 menu runs of 105 s, one menu run of 6 minutes, and 11.8
   minutes of skirmish (GLA against the Easy AI; at 10 minutes the base
   was up and the AI attacked), all with no crash.

Source assists in this step: `W3DVolumetricShadow.cpp` (to name the
function at `0x7a6530`), `W3DBufferManager.cpp/.h` (the slot table size and
the later fix), `MilesAudioManager.cpp` (what the callback does).

## Shared changes, checked on the other ports

Revenant and Commandos were rebuilt with all shared changes (SRW `cpuid`
and `rdtsc`, the relocation rules in strict mode only, the runtime). Both
reach the same screens as their installed apps (mean pixel difference under
0.3 of 255), and their game folders did not change.

## Open

1. The exact money check of the cross-load (Step 5) was not made.
2. Not tested: a full campaign mission, long play, the other campaigns,
   Generals Challenge, replays, the options screen.

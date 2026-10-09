# common: shared parts of the native ports

Everything here is used by more than one game. A game folder has only its own
settings (`game.conf`, `runtime/game.h`, `srw/`, `macos/Info.plist`).

## How a port works

1. **Recompile.** SRW (from [M-HT/SR](https://github.com/M-HT/SR)) translates
   each x86 instruction of the game exe to llasm. llasm makes LLVM IR, and
   LLVM (`opt`, `llc`) makes arm64 code. Guest addresses stay 32-bit: a guest
   address is the host address minus a fixed offset (`-ptrofs`).
2. **Relocations.** Most old exes have no relocation table. `tools/gen_relocs.py`
   finds the values that are addresses (code walk from the entry point,
   vtables, pointer tables, jump tables), so that SRW can move them.
3. **Glue.** Each Windows function that the exe imports becomes an llasm
   procedure that reads the x86 stack and calls a C function `<Name>_c`
   (`tools/gen_glue.py` from `runtime/imports.spec`). COM interfaces
   (DirectX, DirectPlay) come from `runtime/com/*.com` lists
   (`tools/gen_com.py`).
4. **Runtime.** The C functions in `runtime/` implement the Windows APIs on
   SDL2 and macOS frameworks.

## Build rules

`mk/game.mk` has all rules. A game's `Makefile` is:

```make
COMMON := ../../../common/native-mac
include $(COMMON)/mk/game.mk
```

(A port in `other-versions/`, such as Generals: `COMMON := ../../../../common/native-mac`.)

`game.conf` (KEY=value) gives `GAME_NAME` (program name), `GAME_EXE` (exe in
the game folder), `GAME_EXE_SHA256` (the exe version that the port supports:
the build stops for another version, because the fixes in `srw/` use fixed
addresses), `GAME_LLASM` (SRW output name), `GAME_DIR_DEFAULT` (game
folder, relative to `$HOME`), `APP_NAME` and `APP_ICON_SOURCE` (for the app
bundle) and optional `EXTRA_DYLIBS`.

A game can add its own runtime files in `<game>/runtime/` (a C file with the
same name as a shared file replaces it) and its own `runtime/imports.spec`
(added to the shared one).

## Recompiler settings (`<game>/srw/`)

| File | Use |
|------|-----|
| `SR.cfg` | SRW settings. |
| `jump_tables.txt` | Jump tables that `gen_relocs.py` cannot size: `<table> <first index> <entries>`. |
| `data_in_text.txt` | Data inside `.text` that must not be decoded as code: `<start> <length>`. |
| `not_relocations.txt` | Values that look like addresses but are constants (the address of the 4-byte value). |
| `llasm/external_procedures.sci` | x86 functions replaced with native code (`loc_X,name_asm2c`). |
| `llasm/instruction_replacements.sci` | llasm code for x86 instructions that SRW cannot translate. A replacement does not end the flow: it must reach the next instruction or end with `tcall`/`endp`. |
| `llasm/instruction_flags.sci` | Flag dependencies (`tools/fix_flags.py` makes the entries; `tools/check_flags.py` finds the ones that SRW does not report). |
| `llasm/global_aliases.sci` | Names for labels (for example the entry point). |

`tools/run-srw.sh` copies these files into `build/srw/`, runs `gen_relocs.py`
and SRW (with `SRW_KEEP_GOING=1`: an instruction that SRW cannot translate
becomes a trap, listed in `build/srw/srw-traps.txt`), and stages the output.

## Runtime (`runtime/`)

| Files | Contents |
|-------|----------|
| `main.c`, `game-info.h` | Start-up, guest memory, game folder, `game_getenv`. Values from the game's `game.h`. |
| `WinApi-kernel32*.c`, `X86_FS_mem.c`, `CLIB.c` | Files, memory, time, INI files, threads, SEH, the C library. |
| `WinApi-sync.c` | Events, mutexes, waits, threads (`CreateThread`), multimedia timers. |
| `WinApi-user32.c`, `input-script.c` | Window, messages from SDL input, cursor, keyboard state; scripted input for tests. |
| `WinApi-gdi32.c`, `WinApi-gdi-text.c` | GDI objects; fonts and text on DirectDraw surfaces. CoreText finds the macOS font of the same name and gives the metrics. FreeType draws the glyphs with the font hints. Smooth (grayscale) edges only at the sizes where the font's "gasp" table asks for them, as in Wine; next to a color key, edge pixels mix with black. |
| `WinApi-ddraw.c` | DirectDraw 1 to 4: surfaces, blits, color keys, palettes, display. |
| `WinApi-d3d.c` | Direct3D 6 (IDirect3D3, IDirect3DDevice3) with a software rasterizer: transforms, lighting, textures, z-buffer, blending. The depth range is 0..1, and point lights use the Direct3D 6 attenuation rule, as in Wine. |
| `WinApi-dinput.c` | DirectInput 5: system keyboard and mouse (no joysticks). |
| `WinApi-dplay.c` | DirectPlay 4 objects without network (for games that make them at start-up). |
| `WinApi-mss32.c`, `WinApi-mss32-redbook.c`, `audio-mixer.c`, `audio-decode.c` | Miles Sound System on a native mixer; MP3 samples in memory and CD audio from audio files (macOS AudioToolbox). End-of-sample callbacks come from a service thread, or on the main thread (from `Sleep` and `PeekMessageA`) for a game with `GAME_MSS_EOS_MAIN_THREAD` in `game.h`. |
| `WinApi-smackw32.c` | Smacker video decoder and player. |
| `WinApi-amstream.c`, `video-avi.c` | DirectShow multimedia stream: AVI with Cinepak and MS ADPCM. |
| `WinApi-winmm.c`, `WinApi-misc.c` | winmm timers, COM, shell functions, and the network stubs (sockets fail). |
| `WinApi-registry.c` | A read-only registry with the values of the game's `runtime/game.h` (`GAME_REGISTRY_VALUES`). |
| `WinApi-msvcrt.c`, `WinApi-msvcrt-eh.c` | The MSVCRT DLL (for games that import it, such as Generals): files, strings, scanf/printf, time, start-up; C++ exceptions (`_CxxThrowException`, `__CxxFrameHandler`) and the MSVC SEH handler. |
| `WinApi-dinput8.c` | DirectInput 8 on the DirectInput 5 code. |
| `WinApi-various.c` | Smaller kernel32, user32 and gdi32 functions and stubs that Generals needs. |
| `d3d8/` | Direct3D 8 on DXVK and MoltenVK (only for games with `DXVK_INCLUDE` in `game.conf`): a guest wrapper for each DXVK object, lock buffers with tight pitches, kept system-memory locks, a list of display modes when the video driver has none (offscreen). `WinApi-d3d8-gen.c` comes from `tools/gen_d3d8.py`. |
| `bink/` | Bink video on FFmpeg (only for games with `USE_BINK=1`). |
| `lag-trace.c` | The `TRACE_LAG` measurements. |
| `imports.spec` | One line per imported function: return type, name, parameters (`*` marks a pointer). |
| `com/*.com` | COM interface lists in vtable order. |
| `llasm/` | The llasm support code of M-HT/SR and the native replacements for C runtime functions. |

## Options for every game

**Window and full screen.** Cmd+Return or Alt+Return switches between a
window and full screen (at the desktop size). The start mode is
`GAME_DISPLAY_MODE` in the game's `game.h` (0 window, the default; 1 full
screen at the desktop size; 2 full screen with a display mode change). The
player can change it with `Display_Mode=window`, `desktop` or `fullscreen`
in the game's config file (`GAME_CONFIG_FILE` in `game.h`, a path relative
to the game folder: `native-mac/Settings/` of the layout in the top README),
which `runtime/Game-Config.c` reads.

**Cmd+Q does not quit (2026-10-09).** Cmd+Q is next to Cmd+1 and easy to
press by mistake, and the game then quits without a question, so the
progress since the last save is lost. `runtime/mac-quit-key.c` takes the key
off the Quit item of the app menu that SDL makes, and `WinApi-user32.c`
drops the Cmd+Q key events, so the game does not get them either. To quit,
use the game's own menu, the Quit item of the app menu, or Quit in the Dock.
Tested on the Mac 2026-10-09 (Commandos): Cmd+Q did nothing, and the Quit
menu item quit the game. The Commandos and Revenant native apps with this
change (commit `9be69a9`) were installed 2026-10-09. The apps before them
went to the Trash as "<app name> app before Cmd+Q fix (2026-10-09)".

**Path redirects.** `GAME_PATH_REDIRECTS` in a game's `game.h` lists pairs
of a path that the game opens and the real path, both relative to the game
folder, with `/`. `CLIB_FindFile` (`CLIB.c`), which every file function
uses, changes the first match (no case, whole path parts). This keeps the
original game files as they are: Revenant writes `revenant.ini`, `Curmap`
and `Save` into its own folder, and the port sends them to
`../native-mac/Settings` and `../Saves`. Commandos and Generals send the
save folder in My Documents to the shared `Saves` the same way.

**INI overrides.** `GAME_INI_OVERRIDES` in a game's `game.h` lists INI
values (section, key, value) that `GetPrivateProfileStringA` gives in place
of the file. `WritePrivateProfileStringA` does not write these keys, so the
file keeps the player's value.

**VSync.** Off by default: with VSync, each screen update waits for the
display refresh, and Commandos updates the screen at each mouse move (the
camera and the keys then lag). `VSync=on` in the same config file turns it
on. The screen is updated at most about 60 times a second.

**Environment variables.** The prefix is the game's `GAME_ENV_PREFIX` (for
example `REVENANT_`).

| Variable | Effect |
|----------|--------|
| `<P>DATA=<folder>` | Game folder. |
| `<P>SCRIPT=<file>` | Scripted input: `<ms> move x y`, `click x y`, `rclick x y`, `down x y`, `up x y`, `key <name>` (`Ctrl+S`, `Cmd+Return` hold a modifier), `text <string>`, `shot <name>`, `quit`. |
| `<P>DUMP=<folder>` | Saves every 30th frame as BMP; the folder for `shot`. |
| `<P>BACKGROUND=1` | No window on the screen and no sound: SDL draws into memory (its `dummy` video and audio drivers), so a test does not take the screen or the keyboard. Use it with `<P>DUMP` or `shot` to see the frames. Without it, the game starts normally. |
| `<P>TRACE_FILES=1` | File opens and searches. |
| `<P>TRACE_MSG=1` | Window messages and key state reads. |
| `<P>TRACE_SOUND=1` or `2` | Miles and CD audio calls (2: every call). |
| `<P>TRACE_VIDEO=1` | Video player calls. |
| `<P>TRACE_GDI=1` | Fonts and text. |
| `<P>TRACE_DDRAW=1`, `<P>TRACE_D3D=1` or `2` | DirectDraw and Direct3D calls. |
| `<P>TRACE_DINPUT=1`, `<P>TRACE_SYNC=1`, `<P>TRACE_TIME=1` | DirectInput, sync objects, `Sleep` statistics. |
| `<P>TRACE_LAG=1` | Once a second: game frames, game work time, screen updates and the time they block, mouse moves, key delay. Also a "slow" line for each event above its limit (`runtime/lag-trace.c`). |

On a crash, the program prints the host and guest registers and a guest stack
trace (only the first entry is always correct).

## Tools (`tools/`)

| Tool | Use |
|------|-----|
| `env.sh` | Source it to use the conda env without `conda activate`. |
| `install-ldc.sh`, `fetch-sr.sh`, `build-tools.sh` | The D compiler; M-HT/SR at a fixed commit; SRW and llasm with `patches/srw.patch`. Output in the repository's `build/`. |
| `save-srw-patch.sh` | Saves local SRW changes into `patches/srw.patch`. |
| `gen_relocs.py` | Finds the relocations of an exe that has none. |
| `listing.py`, `find_overlaps.py` | A code listing (`build/listing.txt`); jumps into the middle of instructions. |
| `fix_flags.py` | Makes `instruction_flags.sci` entries. |
| `check_flags.py` | Finds jump targets that read CPU flags that SRW did not compute (stale flags). `run-srw.sh` runs it, and the build stops when it finds one. |
| `gen_glue.py`, `gen_extern.py`, `gen_com.py` | The llasm glue between the recompiled code and the runtime. |
| `pe_analyze.py`, `crt_probe.py` | Find and name C runtime functions in the exe. |
| `disasm.py <hex> [before] [after]` | The x86 code at an address. |
| `run-game.sh`, `debug-game.sh` | Run the game for N seconds (with lldb for the second). |
| `sym.sh`, `trace.py` | Turn host addresses and guest stack words into `loc_` names. |

In lldb, each x86 label is a function `loc_XXXXXX` (`breakpoint set -n
loc_XXXXXX`). Heap and stack memory is at `pointer_offset + address`. The
`.data` section of the exe is in the program at `_data`: the exe address
`X` is at `(char *)&_data + X - <start of .data>` (for Revenant the start is
`0x5c5000`).

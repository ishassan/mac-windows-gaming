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
COMMON := ../common
include $(COMMON)/mk/game.mk
```

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
| `llasm/instruction_flags.sci` | Flag dependencies (`tools/fix_flags.py` makes the entries). |
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
| `WinApi-gdi32.c`, `WinApi-gdi-text.c` | GDI objects; fonts and text on DirectDraw surfaces with macOS CoreText. |
| `WinApi-ddraw.c` | DirectDraw 1 to 4: surfaces, blits, color keys, palettes, display. |
| `WinApi-d3d.c` | Direct3D 6 (IDirect3D3, IDirect3DDevice3) with a software rasterizer: transforms, lighting, textures, z-buffer, blending. |
| `WinApi-dinput.c` | DirectInput 5: system keyboard and mouse (no joysticks). |
| `WinApi-dplay.c` | DirectPlay 4 objects without network (for games that make them at start-up). |
| `WinApi-mss32.c`, `WinApi-mss32-redbook.c`, `audio-mixer.c` | Miles Sound System on a native mixer; CD audio from audio files (macOS AudioToolbox). |
| `WinApi-smackw32.c` | Smacker video decoder and player. |
| `WinApi-amstream.c`, `video-avi.c` | DirectShow multimedia stream: AVI with Cinepak and MS ADPCM. |
| `WinApi-winmm.c`, `WinApi-misc.c` | winmm timers, COM, registry, shell functions. |
| `imports.spec` | One line per imported function: return type, name, parameters (`*` marks a pointer). |
| `com/*.com` | COM interface lists in vtable order. |
| `llasm/` | The llasm support code of M-HT/SR and the native replacements for C runtime functions. |

## Options for every game

**Window and full screen.** Cmd+Return or Alt+Return switches between a
window and full screen (at the desktop size). The start mode is
`GAME_DISPLAY_MODE` in the game's `game.h` (0 window, the default; 1 full
screen at the desktop size; 2 full screen with a display mode change). The
player can change it with `Display_Mode=window`, `desktop` or `fullscreen`
in the game's config file (`GAME_CONFIG_FILE` in `game.h`, in the game
folder), which `runtime/Game-Config.c` reads.

**Environment variables.** The prefix is the game's `GAME_ENV_PREFIX` (for
example `REVENANT_`).

| Variable | Effect |
|----------|--------|
| `<P>DATA=<folder>` | Game folder. |
| `<P>SCRIPT=<file>` | Scripted input: `<ms> move x y`, `click x y`, `rclick x y`, `down x y`, `up x y`, `key <name>` (`Ctrl+S`, `Cmd+Return` hold a modifier), `text <string>`, `shot <name>`, `quit`. |
| `<P>DUMP=<folder>` | Saves every 30th frame as BMP; the folder for `shot`. |
| `<P>TRACE_FILES=1` | File opens and searches. |
| `<P>TRACE_MSG=1` | Window messages and key state reads. |
| `<P>TRACE_SOUND=1` or `2` | Miles and CD audio calls (2: every call). |
| `<P>TRACE_VIDEO=1` | Video player calls. |
| `<P>TRACE_GDI=1` | Fonts and text. |
| `<P>TRACE_DDRAW=1`, `<P>TRACE_D3D=1` or `2` | DirectDraw and Direct3D calls. |
| `<P>TRACE_DINPUT=1`, `<P>TRACE_SYNC=1`, `<P>TRACE_TIME=1` | DirectInput, sync objects, `Sleep` statistics. |

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

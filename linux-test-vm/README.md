# Linux test VM

A Linux virtual machine that runs the Wine versions of the games on an
in-memory screen. It takes no window, keyboard or sound from the Mac, so
you can compare the native ports with the Wine version while you work on
the Mac. It shows Linux Wine 11 (Hangover), not the Mac Wine 8 of the Wine
apps. On the Mac itself, a Wine test that must show a picture opens a
window.

## Why a Linux VM (2026-10-05)

The Wine versions could not show a picture without a window on the Mac.
These were the options. The chosen way: this VM for Wine pictures, and a
Wine test on the Mac that must show a picture waits until the user is away
or says yes. The disk sizes of options 1, 3 and 5 are estimates, not
measured.

| # | Option | Disk on the Mac | Same Wine as the Wine apps | 3D (Generals) |
|---|---|---|---|---|
| 1 | EC2: Linux Wine under Xvfb | none (game files copied to EC2) | no (Linux Wine) | yes (software OpenGL) |
| 2 | **Chosen:** Linux VM (Lima) with Hangover Wine and FEX, on an in-memory X screen | about 9 GB VM disk (measured) | no (Linux Wine 11) | yes (software OpenGL) |
| 3 | Our own Mac Wine build with the X11 driver, under the Mac's Xvfb | about 1 to 2 GB of build files | yes | no |
| 4 | **Chosen (with 2):** no new setup on the Mac | none | n/a | n/a |
| 5 | Virtual Mac (`tart`, Apple Virtualization framework) | about 25 to 60 GB | yes | yes (Apple virtual graphics) |

- 1, EC2: Linux Wine draws on Xvfb (a screen in memory) with software
  OpenGL (Mesa `llvmpipe`). It shows Linux Wine, not the Mac Wine.
- 3, Mac Wine with X11: `wine-crossover` and the WineHQ Mac builds (Gcenx)
  are built `--without-x`, so a new build of the CrossOver source with
  `--with-x` is necessary. The Mac's Xvfb (XQuartz 2.8.1) has no OpenGL:
  `glxinfo` gave "couldn't find RGB GLX visual or fbconfig" (tested
  2026-10-05). So only 2D drawing works (Wine Direct3D `renderer=gdi`, the
  GDI renderer of cnc-ddraw). Start Xvfb with `-listen tcp -nolisten unix
  -nolisten local` and `DISPLAY=127.0.0.1:<n>` (`/tmp/.X11-unix` does not
  exist on the Mac).
- 5, Virtual Mac: a second macOS, run with `tart run --no-graphics` and
  controlled over `ssh`. It can use the same Mac Wine, so the pictures are
  the same as in the Wine apps, 3D too. Tried on 2026-10-05, then removed.
  Notes for a next try: `brew install cirruslabs/cli/tart` failed (the
  formula uses `depends_on macos:` outside `on_macos`), so use
  `gh release download -R cirruslabs/tart`. The image
  `ghcr.io/cirruslabs/macos-tahoe-base:latest` is a 27 GB download
  (`tahoe-vanilla`: 24 GB, but no Homebrew and no guest agent for
  `tart exec`). With `tart run --no-graphics --vnc-experimental`, tart has
  no Dock icon, does not become the active app, and prints the VNC
  address; take pictures with `vncdotool`. The Wine game folders use
  absolute links to `/Users/<user>/Games/...`, so the guest needs that path
  to point to the shared copy of the games. Not checked: Rosetta and Wine
  in the guest, 3D speed.

Ways that do not work: a virtual monitor (`CGVirtualDisplay`, as in DeskPad
or BetterDisplay) draws the picture, but the game still becomes the active
app and takes the keyboard. Starting a game behind other windows: these
games make themselves the active window, and full screen moves to a new
desktop. A minimized window is not drawn.

## What it is

- A [Lima](https://lima-vm.io) VM named `games`: Ubuntu 26.04 arm64, Apple
  Virtualization framework (`vz`), 4 CPUs, 6 GB memory, 40 GB sparse disk
  (about 9 GB used, measured 2026-10-05) in `~/.lima/games`. It uses 6 GB
  of memory while it runs, so stop it after a test.
- Wine: [Hangover](https://github.com/AndreRH/hangover) 11.16, an arm64
  Wine that runs the 32-bit x86 code of the games with FEX
  (`HODLL=libwow64fex.dll`). FEX 2609 comes from the PPA `fex-emu/fex`.
- Screen: Xorg `:99` with the dummy driver (`guest/dummy.conf`, modes
  640x480, 800x600, 1024x768, 1280x720, 1280x800). Xvfb does not work for
  these games: it has one size only, and the games change the display mode
  (Revenant 640x480, Generals Zero Hour 1280x800; without that mode
  Generals stops with "Please make sure you have DirectX 8.1").
- Sound: `~/.asoundrc` sends ALSA to a null device. Without a sound device,
  Revenant crashed in `smackw32.dll` (the intro video).

## What the VM sees

| In the VM | On the Mac | |
|---|---|---|
| `$HOME/Games` (the same path as on the Mac) | `~/Library/Caches/games-linux-vm/Games` | Writable. An APFS copy (`cp -cR`) of the game folders in `~/Games`. The real `~/Games` is never mounted, so a test cannot change it. The same path makes the absolute links in the Wine folders work. |
| `/opt/games-vm` | this folder | Read-only: the scripts and key files. |

## Use

```
linux-test-vm/vm.sh create      # once: make the VM (see "Make the VM")
linux-test-vm/vm.sh start       # start the VM and the screen :99
KEYS=/opt/games-vm/keys/revenant-menu.txt \
  linux-test-vm/vm.sh test revenant 50 "$HOME/Games/Revenant/other-versions/wine-11-athei/Revenant (Wine).app/Contents/MacOS/Revenant"
linux-test-vm/vm.sh shots revenant
linux-test-vm/vm.sh stop
```

- `test <name> <seconds> <command>` runs the command in the VM
  (`guest/gtest.sh`) and saves a screenshot of the whole screen every 5
  seconds. The file name is the second (`050.png`). At the end it saves the
  window list (`windows.txt`) and stops all Wine programs.
- `KEYS=<file>` gives input: lines `<second> <xdotool command>`, for
  example `20 key Escape`, or `50 mousemove 432 260` and `52 click 1`. The
  files in `keys/` reach the main menu (or the screen in the file name).
- `shots <name>` copies the pictures to
  `~/Library/Caches/games-linux-vm/shots/<name>/`.
- A program that is not a Wine app: `guest/runexe.sh <prefix> "<folder
  under drive_c>" <exe>`. It starts the program from its own folder, as the
  Wine apps do.
- `refresh` makes a new copy of `~/Games` after a change on the Mac (stop
  the VM first). `shell [command]` opens a shell in the VM.
- The first start of a fresh copy updates each Wine prefix to Wine 11
  (about 30 seconds). The scripts set `WINEDLLOVERRIDES='mscoree=;mshtml='`,
  else Wine shows the Mono installer and waits.

## Make the VM

`vm.sh create` makes the game copy (if there is none), makes the VM with
the mounts above, and runs `guest/provision.sh` in it. Needs Lima 2.0 or
later (`brew install lima`).

Rosetta: the VM keeps the Rosetta share on (`vmOpts.vz.rosetta.enabled`;
with it off, the VM did not boot), but its binfmt rule is off
(`binfmt: false`, and `provision.sh` renames
`/usr/lib/binfmt.d/rosetta.conf` to `.off`). Reason: Rosetta for Linux
cannot run 32-bit x86 code (a test that jumps to the 32-bit code segment
fails with "rosetta error: invalid gdt selector index 4"), and all three
games are 32-bit. FEX then runs every x86 program.

`create` and `provision.sh` repeat the steps that made the VM on
2026-10-05 (that VM was made step by step). Check of the scripts
(2026-10-05): a second VM made with `create` (VM name changed in a copy of
this folder) took about 10 minutes, received about 1.6 GB from the network
(the Mac's network counter, so other traffic is included; the Ubuntu image
is 0.95 GB of it), used 6.9 GB of disk, and showed the Revenant main menu
in a test. That VM was then deleted.

## Results (2026-10-05, the Wine apps' own scripts and folders)

| Test | Result |
|---|---|
| Revenant (Wine app) | intro videos, main menu |
| Revenant Multiplayer screen | opens; provider TCP/IP |
| Revenant `Launcher.exe` | the launcher window shows |
| Commandos (Wine app) | intro, main menu (cnc-ddraw, full screen) |
| Commandos `mpserver.exe` | console server starts and shows its IP address |
| Generals Zero Hour (Wine app) | 3D shell map behind the main menu |
| Generals `WorldBuilder.exe` | starts (MFC42 works); after EA's license dialog the editor opens with the 3D terrain view and its tool windows |

After the move of this kit into the repository (2026-10-05), the Revenant
and Commandos tests reached the main menu again.

Not tested in the VM: the native apps (they are Mac programs), and the
Generals base game (`Generals.exe` needs the EA app).

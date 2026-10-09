# Rules for agents

This repository builds and sets up the games in `~/Games` on this Mac.
Each game folder there has `Original Game Files` (shared), `Saves`
(shared), the main version at the top, and the other versions in
`other-versions/` (see the layout in [README.md](README.md)). These rules
apply to work in this repository and in `~/Games`.

## Test modes

- Normal mode (the default): game windows can come to the front.
- No-focus mode: use it when the user says that they work on the Mac or
  want focus. Then a test must not show a window, take the keyboard or
  play sound. A test that needs a window waits until the user is away or
  says yes, and then runs in normal mode.

Tests with no window:

- Commandos and Revenant native ports: `<P>BACKGROUND=1` (prefix
  `COMMANDOS_` or `REVENANT_`). SDL uses its `dummy` drivers (the
  `offscreen` driver fails for these ports with `SDL_CreateWindow: Invalid
  window`). `<P>DUMP=<folder>` and the script command `shot` save the exact
  game frames. See [common/native-mac/README.md](common/native-mac/README.md).
- Generals native port: `GENERALSZH_BACKGROUND=offscreen`. See
  [its README](games/command-and-conquer-generals-zero-hour/other-versions/native-mac/README.md).
- Wine, start test (no picture): the null display driver in an APFS copy of
  the prefix. Never change the real prefix. See "Test methods" in
  [common/wine/README.md](common/wine/README.md).
- Wine with a picture: the Linux VM in [linux-test-vm/](linux-test-vm/README.md).

## Saves

- Never overwrite or delete saves.
- Before a test or a change that can touch them, record the checksums of
  the `Saves` and `Settings` folders (or make a backup copy). After it,
  compare.
- A copy of a Wine prefix still has links to the real `Original Game
  Files`, `Saves` and `Settings`, so a game in the copy can write there.
  Back up the saves first, or point the links at the copy
  (`benchmark/relink.py`).

## Changes in ~/Games

- Before a change, record the file list and the checksums. After the
  change, make sure that only the planned files changed.
- Do not delete with `rm`. Move files to `~/.Trash` with a clear name that
  has the date, for example `<Game> Windows files (2026-10-05)`. The user
  empties the Trash.
- A file that a test did not open is not "unused". Map editors, launchers,
  settings tools, multiplayer and patch files, and installers of Windows
  parts also count. Remove a file only when it is certain that nothing
  needs it.
- Saves and settings stay inside the game folder, never in `~/Library` or
  `~/Documents`. If an app has a fixed path outside the folder, make that
  path a link into the game folder.
- Use the official game name, not the name of a port or a tool.

## Wine

- Never open a `wine-8` prefix with the athei Wine. athei updates the
  prefix, and the update cannot be undone.
- After you edit a Wine `launcher.sh`, update the installed app too, and
  sign it again: `codesign -s - --force "<app>"`.
- In a new prefix, set the My Documents link before the first start of a
  game. Else the game writes into the Mac `~/Documents`.

## Tools

- Speed tests: only with [benchmark/](benchmark/README.md) (`setup.sh`,
  then `run-all.sh`). It works on copies and checks the saves.
- Linux VM: after a change in `~/Games`, run `linux-test-vm/vm.sh refresh`
  (with the VM stopped). After a test, run `linux-test-vm/vm.sh stop`: the
  VM uses 6 GB of memory while it runs.

## Documents

- Write the build and setup steps in the README of the matching folder in
  this repository. Write the date of each decision and test result.
- Never add game files, or code made from them, to the repository. They
  stay in the ignored `build/` folders.

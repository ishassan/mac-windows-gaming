#!/bin/bash
# vm.sh: the Linux test VM for the Wine versions of the games.
# The VM ("games", Lima, Ubuntu 26.04 arm64) draws on an in-memory screen,
# so a test takes no window, keyboard or sound from the Mac.
# See README.md in this folder.
#
#   vm.sh create                make the VM and install its software
#                               (guest/provision.sh); makes the game copy first
#   vm.sh start                 start the VM and the screen :99
#   vm.sh stop                  stop the VM (frees its memory)
#   vm.sh refresh               new APFS copy of the game folders for the VM
#                               (the VM must be stopped)
#   vm.sh test <name> <secs> <command...>
#                               run a test in the VM (guest/gtest.sh); in
#                               <command>, ~/Games is $HOME/Games, as on the Mac
#   vm.sh shots <name>          copy the screenshots of a test to the Mac
#   vm.sh shell [command]       a shell (or a command) in the VM
#
# The VM sees $HOME/Games = the copy in $COPY (writable; the real ~/Games is
# never mounted) and /opt/games-vm = this folder (read-only).

set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
CACHE="$HOME/Library/Caches/games-linux-vm"
COPY="$CACHE/Games"
GAMES="$HOME/Games"
VM=games

refresh() {
  mkdir -p "$CACHE"
  [ -d "$COPY" ] && mv "$COPY" "$CACHE/Games.old" && rm -rf "$CACHE/Games.old"
  mkdir -p "$COPY"
  for d in "$GAMES"/*/; do
    name="$(basename "$d")"
    [ "$name" = "Linux VM" ] && continue
    cp -cR "$d" "$COPY/$name"
  done
  echo "New copy: $COPY"
}

case "$1" in
  create)
    if limactl list --format '{{.Name}}' | grep -qx "$VM"; then
      echo "The VM $VM exists already" >&2; exit 1
    fi
    [ -d "$COPY" ] || refresh
    # Rosetta stays on (with it off, the VM did not boot), but without its
    # binfmt rule: FEX runs the x86 programs (Rosetta cannot run 32-bit code).
    limactl create --name="$VM" --tty=false --vm-type=vz --cpus=4 --memory=6 --disk=40 --mount-none \
      --set ".mounts=[{\"location\":\"$COPY\",\"mountPoint\":\"$GAMES\",\"writable\":true},{\"location\":\"$HERE\",\"mountPoint\":\"/opt/games-vm\",\"writable\":false}] | .mountType=\"virtiofs\" | .vmOpts.vz.rosetta.enabled=true | .vmOpts.vz.rosetta.binfmt=false" \
      template:ubuntu-26.04
    limactl start "$VM" --tty=false
    limactl shell "$VM" -- /opt/games-vm/guest/provision.sh
    ;;
  start)
    limactl start "$VM" --tty=false >/dev/null 2>&1 || limactl start "$VM" --tty=false
    limactl shell "$VM" -- /opt/games-vm/guest/start-x.sh
    ;;
  stop)
    limactl stop "$VM"
    ;;
  refresh)
    if limactl list --format '{{.Status}}' "$VM" | grep -q Running; then
      echo "Stop the VM first: vm.sh stop" >&2; exit 1
    fi
    refresh
    ;;
  test)
    shift
    limactl shell "$VM" -- env KEYS="${KEYS:-}" /opt/games-vm/guest/gtest.sh "$@"
    ;;
  shots)
    mkdir -p "$CACHE/shots"
    rm -rf "$CACHE/shots/$2"
    limactl copy -r "$VM:/tmp/shots/$2" "$CACHE/shots/"
    echo "$CACHE/shots/$2"
    ;;
  shell)
    shift
    limactl shell "$VM" -- "${@:-bash}"
    ;;
  *)
    sed -n '2,20p' "$0"; exit 1
    ;;
esac

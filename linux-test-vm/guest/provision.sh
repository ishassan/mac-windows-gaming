#!/bin/bash
# provision.sh: install the software of the test VM. "vm.sh create" runs it
# once in the VM, as the VM user. These are the steps of 2026-10-05.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
HANGOVER=11.16

# Rosetta for Linux cannot run 32-bit x86 code ("rosetta error: invalid gdt
# selector index 4"), and the games are 32-bit. Turn its binfmt rule off, so
# FEX runs every x86 program.
if [ -f /usr/lib/binfmt.d/rosetta.conf ]; then
  sudo mv /usr/lib/binfmt.d/rosetta.conf /usr/lib/binfmt.d/rosetta.conf.off
fi
if [ -e /proc/sys/fs/binfmt_misc/rosetta ]; then
  echo -1 | sudo tee /proc/sys/fs/binfmt_misc/rosetta >/dev/null
fi

# FEX (x86 emulator) from its PPA
sudo add-apt-repository -y ppa:fex-emu/fex
sudo apt-get -qq update
sudo apt-get -qq install -y fex-emu-armv8.4 fex-emu-binfmt32 fex-emu-binfmt64

# Hangover: arm64 Wine that runs the 32-bit code with FEX (HODLL=libwow64fex.dll)
cd /tmp
curl -fsSLo hangover.tar "https://github.com/AndreRH/hangover/releases/download/hangover-$HANGOVER/hangover_${HANGOVER}_ubuntu2604_resolute_arm64.tar"
rm -rf hangover && mkdir hangover && tar xf hangover.tar -C hangover
sudo apt-get -qq install -y ./hangover/*.deb

# In-memory screen (Xorg dummy driver, see dummy.conf), screenshots, input
sudo apt-get -qq install -y xserver-xorg-core xserver-xorg-video-dummy \
  x11-xserver-utils x11-utils x11-apps xdotool imagemagick libasound2-plugins

# Sound to a null device. Without a sound device, Revenant crashed in
# smackw32.dll (the intro video).
printf 'pcm.!default { type null }\nctl.!default { type hw card 0 }\n' > ~/.asoundrc

wine --version
echo "provision done"

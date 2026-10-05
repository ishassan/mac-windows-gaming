#!/bin/bash
# hw.sh <command...>: run a command with Hangover Wine (arm64 Wine; FEX runs
# the 32-bit x86 code) on the in-memory screen :99. Mono and Gecko are off.
export DISPLAY=${DISPLAY:-:99}
export WINEDEBUG=${WINEDEBUG:--all}
export WINEDLLOVERRIDES=${WINEDLLOVERRIDES:-mscoree=;mshtml=}
export HODLL=${HODLL:-libwow64fex.dll}
exec "$@"

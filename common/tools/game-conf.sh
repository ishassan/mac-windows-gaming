# Source this file in a game folder: it reads game.conf (KEY=value lines,
# values may contain spaces) into shell variables, and sets
#   GAME_ROOT  the game folder (the current folder)
#   COMMON     the common folder
#   HOSTBIN    the shared tool folder (SRW, llasm)
[ -f game.conf ] || { echo "game.conf not found: run in a game folder" >&2; exit 1; }
while IFS= read -r line || [ -n "$line" ]; do
    case "$line" in ''|'#'*) continue ;; esac
    key=${line%%=*}
    eval "$key=\${line#*=}"
done < game.conf
GAME_ROOT="$(pwd)"
COMMON="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
HOSTBIN="$COMMON/../build/bin"

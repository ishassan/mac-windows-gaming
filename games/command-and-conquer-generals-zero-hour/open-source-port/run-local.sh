# LOCAL CHANGE (see README.md): the app is in the "open-source-port" folder of the
# game folder. The game files are in "Original Game Files" of the game folder, not
# in ~/GeneralsX. The DXVK shader cache and log go to the Settings folder next to
# the app, not into the game files.
VERSION_DIR="$(cd "${CONTENTS_DIR}/../.." && pwd)"
GAME_DIR="$(dirname "${VERSION_DIR}")"
if [[ -z "${CNC_GENERALS_PATH:-}" ]]; then
    export CNC_GENERALS_PATH="${GAME_DIR}/Original Game Files/Command and Conquer Generals"
fi
if [[ -z "${CNC_GENERALS_ZH_PATH:-}" ]]; then
    export CNC_GENERALS_ZH_PATH="${GAME_DIR}/Original Game Files/Command and Conquer Generals Zero Hour"
fi
mkdir -p "${VERSION_DIR}/Settings"
export DXVK_STATE_CACHE_PATH="${DXVK_STATE_CACHE_PATH:-${VERSION_DIR}/Settings}"
export DXVK_LOG_PATH="${DXVK_LOG_PATH:-${VERSION_DIR}/Settings}"

# Source this file to use the conda env of the repository without "conda activate".
# Usage: . common/tools/env.sh   (from the repository root; any folder works
# with the right relative path)
CONDA_BASE="${CONDA_BASE:-$HOME/miniforge3}"
export CONDA_PREFIX="$CONDA_BASE/envs/mac-windows-gaming"
export PATH="$CONDA_PREFIX/bin:$CONDA_PREFIX/opt/ldc2-1.43.0-osx-arm64/bin:$PATH"

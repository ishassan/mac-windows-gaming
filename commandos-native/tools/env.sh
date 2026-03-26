# Source this file to use the commandos-native conda env without "conda activate".
# Usage: . tools/env.sh
CONDA_BASE="${CONDA_BASE:-$HOME/miniforge3}"
export CONDA_PREFIX="$CONDA_BASE/envs/commandos-native"
export PATH="$CONDA_PREFIX/bin:$CONDA_PREFIX/opt/ldc2-1.43.0-osx-arm64/bin:$PATH"

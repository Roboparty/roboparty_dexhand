#!/usr/bin/env bash
# Build for the current machine's system Python; no pip or virtual environment.
set -euo pipefail
dexhand_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cmake -S "$dexhand_root" -B "$dexhand_root/build-python-system" \
    -DROBOPARTY_BUILD_PYTHON=ON \
    -DDH116S_BUILD_DEMO=OFF -DDH116S_BUILD_SMOKE=OFF \
    -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release \
    -DPython_EXECUTABLE=/usr/bin/python3
cmake --build "$dexhand_root/build-python-system" --parallel "${DEXHAND_BUILD_JOBS:-2}"
cmake --install "$dexhand_root/build-python-system" \
    --prefix "$dexhand_root/install/python" --component python
PYTHONPATH="$dexhand_root/install/python" /usr/bin/python3 -c \
    'import roboparty_dexhand_sdk; print("Python SDK ready:", roboparty_dexhand_sdk.__file__)'

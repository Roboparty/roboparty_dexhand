#!/usr/bin/env bash
# Build independent C++, Python and ROS artifacts from the same SDK sources.
set -euo pipefail
dexhand_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
dexhand_mode="${1:-all}"
if [[ $# -gt 1 ]]; then
    printf 'Usage: bash build.sh [cpp|python|ros|all]\n' >&2
    exit 2
fi
case "$dexhand_mode" in
    cpp|python|ros|all) ;;
    -h|--help) printf 'Usage: bash build.sh [cpp|python|ros|all]\n'; exit 0 ;;
    *) printf 'Unknown build mode: %s\n' "$dexhand_mode" >&2; exit 2 ;;
esac
dexhand_jobs="${DEXHAND_BUILD_JOBS:-2}"
if [[ "$dexhand_mode" == cpp || "$dexhand_mode" == all ]]; then
    cmake -S "$dexhand_root" -B "$dexhand_root/build/cpp" \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
        -DROBOPARTY_BUILD_PYTHON=OFF -DDH116S_BUILD_DEMO=ON -DDH116S_BUILD_SMOKE=OFF \
        -DCMAKE_INSTALL_PREFIX="$dexhand_root/install/cpp"
    cmake --build "$dexhand_root/build/cpp" --parallel "$dexhand_jobs"
    cmake --install "$dexhand_root/build/cpp"
fi
if [[ "$dexhand_mode" == python || "$dexhand_mode" == all ]]; then
    cmake -S "$dexhand_root" -B "$dexhand_root/build/python" \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
        -DROBOPARTY_BUILD_PYTHON=ON -DDH116S_BUILD_DEMO=OFF -DDH116S_BUILD_SMOKE=OFF \
        -DPython_EXECUTABLE="${DEXHAND_PYTHON:-/usr/bin/python3}"
    cmake --build "$dexhand_root/build/python" --parallel "$dexhand_jobs"
    cmake --install "$dexhand_root/build/python" --prefix "$dexhand_root/install/python" --component python
    PYTHONPATH="$dexhand_root/install/python" "${DEXHAND_PYTHON:-/usr/bin/python3}" -c \
        'import roboparty_dexhand_sdk; print("Python SDK ready:", roboparty_dexhand_sdk.__file__)'
fi
if [[ "$dexhand_mode" == ros || "$dexhand_mode" == all ]]; then
    if [[ ! -f /opt/ros/jazzy/setup.bash ]]; then
        printf 'ROS 2 Jazzy is required at /opt/ros/jazzy. Use cpp or python for standalone builds.\n' >&2
        exit 1
    fi
    set +u
    source /opt/ros/jazzy/setup.bash
    set -u
    cd "$dexhand_root"
    CMAKE_BUILD_PARALLEL_LEVEL="$dexhand_jobs" colcon --log-base build/ros-log build \
        --base-paths ros2 --build-base build/ros --install-base install/ros \
        --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
fi

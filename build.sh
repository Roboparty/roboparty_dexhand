#!/usr/bin/env bash
# Build independent C++, Python and ROS artifacts from the same SDK sources.
set -euo pipefail
rphand_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
rphand_mode="${1:-all}"
if [[ $# -gt 1 ]]; then
    printf 'Usage: bash build.sh [cpp|python|ros|all]\n' >&2
    exit 2
fi
case "$rphand_mode" in
    cpp|python|ros|all) ;;
    -h|--help) printf 'Usage: bash build.sh [cpp|python|ros|all]\n'; exit 0 ;;
    *) printf 'Unknown build mode: %s\n' "$rphand_mode" >&2; exit 2 ;;
esac
rphand_jobs="${RPHAND_BUILD_JOBS:-2}"
if [[ "$rphand_mode" == cpp || "$rphand_mode" == all ]]; then
    cmake -S "$rphand_root" -B "$rphand_root/build/cpp" \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
        -DRPHAND_BUILD_PYTHON=OFF -DRPHAND_BUILD_DEMO=ON -DRPHAND_BUILD_SMOKE=OFF \
        -DCMAKE_INSTALL_PREFIX="$rphand_root/install/cpp"
    cmake --build "$rphand_root/build/cpp" --parallel "$rphand_jobs"
    cmake --install "$rphand_root/build/cpp"
fi
if [[ "$rphand_mode" == python || "$rphand_mode" == all ]]; then
    cmake -S "$rphand_root" -B "$rphand_root/build/python" \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
        -DRPHAND_BUILD_PYTHON=ON -DRPHAND_BUILD_DEMO=OFF -DRPHAND_BUILD_SMOKE=OFF \
        -DPython_EXECUTABLE="${RPHAND_PYTHON:-/usr/bin/python3}"
    cmake --build "$rphand_root/build/python" --parallel "$rphand_jobs"
    cmake --install "$rphand_root/build/python" --prefix "$rphand_root/install/python" --component python
    PYTHONPATH="$rphand_root/install/python" "${RPHAND_PYTHON:-/usr/bin/python3}" -c \
        'import rphand; print("Python SDK ready:", rphand.__file__)'
fi
if [[ "$rphand_mode" == ros || "$rphand_mode" == all ]]; then
    if [[ ! -f /opt/ros/jazzy/setup.bash ]]; then
        printf 'ROS 2 Jazzy is required at /opt/ros/jazzy. Use cpp or python for standalone builds.\n' >&2
        exit 1
    fi
    set +u
    source /opt/ros/jazzy/setup.bash
    set -u
    cd "$rphand_root"
    CMAKE_BUILD_PARALLEL_LEVEL="$rphand_jobs" colcon --log-base build/ros-log build \
        --base-paths ros2 --build-base build/ros --install-base install/ros \
        --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
fi

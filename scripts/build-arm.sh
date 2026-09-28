#!/bin/sh
set -eu

: "${RK_MPI_ROOT:?Set RK_MPI_ROOT to luckfox_pico_rkmpi_example}"
: "${CROSS_COMPILE:?Set CROSS_COMPILE to the full toolchain prefix ending in '-'}"

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${BUILD_DIR:-"$project_dir/build-arm"}

cmake -S "$project_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$project_dir/cmake/arm-rockchip830-uclibc.cmake" \
    -DCROSS_COMPILE="$CROSS_COMPILE" \
    -DRK_MPI_ROOT="$RK_MPI_ROOT" \
    -DHD_RECORD_AUDIO_BUILD_RECORDER=ON \
    -DBUILD_TESTING=OFF
cmake --build "$build_dir" --parallel
cmake --install "$build_dir" --prefix "$build_dir/install"

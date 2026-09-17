#!/usr/bin/env sh
set -eu

if [ -z "${SDKTARGETSYSROOT:-}" ]; then
    echo "Source the OpenSTLinux SDK environment-setup script first." >&2
    exit 2
fi

cmake -S . -B build-stm32mp1 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/stm32mp1-openstlinux.cmake \
    -DGATEWAY_BUILD_TESTS=OFF
cmake --build build-stm32mp1 --parallel
file build-stm32mp1/device_gateway


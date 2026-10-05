#!/usr/bin/env bash
# PS5-UZDOOM - configure the engine's build for the console.
#
#   configure.sh BUILD_DIR SDK DEPS_PREFIX HOST_BUILD_DIR PS5_VULKAN_DIR
#
# HOST_BUILD_DIR is a native build of this tree: it supplies the tools the
# build runs on the PC (zipdir, lemon, re2c).
#
# Copyright 2026 PS5-UZDOOM port contributors
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

build=$1 sdk=$2 deps=$3 host=$4 vulkan=$5
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
export PS5_CLANG=${PS5_CLANG:-$(command -v clang || true)}

# What is off, and why:
#   HAVE_GLES2, OpenGL     the console has Vulkan only
#   VULKAN_USE_XLIB        and no window system: the surface is the display itself
#   NO_OPENAL              sound goes through the port's own output (not yet written)
#   BUILD_NONFREE          the packs made from the commercial games' art are not shipped
#   NO_OPENMP              the SDK has no OpenMP runtime
#   SEND_ANON_STATS        no statistics leave the console
cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$sdk/toolchain/prospero.cmake" \
    -DCMAKE_VERBOSE_MAKEFILE=OFF \
    -DCMAKE_C_FLAGS="-fno-omit-frame-pointer -ffunction-sections -fdata-sections" \
    -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer -ffunction-sections -fdata-sections" \
    -DIMPORT_EXECUTABLES="$host/ImportExecutables.cmake" \
    -DPS5_VULKAN_DIR="$vulkan" \
    -DVPX_INCLUDE_DIR="$deps/include" -DVPX_LIBRARIES="$deps/lib/libvpx.a" \
    -DHAVE_GLES2=OFF -DNO_OPENAL=ON -DBUILD_NONFREE=OFF -DNO_OPENMP=ON \
    -DSEND_ANON_STATS=OFF -DENABLE_IWYU=OFF -DFORCE_INTERNAL_BZIP2=ON \
    -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_BROTLI=ON -DFT_DISABLE_PNG=ON \
    -DVULKAN_USE_XLIB=OFF -DZWIDGET_BUILD_EXAMPLE=OFF

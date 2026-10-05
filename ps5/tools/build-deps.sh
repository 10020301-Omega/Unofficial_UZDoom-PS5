#!/usr/bin/env bash
# PS5-UZDOOM - third-party libraries the engine needs that it does not carry,
# built for the console as static archives.
#
#   build-deps.sh SDK PREFIX
#
# libvpx (BSD-3-Clause): the VP8/VP9 decoder behind the engine's .ivf cutscenes.
# Decoder only, plain C (no assembly), which is enough for the few mods that
# play one.
#
# Sources are fetched at pinned revisions into PREFIX/src and kept there, so a
# release can ship exactly what was built.
#
# Copyright 2026 PS5-UZDOOM port contributors
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

sdk=$(cd -- "$1" && pwd)
mkdir -p "$2"
prefix=$(cd -- "$2" && pwd)
export PS5_PAYLOAD_SDK="$sdk"
export PS5_CLANG=${PS5_CLANG:-$(command -v clang || true)}
jobs=$(nproc)

libvpx_revision=v1.15.2
libvpx_url=https://github.com/webmproject/libvpx.git

if [[ ! -f $prefix/lib/libvpx.a || $(cat "$prefix/.libvpx-revision" 2> /dev/null) != "$libvpx_revision" ]]; then
    echo "==> [deps] libvpx $libvpx_revision"
    mkdir -p "$prefix/src"
    if [[ ! -d $prefix/src/libvpx/.git ]]; then
        git clone -q --depth 1 --branch "$libvpx_revision" "$libvpx_url" "$prefix/src/libvpx"
    fi
    rm -rf "$prefix/build/libvpx"
    mkdir -p "$prefix/build/libvpx"
    (
        cd "$prefix/build/libvpx"
        CC="$sdk/bin/prospero-clang" CXX="$sdk/bin/prospero-clang++" AR="$sdk/bin/prospero-ar" \
            LD="$sdk/bin/prospero-clang" STRIP=true NM="$sdk/bin/llvm-nm" \
            "$prefix/src/libvpx/configure" --target=generic-gnu --prefix="$prefix" \
            --enable-static --disable-shared --enable-pic \
            --disable-examples --disable-tools --disable-docs --disable-unit-tests \
            --disable-vp8-encoder --disable-vp9-encoder --disable-webm-io --disable-libyuv \
            --extra-cflags="-fno-omit-frame-pointer -ffunction-sections -fdata-sections" > configure.log 2>&1 ||
            { tail -20 configure.log >&2; exit 1; }
        make -j"$jobs" > build.log 2>&1 || { grep -E "error|Error" build.log | head -20 >&2; exit 1; }
        make install > install.log 2>&1
    )
    git -C "$prefix/src/libvpx" rev-parse HEAD > "$prefix/.libvpx-commit"
    echo "$libvpx_revision" > "$prefix/.libvpx-revision"
fi
echo "==> [deps] libvpx: $prefix/lib/libvpx.a"

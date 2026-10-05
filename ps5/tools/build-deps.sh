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
# OpenAL Soft (LGPL-2.0-or-later): the engine's sound output, with the port's
# backend for the console (ps5/patches/).
#
# Sources are fetched at pinned revisions into PREFIX/src and kept there, so a
# release can ship exactly what was built.
#
# Copyright 2026 PS5-UZDOOM port contributors
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
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

# OpenAL Soft (LGPL-2.0-or-later): the engine's sound, with an output backend
# for the console's AudioOut service (ps5/patches/).
openal_revision=1.24.3
openal_url=https://github.com/kcat/openal-soft.git
openal_patch="$here/../patches/openal-soft-$openal_revision-ps5-backend.patch"
openal_stamp="$openal_revision $(sha256sum "$openal_patch" | cut -d' ' -f1)"

if [[ ! -f $prefix/lib/libopenal.a || $(cat "$prefix/.openal-revision" 2> /dev/null) != "$openal_stamp" ]]; then
    echo "==> [deps] OpenAL Soft $openal_revision"
    mkdir -p "$prefix/src"
    rm -rf "$prefix/src/openal-soft" "$prefix/build/openal-soft"
    git clone -q --depth 1 --branch "$openal_revision" "$openal_url" "$prefix/src/openal-soft" 2> /dev/null
    git -C "$prefix/src/openal-soft" rev-parse HEAD > "$prefix/.openal-commit"
    git -C "$prefix/src/openal-soft" apply "$openal_patch"
    cmake -S "$prefix/src/openal-soft" -B "$prefix/build/openal-soft" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$sdk/toolchain/prospero.cmake" -DCMAKE_VERBOSE_MAKEFILE=OFF \
        -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DCMAKE_C_FLAGS="-fno-omit-frame-pointer -ffunction-sections -fdata-sections" \
        -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer -ffunction-sections -fdata-sections" \
        -DLIBTYPE=STATIC -DALSOFT_BACKEND_PS5=ON -DALSOFT_BACKEND_WAVE=OFF \
        -DALSOFT_BACKEND_OSS=OFF -DALSOFT_BACKEND_SOLARIS=OFF -DALSOFT_BACKEND_SNDIO=OFF \
        -DALSOFT_BACKEND_ALSA=OFF -DALSOFT_BACKEND_PULSEAUDIO=OFF -DALSOFT_BACKEND_PIPEWIRE=OFF \
        -DALSOFT_BACKEND_JACK=OFF -DALSOFT_BACKEND_PORTAUDIO=OFF -DALSOFT_BACKEND_SDL2=OFF \
        -DALSOFT_BACKEND_SDL3=OFF \
        -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF -DALSOFT_INSTALL_CONFIG=OFF \
        -DALSOFT_INSTALL_HRTF_DATA=OFF -DALSOFT_INSTALL_AMBDEC_PRESETS=OFF -DALSOFT_INSTALL_EXAMPLES=OFF \
        -DALSOFT_INSTALL_UTILS=OFF -DALSOFT_UPDATE_BUILD_VERSION=OFF -DALSOFT_RTKIT=OFF \
        > "$prefix/build/openal-soft.configure.log" 2>&1 ||
        { tail -20 "$prefix/build/openal-soft.configure.log" >&2; exit 1; }
    ninja -C "$prefix/build/openal-soft" install > "$prefix/build/openal-soft.build.log" 2>&1 ||
        { grep -E "error|FAILED" "$prefix/build/openal-soft.build.log" | head -20 >&2; exit 1; }
    echo "$openal_stamp" > "$prefix/.openal-revision"
fi
echo "==> [deps] OpenAL Soft: $prefix/lib/libopenal.a"

# The audio decoders ZMusic uses for sounds and music that are not in Doom's
# own formats: Ogg Vorbis, Opus, FLAC and WAV through libsndfile, MP3 through
# libmpg123. On a desktop ZMusic loads the two at run time; the console
# refuses to load a library a title brings, so they are linked in.
#   libogg, libvorbis, libFLAC, libopus   BSD-3-Clause
#   libsndfile, libmpg123                 LGPL-2.1-or-later
cmake_dep() {  # cmake_dep <name> <url> <tag or branch> <archive it makes> <source subfolder> [cmake arguments...]
    local name=$1 url=$2 ref=$3 made=$4 sub=$5
    shift 5
    if [[ -f $prefix/lib/$made && $(cat "$prefix/.$name-revision" 2> /dev/null) == "$ref" ]]; then
        echo "==> [deps] $name: $prefix/lib/$made"
        return
    fi
    echo "==> [deps] $name $ref"
    mkdir -p "$prefix/src"
    rm -rf "$prefix/src/$name" "$prefix/build/$name"
    git clone -q --depth 1 --branch "$ref" "$url" "$prefix/src/$name" 2> /dev/null
    git -C "$prefix/src/$name" rev-parse HEAD > "$prefix/.$name-commit"
    cmake -S "$prefix/src/$name/$sub" -B "$prefix/build/$name" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$sdk/toolchain/prospero.cmake" -DCMAKE_VERBOSE_MAKEFILE=OFF \
        -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_FIND_ROOT_PATH="$prefix" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCMAKE_C_FLAGS="-fno-omit-frame-pointer -ffunction-sections -fdata-sections" \
        -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer -ffunction-sections -fdata-sections" \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF "$@" \
        > "$prefix/build/$name.configure.log" 2>&1 ||
        { tail -20 "$prefix/build/$name.configure.log" >&2; exit 1; }
    ninja -C "$prefix/build/$name" install > "$prefix/build/$name.build.log" 2>&1 ||
        { grep -E "error|FAILED" "$prefix/build/$name.build.log" | head -20 >&2; exit 1; }
    [[ -f $prefix/lib/$made ]] || { echo "build-deps.sh: $name did not make lib/$made" >&2; exit 1; }
    echo "$ref" > "$prefix/.$name-revision"
}

cmake_dep libogg https://github.com/xiph/ogg.git v1.3.6 libogg.a . -DINSTALL_DOCS=OFF
cmake_dep libvorbis https://github.com/xiph/vorbis.git v1.3.7 libvorbis.a .
cmake_dep libflac https://github.com/xiph/flac.git 1.5.0 libFLAC.a . \
    -DBUILD_PROGRAMS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOCS=OFF -DINSTALL_MANPAGES=OFF -DBUILD_CXXLIBS=OFF \
    -DWITH_OGG=ON -DWITH_ASM=OFF -DWITH_STACK_PROTECTOR=OFF -DWITH_FORTIFY_SOURCE=OFF -DENABLE_MULTITHREADING=OFF
cmake_dep libopus https://github.com/xiph/opus.git v1.5.2 libopus.a . \
    -DOPUS_BUILD_PROGRAMS=OFF -DOPUS_BUILD_TESTING=OFF -DOPUS_STACK_PROTECTOR=OFF -DOPUS_FORTIFY_SOURCE=OFF \
    -DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF -DOPUS_INSTALL_CMAKE_CONFIG_MODULE=ON
cmake_dep libsndfile https://github.com/libsndfile/libsndfile.git 1.2.2 libsndfile.a . \
    -DBUILD_PROGRAMS=OFF -DBUILD_EXAMPLES=OFF -DENABLE_CPACK=OFF -DENABLE_PACKAGE_CONFIG=OFF \
    -DINSTALL_PKGCONFIG_MODULE=OFF -DINSTALL_MANPAGES=OFF -DENABLE_EXTERNAL_LIBS=ON -DENABLE_MPEG=OFF
cmake_dep libmpg123 https://github.com/libsdl-org/mpg123.git v1.33.7-SDL libmpg123.a ports/cmake \
    -DBUILD_LIBOUT123=OFF -DBUILD_PROGRAMS=OFF -DNETWORK=OFF -DNO_ICY=ON -DNO_MESSAGES=ON

# GeneralUser GS (S. Christian Collins; its own licence, which allows use in
# software projects): the SoundFont MIDI music plays through. The one in
# UZDoom's tree is not shipped (ps5/tools/stage-title.sh says why).
soundfont_revision=684543d5e5efaef08d02be50dcda8d552478fa60
soundfont_url=https://github.com/mrbumpy409/GeneralUser-GS.git
if [[ ! -f $prefix/share/soundfont/GeneralUser-GS.sf2 || $(cat "$prefix/.soundfont-commit" 2> /dev/null) != "$soundfont_revision" ]]; then
    echo "==> [deps] GeneralUser GS"
    rm -rf "$prefix/src/generaluser-gs" "$prefix/share/soundfont"
    git clone -q "$soundfont_url" "$prefix/src/generaluser-gs" 2> /dev/null
    git -C "$prefix/src/generaluser-gs" -c advice.detachedHead=false checkout -q "$soundfont_revision"
    mkdir -p "$prefix/share/soundfont"
    cp "$prefix/src/generaluser-gs/GeneralUser-GS.sf2" "$prefix/src/generaluser-gs/documentation/LICENSE.txt" \
        "$prefix/share/soundfont/"
    echo "$soundfont_revision" > "$prefix/.soundfont-commit"
fi
echo "==> [deps] GeneralUser GS: $prefix/share/soundfont/GeneralUser-GS.sf2"

#!/usr/bin/env bash
# PS5-UZDOOM - link the title and convert it for the console.
#
#   link-title.sh OUTPUT OBJECT... --libs LIBRARY...
#
# CMake runs this in place of its own link step for the engine's executable
# (src/CMakeLists.txt sets CMAKE_CXX_LINK_EXECUTABLE to it). The link is
# PS5_Vulkan's: its RADV release archive, its link recipe (the platform layer,
# the heap and thread wraps, the libc names bound to ps5_*), its start-up code,
# and its native tool, which turns the ELF into what the console loads.
#
#   PS5_VULKAN_DIR    the PS5_Vulkan checkout
#   PS5_PAYLOAD_SDK   the payload SDK the engine was compiled with
#
# Outputs, beside OUTPUT: OUTPUT itself (the linked ELF, kept for reading crash
# addresses), OUTPUT.eboot.elf (converted) and OUTPUT.eboot.bin (signed).
#
# Adapted from PS5_VulkanTemplate's ps5/tools/link-title.sh,
# Copyright (C) 2026 Mihawk, MIT.
# Copyright 2026 PS5-UZDOOM port contributors
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail
[[ -n ${LINK_TRACE:-} ]] && set -x

output=$1
shift
objects=()
libraries=()
into=objects
for argument in "$@"; do
    if [[ $argument == --libs ]]; then
        into=libraries
    elif [[ $into == objects ]]; then
        objects+=("$argument")
    else
        # CMake's list is written for a compiler driver on a desktop. Keep the
        # archives; the system libraries it names (-lm, -lpthread, -pthread,
        # rpath options) are the SDK's stubs here, which the link adds itself.
        case $argument in
            *.a) libraries+=("$argument") ;;
            -Wl,* | -l* | -pthread | -L*) ;;
            *) echo "link-title.sh: unexpected link input: $argument" >&2; exit 2 ;;
        esac
    fi
done

vulkan=${PS5_VULKAN_DIR:?set PS5_VULKAN_DIR to the PS5_Vulkan checkout}
sdk=${PS5_PAYLOAD_SDK:?set PS5_PAYLOAD_SDK to the payload SDK}
archive=${RADV_ARCHIVE:-$vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a}
export PS5_CLANG=${PS5_CLANG:-$(command -v clang || true)}
tool="$vulkan/build/host/ps5-native-tool"
native="$vulkan/tooling/native"
work="$(dirname "$output")/link"

for file in "$archive" "$tool" "$sdk/bin/prospero-lld" "$vulkan/tools/radv-link.sh"; do
    [[ -e $file ]] || { echo "link-title.sh: missing $file" >&2; exit 2; }
done
mkdir -p "$work/obj" "$work/stubs"
cc() { PS5_PAYLOAD_SDK="$sdk" sh "$vulkan/tooling/prospero-clang18" "$@"; }

# The start-up code: sets the floating-point state, runs constructors and main,
# and hands a return from main to the shell.
cc -std=c++20 -O2 -fno-exceptions -fno-rtti -c "$native/app_crt.cpp" -o "$work/obj/app_crt.o"
# RADV calls AGC, which the SDK has no stubs for: these name its imports.
stub() {
    local library=$1 source=$2
    cc -std=c11 -O2 -fPIC -c "$vulkan/$source" -o "$work/obj/${library}_stub.o"
    "$sdk/bin/prospero-lld" --shared -soname "${library}.prx" \
        -o "$work/stubs/${library}.so" "$work/obj/${library}_stub.o"
}
stub libSceAgc vendor/ps5/sdk/stubs/agc_canary_link_stub.c
stub libSceAgcDriver vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c

# shellcheck source=/dev/null
source "$vulkan/tools/radv-link.sh"
radv_link_recipe "$vulkan" "$sdk" "$archive" || exit 2

# The engine's libraries name each other in both directions: one group.
# -z nostart-stop-gc: the engine finds its classes, console variables and
# commands in sections it walks from __start_ to __stop_ symbols; the linker
# must not discard them as unreferenced.
# --wrap=exit: exit() ends a title as a crash; calls to it go to the port's
# __wrap_exit (ps5_main.cpp), which asks the shell to close the title.
# --no-dynamic-linker: Mesa names every Vulkan entry point through weak
# references that are meant to read as NULL. Without this, LLD 18 turns the
# unresolved ones into imports (2,712 of them), and the converter refuses a
# title that imports what no system module exports. PS5_VulkanTemplate's own
# link script does not pass it and failed that way here with LLD 18.1.3.
"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
    --version-script "$native/app-symbols.map" --exclude-libs=ALL \
    -z nostart-stop-gc --no-dynamic-linker --wrap=exit --error-limit=0 -e _start -o "$output" \
    "$work/obj/app_crt.o" "${objects[@]}" \
    --start-group "${libraries[@]}" --end-group \
    "$work/stubs/libSceAgc.so" "$work/stubs/libSceAgcDriver.so" \
    "${radv_link_inputs[@]}" \
    --as-needed "$sdk"/target/lib/*.so

# An import that only libkernel_sys's or libScePosixForWebKit's stub defines
# links, and is null at run time: its first call jumps to address 0. Refuse it
# here rather than find it on the console.
null_imports=$(comm -23 \
    <("$sdk/bin/llvm-nm" -D --undefined-only "$output" |
        awk '$1 == "U" { sub(/@.*/, "", $2); print $2 }' | sort -u) \
    <(for library in "$sdk"/target/lib/*.so "$work/stubs/libSceAgc.so" "$work/stubs/libSceAgcDriver.so"; do
        case ${library##*/} in libkernel_sys.so | libScePosixForWebKit.so) continue ;; esac
        "$sdk/bin/llvm-nm" -D --defined-only "$library" 2>/dev/null | awk '{ print $NF }'
    done | sort -u))
if [[ -n $null_imports ]]; then
    echo "link-title.sh: imports that no module a title loads exports (null at run time):" >&2
    echo "  ${null_imports//$'\n'/ }" >&2
    if [[ -z ${PS5_ALLOW_NULL_IMPORTS:-} ]]; then
        echo "link-title.sh: give each one an implementation, or set PS5_ALLOW_NULL_IMPORTS=1 to see the rest" >&2
        exit 1
    fi
fi

"$tool" link --in "$output" --out "$output.eboot.elf" \
    --stub-dir "$sdk/target/lib" --stub "$work/stubs/libSceAgc.so" \
    --stub "$work/stubs/libSceAgcDriver.so" --module-sdk 0x02000009 \
    --companion-sdk 0x08050001 --file-name eboot.elf
"$tool" self --sign --in "$output.eboot.elf" --out "$output.eboot.bin" --magic 0x1D3D154F
"$tool" self --inspect --file "$output.eboot.bin" > /dev/null
printf '==> linked %s (eboot.bin %s bytes)\n' "$output" "$(stat -c %s "$output.eboot.bin")"

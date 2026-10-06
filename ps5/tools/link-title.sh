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
    cc -std=c11 -O2 -fPIC -c "$source" -o "$work/obj/${library}_stub.o"
    "$sdk/bin/prospero-lld" --shared -soname "${library}.prx" \
        -o "$work/stubs/${library}.so" "$work/obj/${library}_stub.o"
}
stub libSceAgc "$vulkan/vendor/ps5/sdk/stubs/agc_canary_link_stub.c"
stub libSceAgcDriver "$vulkan/vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c"
# The keyboard's text-input library, which the SDK has no stubs for either.
stub libSceIme "$(dirname -- "$0")/../stubs/libSceIme_stub.c"

# shellcheck source=/dev/null
source "$vulkan/tools/radv-link.sh"
radv_link_recipe "$vulkan" "$sdk" "$archive" || exit 2

# The recipe binds the libc names RADV needs to the platform layer's ps5_*
# functions. The engine calls more of libc than RADV does, so the same is done
# for the rest: every name the link's inputs leave undefined that the platform
# layer implements (realpath, getcwd, sysconf, isatty, umask, gethostbyname ...
# each missing, refused or faulting on the console: ps5platform/libc.h says
# which), and the few the port implements itself (uzps5_*, ps5_libc.cpp). A
# bound name stays local, as the recipe keeps its own: a title must not export.
nm="$sdk/bin/llvm-nm"
already=$(printf '%s\n' "${radv_link_flags[@]}" | sed -n 's/^--defsym=\([^=]*\)=.*/\1/p; s/^--wrap=//p' | sort -u)
archives=()
for input in "${radv_link_inputs[@]}"; do
    [[ $input == *.a ]] && archives+=("$input")
done
wanted=$("$nm" --undefined-only "${objects[@]}" "${libraries[@]}" "${archives[@]}" 2> /dev/null |
    awk '$1 == "U" { print $2 }' | sort -u)
platform_has=$("$nm" --defined-only "$sdk/target/lib/libps5platform.a" 2> /dev/null |
    awk '$2 == "T" && $3 ~ /^ps5_/ { print substr($3, 5) }' | sort -u)
port_has=$("$nm" --defined-only "${objects[@]}" 2> /dev/null |
    awk '$2 == "T" && $3 ~ /^uzps5_/ { print substr($3, 7) }' | sort -u)
# Not every one can be bound this way. A platform function that calls the
# library function it stands in front of (ps5_sysconf calls sysconf for the
# names it does not answer itself; ps5_pthread_exit ends in pthread_exit) would
# be bound to itself and call itself until the stack ran out: the first console
# run died exactly so, in ps5_sysconf. Those names keep the console's own
# function, which works, with the quirk the platform's version was written
# to smooth over (ps5platform/libc.h).
platform_listing=$("$nm" -A "$sdk/target/lib/libps5platform.a" 2> /dev/null)
calls_itself() {  # does the object defining ps5_$1 also call $1?
    local member
    member=$(awk -v s="ps5_$1" '$NF == s && $(NF-1) == "T" { split($1, a, ":"); print a[2] }' <<< "$platform_listing" | head -1)
    [[ -n $member ]] && awk -v m="$member" -v s="$1" '$NF == s && $(NF-1) == "U" { split($1, a, ":"); if (a[2] == m) found = 1 }
        END { exit !found }' <<< "$platform_listing"
}
bound=()
unbound=()
for name in $(comm -12 <(echo "$wanted") <(echo "$platform_has") | comm -23 - <(echo "$already")); do
    if calls_itself "$name"; then
        unbound+=("$name")
        continue
    fi
    radv_link_flags+=("--defsym=$name=ps5_$name")
    bound+=("$name")
done
[[ ${#unbound[@]} -eq 0 ]] || echo "==> left to the console's own (the platform's version calls it): ${unbound[*]}"
for name in $(comm -12 <(echo "$wanted") <(echo "$port_has") | comm -23 - <(echo "$already")); do
    radv_link_flags+=("--defsym=$name=uzps5_$name")
    bound+=("$name")
done
# The same goes for a function the engine defines under a name a system
# library also has (its own isnan, for one): the linker would export the
# engine's to stand in for the library's. The engine keeps its own, privately.
stub_names=$(for library in "$sdk"/target/lib/*.so; do
    "$nm" -D --defined-only "$library" 2> /dev/null | awk '{ print $NF }'
done | sort -u)
shadowed=$("$nm" --defined-only --extern-only "${objects[@]}" 2> /dev/null |
    awk 'NF == 3 && $2 ~ /^[TDBRW]$/ { print $3 }' | sort -u | comm -12 - <(echo "$stub_names"))
[[ -z $shadowed ]] || echo "==> kept private (the engine's own, named like a system function): ${shadowed//$'\n'/ }"
{
    printf '{\n    local:\n'
    printf '        %s;\n' "${bound[@]}" $shadowed
    printf '};\n'
} > "$work/bound-local.map"
radv_link_flags+=(--version-script "$work/bound-local.map")
printf '%s\n' "${bound[@]}" > "$work/bound-names.txt"
echo "==> bound to the platform layer or the port: ${bound[*]}"

# The engine's libraries name each other in both directions: one group.
# --wrap=exit: exit() ends a title as a crash; calls to it go to the port's
# __wrap_exit (ps5_main.cpp), which asks the shell to close the title.
# --no-dynamic-linker: Mesa names every Vulkan entry point through weak
# references that are meant to read as NULL. Without this, LLD 18 turns the
# unresolved ones into imports (2,712 of them), and the converter refuses a
# title that imports what no system module exports. PS5_VulkanTemplate's own
# link script does not pass it and failed that way here with LLD 18.1.3.
"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
    --version-script "$native/app-symbols.map" --exclude-libs=ALL \
    --no-dynamic-linker --wrap=exit --error-limit=0 -e _start -o "$output" \
    "$work/obj/app_crt.o" "${objects[@]}" \
    --start-group "${libraries[@]}" --end-group \
    "$work/stubs/libSceAgc.so" "$work/stubs/libSceAgcDriver.so" "$work/stubs/libSceIme.so" \
    "${radv_link_inputs[@]}" \
    --as-needed "$sdk"/target/lib/*.so

# An import that only libkernel_sys's or libScePosixForWebKit's stub defines
# links, and is null at run time: its first call jumps to address 0. Refuse it
# here rather than find it on the console.
null_imports=$(comm -23 \
    <("$sdk/bin/llvm-nm" -D --undefined-only "$output" |
        awk '$1 == "U" { sub(/@.*/, "", $2); print $2 }' | sort -u) \
    <(for library in "$sdk"/target/lib/*.so "$work/stubs/libSceAgc.so" "$work/stubs/libSceAgcDriver.so" "$work/stubs/libSceIme.so"; do
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
    --stub "$work/stubs/libSceAgcDriver.so" --stub "$work/stubs/libSceIme.so" --module-sdk 0x02000009 \
    --companion-sdk 0x08050001 --file-name eboot.elf
"$tool" self --sign --in "$output.eboot.elf" --out "$output.eboot.bin" --magic 0x1D3D154F
"$tool" self --inspect --file "$output.eboot.bin" > /dev/null
printf '==> linked %s (eboot.bin %s bytes)\n' "$output" "$(stat -c %s "$output.eboot.bin")"

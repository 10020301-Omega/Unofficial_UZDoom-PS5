#!/usr/bin/env bash
# PS5-UZDOOM - build the title folder, dist/<TITLE_ID>/.
#
#   ps5/tools/build.sh
#
# Expects the toolkit beside this repository, as PS5_VulkanTemplate's
# bootstrap leaves it:
#
#   ../PS5_Vulkan        the RADV archive, the link recipe, the native tool, libc.prx
#   ../PS5_PayloadSDK    the payload SDK fork (its platform layer)
#
#   PS5_VULKAN_DIR, PS5_PAYLOAD_SDK_FORK   override where those are
#   PS5_PAYLOAD_SDK                        an installed SDK to use as it is
#   PS5_CLANG                              the host clang (default: clang)
#
# Steps: the SDK at this port's pin, the third-party libraries
# (build-deps.sh), the tools the build runs on the PC (a small native build),
# the engine for the console, the link (link-title.sh), then the title folder.
#
# Copyright 2026 PS5-UZDOOM port contributors
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

ps5=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=$(dirname "$ps5")
parent=$(dirname "$root")
vulkan=$(cd -- "${PS5_VULKAN_DIR:-$parent/PS5_Vulkan}" && pwd)
export PS5_CLANG=${PS5_CLANG:-$(command -v clang || true)}
work="$root/build"
mkdir -p "$work"

# The payload SDK: this port's pin of the fork, installed into build/sdk
sdk=${PS5_PAYLOAD_SDK:-}
if [[ -z $sdk ]]; then
    sdk=$(bash "$ps5/tools/setup-sdk.sh")
fi
export PS5_PAYLOAD_SDK="$sdk"

for file in "$vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a" "$vulkan/build/host/ps5-native-tool" \
        "$vulkan/runtime/libc.prx"; do
    [[ -e $file ]] || { echo "build.sh: missing $file (run PS5_VulkanTemplate's ps5/tools/bootstrap.sh)" >&2; exit 2; }
done

bash "$ps5/tools/build-deps.sh" "$sdk" "$work/deps"

# The tools the build runs on this PC
if [[ ! -f $work/host/ImportExecutables.cmake ]]; then
    echo "==> [host] zipdir, lemon, re2c"
    cmake -S "$root" -B "$work/host" -G Ninja -DCMAKE_BUILD_TYPE=Release > "$work/host.configure.log" 2>&1 ||
        { tail -20 "$work/host.configure.log" >&2; exit 1; }
    ninja -C "$work/host" zipdir lemon re2c > "$work/host.build.log" 2>&1 ||
        { tail -20 "$work/host.build.log" >&2; exit 1; }
fi

if [[ ! -f $work/ps5/build.ninja ]]; then
    echo "==> [ps5] configuring"
    bash "$ps5/tools/configure.sh" "$work/ps5" "$sdk" "$work/deps" "$work/host" "$vulkan" > "$work/ps5.configure.log" 2>&1 ||
        { tail -30 "$work/ps5.configure.log" >&2; exit 1; }
fi
echo "==> [ps5] building"
ninja -C "$work/ps5" zdoom

bash "$ps5/tools/stage-title.sh" "$work/ps5" "$vulkan" "$root/dist"

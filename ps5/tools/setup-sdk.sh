#!/usr/bin/env bash
# PS5-UZDOOM - the payload SDK this title builds with, at a pinned revision.
#
#   ps5/tools/setup-sdk.sh       install it into build/sdk (once a revision); prints its path
#
# The SDK is Mihawk's fork of ps5-payload-dev/sdk, ../PS5_PayloadSDK: the
# upstream release with the PS5 platform layer (libps5platform.a) installed
# over it. The pinned revision is exported with git archive, so a build never
# depends on the fork's working tree, and the SDK folder records it.
#
# Adapted from PS5_VulkanTemplate's ps5/tools/setup-sdk.sh,
# Copyright (C) 2026 Mihawk, MIT.
# Copyright 2026 PS5-UZDOOM port contributors
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cache="$root/build"
sdk="$cache/sdk"
sdk_fork="${PS5_PAYLOAD_SDK_FORK:-$root/../PS5_PayloadSDK}"
# The revision PS5_VulkanTemplate 6d0221a pins: never older than fa69d00 (the
# platform layer's localeconv), and what the toolkit's own titles were proven with.
sdk_revision=adc8dd795ada7bf696cd98748aae463a3970ffbb

if [[ ! -f $sdk/.ps5-sdk-revision || $(<"$sdk/.ps5-sdk-revision") != "$sdk_revision" ]]; then
    git -C "$sdk_fork" cat-file -e "$sdk_revision^{commit}" 2>/dev/null || {
        echo "the payload SDK fork at $sdk_fork does not have $sdk_revision" >&2
        exit 2
    }
    mkdir -p "$cache"
    sdk_tree=$(mktemp -d)
    git -C "$sdk_fork" archive "$sdk_revision" | tar -x -C "$sdk_tree"
    bash "$sdk_tree/platform/tools/setup-sdk.sh" "$sdk" "$sdk_revision" "$cache" >&2
    rm -rf -- "$sdk_tree"
fi
[[ -x $sdk/bin/prospero-lld && -d $sdk/target/include ]] || { echo "the pinned PS5 payload SDK is incomplete" >&2; exit 2; }
echo "$sdk"

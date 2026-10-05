#!/usr/bin/env bash
# PS5-UZDOOM - lay the title folder out: what gets copied to the console.
#
#   stage-title.sh BUILD_DIR PS5_VULKAN_DIR DIST_DIR
#
#   <DIST_DIR>/<TITLE_ID>/
#     eboot.bin               the title
#     sce_sys/                its identity and icon
#     sce_module/libc.prx     the C library module titles load (PS5_Vulkan's)
#     uzdoom.pk3 ...          the engine's own data
#     LEGAL.txt, licenses/    what it is licensed under, part by part
#     AI-DISCLOSURE.txt       which parts were written with an AI system
#     iwads/ mods/            empty: where games and mods go when the title
#                             cannot reach /data (see ps5_paths.h)
#
# _source/ is added by package-source.sh, which a release needs and a test
# build does not.
#
# Not shipped, on purpose:
#   soundfont/uzdoom.sf2      its own header reads "Copyright 1996 Roland
#                             Corporation U.S." and the repository states no
#                             licence for it. GeneralUser GS takes its place,
#                             under the name the engine looks for
#                             (soundfonts/uzdoom.sf2).
#   fm_banks/                 instrument banks under a licence each; the OPL
#                             emulation the port defaults to does not need them
#   brightmaps.pk3, game_widescreen_gfx.pk3 and game_support.pk3's nonfree
#                             part: made from the commercial games' art
#                             (the build is configured with BUILD_NONFREE=OFF)
#
# Copyright 2026 PS5-UZDOOM port contributors
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

build=$(cd -- "$1" && pwd)
vulkan=$(cd -- "$2" && pwd)
ps5=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=$(dirname "$ps5")
title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$ps5/sce_sys/param.json")
mkdir -p "$3"
app="$(cd -- "$3" && pwd)/$title_id"

eboot=$(find "$build" -maxdepth 2 -name '*.eboot.bin' -newer "$build/build.ninja" | head -1)
[[ -n $eboot ]] || { echo "stage-title.sh: no linked title in $build" >&2; exit 2; }

mkdir -p "$app/sce_sys" "$app/sce_module" "$app/iwads" "$app/mods" "$app/soundfonts"
cp "$eboot" "$app/eboot.bin"
cp "$ps5/sce_sys/param.json" "$ps5/sce_sys/icon0.png" "$app/sce_sys/"
(cd "$vulkan/runtime" && sha256sum --check --strict --quiet libc.prx.sha256)
cp "$vulkan/runtime/libc.prx" "$app/sce_module/libc.prx"
for pack in uzdoom.pk3 lights.pk3 game_support.pk3; do
    [[ -f $build/$pack ]] || { echo "stage-title.sh: $build/$pack was not built" >&2; exit 2; }
    cp "$build/$pack" "$app/$pack"
done
rm -f "$app/brightmaps.pk3" "$app/game_widescreen_gfx.pk3"

cat > "$app/iwads/README.txt" <<'TEXT'
Put your game files here: DOOM.WAD, DOOM2.WAD, TNT.WAD, PLUTONIA.WAD,
HERETIC.WAD, HEXEN.WAD, freedoom1.wad, freedoom2.wad ...

None is included. Use files from a copy of the game you own.
TEXT
cat > "$app/mods/README.txt" <<'TEXT'
Put mods here: .pk3 and .wad files, in folders if you like.
Tick them in the launcher's "Select Mods" screen; the number beside each is
its place in the load order.
TEXT
deps=$(sed -n 's|^VPX_INCLUDE_DIR:[A-Z]*=\(.*\)/include$|\1|p' "$build/CMakeCache.txt")
[[ -f $deps/share/soundfont/GeneralUser-GS.sf2 ]] || { echo "stage-title.sh: no soundfont in $deps (run build-deps.sh)" >&2; exit 2; }
cp "$deps/share/soundfont/GeneralUser-GS.sf2" "$app/soundfonts/uzdoom.sf2"
cat > "$app/soundfonts/README.txt" <<'TEXT'
uzdoom.sf2 is GeneralUser GS by S. Christian Collins, under its own licence
(licenses/generaluser-gs/LICENSE.txt), renamed to the name the engine looks
for. MIDI music plays through it.

To use another SoundFont, replace uzdoom.sf2 with it (keep the name), or add
it here and choose it in the engine's sound options.
TEXT

python3 "$ps5/tools/stage-notices.py" "$app" "$build" "$vulkan"
printf '==> %s: %s (eboot.bin %s bytes)\n' "$title_id" "$app" "$(stat -c %s "$app/eboot.bin")"

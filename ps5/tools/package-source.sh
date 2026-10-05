#!/usr/bin/env bash
# PS5-UZDOOM - put the complete source inside the title folder: _source/.
#
#   package-source.sh APP_DIR
#
# One .tar.xz per part named in APP_DIR/licenses/components.json, each made
# with git archive at the exact revision that part was built from, plus
# SOURCES.txt (what each archive is, its licence, where it came from) and
# SHA256SUMS. tar keeps what a Linux build needs (which scripts are
# executable, symbolic links); xz keeps it small.
#
# A part built from uncommitted changes cannot be archived truthfully, so
# the script stops: commit first, rebuild, then package.
#
# Copyright 2026 PS5-UZDOOM port contributors
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

app=$(cd -- "$1" && pwd)
components="$app/licenses/components.json"
[[ -f $components ]] || { echo "package-source.sh: $components is missing (run stage-title.sh)" >&2; exit 2; }
out="$app/_source"
rm -rf "$out"
mkdir -p "$out"

python3 - "$components" "$out" <<'PY'
import json
import subprocess
import sys
from pathlib import Path

components, out = Path(sys.argv[1]), Path(sys.argv[2])
parts = json.loads(components.read_text())
dirty = [p["id"] for p in parts if p["source"].get("dirty")]
if dirty:
    sys.exit(f"package-source.sh: built from uncommitted changes: {', '.join(dirty)}. Commit, rebuild, then package.")

lines = ["The source of this title", "========================", "",
         "Each archive is one part at the exact revision it was built from.",
         "How they fit together and how to build: uzdoom-*/ps5/README.md.", ""]
for part in parts:
    src = part["source"]
    if src["kind"] != "git":
        lines += [f'{part["name"]}', f'  licence: {part["licence"]}',
                  f'  not archived here (no source obligation): {src["revision"]}', f'  {src["url"]}', ""]
        continue
    rev = src["revision"]
    name = f'{part["id"]}-{rev[:12]}'
    archive = out / f"{name}.tar.xz"
    print(f"==> [source] {archive.name}", flush=True)
    with open(archive, "wb") as file:
        tar = subprocess.Popen(["git", "-C", src["path"], "archive", "--format=tar", f"--prefix={name}/", rev],
                               stdout=subprocess.PIPE)
        xz = subprocess.run(["xz", "-T0", "-6", "-c"], stdin=tar.stdout, stdout=file)
        if tar.wait() != 0 or xz.returncode != 0:
            sys.exit(f"package-source.sh: could not archive {part['id']} at {rev}")
    lines += [f"{archive.name}", f'  {part["name"]}', f'  licence:  {part["licence"]}',
              f'  from:     {src["remote"]}', f"  revision: {rev}", f'  changes:  {part.get("modifications", "none")}', ""]
(out / "SOURCES.txt").write_text("\n".join(lines) + "\n")
PY

(cd "$out" && sha256sum -- *.tar.xz SOURCES.txt > SHA256SUMS)
du -sh "$out" | awk '{ print "==> [source] " $2 ": " $1 }'

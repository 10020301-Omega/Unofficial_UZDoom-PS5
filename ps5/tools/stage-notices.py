#!/usr/bin/env python3
"""PS5-UZDOOM - the notices a title folder carries: LEGAL.txt and licenses/.

    stage-notices.py APP_DIR BUILD_DIR PS5_VULKAN_DIR

Writes, beside eboot.bin:

  LEGAL.txt                 what the title is, what it is licensed under, trademarks
  licenses/README.txt       the same notice, then every part: licence, copyright, source
  licenses/components.json  the same parts as data, each with the revision it was built
                            from (package-source.sh archives the source from it)
  licenses/<part>/...       the licence texts each part requires

Texts are read at the revision each part was built from (git show), not from a
working tree, so the notices match the source a release carries. A part whose
repository has uncommitted changes is recorded as "dirty", and package-source.sh
refuses to make a release from it.

Adapted from PS5_VulkanTemplate's ps5/tools/stage-notices.py,
Copyright (C) 2026 Mihawk, MIT.
Copyright 2026 PS5-UZDOOM port contributors
SPDX-License-Identifier: GPL-3.0-or-later
"""
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

PS5 = Path(__file__).resolve().parent.parent
ROOT = PS5.parent

# Files that state a licence, wherever a library keeps them
LICENCE_FILE = re.compile(r"(^|/)(LICEN[CS]E|COPYING|COPYRIGHT|NOTICE|PATENTS|AUTHORS)[^/]*$|(^|/)docs/(FTL|GPLv2|LICENSE)\.TXT$",
                          re.I)
LICENCE_ROOTS = ("LICENSE", "docs/licenses/", "libraries/", "src/common/thirdparty/", "src/common/platform/posix/ps5/console/",
                 "wadsrc/static/credits/", "wadsrc_extra/static/credits/", "wadsrc_lights/static/credits/", "tools/")
# Not in the console build, or not shipped (stage-title.sh)
NOT_SHIPPED = ("libraries/ZMusic/thirdparty/fluidsynth/doc/", "bin/", "soundfont/", "fm_banks/", "wadsrc_bm/", "wadsrc_widepix/",
               "wadsrc_extra/nonfree/")


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True, check=True).stdout.strip()


def revision(repo):
    """The repository's HEAD, its remote, and whether its tracked files differ from it."""
    rev = git(repo, "rev-parse", "HEAD")
    dirty = bool(git(repo, "status", "--porcelain", "--untracked-files=no"))
    remotes = git(repo, "remote").split()
    name = "origin" if "origin" in remotes else (remotes[0] if remotes else None)
    remote = git(repo, "remote", "get-url", name) if name else "(no remote)"
    return rev, dirty, remote.removesuffix(".git")


def pinned(script, name):
    """A revision a setup script pins ("name=<sha>")."""
    m = re.search(rf"^{name}=([0-9a-f]{{40}})", (PS5 / script).read_text(), re.M)
    return m.group(1) if m else None


def write_text(dest, repo, rev, path):
    """A file of a repository at a revision, into the licenses folder."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    data = subprocess.run(["git", "-C", str(repo), "show", f"{rev}:{path}"], capture_output=True, check=True).stdout
    dest.write_bytes(data)


def write_tree(dest, repo, rev, path):
    """A folder of a repository at a revision (Mesa's licenses/)."""
    dest.mkdir(parents=True, exist_ok=True)
    tar = subprocess.run(["git", "-C", str(repo), "archive", rev, path], capture_output=True, check=True).stdout
    subprocess.run(["tar", "-x", "-C", str(dest), "--strip-components", str(len(Path(path).parts))], input=tar, check=True)


LEGAL = """{name} for PlayStation 5 - legal notice
{rule}

This is an unofficial port of UZDoom. It is not made, endorsed or supported
by the UZDoom team, by id Software or by Sony Interactive Entertainment.

NO GAME DATA IS INCLUDED. This title contains no DOOM, Heretic, Hexen or
Strife game files, no console firmware and no keys, and none will be
provided or linked to. Use game files from a copy of the game you own.

Licence: the title as a whole is distributed under the GNU General Public
License, version 3 or later (licenses/uzdoom/LICENSE and
licenses/uzdoom/docs/licenses/gpl.txt). UZDoom is GPL-3.0-or-later; the PS5
platform layer, the link recipe and the start-up code it is linked with are
GPL-3.0-or-later; the Vulkan driver (RADV, from Mesa) is MIT; OpenAL Soft is
LGPL-2.0-or-later. Every part, its licence, its copyright and the exact
revision it was built from are listed in licenses/README.txt.

Source: the complete corresponding source of everything in this folder is
in _source/, one archive per part, with _source/SOURCES.txt saying which is
which. If you pass this title on, pass the whole folder on, _source/
included.

There is no warranty, to the extent permitted by law. Running homebrew
needs a modified console, which may void its warranty or breach the
platform's terms of service.

"PlayStation" and "PS5" are trademarks of Sony Interactive Entertainment
Inc. DOOM is a trademark of id Software LLC. Vulkan is a registered
trademark of the Khronos Group Inc.; this build of RADV is not a conformant
implementation (its conformanceVersion is 0.0.0.0).
"""


def main():
    app, build, vulkan = (Path(a).resolve() for a in sys.argv[1:4])
    deps = build.parent / "deps" if (build.parent / "deps").is_dir() else Path(
        re.search(r"^VPX_INCLUDE_DIR:\w+=(.*)/include$", (build / "CMakeCache.txt").read_text(), re.M).group(1))
    name = json.loads((PS5 / "sce_sys/param.json").read_text())["localizedParameters"]["en-US"]["titleName"]
    out = app / "licenses"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    parts = []

    # The engine and everything in its tree, with the port
    rev, dirty, remote = revision(ROOT)
    upstream = git(ROOT, "merge-base", rev, "809e46c25fe2a2f89430de3fbac89626100df384")
    count = 0
    for path in git(ROOT, "ls-tree", "-r", "--name-only", rev).splitlines():
        if path.startswith(NOT_SHIPPED) or not path.startswith(LICENCE_ROOTS):
            continue
        if path.startswith(("docs/licenses/", "libraries/ZMusic/licenses/")) or "/credits/" in path or LICENCE_FILE.search(path):
            write_text(out / "uzdoom" / path, ROOT, rev, path)
            count += 1
    parts.append(dict(
        id="uzdoom",
        name="UZDoom, with this port's PS5 backend and launcher, and the libraries in its tree (ZMusic with its "
             "synthesizers, ZVulkan with glslang, volk and the Vulkan memory allocator, ZWidget, FreeType, HarfBuzz, "
             "abseil, bzip2, LZMA, miniz, libwebp, cppdap, and others)",
        licence="GPL-3.0-or-later as a whole; parts under BSD-style, zlib, MIT, LGPL and other compatible licences "
                "(licenses/uzdoom/docs/licenses/README.TXT)",
        copyright=["Copyright 1993-1996 id Software", "Copyright 1998-2016 ZDoom maintainers and contributors",
                   "Copyright 2005-2025 GZDoom maintainers and contributors",
                   "Copyright 2025-2026 UZDoom maintainers and contributors",
                   "Copyright 2026 PS5-UZDOOM port contributors",
                   "Copyright (C) 2026 Mihawk (the console's pad, sound and exit calls, MIT)",
                   "and each library's authors"],
        modifications=f"the PS5 port: every change since UZDoom {upstream[:12]} (git log {upstream[:12]}..{rev[:12]})",
        artifacts=["eboot.bin", "uzdoom.pk3", "lights.pk3", "game_support.pk3", "sce_sys/icon0.png"],
        texts="licenses/uzdoom/",
        # The port has no public repository: its revision exists only in the
        # archive that travels with the title.
        source=dict(kind="git", path=str(ROOT),
                    remote=f"this port, not published elsewhere: _source/ holds it; upstream {remote} at {upstream}",
                    revision=rev, dirty=dirty, upstream=upstream)))

    # OpenAL Soft, with the port's output backend
    openal = deps / "src/openal-soft"
    openal_rev = (deps / ".openal-commit").read_text().strip()
    for text in ("COPYING", "BSD-3Clause"):
        write_text(out / "openal-soft" / text, openal, openal_rev, text)
    patch = next((PS5 / "patches").glob("openal-soft-*.patch"))
    parts.append(dict(
        id="openal-soft", name="OpenAL Soft, with this port's output backend for the console",
        licence="LGPL-2.0-or-later; some files BSD-3-Clause",
        copyright=["Copyright (C) 1999-2025 the OpenAL Soft authors", "Copyright 2026 PS5-UZDOOM port contributors"],
        modifications=f"ps5/patches/{patch.name} in this port's source: a PlayStation 5 backend (alc/backends/ps5.cpp)",
        artifacts=["eboot.bin"], texts="licenses/openal-soft/",
        source=dict(kind="git", path=str(openal), remote="https://github.com/kcat/openal-soft", revision=openal_rev,
                    dirty=False, patched=True)))

    # libvpx
    vpx = deps / "src/libvpx"
    vpx_rev = (deps / ".libvpx-commit").read_text().strip()
    for text in ("LICENSE", "PATENTS", "AUTHORS"):
        write_text(out / "libvpx" / text, vpx, vpx_rev, text)
    parts.append(dict(
        id="libvpx", name="libvpx, the VP8/VP9 decoder", licence="BSD-3-Clause, with a patent grant (licenses/libvpx/PATENTS)",
        copyright=["Copyright (c) 2010 The WebM project authors"], modifications="none",
        artifacts=["eboot.bin"], texts="licenses/libvpx/",
        source=dict(kind="git", path=str(vpx), remote="https://github.com/webmproject/libvpx", revision=vpx_rev,
                    dirty=bool(git(vpx, "status", "--porcelain", "--untracked-files=no")))))

    # The payload SDK fork: its platform layer and headers
    sdk = ROOT.parent / "PS5_PayloadSDK"
    sdk_rev = pinned("tools/setup-sdk.sh", "sdk_revision")
    write_text(out / "platform/GPL-3.0.txt", sdk, sdk_rev, "LICENSE")
    write_text(out / "platform/musl-regex-COPYRIGHT.txt", sdk, sdk_rev, "platform/src/regex/COPYRIGHT.musl")
    write_text(out / "platform/dlmalloc-SOURCE.txt", sdk, sdk_rev, "platform/src/dlmalloc/SOURCE")
    parts.append(dict(
        id="platform", name="the PS5 platform layer (libps5platform.a) and the SDK's headers and start files, from "
                            "Mihawk's fork of the ps5-payload-dev SDK, with musl's regex and dlmalloc",
        licence="GPL-3.0-or-later; musl's regex MIT; dlmalloc public domain",
        copyright=["Copyright (C) John Törnblom and the ps5-payload-dev contributors", "Copyright (C) 2026 Mihawk"],
        modifications="none by this port", artifacts=["eboot.bin"], texts="licenses/platform/",
        source=dict(kind="git", path=str(sdk), remote="https://github.com/mihawk-99/PS5_PayloadSDK", revision=sdk_rev,
                    dirty=False)))

    # PS5_Vulkan: the link recipe, the start-up code, the AGC import stubs, libc.prx
    vrev, vdirty, vremote = revision(vulkan)
    write_text(out / "ps5-vulkan/GPL-3.0.txt", vulkan, vrev, "LICENSE")
    write_text(out / "ps5-vulkan/NOTICE.md", vulkan, vrev, "NOTICE.md")
    parts.append(dict(
        id="ps5-vulkan", name="PS5_Vulkan: the RADV link recipe, the C runtime start-up, the AGC import stubs, the "
                              "tool that converts the title for the console, and sce_module/libc.prx",
        licence="GPL-3.0-or-later", copyright=["Copyright (C) 2026 Mihawk", "Copyright (C) 2026 BlackBearReloaded"],
        modifications="none by this port", artifacts=["eboot.bin", "sce_module/libc.prx"], texts="licenses/ps5-vulkan/",
        source=dict(kind="git", path=str(vulkan), remote=vremote, revision=vrev, dirty=vdirty)))

    # RADV: Mesa's Vulkan driver, from the PS5_Mesa fork, built by PS5_Vulkan
    provenance = (vulkan / ".deps/native/radv-release/PROVENANCE.txt").read_text()
    mesa_rev = re.search(r"^revision: ([0-9a-f]{40})", provenance, re.M).group(1)
    radv_sdk = re.search(r"^sdk: ([0-9a-f]{40})", provenance, re.M)
    mesa = ROOT.parent / "PS5_Mesa"
    write_text(out / "radv/Mesa-license.rst", mesa, mesa_rev, "docs/license.rst")
    write_tree(out / "radv/licenses", mesa, mesa_rev, "licenses")
    parts.append(dict(
        id="radv", name="RADV, Mesa's Vulkan driver (with ACO, NIR and Mesa's Vulkan runtime), from Mihawk's PS5_Mesa "
                        "fork, and the zlib it compresses its shader cache with",
        licence="MIT, with other licences stated per file (licenses/radv/Mesa-license.rst); zlib under the zlib licence",
        copyright=["Copyright the Mesa contributors (per-file notices in the source)", "Copyright (C) 2026 Mihawk",
                   "Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler"],
        modifications="none by this port", artifacts=["eboot.bin"], texts="licenses/radv/",
        source=dict(kind="git", path=str(mesa), remote="https://github.com/mihawk-99/PS5_Mesa", revision=mesa_rev,
                    dirty=False, built_with_sdk=radv_sdk.group(1) if radv_sdk else None)))

    # The LLVM runtime the SDK links in
    parts.append(dict(
        id="llvm-runtime", name="LLVM libc++, libc++abi, libunwind and compiler-rt builtins (linked into eboot.bin)",
        licence="Apache-2.0 WITH LLVM-exception",
        copyright=["Copyright (c) 2003-2026 University of Illinois at Urbana-Champaign and the LLVM project contributors"],
        modifications="none", artifacts=["eboot.bin"], texts="licenses/llvm-runtime/",
        source=dict(kind="fixed", revision="ps5-payload-dev SDK v0.42 release archives, and the host's clang",
                    url="https://github.com/ps5-payload-dev/sdk/releases/tag/v0.42")))
    llvm_text = Path("/usr/lib/llvm-18/include/llvm/Support/LICENSE.TXT")
    candidates = [p for p in (llvm_text, *Path("/usr/share/doc").glob("libclang-rt-*/copyright"),
                              *Path("/usr/share/licenses").glob("compiler-rt/LICENSE*"),
                              *Path("/usr/share/licenses").glob("llvm*/LICENSE*")) if p.is_file()]
    (out / "llvm-runtime").mkdir()
    if candidates:
        shutil.copy2(candidates[0], out / "llvm-runtime/LICENSE.TXT")
    else:
        (out / "llvm-runtime/LICENSE.TXT").write_text(
            "Apache License 2.0 with LLVM Exceptions: https://llvm.org/LICENSE.txt\n"
            "(the build host had no copy of the text to include)\n")

    (out / "components.json").write_text(json.dumps(parts, indent=1, ensure_ascii=False) + "\n")
    legal = LEGAL.format(name=name, rule="=" * (len(name) + 35))
    (app / "LEGAL.txt").write_text(legal)
    lines = [legal, "", "The parts of this title", "=======================", ""]
    for p in parts:
        src = p["source"]
        where = (f'{src["remote"]} (revision {src["revision"]})' if src["kind"] == "git"
                 else f'{src["revision"]}: {src["url"]}')
        lines += [p["name"], f'  licence:   {p["licence"]}', f'  copyright: {"; ".join(p["copyright"])}',
                  f'  changes:   {p["modifications"]}', f'  in:        {", ".join(p["artifacts"])}',
                  f'  texts:     {p["texts"]}', f'  source:    {where}', ""]
    (out / "README.txt").write_text("\n".join(lines) + "\n")
    dirty = [p["id"] for p in parts if p["source"].get("dirty")]
    print(f"==> notices: LEGAL.txt, licenses/ ({len(parts)} parts, {count} texts from the engine's tree"
          f"{'; dirty: ' + ', '.join(dirty) if dirty else ''})")


main()

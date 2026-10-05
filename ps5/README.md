# UZDoom for PlayStation 5

An unofficial port of [UZDoom](https://github.com/UZDoom/UZDoom) to the
PlayStation 5 as a homebrew title. It renders with UZDoom's own Vulkan
renderer on RADV, Mesa's Vulkan driver, built for the console by
[PS5_VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate)'s
toolkit, and starts in a DOS-style launcher where the game, the mods and the
start options are chosen with the pad.

Not made, endorsed or supported by the UZDoom team, id Software or Sony
Interactive Entertainment. No game data is included.

**Status: builds and links; not yet run on a console.** See "What is proven"
at the end.

## What the port adds to UZDoom

| Where | What |
| --- | --- |
| `src/common/platform/posix/ps5/` | The console backend, in place of the SDL one: entry point, the display (`VK_KHR_display`), the pad, system services, the launcher's Vulkan presenter. `console/` is PS5_VulkanTemplate's platform code (pad, sound, klog, exit), MIT. |
| `ps5/launcher/` | The launcher: an 80 x 25 text-mode screen in code page 437. It knows nothing of the console, so `test_launcher.cpp` runs it on a PC. |
| `ps5/tools/` | The build: `build.sh` drives it. |
| `ps5/patches/` | The PlayStation 5 output backend for OpenAL Soft. |
| `ps5/sce_sys/` | The title's identity (`param.json`) and icon. |
| `libraries/ZMusic/thirdparty/fluidsynth/src/utils/posix_glibstubs.*` | Stand-ins for the parts of GLib FluidSynth uses, on POSIX threads. |
| `libraries/ZVulkan/src/vulkaninstance.cpp` | Vulkan's functions come from the driver linked into the title, not from a loader. |
| `src/CMakeLists.txt`, `CMakeLists.txt` | A `PS5` path: the console's platform sources, no OpenGL, no SDL, the console's link step. |
| `src/versioninfo.h`, `src/common/scripting/dap/PexCache.cpp` | Two portability fixes. |

Every change is guarded by `PS5` in CMake or `__PROSPERO__` in code; other
platforms build as before.

## Building

On a Linux PC, with the toolkit checked out beside this repository
(`toolkit/setup-toolkit.sh` in the project folder fetches and builds it):

```text
PS5_VulkanTemplate/   PS5_Vulkan/   PS5_Mesa/   PS5_PayloadSDK/   UZDoom/   <- this repository
```

```bash
ps5/tools/build.sh                      # the title folder, dist/PPSA99666/
ps5/tools/package-source.sh dist/PPSA99666   # for a release: adds _source/
```

Host packages (Ubuntu 24.04 names; this is what the build was done with):
`git cmake ninja-build clang lld llvm-18-dev libclang-18-dev libclang-cpp18-dev
libclang-rt-18-dev libllvmspirvlib-18-dev libclc-18-dev spirv-tools glslang-tools
meson python3-mako python3-numpy python3-pil rsync zstd flex bison pkg-config
libsdl2-dev libopenal-dev libvpx-dev libwebp-dev libbz2-dev` (the last five are for
the small native build that makes the build's own tools).

## On the console

```text
/data/homebrew/PPSA99666/        the title folder (/app0 while it runs)
  eboot.bin  sce_sys/  sce_module/
  uzdoom.pk3  lights.pk3  game_support.pk3
  LEGAL.txt  licenses/           what it is licensed under, readable as they are
  _source/                       the complete source, one .tar.xz a part
  iwads/  mods/  soundfonts/     the user's files, when /data cannot be reached
  saves/  config/  uzdoom.log    written by the title
```

The user's files go in `/data/uzdoom/` (`iwads/`, `mods/`, `saves/`, `config/`)
when the title can write there, which depends on the console's setup: a title
is sandboxed to its own folder unless something opens `/data` to it. The port
tries it at start-up and otherwise uses the title folder; the launcher shows
which, as the path an FTP client uses.

Launcher: D-pad or left stick to move, Cross to choose, Circle to go back,
Options to start. In the game the pad is UZDoom's standard gamepad.

## What the source archive leaves out

`_source/uzdoom-*.tar.xz` is this repository at the revision built, without
(`.gitattributes`, `export-ignore`): `soundfont/uzdoom.sf2`, `fm_banks/`,
`bin/` (prebuilt Windows libraries), and the art made from the commercial
games (`wadsrc_bm/static`, `wadsrc_widepix/static`, `wadsrc_extra/nonfree`).
None is used to build the console title, which is configured with
`BUILD_NONFREE=OFF`; all are in upstream UZDoom at the revision named in
`_source/SOURCES.txt`.

## What is proven

Proven on a PC:
- the engine, with the console backend, compiles for `x86_64-sie-ps5` and links
  against the RADV archive with the toolkit's link recipe, and the result
  converts to a signed `eboot.bin`;
- the launcher's logic and drawing (`ps5/launcher/test_launcher.cpp`).

Not proven, because nothing has run on a console yet:
- that it starts at all;
- the display and swapchain, the pad, sound;
- every C library call the engine makes (the console lacks or refuses some
  that link);
- performance.

Known gaps:
- No OGG, FLAC or MP3 decoding yet (libsndfile and mpg123 are not built for the
  console), so mods with music or sounds in those formats are silent there.
- The script JIT is off; scripts run in the interpreter.
- No SoundFont is shipped (see `tools/stage-title.sh`), so MIDI music uses the OPL
  emulation unless the user adds one.
- No keyboard: the console (`~`) and text entry are not reachable.
- One player. Split-screen is not started.

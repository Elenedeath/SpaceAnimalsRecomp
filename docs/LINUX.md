# Linux port (`linux` branch)

> English translation of [LINUX-fr.md](LINUX-fr.md). Both files are updated together; if they differ, the French text
> is the reference.

Same game, same launcher as on Windows, built for Linux and shipped as an **AppImage** (Steam Deck, Linux PCs). As on
Windows, nothing from the game is distributed: the AppImage contains the launcher, the recompiler sources and the build
tools; the game is recompiled on the player's machine from their own copy (`.nup`).

Windows does not change: all Linux-specific code is behind `#ifdef _WIN32 … #else … #endif`. Checked by compiling both
branches with the bundled GCC: launcher and runtime objects are byte-for-byte identical, except `vk_render.o`, where only
the line numbers quoted by Vulkan error messages (`VKCHK`, `__LINE__`) differ.

## What changes on Linux

| Windows | Linux |
|---|---|
| `VirtualAlloc` (commit on demand) | `mmap(PROT_NONE)` + `mprotect` (the kernel provides RAM on first access) — `memory.cpp` |
| vectored exception handler | `sigaction(SIGSEGV…)` — `host_main.cpp` |
| threads, events, fibers | pthreads, futex, `ucontext` — `kernel.cpp`, `plat_linux.h` |
| Win32 window, `vkCreateWin32SurfaceKHR` | SDL3 window, SDL Vulkan surface (main thread: `gfx_boucle_principale`) — `present.cpp` |
| Win32 keyboard, XInput | SDL3 keyboard and controllers (`recomp.ini` scancodes unchanged: `scancodes.h`) — `input.cpp` |
| WASAPI | SDL3 audio stream — `audio_out.cpp` |
| DLSS: driver's `_nvngx.dll`, `nvngx_dlss.dll` | NVIDIA driver's `libnvidia-ngx.so.1`, `libnvidia-ngx-dlss.so` from the same SDK (outils/dlss) |
| Media Foundation recording | not available |
| WinLibs GCC + Clang | zig 0.17 (clang, libc++, glibc 2.31 headers) — `plateforme.py` |
| launcher: Win32, Direct2D, DirectWrite, Media Foundation | **same code**: Win32 implemented on POSIX/SDL3 (`win32_linux.cpp`), Direct2D/DirectWrite on cairo/pango (`d2d_cairo.h`), MPEG-1 background videos played by pl_mpeg |
| saves in `%APPDATA%\SpaceAnimalsRecomp` | `${XDG_DATA_HOME:-~/.local/share}/SpaceAnimalsRecomp` |

Memory the game gives back (`UnmapPhysicalMemory`) is made inaccessible again, except the pages a Vulkan import still
covers: the Linux driver re-reads the pages of a host memory import at every submission while the import exists, and an
inaccessible range makes the submission fail (DEVICE LOST when going back to the menu). Those pages are only emptied
(`MADV_DONTNEED`, they read back as zero). `RECOMP_UNMAP_PINNED_OLD=1`: old behaviour (the whole range inaccessible).

Numbers always use a decimal point. On a system that is not in English (`fr_FR`…), SDL briefly switches the process
locale to the system's under X11, and the render scale was then written as `1,0000` in the engine settings, which the
engine cannot read: black 3D scene, HUD only. The game sets `LC_NUMERIC=C` at start (before SDL), and the render scale is
written with integers. `RECOMP_NUMERIC_LOCALE=1`: old behaviour (the system's locale).

## Building the AppImage

On Linux (or WSL), with clang/ld.lld, cmake, ninja, readelf and ffmpeg (background clips), after preparing the system
root (`sysroot_linux.py`: Steam Runtime "sniper", glibc 2.31, SDL3, cairo, pango):

```sh
python3 recomp/launcher/sysroot_linux.py build/sysroot     # once: ~1.2 GB download
python3 recomp/launcher/paquet_linux.py build/release --sysroot build/sysroot \
    --extraits SpaceAnimalsRecomp-1.1.0-update.zip     # or a folder of extrait_*.mp4
```

Produces `SpaceAnimalsRecomp-<version>-x86_64.AppImage`. On launch, `AppRun` keeps a copy of `app/` (launcher and
sources) in `${XDG_DATA_HOME:-~/.local/share}/SpaceAnimalsRecomp/app`: the built game, `recomp.ini` and the caches stay
there; the tools (Python, zig, glslang, SDL3) stay in the mounted image.

Desktop: `AppRun` installs the launcher's menu entry and icon (`spaceanimalsrecomp`). The game installs its own hidden
entry (`spaceanimalsrecomp-game`, `NoDisplay`) with the game icon taken from your copy (`build/full/icone_jeu.jpg`,
decoded by stb_image): the game window shows the game icon and the launcher keeps its own, as on Windows, and the dock
shows the icon of the active application. UNINSTALL removes both.

Launcher only: `python3 recomp/launcher/construire.py --sysroot <sysroot>` (writes `lanceur` at the root).

## Known limitations

- Updating and moving from the launcher: disabled (the AppImage is replaced as a whole). Uninstalling goes through
  `desinstaller_linux.py` (UNINSTALL button).
- Steam Deck (RADV VANGOGH): the game runs, 60 fps on the title screen. In the prologue cutscene, the volumetric fog
  (`volumetric_fog_light.comp`) takes 25 ms per frame, 82 % of the GPU: turning it off (Graphics page,
  `volumetric_fog = false`) raises it from 13-22 to 45-60 fps. The rest depends on the screen area covered by the
  characters (pixel shaders): 3D rendering at 75 % recommended.
- Fonts: Barlow (OFL) replaces Bahnschrift, which Linux lacks.
- DLSS: off on Linux for now. The NVIDIA driver answers `0xbad00012` when NGX asks for the DLSS extensions, and the
  early GPU enumeration it needs raced with SDL inside libvulkan (crash at start). The Graphics page shows it as not
  available. `RECOMP_DLSS_LINUX=1`: tests (wired as above).
- Automatic GPU choice: Mesa's software renderer (llvmpipe) is set aside by the game and not listed by the launcher. It
  reports system RAM as video memory and was picked over an integrated GPU on PCs without a discrete card (no picture).
  The `gpu` setting can still name it.
- Swapchain: `B8G8R8A8_SRGB` in the sRGB color space first, then `R8G8B8A8_SRGB`, then any format in sRGB. The format
  list and the choice are written to the log (`swapchain formats`). `RECOMP_SWAPCHAIN_ANY_COLORSPACE=1`: old choice
  (last `B8G8R8A8_SRGB` whatever its color space, else the first format). A too-bright picture (grey blacks) was
  reported once; this is not confirmed as its cause.

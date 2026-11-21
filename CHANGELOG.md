# Changelog

## 1.2.0 — Linux, mouse mode, in-game menu, smoother loading

A big one, and most of it comes from contributors: thanks to Elenedeath, sKuhLight and razzamatazm!

- **Linux (experimental)** (contributed by Elenedeath, #39, asked for in #21 and #53): a native Linux version, as an
  AppImage or a `.deb`, with the same launcher. The game is still built from your own copy. DLSS works on NVIDIA RTX
  cards with NVIDIA's driver; FSR is not available on Linux yet. Updating or moving the game from the launcher is
  turned off there (replace the AppImage instead). Tried on a Linux Mint PC (NVIDIA and AMD), and by Elenedeath on the
  Steam Deck, where turning off **Fog** in **GRAPHICS** and using 75 % 3D rendering make the prologue smoother.
- **Mouse mode (experimental, Windows only)** (contributed by sKuhLight, #51, asked for in #45): the PC mouse becomes
  the Joy-Con 2 mouse, and the game switches to mouse aiming by itself, like on the console. Off by default: turn it on
  in **CONTROLS › MOUSE**. Every mouse button and the wheel can be mapped, keys can use Ctrl, Shift or Alt, and F9
  frees the cursor.
- **In-game menu** (contributed by razzamatazm, #48, asked for in #56): F1 or Insert opens a settings menu over the
  game, on a controller the Guide button or Back and Start together. Window mode, volume, keys, controller buttons,
  vibration, an FPS counter and more apply at once; resolution, graphics card, DLSS, FSR and language on the next
  launch. **Quit the game** is there too.
- **Launcher with a controller** (contributed by Elenedeath, #57): D-pad or stick to move, A to click, B to go back,
  Start to play, LB and RB for tabs. The questions (update, uninstall…) are now drawn in the launcher window so they
  work with a controller too. Handy on a Steam Deck or a TV.
- **Smoother loading** (contributed by Elenedeath, #54): hitches when the game loads a lot of textures are about
  halved. Elenedeath also measured less CPU work per frame, which may help slower PCs (no fps change on our fast
  one). The main switches are in a new `[performance]` section of `recomp.ini`.
- **Faster shaders** (contributed by Elenedeath, #40): loops are translated as real loops and global-memory accesses
  are cheaper. Up to 50 % more frames on weak AMD cards, about 5 % on NVIDIA.
- **Smoother motion** (#41): frames are now shown at regular intervals instead of as soon as they are ready. If your
  PC can't keep up with your settings, nothing changes. `frame_pacing = false` in `recomp\build\full\recomp.ini` brings
  back the 1.1.0 behavior.
- Fixed (#26): the game could stop by itself after a long freeze on graphics cards that prepare effects the first time
  they appear (seen at the end of Corneria on a Radeon RX 580). The freeze can still happen, but the game goes on.
- Fixed (#3): if the controller, FSR or DLSS libraries are missing from your install, the launcher now asks for a
  rebuild, and the rebuild downloads them.
- Fixed: shader precompilation now uses the graphics card chosen in the launcher (it always used the automatic choice,
  so the game could freeze the first time effects appeared on another card).
- #21: the Windows launcher no longer shows a white window when a Windows drawing effect is missing (seen with Wine
  and Proton).
- When the game stops showing frames, the log now says what the renderer was waiting for. It doesn't fix freezes yet
  (#38, #33), but a bug report made after one will tell us the cause.
- #37, #43: an install without its `outils` folder (the update zip extracted on its own) now tells you to download
  the full package, instead of asking you to install Python.
- Updating from 1.1.0 on Windows: say yes to the update, then **REBUILD NOW ›** (a few minutes: the game itself isn't
  recompiled). On Linux, download the AppImage or the `.deb` and build the game from your copy.

## 1.1.0 — FSR and DLSS, PlayStation and Switch controllers, better performance

Thanks again to everyone who opened an issue: most of this comes from your reports.

- **AMD FSR (experimental)**: upscaling for all graphics cards. FSR 3.1 on most cards, FSR 4 on some Radeon cards
  (we couldn't try FSR 4 ourselves: tell us how it goes).
- **NVIDIA DLSS (experimental)**, RTX cards only: DLAA or upscaling, with the DLSS 4 or DLSS 4.5 model.
- **PlayStation, Switch Pro and Joy-Con controllers** should now work, USB or Bluetooth (#3, #16).
- **Controller remapping** (#8): **CONTROLS › CONTROLLER** in the launcher. Click a button, then press the new one or
  click it on the drawing.
- **Improved performance** in busy scenes, and shorter hitches when the scene changes or a lot happens on screen
  (#10, #25: tell us if combat feels smoother).
- **Graphics card choice** in **GRAPHICS** (Auto takes the dedicated card with the most video memory).
- **3D rendering** at 100, 85, 75 or 67 % (experimental). It replaces the "Like the console" setting, which gave a
  black or grey 3D scene (#5).
- **Game location and desktop shortcut** (#14) in **SETTINGS**: **Move…** moves the whole install to another folder,
  without rebuilding the game.

Also in this version:

- Fixed: blue patches on the ground in Corneria city. Ground markings, cracks and explosion marks now look like on
  the console.
- Fixed: **UNINSTALL** left the install folder behind.
- #17, #19: should fix a rare crash, seen against Andross and at the end of the tutorial.
- #24: the build now keeps an eye on memory while it runs, instead of guessing at the start. It could fill all the
  memory on PCs with many CPU threads (seen on Linux with Proton).
- #18: the game no longer picks Microsoft's "Vulkan on Direct3D 12" layer instead of your real graphics driver. This
  should also help laptops with two graphics chips that showed a black screen (#23).
- #10: sharper picture on scaled screens (125 %, 150 %…).
- #5: the bug report no longer contains the path or the file name of your copy of the game, and the game's log no
  longer shows your Windows user name.
- If you see a new picture defect, `fast_rendering = false` in `recomp\build\full\recomp.ini` turns this version's
  rendering optimizations off. Please open an issue too.
- Updating from 1.0.3: say yes to the update, then **REBUILD NOW ›**. Only the last build steps run again (about
  2 minutes: the game itself isn't recompiled). Without rebuilding, only the launcher changes are active. The update
  also downloads the controller, FSR and DLSS libraries (about 96 MB) from their official sources.

This software uses NVIDIA DLSS. The DLSS library stays under NVIDIA's license, not the GPL (see the README).

## 1.0.3 — less RAM, better AMD support, faster build

Thanks to everyone who opened an issue: most of this comes from your reports.

- **Improved RAM usage**: the game uses much less memory.
- **Improved AMD card support.**
- **Faster build**: the game is now compiled with Clang, included in the package.

**About performance**: on Switch 2, the game needs DLSS to run smoothly: it draws at a lower resolution and DLSS
upscales the picture. SpaceAnimalsRecomp has no DLSS yet and draws everything at native resolution. The picture is
sharper, but it needs a lot more power from your graphics card. DLSS on PC is on the roadmap.

Also in this version:

- Fixed #1: the build no longer stops when the GPU driver crashes on a shader, or when the folder path has square
  brackets.
- Fixed #4: you can move or rename the install folder without rebuilding.
- Fixed #12: the launcher window no longer stays black on screens at 270 Hz or more.
- #13: the game should no longer stop by itself after a long black screen at start (seen on an AMD card).
- Should fix pieces of the Arwing drawn on Slippy's face in a cutscene.
- #9: might fix **Single Player** not responding in the campaign menu after the tutorial. Please tell us if you still see it.
- **BUG REPORT** button in the launcher: it makes one zip with your hardware and the logs, ready to attach to an issue.
- The launcher asks before installing an update.
- Nothing opens by itself during the build anymore, and the launcher window is bigger.
- You can change the keyboard keys on the launcher's **CONTROLS** page, with two layouts ready: WASD (QWERTY) and ZQSD
  (AZERTY). Controllers still follow the button position.
- **UNINSTALL** button, or `uninstall.bat`. It never touches your copy of the game.
- Saves are now in `%APPDATA%\SpaceAnimalsRecomp\save`. Your old saves are copied there the first time you play
  after rebuilding.
- Updating from 1.0.2: say yes to the update. The launcher then asks if you want to rebuild the game, or you can
  click **REBUILD NOW ›** later. Some improvements require rebuilding the game: without it, only the launcher changes
  are active, and the RAM, AMD, save and keyboard key changes are not. Your copy, your saves and your settings stay.

## 1.0.2 — launcher background included, shorter build

- **Launcher background included**: the clips that play behind the launcher (gameplay recorded by SerenityG4K) now
  come with the package. The build no longer runs the game for several minutes to record them; the launcher shows them
  from the first start. **SETTINGS › RE-RECORD FOOTAGE** still records new ones from your own copy.
- **Shaders created by the game**: the build now runs the game for a few seconds, muted, to collect the 80 shaders the
  game creates when it starts (they are not in the shader archive of the copy), then translates and precompiles them
  with the others: 2,388 shaders. Up to 1.0.1, the clip recording did this along the way.
- **Correction of the 1.0.1 notes**: they said that a new install lacked these 80 shaders. In fact the clip recording
  collected them; only a build where the recording was skipped or failed lacked them.
- **Updating from 1.0.1** with the launcher: the update is 60 MB, because of the clips. Nothing is rebuilt.

## 1.0.1 — lighter package, third-party licenses

- **Licenses of the bundled tools**: the package now has `THIRD_PARTY_NOTICES.txt`, which lists every bundled component
  with its exact version, its license and where to get its source code, and a `LICENCES` folder with the full license
  texts (GPL 3, LGPL 3 and 2.1, GCC Runtime Library Exception, MinGW-w64, isl, zlib, zstd, glslang). Both are also in
  the source repository, in `recomp/licences/`.
- **Lighter package**: GCC and glslang are reduced to the files the build uses (list made by tracing the real build
  commands). The download is 91 MB instead of 316 MB, and 424 MB once extracted instead of 1.1 GB. When the install
  path has spaces, the compiler copy in `C:\ProgramData\StarFoxRecomp` is about 400 MB instead of 1 GB. Removed:
  Fortran, Objective-C, LTO, gdb and its Python, CMake, Doxygen, Cppcheck, ctags, premake, NASM, Yasm, ccache, gcov and
  the documentation.
- **Launcher**: in the source tree, `lanceur.exe` is now built at the root, next to `recomp`, as in the package.
- **README**: the project is experimental. It is at the "it runs" stage, not optimized yet, and has only been tested on
  an AMD Ryzen 7 9700X with an NVIDIA GeForce RTX 5070 Ti. Handheld PCs (Steam Deck, Lenovo Legion Go, ASUS ROG Ally…)
  are not tested yet.
- **Updating from 1.0.0** with the launcher: no code of the game changed, so nothing is rebuilt and **UPDATE GAME** takes
  a few seconds. An install updated from 1.0.0 keeps its full compiler, which still works. To get the lighter one,
  extract the 1.0.1 package to a new folder.
- **Known issue** (also in 1.0.0): on a new install, the build extracts 2,308 of the game's shaders from your copy;
  80 more (72 compute shaders) that the game loads at start are not extracted yet. A scene that uses one would miss
  that effect. A fix is planned for the next version.

## 1.0.0 — first public release

StarFox Remake [recompiled]: Star Fox (Nintendo Switch 2, update 2.0.0) as a native Windows program, built on your PC
from your own copy.

- **Recompiler**: the whole game code (main, sdk, subsdk0 to subsdk3) translated from ARM64 to C++, then compiled to x86-64.
- **Runtime**: Horizon OS kernel and services emulated at the system-call and IPC level; the NVN graphics API runs on
  Vulkan 1.3; the game's shaders translated to SPIR-V (2,388 stages).
- **Shader precompilation**: every shader compiled for your GPU on the first launch with Vulkan graphics pipeline
  libraries, plus a pipeline cache on disk: no shader compilation stutter in game.
- **Launcher** (`lanceur.exe`): builds the game from your `.nup` copy (SHA-256 check, extraction, recompilation, link,
  shaders), with progress and logs; full screen or windowed; game language (15), resolution up to 4K, frame rate,
  3D rendering, volume; background clips recorded from your copy's cutscenes; French and English.
- **Updates**: the launcher checks this repository's releases, downloads and verifies an update (SHA-256), installs it,
  and rebuilds only the parts whose code changed.
- **Game icon** taken from your copy at build time (never distributed).
- The whole game can be finished: it has been played through completely several times at 60 fps with sound, and it is
  rather stable.
- Known limits: 80 of the 2,468 shader stages are left to translate; 90 and 120 fps are not stable yet.
- Planned: NVIDIA DLSS, frame generation, mod support, online play, mouse controls, key remapping and several controllers, Linux.

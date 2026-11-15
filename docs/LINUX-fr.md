# Portage Linux (branche `linux`)

> Traduction anglaise : [LINUX.md](LINUX.md). Les deux fichiers sont tenus à jour ensemble ; ce texte-ci fait foi.

Même jeu, même launcher que sous Windows, compilés pour Linux et livrés en **AppImage** (Steam Deck, PC Linux). Comme
sous Windows, rien du jeu n'est distribué : l'AppImage contient le launcher, les sources du recompilateur et les outils
de construction ; le jeu est recompilé sur la machine du joueur depuis sa propre copie (`.nup`).

Windows ne change pas : tout le code propre à Linux est derrière `#ifdef _WIN32 … #else … #endif`. Vérifié en compilant
les deux branches avec le GCC fourni : objets du launcher et du runtime identiques octet par octet, sauf `vk_render.o`
dont seuls les numéros de ligne cités par les messages d'erreur Vulkan (`VKCHK`, `__LINE__`) diffèrent.

## Ce qui change sous Linux

| Windows | Linux |
|---|---|
| `VirtualAlloc` (engagement à la demande) | `mmap(PROT_NONE)` + `mprotect` (le noyau fournit la RAM au premier accès) — `memory.cpp` |
| gestionnaire vectoriel d'exceptions | `sigaction(SIGSEGV…)` — `host_main.cpp` |
| fils, événements, fibres | pthreads, futex, `ucontext` — `kernel.cpp`, `plat_linux.h` |
| fenêtre Win32, `vkCreateWin32SurfaceKHR` | fenêtre SDL3, surface Vulkan de SDL (fil principal : `gfx_boucle_principale`) — `present.cpp` |
| clavier Win32, XInput | clavier et manettes SDL3 (scancodes de `recomp.ini` inchangés : `scancodes.h`) — `input.cpp` |
| WASAPI | flux audio SDL3 — `audio_out.cpp` |
| DLSS : `_nvngx.dll` du pilote, `nvngx_dlss.dll` | `libnvidia-ngx.so.1` du pilote NVIDIA, `libnvidia-ngx-dlss.so` du même SDK (outils/dlss) |
| Enregistrement Media Foundation | absent |
| GCC + Clang de WinLibs | zig 0.17 (clang, libc++, en-têtes de la glibc 2.31) — `plateforme.py` |
| launcher : Win32, Direct2D, DirectWrite, Media Foundation | **même code** : Win32 réalisé sur POSIX/SDL3 (`win32_linux.cpp`), Direct2D/DirectWrite sur cairo/pango (`d2d_cairo.h`), vidéos de fond en MPEG-1 lues par pl_mpeg |
| sauvegardes `%APPDATA%\SpaceAnimalsRecomp` | `${XDG_DATA_HOME:-~/.local/share}/SpaceAnimalsRecomp` |

## Construire l'AppImage

Sous Linux (ou WSL), avec clang/ld.lld, cmake, ninja, readelf et ffmpeg (extraits du fond), après avoir préparé la
racine système (`sysroot_linux.py` : Steam Runtime « sniper », glibc 2.31, SDL3, cairo, pango) :

```sh
python3 recomp/launcher/sysroot_linux.py build/sysroot     # une fois : ~1,2 Go téléchargés
python3 recomp/launcher/paquet_linux.py build/release --sysroot build/sysroot \
    --extraits SpaceAnimalsRecomp-1.1.0-update.zip     # ou un dossier d'extrait_*.mp4
```

Produit `SpaceAnimalsRecomp-<version>-x86_64.AppImage`. Au lancement, `AppRun` garde une copie de `app/` (launcher et
sources) dans `${XDG_DATA_HOME:-~/.local/share}/SpaceAnimalsRecomp/app` : le jeu construit, `recomp.ini` et les caches
y restent ; les outils (Python, zig, glslang, SDL3) restent dans l'image montée.

Pour le launcher seul : `python3 recomp/launcher/construire.py --sysroot <sysroot>` (écrit `lanceur` à la racine).

## Limites connues

- Mise à jour et déplacement depuis le launcher : désactivés (l'AppImage se remplace d'un bloc). La désinstallation passe
  par `desinstaller_linux.py` (bouton DÉSINSTALLER).
- Steam Deck (RADV VANGOGH) : le jeu tourne, 60 img/s à l'écran titre. Dans la cinématique du prologue, le brouillard
  volumétrique (`volumetric_fog_light.comp`) prend 25 ms par image, 82 % du GPU : le couper (page Graphismes,
  `volumetric_fog = false`) fait passer de 13-22 à 45-60 img/s. Le reste dépend de la surface couverte par les
  personnages (shaders de pixels) : rendu 3D à 75 % conseillé.
- Polices : Barlow (OFL) remplace Bahnschrift, absente de Linux.

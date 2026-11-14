#!/usr/bin/env python3
"""Prépare la racine système de construction Linux (--sysroot de paquet_linux.py et de construire.py) : le SDK du Steam
Runtime 3 « sniper » de Valve (glibc 2.31, cairo, pango, fontconfig, glib), plus SDL 3.4.18 compilé contre lui.

  python3 sysroot_linux.py <dossier> [--cache <dossier des téléchargements>]

Pourquoi : le launcher et les bibliothèques fournies dans l'AppImage, compilés contre la glibc de la machine de
construction, exigeraient cette glibc ou une plus récente chez le joueur. Compilés contre celle du SDK (2.31), ils
démarrent sur SteamOS et sur toute distribution au moins aussi récente que Debian 11.

SDL3 : celui du SDK est remplacé par SDL 3.4.18 compilé ici, qui charge X11, Wayland, PulseAudio, PipeWire et ALSA à
l'exécution (SDL_DEPS_SHARED) au lieu de les lier : il ne demande que la glibc, le bureau du joueur fournit le reste.
Il va dans <dossier>/usr/local, où paquet_linux.py et construire.py le cherchent.

Il faut clang, ld.lld, cmake, ninja, pkg-config, readelf et tar. Premier lancement : ~1,2 Go téléchargés (gardés dans
le cache, <dossier>.cache par défaut), quelques minutes. Chaque téléchargement est vérifié par son sha256.
"""
import argparse
import os
import shutil
import subprocess
import sys

LANCEUR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, LANCEUR)
from paquet_linux import dire, telecharger  # noqa: E402

T = "x86_64-linux-gnu"
SNIPER = "3.0.20261021.266025"
PINS = {
    "sniper": (f"https://repo.steampowered.com/steamrt-images-sniper/snapshots/{SNIPER}/"
               "com.valvesoftware.SteamRuntime.Sdk-amd64%2Ci386-sniper-sysroot.tar.gz",
               "8f71947035f97925a311b2ed753f22efcf2f13d6f1631945b8e537f8903ce2cf"),
    "sdl3": ("https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz",
             "9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3"),
}
# parties du SDK que lisent une compilation et une édition de liens x86_64 (le reste de l'archive n'est pas extrait)
PARTIES = ["usr/include", f"usr/lib/{T}", f"usr/lib/gcc/{T}", f"lib/{T}", "lib64", "usr/share/pkgconfig",
           "usr/share/wayland-protocols", "usr/bin/wayland-scanner"]
# seules dépendances permises de notre libSDL3 : la glibc (SDL charge le reste lui-même)
SDL_PERMIS = {"libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "librt.so.1", "ld-linux-x86-64.so.2"}


def deballer_sdk(sr, cache):
    archive = telecharger(*PINS["sniper"], os.path.join(cache, f"sniper-sysroot-{SNIPER}.tar.gz"))
    shutil.rmtree(sr, ignore_errors=True)
    os.makedirs(sr)
    dire("extraction du SDK sniper")
    subprocess.run(["tar", "-xzf", archive, "-C", sr, "--wildcards", "--no-wildcards-match-slash", *PARTIES],
                   check=True)
    # liens absolus du SDK (/lib/…, /usr/lib/…) : hors de son conteneur, ils viseraient cette machine
    for racine, dossiers, fichiers in os.walk(sr):
        for n in dossiers + fichiers:
            p = os.path.join(racine, n)
            if os.path.islink(p) and os.readlink(p).startswith("/"):
                cible = os.path.join(sr, os.readlink(p).lstrip("/"))
                os.unlink(p)
                os.symlink(os.path.relpath(cible, racine), p)


def construire_sdl3(sr, cache):
    # celui du SDK retiré : aucune compilation ni édition de liens ne doit le trouver à la place du nôtre
    lib_sdk = os.path.join(sr, "usr", "lib", T)
    for n in os.listdir(lib_sdk):
        if n.startswith(("libSDL3.", "libSDL3_test.")):
            os.unlink(os.path.join(lib_sdk, n))
    for p in (os.path.join(lib_sdk, "pkgconfig", "sdl3.pc"),):
        if os.path.lexists(p):
            os.unlink(p)
    shutil.rmtree(os.path.join(sr, "usr", "include", "SDL3"), ignore_errors=True)
    archive = telecharger(*PINS["sdl3"], os.path.join(cache, "SDL3-3.4.18.tar.gz"))
    src = os.path.join(cache, "SDL3-3.4.18")
    shutil.rmtree(src, ignore_errors=True)
    subprocess.run(["tar", "-xzf", archive, "-C", cache], check=True)
    local = os.path.join(sr, "usr", "local")
    # wayland-scanner du SDK : un plus récent écrit des appels que sa libwayland 1.18 n'a pas (wl_proxy_marshal_flags).
    # Il tourne ici avec les bibliothèques du SDK (libxml2), jamais avec sa glibc (lib/ n'est pas dans le chemin).
    scanner = os.path.join(local, "bin", "wayland-scanner")
    os.makedirs(os.path.dirname(scanner), exist_ok=True)
    with open(scanner, "w") as f:
        f.write(f'#!/bin/sh\nLD_LIBRARY_PATH="{sr}/usr/lib/{T}" exec "{sr}/usr/bin/wayland-scanner" "$@"\n')
    os.chmod(scanner, 0o755)
    pc = [os.path.join(sr, "usr", "lib", T, "pkgconfig"), os.path.join(sr, "usr", "share", "pkgconfig")]
    env = dict(os.environ, PKG_CONFIG_SYSROOT_DIR=sr, PKG_CONFIG_PATH="", PKG_CONFIG_LIBDIR=os.pathsep.join(pc))
    b = os.path.join(src, "build")
    dire("compilation de SDL 3.4.18")
    subprocess.run(["cmake", "-S", src, "-B", b, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                    "-DCMAKE_C_COMPILER=clang", f"-DCMAKE_C_COMPILER_TARGET={T}", "-DCMAKE_SYSTEM_NAME=Linux",
                    "-DCMAKE_SYSTEM_PROCESSOR=x86_64", f"-DCMAKE_SYSROOT={sr}",
                    "-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld", "-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld",
                    # en-têtes et bibliothèques de la racine système seulement, jamais de cette machine
                    "-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY", "-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY",
                    "-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY", "-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER",
                    f"-DCMAKE_INSTALL_PREFIX={local}", "-DCMAKE_INSTALL_LIBDIR=lib", "-DSDL_STATIC=OFF",
                    "-DSDL_TEST_LIBRARY=OFF", "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF", "-DSDL_RPATH=OFF",
                    "-DSDL_DEPS_SHARED=ON", f"-DWAYLAND_SCANNER={scanner}"], env=env, check=True)
    subprocess.run(["cmake", "--build", b], check=True)
    subprocess.run(["cmake", "--install", b], check=True, stdout=subprocess.DEVNULL)
    os.makedirs(os.path.join(local, "share", "doc", "SDL3"), exist_ok=True)
    shutil.copy2(os.path.join(src, "LICENSE.txt"), os.path.join(local, "share", "doc", "SDL3", "LICENSE.txt"))
    shutil.rmtree(src)
    lib = os.path.realpath(os.path.join(local, "lib", "libSDL3.so.0"))
    dyn = subprocess.run(["readelf", "-d", lib], capture_output=True, text=True, check=True).stdout
    liees = {l.split("[", 1)[1].rstrip("]") for l in dyn.splitlines() if "(NEEDED)" in l}
    if liees - SDL_PERMIS:
        sys.exit(f"{lib} lie {' '.join(sorted(liees - SDL_PERMIS))} : ses options SDL_*_SHARED sont coupées")


def main():
    ap = argparse.ArgumentParser(description="racine système de construction Linux (Steam Runtime sniper + SDL3)")
    ap.add_argument("dossier")
    ap.add_argument("--cache", help="téléchargements gardés (par défaut <dossier>.cache)")
    a = ap.parse_args()
    if not sys.platform.startswith("linux"):
        sys.exit("à lancer sous Linux (ou WSL)")
    sr = os.path.abspath(a.dossier)
    cache = os.path.abspath(a.cache or sr.rstrip("/") + ".cache")
    os.makedirs(cache, exist_ok=True)
    deballer_sdk(sr, cache)
    construire_sdl3(sr, cache)
    with open(os.path.join(sr, ".sysroot_linux"), "w") as f:
        f.write(f"sniper {SNIPER}\nSDL 3.4.18\n")
    dire(f"OK {sr}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

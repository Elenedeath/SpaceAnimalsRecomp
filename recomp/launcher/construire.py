#!/usr/bin/env python3
"""Compile le launcher : lanceur.exe à la racine du projet, à côté de recomp/, comme dans le paquet (Win32, Direct2D,
DirectWrite, Media Foundation ; aucune DLL à fournir à côté).

  python construire.py [--out lanceur_new.exe] [--classe NomDeClasse]   # g++ et windres de MSYS2 (ucrt64) ou du PATH

--classe : version d'essai avec une autre classe de fenêtre (instance unique par classe), qui peut tourner à côté du
launcher du joueur.
"""
import os
import shutil
import subprocess
import sys

ICI = os.path.dirname(os.path.abspath(__file__))
SOURCES = ["lanceur.cpp", "dessin.cpp", "video.cpp", "textes.cpp", "manette.cpp"]
LIBS = ["-ld3d11", "-ldxgi", "-ld2d1", "-ldwrite", "-lwindowscodecs", "-lmfplat", "-lmfreadwrite", "-lmfuuid", "-lole32",
        "-loleaut32", "-luuid", "-lshell32", "-lshlwapi", "-ldwmapi", "-lwinmm"]


def outil(nom):
    sys.path.insert(0, ICI)
    from preparer import gcc_sans_espaces   # copie hors des chemins à espaces si besoin (voir preparer.py)
    for o in (os.path.join(ICI, "..", "..", "outils"), os.path.join(ICI, "..", "outils")):
        p = os.path.join(o, "gcc", "bin", nom + ".exe")
        if os.path.exists(p):
            return os.path.join(gcc_sans_espaces(os.path.abspath(o)), nom + ".exe")
    p = os.path.join(r"C:\msys64\ucrt64\bin", nom + ".exe")
    if os.path.exists(p):
        return p
    return shutil.which(nom) or nom


def linux():
    """Linux (AppImage du Steam Deck) : le même launcher, compilé par clang contre une racine système (Steam Runtime
    « sniper », glibc 2.31 : sysroot_linux.py), avec SDL3, cairo, pango et fontconfig. L'API Win32
    dont il se sert est réalisée par win32_linux.cpp, Direct2D par d2d_cairo.h.

      python3 construire.py --sysroot <racine système> [--out ../../lanceur]   (clang++ et ld.lld du PATH, ou CXX=…)
    """
    if "--sysroot" not in sys.argv:
        print("--sysroot <racine système> : obligatoire sous Linux (glibc 2.31, SDL3, cairo, pango)")
        return 1
    sr = os.path.abspath(sys.argv[sys.argv.index("--sysroot") + 1])
    sortie = sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else os.path.join(ICI, "..", "..", "lanceur")
    cxx = os.environ.get("CXX") or shutil.which("clang++") or "clang++"
    t = "x86_64-linux-gnu"
    inc = [os.path.join(sr, "usr", "local", "include")] + [os.path.join(sr, "usr", "include", d) for d in
           ("pango-1.0", "cairo", "glib-2.0", "harfbuzz", "freetype2", "fribidi", "pixman-1", "libpng16")]
    inc.append(os.path.join(sr, "usr", "lib", t, "glib-2.0", "include"))
    libs = [os.path.join(sr, "usr", "local", "lib"), os.path.join(sr, "usr", "lib", t), os.path.join(sr, "lib", t)]
    cmd = [cxx, "--target=" + t, "--sysroot=" + sr, "-fuse-ld=lld", "-std=c++20", "-O2", "-march=x86-64-v2", "-Wall",
           "-Wno-unknown-pragmas", "-Wno-missing-braces", *("-isystem" + i for i in inc),
           "-I" + os.path.join(ICI, "..", "runtime", "third_party"),
           *(os.path.join(ICI, s) for s in SOURCES + ["win32_linux.cpp"]), "-o", sortie,
           *("-L" + l for l in libs), "-Wl,-rpath-link," + ":".join(libs), "-Wl,-rpath,$ORIGIN/lib",
           "-lSDL3", "-lpangocairo-1.0", "-lpango-1.0", "-lgobject-2.0", "-lglib-2.0", "-lcairo", "-lfontconfig",
           "-ldl", "-lpthread"]
    r = subprocess.run(cmd, cwd=ICI, capture_output=True, text=True)
    messages = (r.stdout + r.stderr).strip()
    if messages:
        print(messages[:12000])
    if r.returncode:
        return 1
    fini = os.path.abspath(sortie)
    print(f"OK {fini} ({os.path.getsize(fini) / 1e6:.1f} Mo)")
    return 0


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    if sys.platform.startswith("linux"):
        return linux()
    # à la racine du projet, comme dans le paquet (recomp/ à côté) ; autre nom avec --out si lanceur.exe tourne.
    # Chemin relatif au dossier du launcher : sans espaces, quel que soit le dossier du projet.
    sortie = sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else os.path.join("..", "..", "lanceur.exe")
    os.makedirs(os.path.join(ICI, "build"), exist_ok=True)
    if not os.path.exists(os.path.join(ICI, "icone.ico")):
        subprocess.run([sys.executable, "-I", os.path.join(ICI, "icone.py")], check=True)
    env = dict(os.environ)
    gxx = outil("g++")
    env["PATH"] = os.path.dirname(gxx) + os.pathsep + env.get("PATH", "")
    res = os.path.join(ICI, "build", "lanceur_res.o")
    r = subprocess.run([outil("windres"), "lanceur.rc", "-O", "coff", "-o", res], cwd=ICI, env=env, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout + r.stderr)
        return 1
    # GCC lie d'office default-manifest.o (manifeste minimal). Celui de MSYS2 est en langue neutre et ld l'écarte devant
    # le nôtre ; celui de WinLibs (outils/) est en anglais (1033) : ld garde alors les deux manifestes n° 1 (« .rsrc
    # merge failure: multiple non-default manifests »). Un default-manifest.o vide, trouvé d'abord grâce à -B, l'évite.
    # Chemin relatif : sans espaces, quel que soit le dossier du launcher.
    sans_manifeste = os.path.join("build", "sans_manifeste")
    os.makedirs(os.path.join(ICI, sans_manifeste), exist_ok=True)
    vide = os.path.join(sans_manifeste, "vide.c")
    open(os.path.join(ICI, vide), "w").close()
    r = subprocess.run([gxx, "-c", "-x", "c", vide, "-o", os.path.join(sans_manifeste, "default-manifest.o")], cwd=ICI, env=env,
                       capture_output=True, text=True)
    if r.returncode:
        print(r.stdout + r.stderr)
        return 1
    # en-têtes Vulkan du moteur (liste des cartes graphiques, lanceur.cpp : vulkan-1.dll est chargé à l'exécution)
    vulkan = os.path.join("..", "runtime", "third_party")
    classe = []
    if "--classe" in sys.argv:
        nom = sys.argv[sys.argv.index("--classe") + 1]
        if not nom.isalnum():
            print("--classe : lettres et chiffres seulement")
            return 1
        classe = [f'-DLANCEUR_CLASSE=L"{nom}"']
    cmd = [gxx, "-std=c++20", "-O2", "-municode", "-mwindows", "-Wall", "-Wno-unknown-pragmas", "-static", "-static-libgcc",
           "-static-libstdc++", "-I" + vulkan, *classe, "-B" + sans_manifeste + "/", *SOURCES, res, "-o", sortie, *LIBS]
    r = subprocess.run(cmd, cwd=ICI, env=env, capture_output=True, text=True)
    messages = (r.stdout + r.stderr).strip()
    if messages:
        print(messages[:12000])
    if r.returncode:
        return 1
    fini = os.path.abspath(os.path.join(ICI, sortie))
    print(f"OK {fini} ({os.path.getsize(fini) / 1e6:.1f} Mo)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

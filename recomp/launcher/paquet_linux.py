#!/usr/bin/env python3
"""Assemble l'AppImage Linux (Steam Deck et PC Linux) : le même launcher que sous Windows, compilé pour Linux, les sources
du recompilateur et les outils de construction, SANS rien qui vienne du jeu. Comme le paquet Windows (paquet.py), le
jeu est recompilé sur la machine du joueur, depuis sa propre copie.

  python3 paquet_linux.py <dossier de sortie> --sysroot <racine système> [--extraits <dossier des extrait_*.mp4>]
                          [--version 1.1.0] [--garder]

À lancer sous Linux (ou WSL) avec clang, ld.lld, readelf et, pour les extraits, ffmpeg. La racine système est faite
par sysroot_linux.py (Steam Runtime « sniper », glibc 2.31, SDL3, cairo, pango) : le launcher et les
bibliothèques fournies démarrent sur tout système au moins aussi récent que Debian 11, dont SteamOS.

Contenu de l'AppImage :
  AppRun                 entrée : garde une copie de app/ dans ${XDG_DATA_HOME:-~/.local/share}/SpaceAnimalsRecomp/app
                         (mise à jour quand l'AppImage en apporte une autre), y relie outils/ (dans l'image montée) et
                         lance le launcher ; le jeu construit, ses réglages et ses caches restent dans ce dossier
  app/lanceur            le launcher (construire.py --sysroot)
  app/recomp/…           recompilateur, runtime, shaders, scripts du launcher (mêmes fichiers que le paquet Windows)
  app/recomp/launcher/extraits/*.mpg   extraits du fond, en MPEG-1 (lu par pl_mpeg), convertis depuis les MP4
  app/polices/           Barlow (licence OFL), à la place de Bahnschrift
  outils/                Python, zig, glslang (outils.py), SDL3 (racine système)
  lib/                   bibliothèques du launcher absentes d'un système de base (racine système)
  lib-secours/           fontconfig, prise seulement si le système n'en a pas (AppRun)
"""
import argparse
import hashlib
import os
import shutil
import stat
import struct
import subprocess
import sys
import urllib.request

LANCEUR = os.path.dirname(os.path.abspath(__file__))
RECOMP = os.path.dirname(LANCEUR)
PROJET = os.path.dirname(RECOMP)
sys.path.insert(0, LANCEUR)
sys.path.insert(0, RECOMP)
import paquet  # noqa: E402  (copier, copier_arbre, empreinte)

NOM = "SpaceAnimalsRecomp"
T = "x86_64-linux-gnu"
# appimagetool et l'exécutable de démarrage de type 2 (sans libfuse2 chez le joueur)
PINS = {
    "appimagetool": ("https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage",
                     "ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"),
    "runtime": ("https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64",
                "2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d"),
}
_GF = "https://raw.githubusercontent.com/google/fonts/6cdf01867df0813c2390f90dff7dc66c87f14cf7/ofl/barlow/"
POLICES_SHA256 = {
    "Barlow-Light.ttf": "20b1375c0c6606701867bf34c7cc97f44087850567a36587d25abb9510b183cc",
    "Barlow-Regular.ttf": "95aa02c7c43096e0dd44d787ba6216864a67157e402adab59b35572e0c1577ea",
    "Barlow-SemiBold.ttf": "86577cb32f8abe3673db53ca0f4221e6856751a4f6730c867e00f720f8bb1fc5",
    "Barlow-Bold.ttf": "84e6a4d61e7c3e21f3c50ea6a4f7e5303a3467864c038be6ea3759bab8d547f9",
    "OFL.txt": "186d750eb496a4c17a76385f82be6aea2ac1cf2de074a811d63786cf374ea73f",
}
POLICES = list(POLICES_SHA256)
# Bibliothèques que tout bureau Linux a et que l'AppImage doit prendre chez lui : la glibc, le chargeur Vulkan (il
# trouve les pilotes du système), libgcc_s et libstdc++ (celle de la racine système est plus ancienne que celle de
# tout bureau, et les pilotes de Mesa veulent la plus récente).
SYSTEME = {"libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "librt.so.1", "ld-linux-x86-64.so.2",
           "libvulkan.so.1", "libgcc_s.so.1", "libstdc++.so.6"}
# Fournies à part (lib-secours/) : prises seulement si le système n'en a pas. fontconfig lit la configuration du
# système (/etc/fonts) : celle de la racine système (2.13) avertissait sur les éléments récents (« reset-dirs »,
# Ubuntu 24.04) ; celle du système la comprend toujours. Presque tout bureau l'a (SteamOS compris).
SECOURS = {"libfontconfig.so.1"}

APPRUN = """#!/bin/sh
# Entrée de l'AppImage (recomp/launcher/paquet_linux.py) : le launcher tourne depuis une copie de app/ dans le dossier
# de données du joueur, qui garde le jeu construit, ses réglages et ses caches ; outils/ et lib/ restent dans l'image.
set -eu
here=$(dirname "$(readlink -f "$0")")
home=${XDG_DATA_HOME:-$HOME/.local/share}/SpaceAnimalsRecomp/app
mkdir -p "$home"
if ! cmp -s "$here/app/.paquet" "$home/.paquet"; then
    # sources et launcher de cette AppImage ; recomp/build (le jeu), lanceur.ini et les journaux restent
    rm -f "$home/lanceur"
    cp -a "$here/app/." "$home/"
fi
ln -sfn "$here/outils" "$home/outils"
# fontconfig : celle du système quand il en a une (elle comprend toujours sa propre configuration ; la copie de
# l'AppImage, plus ancienne, avertissait sur les éléments récents), sinon la copie de secours
libs="$here/lib"
if ! { /sbin/ldconfig -p 2>/dev/null || ldconfig -p 2>/dev/null; } | grep -q "libfontconfig\.so\.1 "; then
    libs="$libs:$here/lib-secours"
fi
export SA_LD_LIBRARY_PATH_SYSTEME="${LD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH="$libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$home/lanceur" "$@"
"""

DESKTOP = """[Desktop Entry]
Type=Application
Name=StarFox Remake [recompiled]
Comment=Native port of Star Fox (Switch 2), built on your PC from your own copy
Exec=AppRun
Icon=spaceanimalsrecomp
Categories=Game;
Terminal=false
"""


def dire(m):
    print(m, flush=True)


def telecharger(url, sha, dest):
    if os.path.exists(dest) and paquet.empreinte(dest) == sha:
        return dest
    dire(f"  téléchargement : {url}")
    req = urllib.request.Request(url, headers={"User-Agent": "spaceanimalsrecomp-paquet/1.0"})
    with urllib.request.urlopen(req, timeout=300) as r, open(dest + ".part", "wb") as f:
        shutil.copyfileobj(r, f, 1 << 20)
    h = paquet.empreinte(dest + ".part")
    if sha and h != sha:
        os.remove(dest + ".part")
        sys.exit(f"empreinte différente pour {url} : {h}")
    os.replace(dest + ".part", dest)
    return dest


def besoins(elf):
    out = subprocess.run(["readelf", "-d", elf], capture_output=True, text=True, check=True).stdout
    return [l.split("[", 1)[1].rstrip("]") for l in out.splitlines() if "(NEEDED)" in l]


def trouver_lib(sr, nom):
    for d in ("usr/local/lib", f"usr/lib/{T}", f"lib/{T}"):
        p = os.path.join(sr, d, nom)
        if os.path.exists(p):
            return os.path.realpath(p)
    sys.exit(f"{nom} : absente de {sr}")


def fournir_libs(sr, exe, lib):
    os.makedirs(lib, exist_ok=True)
    a_faire = [n for n in besoins(exe) if n not in SYSTEME]
    while a_faire:
        n = a_faire.pop()
        if os.path.exists(os.path.join(lib, n)):
            continue
        src = trouver_lib(sr, n)
        shutil.copy2(src, os.path.join(lib, n))
        a_faire += [x for x in besoins(src) if x not in SYSTEME]
    # bibliothèques de secours (AppRun ne les prend que si le système n'en a pas) : lib-secours/, à côté de lib/
    secours = os.path.join(os.path.dirname(lib), "lib-secours")
    os.makedirs(secours, exist_ok=True)
    for n in SECOURS:
        if os.path.exists(os.path.join(lib, n)):
            shutil.move(os.path.join(lib, n), os.path.join(secours, n))
    dire("bibliothèques fournies : " + " ".join(sorted(os.listdir(lib))))
    dire("de secours : " + " ".join(sorted(os.listdir(secours))))


def icone_png(dest):
    """PNG 256 × 256 de l'icône du launcher (icone.ico : son image de 256 px y est déjà en PNG)."""
    data = open(os.path.join(LANCEUR, "icone.ico"), "rb").read()
    _, _, n = struct.unpack_from("<HHH", data, 0)
    meilleur = None
    for i in range(n):
        w, h, _, _, _, _, taille, off = struct.unpack_from("<BBBBHHII", data, 6 + 16 * i)
        img = data[off:off + taille]
        if img[:8] == b"\x89PNG\r\n\x1a\n" and (meilleur is None or (w or 256) > meilleur[0]):
            meilleur = (w or 256, img)
    if not meilleur:
        sys.exit("icone.ico : pas d'image PNG")
    open(dest, "wb").write(meilleur[1])


def polices(dest, cache):
    os.makedirs(dest, exist_ok=True)
    for f in POLICES:
        p = telecharger(_GF + f, POLICES_SHA256.get(f), os.path.join(cache, f))
        dire(f"  {f} : SHA-256 {paquet.empreinte(p)}")
        shutil.copy2(p, os.path.join(dest, f))


def extraits(src, dest, cache):
    """MP4 → MPEG-1 (pl_mpeg ne lit que lui) : sans son, 30 images par seconde (cadences du MPEG-1), 1280 px au plus.
    `src` : dossier des extrait_*.mp4, ou archive d'une release Windows (…-update.zip, …-windows.zip)."""
    os.makedirs(dest, exist_ok=True)
    if src.endswith(".zip"):
        import zipfile
        dossier = os.path.join(cache, "extraits_mp4")
        os.makedirs(dossier, exist_ok=True)
        with zipfile.ZipFile(src) as z:
            for m in z.namelist():
                b = os.path.basename(m)
                if "/launcher/extraits/" in m and b.startswith("extrait_") and b.endswith(".mp4"):
                    with z.open(m) as s, open(os.path.join(dossier, b), "wb") as d:
                        shutil.copyfileobj(s, d)
        src = dossier
    noms = sorted(f for f in os.listdir(src) if f.startswith("extrait_") and f.endswith(".mp4"))
    if not noms:
        sys.exit(f"aucun extrait_*.mp4 dans {src}")
    for f in noms:
        sortie = os.path.join(dest, f[:-4] + ".mpg")
        # FFMPEG : autre ffmpeg (sous WSL, un ffmpeg.exe de Windows : chemins passés par wslpath)
        ff = os.environ.get("FFMPEG", "ffmpeg")
        chemin = (lambda p: subprocess.run(["wslpath", "-w", p], capture_output=True, text=True, check=True).stdout.strip()) \
            if ff.lower().endswith(".exe") else (lambda p: p)
        subprocess.run([ff, "-loglevel", "error", "-y", "-i", chemin(os.path.join(src, f)), "-an", "-c:v", "mpeg1video",
                        "-q:v", "4", "-r", "30", "-vf", "scale='min(1280,iw)':-2", "-f", "mpeg", chemin(sortie)], check=True)
        dire(f"  {f} → {os.path.basename(sortie)} ({os.path.getsize(sortie) / 1e6:.1f} Mo)")


def sources(app):
    r = os.path.join(app, "recomp")
    for f in ("recomp.py", "build.py", "plateforme.py", "fullbuild.py", "link.py", "hooks.txt", "lots.txt", "VERSION"):
        paquet.copier(os.path.join(RECOMP, f), os.path.join(r, f))
    paquet.copier_arbre(os.path.join(RECOMP, "a64"), os.path.join(r, "a64"), (".py",))
    paquet.copier_arbre(os.path.join(RECOMP, "runtime"), os.path.join(r, "runtime"),
                        (".cpp", ".h", ".hpp", ".c", ".inl", ".comp", ".py", ".md", ".txt"))
    for f in os.listdir(os.path.join(RECOMP, "shaders")):
        if f.endswith(".py"):
            paquet.copier(os.path.join(RECOMP, "shaders", f), os.path.join(r, "shaders", f))
    # (pas de maj.py, desinstaller.py ni deplacer.py : l'AppImage se remplace, se supprime et se déplace d'un bloc)
    for f in ("preparer.py", "outils.py", "rapport.py", "sauvegardes.py"):
        paquet.copier(os.path.join(LANCEUR, f), os.path.join(r, "launcher", f))
    for f in ("nup.py", "nso.py"):
        paquet.copier(os.path.join(PROJET, "tools", f), os.path.join(app, "tools", f))
    licences = os.path.join(RECOMP, "licences")
    paquet.copier(os.path.join(licences, "THIRD_PARTY_NOTICES.txt"), os.path.join(app, "THIRD_PARTY_NOTICES.txt"))
    for f in sorted(os.listdir(licences)):
        if f.endswith(".txt") and f != "THIRD_PARTY_NOTICES.txt":
            paquet.copier(os.path.join(licences, f), os.path.join(app, "LICENCES", f))


def outils(dest, sr, cache):
    import plateforme
    subprocess.run([sys.executable, os.path.join(LANCEUR, "outils.py"), "--dest", dest, "--garder-archives"], check=True)
    # archives gardées dans le cache, pas dans l'AppImage
    arch = os.path.join(dest, "_archives")
    if os.path.isdir(arch):
        os.makedirs(os.path.join(cache, "archives"), exist_ok=True)
        for f in os.listdir(arch):
            shutil.move(os.path.join(arch, f), os.path.join(cache, "archives", f))
        os.rmdir(arch)
    # SDL3 : en-têtes (runtime/os) et bibliothèque (le jeu la trouve par son chemin relatif, link.py)
    sdl = os.path.join(dest, "sdl3")
    shutil.rmtree(sdl, ignore_errors=True)
    shutil.copytree(os.path.join(sr, "usr", "local", "include", "SDL3"), os.path.join(sdl, "include", "SDL3"))
    os.makedirs(os.path.join(sdl, "lib"))
    shutil.copy2(trouver_lib(sr, "libSDL3.so.0"), os.path.join(sdl, "lib", "libSDL3.so.0"))
    os.symlink("libSDL3.so.0", os.path.join(sdl, "lib", "libSDL3.so"))
    lic = os.path.join(sr, "usr", "local", "share", "doc", "SDL3", "LICENSE.txt")
    if os.path.exists(lic):
        shutil.copy2(lic, os.path.join(sdl, "LICENSE.txt"))
    # zig se lance par « c++ » (plateforme.zig_cxx) : le script est posé ici, l'image montée est en lecture seule
    if not plateforme.enveloppe_zig(os.path.join(dest, "zig", "zig")):
        sys.exit("script c++ de zig non posé")
    with open(os.path.join(dest, "LISEZMOI.txt"), "a", encoding="utf-8") as f:
        f.write("sdl3 : SDL 3 de la racine système de construction (Steam Runtime « sniper », sysroot_linux.py)\n  licence : zlib\n\n")


def controle(app):
    """Rien qui vienne du jeu (mêmes extensions interdites que paquet.py)."""
    interdits = (".nup", ".bin", ".o", ".spv", ".log", ".save", ".ini", ".cache", ".mp4")
    for racine, _, fichiers in os.walk(app):
        for f in fichiers:
            p = os.path.join(racine, f)
            if f.lower().endswith(interdits) and "/outils/" not in p:
                sys.exit(f"fichier interdit dans l'AppImage : {p}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sortie")
    ap.add_argument("--sysroot", required=True)
    ap.add_argument("--extraits", help="dossier des extrait_*.mp4, ou archive d'une release Windows (…-update.zip)")
    ap.add_argument("--version", default=open(os.path.join(RECOMP, "VERSION"), encoding="utf-8").read().strip())
    ap.add_argument("--garder", action="store_true", help="garder le dossier AppDir")
    a = ap.parse_args()
    sortie = os.path.abspath(a.sortie)
    sr = os.path.abspath(a.sysroot)
    cache = os.path.join(sortie, "cache")
    os.makedirs(cache, exist_ok=True)
    appdir = os.path.join(sortie, "AppDir")
    shutil.rmtree(appdir, ignore_errors=True)
    app = os.path.join(appdir, "app")
    os.makedirs(app)
    dire("launcher")
    r = subprocess.run([sys.executable, os.path.join(LANCEUR, "construire.py"), "--sysroot", sr,
                        "--out", os.path.join(app, "lanceur")])
    if r.returncode:
        sys.exit("construire.py a échoué")
    glibc = subprocess.run(["objdump", "-T", os.path.join(app, "lanceur")], capture_output=True, text=True).stdout
    vs = [tuple(map(int, w[len("(GLIBC_"):-1].split("."))) for w in glibc.split() if w.startswith("(GLIBC_2.")]
    if vs and max(vs) > (2, 31):
        sys.exit(f"le launcher demande la glibc {max(vs)} : racine système ?")
    dire("sources")
    sources(app)
    if a.extraits:
        dire("extraits")
        extraits(os.path.abspath(a.extraits), os.path.join(app, "recomp", "launcher", "extraits"), cache)
    else:
        dire("extraits : aucun (--extraits) : le fond du launcher sera le globe")
    dire("polices")
    polices(os.path.join(app, "polices"), cache)
    icone_png(os.path.join(app, "lanceur.png"))
    with open(os.path.join(app, ".paquet"), "w") as f:   # change à chaque AppImage : AppRun recopie app/
        f.write(f"{a.version} {paquet.empreinte(os.path.join(app, 'lanceur'))}\n")
    dire("outils")
    outils(os.path.join(appdir, "outils"), sr, cache)
    dire("bibliothèques")
    fournir_libs(sr, os.path.join(app, "lanceur"), os.path.join(appdir, "lib"))
    controle(app)
    with open(os.path.join(appdir, "AppRun"), "w") as f:
        f.write(APPRUN)
    os.chmod(os.path.join(appdir, "AppRun"), 0o755)
    with open(os.path.join(appdir, "spaceanimalsrecomp.desktop"), "w") as f:
        f.write(DESKTOP)
    shutil.copy2(os.path.join(app, "lanceur.png"), os.path.join(appdir, "spaceanimalsrecomp.png"))
    os.symlink("spaceanimalsrecomp.png", os.path.join(appdir, ".DirIcon"))
    outil = telecharger(*PINS["appimagetool"], os.path.join(cache, "appimagetool-x86_64.AppImage"))
    runtime = telecharger(*PINS["runtime"], os.path.join(cache, "runtime-x86_64"))
    os.chmod(outil, os.stat(outil).st_mode | stat.S_IEXEC)
    out = os.path.join(sortie, f"{NOM}-{a.version}-x86_64.AppImage")
    part = out + ".part"
    env = dict(os.environ, ARCH="x86_64", APPIMAGE_EXTRACT_AND_RUN="1")
    subprocess.run([outil, "--no-appstream", "--runtime-file", runtime, appdir, part], env=env, check=True)
    os.replace(part, out)
    if not a.garder:
        shutil.rmtree(appdir)
    dire(f"OK {out} ({os.path.getsize(out) / 1e6:.0f} Mo), SHA-256 {paquet.empreinte(out)}")


if __name__ == "__main__":
    main()

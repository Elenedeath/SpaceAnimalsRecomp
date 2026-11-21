#!/usr/bin/env python3
"""Paquet .deb (Debian, Ubuntu, Linux Mint, Pop!_OS…) : le contenu de l'AppImage, installé par le gestionnaire de paquets.
Comme l'AppImage et le paquet Windows, il ne contient rien qui vienne du jeu : le jeu est recompilé sur la machine du
joueur, depuis sa propre copie.

  python3 paquet_deb.py <dossier de sortie de paquet_linux.py> [--version 1.1.0]

À lancer sous Linux après « paquet_linux.py <sortie> --sysroot … --garder » (--garder laisse <sortie>/AppDir, repris tel
quel ici). Il faut dpkg-deb (présent sur toute distribution de la famille Debian).

Contenu du paquet :
  /opt/spaceanimalsrecomp/AppRun, app/, outils/   les mêmes fichiers que dans l'AppImage ; AppRun garde une copie de app/
                                                  dans ${XDG_DATA_HOME:-~/.local/share}/SpaceAnimalsRecomp/app et y relie
                                                  outils/, comme pour l'AppImage : le jeu construit, ses réglages et ses
                                                  caches sont dans le dossier personnel, /opt reste en lecture seule
  /usr/bin/spaceanimalsrecomp                     lance AppRun
  /usr/share/applications/spaceanimalsrecomp.desktop, /usr/share/icons/hicolor/256x256/apps/spaceanimalsrecomp.png
                                                  entrée de menu et icône (identifiant des fenêtres du launcher)
  /usr/share/doc/spaceanimalsrecomp/copyright     licences (renvoi vers /opt/spaceanimalsrecomp/app/LICENCES)

Désinstallation : « sudo apt remove spaceanimalsrecomp » retire le paquet ; le jeu construit et les sauvegardes restent
dans ~/.local/share/SpaceAnimalsRecomp (comme tout paquet, il ne touche pas au dossier personnel). Le bouton DÉSINSTALLER
du launcher n'est montré que pour l'AppImage (variable APPIMAGE).

Dépendances : celles que le launcher prend au système (paquet_linux.py, SYSTEME) et Vulkan pour le jeu. Compression xz :
un .deb en zstd (défaut d'Ubuntu) n'est pas lu par le dpkg de Debian 11.
"""
import argparse
import os
import shutil
import stat
import subprocess
import sys

LANCEUR = os.path.dirname(os.path.abspath(__file__))
RECOMP = os.path.dirname(LANCEUR)
sys.path.insert(0, LANCEUR)
import paquet  # noqa: E402  (empreinte)
import paquet_linux  # noqa: E402  (controle, DESKTOP)

PAQUET = "spaceanimalsrecomp"
OPT = "opt/" + PAQUET
# bibliothèques prises au système par le launcher (paquet_linux.SYSTEME) et par le jeu (libvulkan.so.1) ; glib : nom
# d'avant et d'après le passage au time_t de 64 bits (Ubuntu 24.04, Debian 13)
DEPENDANCES = ("libc6 (>= 2.31), libstdc++6, libgcc-s1, libcairo2, libpango-1.0-0, libpangocairo-1.0-0, "
               "libglib2.0-0t64 | libglib2.0-0, libfontconfig1, libvulkan1")
# ouverture des dossiers et des journaux (xdg-open), boîte de choix de la copie du jeu (SDL : portail du bureau ou zenity)
RECOMMANDES = "xdg-utils, zenity | kdialog"
CONTROLE = """Package: {paquet}
Version: {version}
Architecture: amd64
Maintainer: SpaceAnimalsRecomp project <noreply@users.noreply.github.com>
Installed-Size: {taille}
Depends: {dependances}
Recommends: {recommandes}
Section: games
Priority: optional
Homepage: https://github.com/SerenityG4K/SpaceAnimalsRecomp
Description: StarFox Remake [recompiled], built on your PC from your own game copy
 Launcher, recompiler and build tools of SpaceAnimalsRecomp, a native port of
 Star Fox (Switch 2). The package contains no game code and no game data: the
 launcher asks for your own copy of the game and recompiles it on this machine.
 .
 The built game, its settings and your saves are kept in
 ~/.local/share/SpaceAnimalsRecomp; removing the package leaves them in place.
"""
COPYRIGHT = """SpaceAnimalsRecomp is distributed under the GNU General Public License, version 3.
Source code: https://github.com/SerenityG4K/SpaceAnimalsRecomp

Third-party components shipped in this package (Python, zig and LLVM, glslang, SDL 3, libdecor, Barlow fonts,
NVIDIA DLSS library) keep their own licences. Their notices and licence texts are installed in:
  /opt/spaceanimalsrecomp/app/THIRD_PARTY_NOTICES.txt
  /opt/spaceanimalsrecomp/app/LICENCES/
  /opt/spaceanimalsrecomp/outils/LISEZMOI.txt
"""
LANCE = "#!/bin/sh\nexec /opt/spaceanimalsrecomp/AppRun \"$@\"\n"


def dire(m):
    print(m, flush=True)


def droits(racine):
    """Droits d'un paquet : dossiers 755 ; fichiers 755 s'ils sont exécutables, sinon 644 ; liens laissés."""
    for d, sous, fichiers in os.walk(racine):
        os.chmod(d, 0o755)
        for f in fichiers:
            p = os.path.join(d, f)
            if os.path.islink(p):
                continue
            os.chmod(p, 0o755 if os.stat(p).st_mode & stat.S_IXUSR else 0o644)


def taille_ko(racine):
    n = 0
    for d, _, fichiers in os.walk(racine):
        for f in fichiers:
            p = os.path.join(d, f)
            if not os.path.islink(p):
                n += os.path.getsize(p)
    return (n + 1023) // 1024


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sortie", help="dossier de sortie de paquet_linux.py (avec AppDir : option --garder)")
    ap.add_argument("--version", default=open(os.path.join(RECOMP, "VERSION"), encoding="utf-8").read().strip())
    a = ap.parse_args()
    if not sys.platform.startswith("linux"):
        sys.exit("à lancer sous Linux (ou WSL)")
    if not shutil.which("dpkg-deb"):
        sys.exit("dpkg-deb introuvable (paquet dpkg)")
    sortie = os.path.abspath(a.sortie)
    appdir = os.path.join(sortie, "AppDir")
    for f in ("AppRun", "app/lanceur", "app/.paquet", "outils/zig/zig", "spaceanimalsrecomp.png"):
        if not os.path.exists(os.path.join(appdir, f)):
            sys.exit(f"{os.path.join(appdir, f)} : absent (lancer d'abord paquet_linux.py avec --garder)")
    paquet_linux.controle(os.path.join(appdir, "app"))   # rien qui vienne du jeu
    racine = os.path.join(sortie, "deb")
    shutil.rmtree(racine, ignore_errors=True)
    opt = os.path.join(racine, OPT)
    os.makedirs(opt)
    dire("copie de l'AppDir dans /opt")
    for n in ("AppRun", "app", "outils", "spaceanimalsrecomp.png"):
        s = os.path.join(appdir, n)
        if os.path.isdir(s):
            shutil.copytree(s, os.path.join(opt, n), symlinks=True)
        else:
            shutil.copy2(s, os.path.join(opt, n))
    os.makedirs(os.path.join(racine, "usr", "bin"))
    with open(os.path.join(racine, "usr", "bin", PAQUET), "w") as f:
        f.write(LANCE)
    os.chmod(os.path.join(racine, "usr", "bin", PAQUET), 0o755)
    apps = os.path.join(racine, "usr", "share", "applications")
    os.makedirs(apps)
    with open(os.path.join(apps, PAQUET + ".desktop"), "w", encoding="utf-8") as f:
        f.write(paquet_linux.DESKTOP.replace("Exec=AppRun", "Exec=" + PAQUET))
    icones = os.path.join(racine, "usr", "share", "icons", "hicolor", "256x256", "apps")
    os.makedirs(icones)
    shutil.copy2(os.path.join(appdir, "spaceanimalsrecomp.png"), os.path.join(icones, PAQUET + ".png"))
    doc = os.path.join(racine, "usr", "share", "doc", PAQUET)
    os.makedirs(doc)
    with open(os.path.join(doc, "copyright"), "w", encoding="utf-8") as f:
        f.write(COPYRIGHT)
    droits(racine)
    os.chmod(os.path.join(opt, "AppRun"), 0o755)
    os.makedirs(os.path.join(racine, "DEBIAN"))
    with open(os.path.join(racine, "DEBIAN", "control"), "w", encoding="utf-8") as f:
        f.write(CONTROLE.format(paquet=PAQUET, version=a.version, taille=taille_ko(racine), dependances=DEPENDANCES,
                                recommandes=RECOMMANDES))
    os.chmod(os.path.join(racine, "DEBIAN"), 0o755)
    os.chmod(os.path.join(racine, "DEBIAN", "control"), 0o644)
    out = os.path.join(sortie, f"{PAQUET}_{a.version}_amd64.deb")
    dire("dpkg-deb (xz)")
    subprocess.run(["dpkg-deb", "--root-owner-group", "-Zxz", "--build", racine, out + ".part"], check=True)
    os.replace(out + ".part", out)
    shutil.rmtree(racine)
    dire(f"OK {out} ({os.path.getsize(out) / 1e6:.0f} Mo), SHA-256 {paquet.empreinte(out)}")


if __name__ == "__main__":
    main()

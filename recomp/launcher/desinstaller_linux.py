#!/usr/bin/env python3
"""Nettoyage et désinstallation de SpaceAnimalsRecomp sous Linux (AppImage) : tout ce que l'AppImage a écrit sur la
machine, en une commande. Même usage que desinstaller.py (Windows).

  python3 desinstaller_linux.py --lister                 ce qui existe sur cette machine, avec les tailles ; rien n'est supprimé
  python3 desinstaller_linux.py --nettoyer [--oui]       le jeu construit, le code généré, les images extraites et les
                                                         caches du compilateur : la place revient ; le launcher, vos
                                                         réglages et vos sauvegardes restent, le jeu se reconstruit
  python3 desinstaller_linux.py --desinstaller [--sauvegardes] [--oui] [--appimage A]
                                                         tout ; les sauvegardes seulement avec --sauvegardes

Options : --racine R (installation, par défaut celle de ce script : ~/.local/share/SpaceAnimalsRecomp/app), --appimage A
(le fichier AppImage, supprimé avec --desinstaller ; par défaut $APPIMAGE), --oui (sans question).

Jamais supprimés : votre copie du jeu (.nup, .nsp, .xci, .nsz, où qu'elle soit), et vos sauvegardes sans --sauvegardes.
Avant toute suppression, les sauvegardes sont copiées dans ~/.local/share/SpaceAnimalsRecomp/save_backups. Refus dans
un dossier de sources (dépôt cloné) : ce script ne supprime qu'une installation faite par l'AppImage.

Sous Linux, un fichier ouvert peut être supprimé : le launcher qui lance ce script n'a pas à être fermé avant. Il attend
la fin du script (l'image montée de l'AppImage, où est ce Python, disparaît avec lui), puis se ferme.

Ce que l'AppImage écrit sur une machine :
  <installation>/recomp/build                jeu construit, shaders, journaux, réglages (recomp.ini)
  <installation>/recomp/out                  code C++ généré depuis votre copie du jeu
  <installation>/extracted, /work            fichiers extraits de votre copie du jeu
  ${XDG_CACHE_HOME:-~/.cache}/spaceanimalsrecomp-zig   caches du compilateur (zig)
  <installation>                             le launcher et les sources (copiés depuis l'AppImage)
  l'AppImage                                 le fichier téléchargé
  Bureau/StarFox Remake [recompiled].desktop raccourci créé depuis le launcher, s'il mène à cette installation
  ~/.local/share/SpaceAnimalsRecomp          sauvegardes et copies de secours (à côté de l'installation)
Journal : ${TMPDIR:-/tmp}/SpaceAnimalsRecomp-uninstall.log (en anglais).
"""
import argparse
import glob
import os
import shutil
import stat
import sys
import tempfile
import time

LANCEUR = os.path.dirname(os.path.abspath(__file__))
COPIES_DU_JEU = (".nup", ".nsp", ".xci", ".nsz")
JOURNAL = os.path.join(tempfile.gettempdir(), "SpaceAnimalsRecomp-uninstall.log")
NOM_RACCOURCI = "StarFox Remake [recompiled].desktop"


def dire(texte):
    print(texte, flush=True)
    try:
        with open(JOURNAL, "a", encoding="utf-8") as f:
            f.write(time.strftime("%Y-%m-%d %H:%M:%S  ") + texte + "\n")
    except OSError:
        pass


def taille(p):
    if os.path.islink(p):
        return 0
    if os.path.isfile(p):
        return os.path.getsize(p)
    n = 0
    for d, _, fichiers in os.walk(p):   # (os.walk ne suit pas les liens : outils/ est dans l'image montée)
        for f in fichiers:
            try:
                n += os.lstat(os.path.join(d, f)).st_size
            except OSError:
                pass
    return n


def go(n):
    return f"{n / 2**30:.1f} GB" if n >= 2**30 else f"{n / 2**20:.0f} MB"


def donnees():
    """${XDG_DATA_HOME:-~/.local/share}/SpaceAnimalsRecomp : installation (app/), sauvegardes et copies de secours."""
    base = os.environ.get("XDG_DATA_HOME") or os.path.join(os.path.expanduser("~"), ".local", "share")
    return os.path.join(base, "SpaceAnimalsRecomp")


def caches():
    base = os.environ.get("XDG_CACHE_HOME") or os.path.join(os.path.expanduser("~"), ".cache")
    return [os.path.join(base, "spaceanimalsrecomp-zig")]   # (pas ~/.cache/zig : à l'utilisateur, plateforme.py)


def copies_du_jeu(racine):
    """Copies du jeu du joueur sous l'installation : jamais supprimées."""
    out = []
    for d, sous, fichiers in os.walk(racine):
        sous[:] = [s for s in sous if s.lower() not in ("outils", "out", "build", "shader_cache")]
        out += [os.path.join(d, f) for f in fichiers if f.lower().endswith(COPIES_DU_JEU)]
    return out


def bureau():
    if os.environ.get("LANCEUR_BUREAU"):
        return os.environ["LANCEUR_BUREAU"]
    home = os.path.expanduser("~")
    cfg = os.environ.get("XDG_CONFIG_HOME") or os.path.join(home, ".config")
    try:
        with open(os.path.join(cfg, "user-dirs.dirs"), encoding="utf-8") as f:
            for l in f:
                if l.startswith("XDG_DESKTOP_DIR="):
                    return l.split("=", 1)[1].strip().strip('"').replace("$HOME", home)
    except OSError:
        pass
    return os.path.join(home, "Desktop")


def raccourci_bureau(racine, appimage):
    """Raccourci du launcher (page Réglages), s'il mène à cette installation ou à cette AppImage."""
    p = os.path.join(bureau(), NOM_RACCOURCI)
    try:
        with open(p, encoding="utf-8", errors="replace") as f:
            exec_ = next((l[5:].strip().strip('"') for l in f if l.startswith("Exec=")), "")
    except OSError:
        return []
    cibles = {os.path.join(os.path.abspath(racine), "lanceur")}
    if appimage:
        cibles.add(os.path.abspath(appimage))
    return [p] if os.path.abspath(exec_) in cibles else []


def elements(racine, appimage):
    """(clé, description en anglais, chemins) de tout ce que l'AppImage a écrit ; les chemins absents sont retirés."""
    r = os.path.join(racine, "recomp")
    d = donnees()
    sauv = [os.path.join(d, x) for x in os.listdir(d)] if os.path.isdir(d) else []
    sauv = [p for p in sauv if os.path.abspath(p) != os.path.abspath(racine)]   # tout sauf l'installation (app/)
    liste = [
        ("jeu", "built game, shaders, logs", [os.path.join(r, "build")]),
        ("code", "C++ code generated from your game copy", [os.path.join(r, "out")]),
        ("extraits", "files extracted from your game copy", [os.path.join(racine, "extracted"), os.path.join(racine, "work")]),
        ("compilateur", "compiler caches", caches()),
        ("temporaires", "temporary files", glob.glob(os.path.join(glob.escape(tempfile.gettempdir()), "recomp_textures_*.tmp"))),
        ("paquet", "the launcher and the sources", [racine]),
        ("appimage", "the AppImage", [appimage] if appimage else []),
        ("raccourci", "desktop shortcut", raccourci_bureau(racine, appimage)),
        ("sauvegardes", "your saves and their backups", sauv),
    ]
    return [(k, desc, [p for p in ch if os.path.lexists(p)]) for k, desc, ch in liste]


def installation(racine):
    """None si `racine` est une installation faite par l'AppImage, sinon la raison du refus (en anglais)."""
    if not (os.path.exists(os.path.join(racine, "lanceur")) and os.path.exists(os.path.join(racine, "recomp", "fullbuild.py"))
            and os.path.exists(os.path.join(racine, ".paquet"))):
        return "this folder is not a SpaceAnimalsRecomp installation made by the AppImage"
    for marque in (".git", os.path.join("recomp", "launcher", "lanceur.cpp"), os.path.join("recomp", "verif"),
                   os.path.join("recomp", "depot")):
        if os.path.exists(os.path.join(racine, marque)):
            return f"this folder holds the project's sources ({marque}): delete it yourself if that is what you want"
    return None


def jeu_en_marche(racine):
    """Le jeu de cette installation tourne-t-il ? (/proc/*/exe)"""
    jeu = os.path.realpath(os.path.join(racine, "recomp", "build", "full", "recomp_main"))
    for pid in os.listdir("/proc"):
        if pid.isdigit():
            try:
                if os.path.realpath(os.readlink(f"/proc/{pid}/exe")).removesuffix(" (deleted)") == jeu:
                    return True
            except OSError:
                pass
    return False


def supprimer(chemin, garder=()):
    """Supprime un fichier, un lien ou un dossier (sans suivre les liens), sauf les fichiers de `garder` et les dossiers
    qui les contiennent. Rend la liste de ce qui n'a pas pu être supprimé."""
    garder = {os.path.abspath(g) for g in garder}
    restes = []

    def forcer(fonction, p, _):
        try:
            os.chmod(os.path.dirname(p), stat.S_IRWXU)
            fonction(p)
        except OSError:
            restes.append(p)

    a = os.path.abspath(chemin)
    if os.path.islink(chemin) or os.path.isfile(chemin):
        if a not in garder:
            try:
                os.remove(chemin)
            except OSError:
                restes.append(chemin)
        return restes
    if not any(g.startswith(a + os.sep) for g in garder):
        shutil.rmtree(chemin, onerror=forcer)
        return restes
    for nom in os.listdir(chemin):   # ce dossier contient une copie du jeu : il reste, vidé du reste
        restes += supprimer(os.path.join(chemin, nom), garder)
    return restes


def sauver_les_sauvegardes():
    try:
        sys.path.insert(0, LANCEUR)
        import sauvegardes
        sauvegardes.secours("desinstallation", dire)
    except Exception as ex:  # noqa: BLE001
        dire(f"saves: backup not made ({type(ex).__name__}: {ex})")


def question(texte):
    try:
        return input(texte + " [o/y = oui/yes, autre = non/no] ").strip().lower() in ("o", "oui", "y", "yes")
    except EOFError:
        return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--lister", action="store_true")
    g.add_argument("--nettoyer", action="store_true")
    g.add_argument("--desinstaller", action="store_true")
    ap.add_argument("--sauvegardes", action="store_true")
    ap.add_argument("--oui", action="store_true")
    ap.add_argument("--pid", type=int, default=0, help="(accepté comme desinstaller.py : rien à attendre sous Linux)")
    ap.add_argument("--racine", default=os.path.dirname(os.path.dirname(LANCEUR)))
    ap.add_argument("--appimage", default=os.environ.get("APPIMAGE") or "")
    a = ap.parse_args()
    racine = os.path.abspath(a.racine)
    appimage = os.path.abspath(a.appimage) if a.appimage and os.path.isfile(a.appimage) else ""
    el = elements(racine, appimage)
    copies = copies_du_jeu(racine)

    if a.lister:
        dire(f"SpaceAnimalsRecomp on this machine (installation: {racine})")
        for _, desc, chemins in el:
            for p in chemins:
                dire(f"  {desc}: {p} ({go(taille(p))})")
        for c in copies:
            dire(f"  your game copy, never deleted: {c}")
        return 0

    refus = installation(racine)
    if refus:
        dire("@erreur " + refus)
        return 2
    if jeu_en_marche(racine):
        dire("@erreur recomp_main is running: close the game first")
        return 2
    voulu = ("jeu", "code", "extraits", "compilateur", "temporaires") if a.nettoyer else \
        ("jeu", "code", "extraits", "compilateur", "temporaires", "paquet", "appimage", "raccourci") + \
        (("sauvegardes",) if a.sauvegardes else ())
    cibles = [(cle, desc, chemins) for cle, desc, chemins in el if cle in voulu and chemins]
    dire(("Cleaning" if a.nettoyer else "Uninstalling") + f" SpaceAnimalsRecomp ({racine})")
    for cle, desc, chemins in cibles:
        for p in chemins:
            dire(f"  to delete: {desc}: {p}" + ("" if cle == "paquet" else f" ({go(taille(p))})"))
    for c in copies:
        dire(f"  kept, your game copy: {c}")
    if not a.sauvegardes:
        dire(f"  kept, your saves: {donnees()}")
    if not a.oui:
        if not question("Supprimer ces fichiers ? / Delete these files?"):
            dire("nothing deleted")
            return 1
        if a.desinstaller and not a.sauvegardes and any(cle == "sauvegardes" and ch for cle, _, ch in el):
            if question("Supprimer aussi vos sauvegardes ? / Delete your saves too?"):
                cibles += [(cle, desc, ch) for cle, desc, ch in el if cle == "sauvegardes"]
                a.sauvegardes = True

    if not a.sauvegardes:
        sauver_les_sauvegardes()
    reglages = os.path.join(racine, "recomp", "build", "full", "recomp.ini")
    ini = open(reglages, "rb").read() if a.nettoyer and os.path.exists(reglages) else None
    restes = []
    for cle, desc, chemins in cibles:
        for p in chemins:
            r = supprimer(p, copies)
            restes += r
            dire(f"  deleted: {p}" + (f" (except {len(r)} file(s))" if r else ""))
    if ini is not None:   # nettoyage : les réglages du jeu restent
        os.makedirs(os.path.dirname(reglages), exist_ok=True)
        open(reglages, "wb").write(ini)
    if a.desinstaller:
        d = donnees()   # dossier de données vide (sauvegardes supprimées, ou aucune) : retiré aussi
        if os.path.isdir(d) and not os.listdir(d):
            os.rmdir(d)
        if restes:
            dire(f"  {len(restes)} file(s) could not be deleted, for example {restes[0]}")
        dire("@fini uninstalled" + (f"; your game copy is still in {os.path.dirname(copies[0])}" if copies else ""))
    else:
        dire("@fini cleaned: the launcher will rebuild the game")
    return 0


if __name__ == "__main__":
    sys.exit(main())

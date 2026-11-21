"""Différences entre Windows et Linux pour les scripts de construction (build.py, link.py, launcher/preparer.py…).

Sous Windows, rien ne change : ces fonctions rendent ce que les scripts utilisaient déjà. Sous Linux (portage Linux,
AppImage pour le Steam Deck), la chaîne de construction est zig (clang et la bibliothèque C++ de LLVM, avec les
en-têtes de la glibc) : un seul compilateur, fourni avec le paquet, compile le code généré, le runtime et fait
l'édition de liens. SDL3 (fenêtre, son, manettes) et glslang sont fournis aussi, dans outils/.
"""
import os
import sys

LINUX = sys.platform.startswith("linux")
EXE = "" if LINUX else ".exe"            # suffixe des exécutables (recomp_main, lanceur)
ICI = os.path.dirname(os.path.abspath(__file__))   # recomp/
RACINE = os.path.dirname(ICI)
# glibc visée par zig : celle du Steam Runtime « sniper » (Debian 11). Le jeu est construit sur le PC du joueur, mais
# une cible fixe rend la construction identique d'un PC à l'autre (mêmes empreintes d'objets) et l'exécutable
# démarre sur toute distribution au moins aussi récente.
CIBLE_LINUX = "x86_64-linux-gnu.2.31"


def dossiers_outils():
    """outils/ du paquet (à côté de recomp/), puis recomp/outils (arbre de développement)."""
    return [d for d in (os.path.join(RACINE, "outils"), os.path.join(ICI, "outils")) if os.path.isdir(d)]


def outil(*chemin):
    """Premier fichier outils/<chemin> trouvé, sinon None."""
    for d in dossiers_outils():
        p = os.path.join(d, *chemin)
        if os.path.exists(p):
            return p
    return None


def zig_cxx():
    """Compilateur C++ sous Linux : RECOMP_CXX, sinon le zig fourni (outils/zig), appelé par un petit script « c++ »
    (zig se lance en « zig c++ » : deux mots, alors que les scripts passent un seul chemin de compilateur), sinon zig
    ou clang++ du PATH."""
    choix = os.environ.get("RECOMP_CXX")
    if choix:
        return choix
    zig = outil("zig", "zig")
    if not zig:
        import shutil
        zig = shutil.which("zig")
        if not zig:
            return shutil.which("clang++") or "clang++"
    return enveloppe_zig(zig) or os.path.join(os.path.dirname(zig), "c++")


def enveloppe_zig(zig):
    """Pose (si besoin) le script « c++ » à côté de zig ; rend son chemin, None s'il n'a pas pu être écrit."""
    enveloppe = os.path.join(os.path.dirname(zig), "c++")
    # (zig nomme les processeurs à sa façon : « x86_64_v3 » pour le « x86-64-v3 » de clang et de GCC)
    # Une compilation (-c) a son propre cache de zig, sur le disque du dossier personnel : plusieurs zig compilant en
    # même temps dans le même cache échouaient au hasard (« unable to rename temporary … to output file », vu le
    # 2026-11-14 sur un tiers des lots). L'édition de liens garde le cache commun (libc++ compilée une fois).
    texte = ('#!/bin/sh\n'
             'for a do shift; case "$a" in -march=x86-64-v*) a="-march=x86_64_v${a#-march=x86-64-v}";; esac; set -- "$@" "$a"; done\n'
             'z="$(dirname "$0")/zig"\n'
             'case " $* " in *" -c "*)\n'
             '  b="${XDG_CACHE_HOME:-$HOME/.cache}/spaceanimalsrecomp-zig"; mkdir -p "$b"\n'
             '  c=$(mktemp -d "$b/c.XXXXXX") || exit 1\n'
             '  ZIG_GLOBAL_CACHE_DIR="$c" ZIG_LOCAL_CACHE_DIR="$c" "$z" c++ "$@"; r=$?; rm -rf "$c"; exit $r;;\n'
             'esac\n'
             'exec "$z" c++ "$@"\n')
    try:
        if not os.path.exists(enveloppe) or open(enveloppe).read() != texte:
            with open(enveloppe, "w") as f:
                f.write(texte)
            os.chmod(enveloppe, 0o755)
    except OSError:   # dossier en lecture seule (image montée) : le script y est déjà
        return enveloppe if os.path.exists(enveloppe) else None
    return enveloppe


def est_zig(cxx):
    return os.path.basename(str(cxx)) == "c++" and os.path.exists(os.path.join(os.path.dirname(str(cxx)), "zig"))


def cible_linux(cxx):
    """Drapeaux de cible du compilateur Linux (zig : glibc 2.31 ; clang du système : sa glibc)."""
    return ["-target", CIBLE_LINUX] if est_zig(cxx) else ["--target=x86_64-linux-gnu"]


def sdl3():
    """(dossier des en-têtes, dossier de libSDL3.so) fournis avec le paquet, ou (None, None)."""
    inc, lib = outil("sdl3", "include"), outil("sdl3", "lib")
    return (inc, lib) if inc and lib else (None, None)


def glslang():
    """glslang fourni (outils/glslang/bin), sinon None."""
    for nom in ("glslang" + EXE, "glslangValidator" + EXE):   # (Linux : glslangValidator est un lien vers glslang)
        p = outil("glslang", "bin", nom)
        if p:
            return p
    return None


def _meminfo():
    v = {}
    with open("/proc/meminfo") as f:
        for ligne in f:
            k, _, reste = ligne.partition(":")
            try:
                v[k] = int(reste.split()[0]) * 1024
            except (ValueError, IndexError):
                pass
    return v


def memoire_libre():
    """(Go de RAM disponibles, Go « validables » disponibles). Linux : MemAvailable, et MemAvailable + SwapFree (Linux
    surengage : la mémoire validable de Windows n'a pas d'équivalent strict). Lève OSError si illisible."""
    if os.environ.get("RECOMP_MEM_LIBRE_GO"):
        v = float(os.environ["RECOMP_MEM_LIBRE_GO"])
        return v, v
    if LINUX:
        m = _meminfo()
        ram = m.get("MemAvailable", 0)
        return ram / 1e9, (ram + m.get("SwapFree", 0)) / 1e9
    import ctypes

    class Etat(ctypes.Structure):
        _fields_ = [("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong), ("ullTotalPhys", ctypes.c_ulonglong),
                    ("ullAvailPhys", ctypes.c_ulonglong), ("ullTotalPageFile", ctypes.c_ulonglong),
                    ("ullAvailPageFile", ctypes.c_ulonglong), ("ullTotalVirtual", ctypes.c_ulonglong),
                    ("ullAvailVirtual", ctypes.c_ulonglong), ("ullAvailExtendedVirtual", ctypes.c_ulonglong)]
    e = Etat()
    e.dwLength = ctypes.sizeof(Etat)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(e)):
        raise OSError("GlobalMemoryStatusEx")
    return e.ullAvailPhys / 1e9, e.ullAvailPageFile / 1e9


def _enfants_linux(pid):
    """pids des enfants directs (/proc/<pid>/task/*/children ; sinon le ppid de chaque /proc/<n>/stat)."""
    enfants = []
    try:
        for t in os.listdir(f"/proc/{pid}/task"):
            with open(f"/proc/{pid}/task/{t}/children") as f:
                enfants += [int(x) for x in f.read().split()]
        return enfants
    except (OSError, ValueError):
        pass
    for n in os.listdir("/proc"):
        if n.isdigit():
            try:
                with open(f"/proc/{n}/stat") as f:
                    if int(f.read().rsplit(")", 1)[1].split()[1]) == pid:
                        enfants.append(int(n))
            except (OSError, ValueError, IndexError):
                pass
    return enfants


def memoire_processus_linux(pid):
    """Mémoire (Go) d'un processus Linux et de ses descendants : pour chacun, le plus grand de VmHWM (pic) et VmRSS,
    additionnés ; 0 si illisible. Les descendants comptent : une compilation est lancée par le script c++ (zig), qui
    passe la main à zig puis à clang, son enfant ; le seul processus lancé ne pèse que quelques Mo (« 0.00 GB each »
    jusqu'au 2026-11-15, et le garde-fou mémoire ne freinait jamais rien)."""
    total, vus, a_voir = 0, set(), [pid]
    while a_voir:
        p = a_voir.pop()
        if p in vus:
            continue
        vus.add(p)
        try:
            with open(f"/proc/{p}/status") as f:
                v = [int(l.split()[1]) * 1024 for l in f if l.startswith(("VmHWM:", "VmRSS:"))]
            total += max(v) if v else 0
        except (OSError, ValueError, IndexError):
            continue
        a_voir += _enfants_linux(p)
    return total / 1e9


def avx2():
    """Le processeur a-t-il l'AVX2 ? (Linux : /proc/cpuinfo ; Windows : IsProcessorFeaturePresent)"""
    if LINUX:
        try:
            with open("/proc/cpuinfo") as f:
                for ligne in f:
                    if ligne.startswith("flags"):
                        return " avx2" in ligne
        except OSError:
            return True
        return True
    import ctypes
    return bool(ctypes.windll.kernel32.IsProcessorFeaturePresent(40))   # PF_AVX2_INSTRUCTIONS_AVAILABLE


def dossier_donnees():
    """Dossier des données du joueur hors de l'installation : %APPDATA%\\SpaceAnimalsRecomp sous Windows,
    $XDG_DATA_HOME/SpaceAnimalsRecomp (~/.local/share/…) sous Linux, comme runtime/os/svc_fs.cpp. None si inconnu."""
    if LINUX:
        base = os.environ.get("XDG_DATA_HOME") or (os.path.join(os.environ["HOME"], ".local", "share")
                                                   if os.environ.get("HOME") else None)
    else:
        base = os.environ.get("APPDATA")
    return os.path.join(base, "SpaceAnimalsRecomp") if base else None

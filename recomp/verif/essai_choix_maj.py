#!/usr/bin/env python3
"""Les mises à jour laissent-elles le choix au joueur ? Essai du launcher dans un dossier jetable.

  python essai_choix_maj.py <lanceur.exe à essayer> <copie .nup> [--garder]

Le dossier jetable est un faux paquet : faux maj.py (annonce la version 9.9.9, n'installe rien), faux preparer.py (ne
construit rien), faux jeu « construit par la version 1.0.2 » alors que le paquet est en 1.0.3. Le launcher, lui, est le
vrai, sans crochet d'essai : ses questions s'ouvrent pour de bon et le script y répond.

Depuis la 1.2.0 (PR n° 57), les questions sont dessinées dans la fenêtre du launcher : plus de boîte de Windows à
chercher. Le script les voit par deux lignes du journal (« question shown in the window », « question answered: »,
lanceur.cpp, demander) et répond par des touches postées à la fenêtre : espace pour le bouton par défaut (« oui » pour
ces deux questions), Échap pour « non ». Jamais Entrée : hors d'une question, c'est le bouton principal. Laquelle des
deux questions était à l'écran se lit à ce que le launcher écrit après la réponse. Une boîte de Windows qui s'ouvrirait
quand même (seconde question par-dessus la première) est comptée comme un échec.

  A  nouvelle version du launcher, jeu à refaire, réponse « non » : rien ne démarre, la question n'est pas reposée,
     la reconstruction reste proposée (elle n'est jamais imposée : le jeu actuel reste jouable)
  B  même situation, réponse « oui » : la mise à jour du jeu démarre
  C  mise à jour disponible, réponse « non » : rien n'est installé, refus retenu, la question n'est pas reposée
  D  mise à jour disponible, réponse « oui » : l'installation démarre
  E  les deux à la fois : une seule question à l'écran, l'autre attend son tour

La copie du jeu n'est lue que par le launcher (en-tête et index). Refus si un launcher ou le jeu est ouvert.
"""
import ctypes
import os
import shutil
import subprocess
import sys
import tempfile
import time
from ctypes import wintypes

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

FAUX_MAJ = '''import os
import sys

racine = sys.argv[sys.argv.index("--racine") + 1] if "--racine" in sys.argv else os.getcwd()
if "--verifier" in sys.argv:
    print("@maj 9.9.9")
elif "--installer" in sys.argv:
    with open(os.path.join(racine, "essai_installer.txt"), "a") as f:
        f.write("installer\\n")
    print("essai : rien n'est installe")
    sys.exit(1)
'''
FAUX_PREPARER = '''import os
import sys

racine = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if "--a-refaire" in sys.argv:
    print("@a_refaire lier")
else:
    with open(os.path.join(racine, "essai_construction.txt"), "a") as f:
        f.write("construction\\n")
'''

u = ctypes.WinDLL("user32", use_last_error=True)
ENUM = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
u.EnumWindows.argtypes = [ENUM, wintypes.LPARAM]
u.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
u.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
u.IsWindowVisible.argtypes = [wintypes.HWND]
u.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
WM_CLOSE, WM_KEYDOWN, WM_KEYUP, WM_MOUSELEAVE = 0x0010, 0x0100, 0x0101, 0x02A3
VK_ECHAP, VK_ESPACE = 0x1B, 0x20
POSEE, REPONDUE = "question shown in the window", "question answered: "   # lignes du journal (lanceur.cpp, demander)


def classe(h):
    b = ctypes.create_unicode_buffer(128)
    u.GetClassNameW(h, b, 128)
    return b.value


def fenetres(pid):
    """Fenêtres visibles de premier niveau du processus : (poignée, classe)."""
    vues = []

    def une(h, _):
        p = wintypes.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and u.IsWindowVisible(h):
            vues.append((h, classe(h)))
        return True

    u.EnumWindows(ENUM(une), 0)
    return vues


def dialogues(pid):
    """Boîtes de Windows du processus : il ne doit plus y en avoir (questions dessinées dans la fenêtre)."""
    return [h for h, c in fenetres(pid) if c == "#32770"]


def fenetre_principale(pid):
    return next((h for h, c in fenetres(pid) if c != "#32770"), None)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) < 2:
        sys.exit(__doc__)
    exe, copie = os.path.abspath(args[0]), os.path.abspath(args[1])
    taches = subprocess.run(["tasklist"], capture_output=True, text=True, errors="replace").stdout.lower()
    if "lanceur" in taches or "recomp_main.exe" in taches:
        sys.exit("refus : un launcher ou le jeu est ouvert")
    travail = tempfile.mkdtemp(prefix="sar_choix_maj_")
    racine = os.path.join(travail, "paquet")
    jeu = os.path.join(racine, "recomp", "build", "full")
    os.makedirs(os.path.join(racine, "recomp", "launcher"))
    os.makedirs(os.path.join(jeu, "shader_cache"))
    os.makedirs(os.path.join(jeu, ".preparation"))
    # le launcher essayé, sous un autre nom : ce script ne voit et ne ferme jamais que le sien
    lanceur = os.path.join(racine, "lanceur_essai.exe")
    shutil.copy2(exe, lanceur)
    for chemin, contenu in ((("recomp", "fullbuild.py"), "# faux paquet\n"), (("recomp", "VERSION"), "1.0.3\n"),
                            (("recomp", "launcher", "maj.py"), FAUX_MAJ), (("recomp", "launcher", "preparer.py"), FAUX_PREPARER),
                            (("recomp", "build", "full", "recomp_main.exe"), ""), (("recomp", "build", "full", "manifest.txt"), ""),
                            (("recomp", "build", "full", ".preparation", "version.ok"), "1.0.2\n")):
        with open(os.path.join(racine, *chemin), "w", encoding="utf-8", newline="\n") as f:
            f.write(contenu)
    for i in range(1001):   # le launcher compte plus de 1 000 shaders pour dire le jeu construit
        open(os.path.join(jeu, "shader_cache", f"{i:04d}.spv"), "wb").close()
    ini, journal = os.path.join(racine, "lanceur.ini"), os.path.join(racine, "lanceur.log")
    m_cons, m_inst = os.path.join(racine, "essai_construction.txt"), os.path.join(racine, "essai_installer.txt")
    env = {k: v for k, v in os.environ.items() if not k.startswith("LANCEUR_")}
    for nom in ("APPDATA", "LOCALAPPDATA", "ProgramData"):   # jamais le vrai profil
        env[nom] = os.path.join(travail, nom)
        os.makedirs(env[nom])
    bilan = []

    def note(nom, ok, detail=""):
        bilan.append(ok)
        print(f"  {'ok ' if ok else 'NON'}  {nom}{' : ' + detail if detail else ''}")

    def ecrire_ini(**cles):
        with open(ini, "w", encoding="utf-16") as f:   # UTF-16 avec BOM, comme le launcher
            f.write("[lanceur]\n" + f"copie={copie}\n" + "".join(f"{k}={v}\n" for k, v in cles.items()))

    def lire(chemin, codage="utf-8"):
        return open(chemin, encoding=codage, errors="replace").read() if os.path.exists(chemin) else ""

    def ouvrir(titre):
        print(f"--- {titre}")
        for f in (journal, m_cons, m_inst):
            if os.path.exists(f):
                os.remove(f)
        return subprocess.Popen([lanceur], cwd=racine, env=env)

    def fermer(p):
        for h, c in fenetres(p.pid):
            if c != "#32770":
                u.PostMessageW(h, WM_CLOSE, 0, 0)
        try:
            p.wait(10)
        except subprocess.TimeoutExpired:
            p.kill()   # le launcher de cet essai, jamais un autre
            note("fermeture du launcher", False, "il a fallu l'arrêter")

    def questions():
        """(posées, répondues) : questions dessinées dans la fenêtre depuis l'ouverture, d'après le journal."""
        t = lire(journal)
        return t.count(POSEE), t.count(REPONDUE)

    def attendre_question(delai, deja=0):
        """Attend une question à l'écran (posée, pas encore répondue), la `deja` + 1ᵉ depuis l'ouverture."""
        fin = time.time() + delai
        while time.time() < fin:
            posees, repondues = questions()
            if posees > deja and posees == repondues + 1:
                return True
            time.sleep(0.2)
        return False

    def repondre(p, oui):
        """Répond à la question à l'écran, au clavier ; rend la réponse que le launcher a notée (« yes », « no », « »)."""
        h = fenetre_principale(p.pid)
        avant = questions()[1]
        if not h:
            return ""
        if oui:   # la souris du joueur, restée au-dessus de « non », le sélectionnerait : survol oublié d'abord
            u.PostMessageW(h, WM_MOUSELEAVE, 0, 0)
        touche = VK_ESPACE if oui else VK_ECHAP
        u.PostMessageW(h, WM_KEYDOWN, touche, 1)
        u.PostMessageW(h, WM_KEYUP, touche, 0xC0000001)
        fin = time.time() + 5
        while time.time() < fin and questions()[1] == avant:
            time.sleep(0.1)
        recues = [l.split(REPONDUE, 1)[1].strip() for l in lire(journal).splitlines() if REPONDUE in l]
        return recues[-1] if len(recues) > avant else ""

    def poser_et_repondre(p, delai, oui, deja=0):
        """Une question doit s'afficher, et recevoir la réponse voulue. Rend vrai si les deux."""
        vue = attendre_question(delai, deja)
        note("une question s'affiche dans la fenêtre", vue)
        recue = repondre(p, oui) if vue else ""
        voulue = "yes" if oui else "no"
        note(f"elle reçoit « {'oui' if oui else 'non'} »", recue == voulue, "" if recue == voulue else f"réponse notée : {recue or 'aucune'}")
        return vue and recue == voulue

    try:
        # A : nouvelle version du launcher, jeu à refaire, « non »
        ecrire_ini(maj_auto=0)
        p = ouvrir("A  jeu à refaire après une nouvelle version, réponse « non »")
        poser_et_repondre(p, 25, False)
        time.sleep(3)
        note("c'était la question sur le jeu : refus dans le journal", "game update postponed by the player" in lire(journal))
        note("rien ne démarre", not os.path.exists(m_cons) and "build:" not in lire(journal))
        note("version notée comme vue", "version_vue=1.0.3" in lire(ini, "utf-16"))
        note("pas d'autre question", questions() == (1, 1) and not dialogues(p.pid))
        fermer(p)
        p = ouvrir("A' réouverture")
        note("la question n'est pas reposée", not attendre_question(7) and not dialogues(p.pid))
        note("la reconstruction reste proposée", "rebuild offered" in lire(journal))
        note("rien ne démarre", not os.path.exists(m_cons))
        fermer(p)

        # B : même situation, « oui »
        ecrire_ini(maj_auto=0)
        p = ouvrir("B  jeu à refaire après une nouvelle version, réponse « oui »")
        poser_et_repondre(p, 25, True)
        fin = time.time() + 20
        while time.time() < fin and not os.path.exists(m_cons):
            time.sleep(0.3)
        note("c'était la question sur le jeu : accord dans le journal", "game update accepted by the player" in lire(journal))
        note("la mise à jour du jeu démarre", os.path.exists(m_cons))
        time.sleep(2)
        fermer(p)

        # C : mise à jour disponible, « non »
        ecrire_ini(maj_auto=1, version_vue="1.0.3")
        p = ouvrir("C  mise à jour 9.9.9 disponible, réponse « non »")
        poser_et_repondre(p, 30, False)
        time.sleep(3)
        note("c'était la question sur la nouvelle version : refus dans le journal", "update 9.9.9 declined" in lire(journal))
        note("rien n'est installé", not os.path.exists(m_inst) and "update: installing" not in lire(journal))
        note("refus retenu pour cette version", "maj_refusee=9.9.9" in lire(ini, "utf-16"))
        fermer(p)
        p = ouvrir("C' réouverture")
        note("la question n'est pas reposée", not attendre_question(9) and not dialogues(p.pid))
        note("la version reste proposée (bandeau)", "update available: 9.9.9" in lire(journal))
        note("rien n'est installé", not os.path.exists(m_inst))
        fermer(p)

        # D : mise à jour disponible, « oui »
        ecrire_ini(maj_auto=1, version_vue="1.0.3")
        p = ouvrir("D  mise à jour 9.9.9 disponible, réponse « oui »")
        poser_et_repondre(p, 30, True)
        fin = time.time() + 20
        while time.time() < fin and not os.path.exists(m_inst):
            time.sleep(0.3)
        note("c'était la question sur la nouvelle version : l'installation démarre",
             os.path.exists(m_inst) and "update: installing 9.9.9" in lire(journal))
        time.sleep(2)
        fermer(p)

        # E : les deux à la fois
        ecrire_ini(maj_auto=1)
        p = ouvrir("E  jeu à refaire et mise à jour disponible en même temps")
        vue = attendre_question(25)
        note("une première question s'affiche dans la fenêtre", vue)
        fin = time.time() + 20
        while time.time() < fin and "update available" not in lire(journal):
            time.sleep(0.3)
        time.sleep(4)   # la seconde question aurait eu le temps de s'ouvrir par-dessus
        posees, repondues = questions()
        note("une seule question à l'écran", posees - repondues == 1 and not dialogues(p.pid),
             f"{posees - repondues} dans la fenêtre, {len(dialogues(p.pid))} boîte(s) de Windows")
        recue = repondre(p, False) if vue else ""
        time.sleep(1)
        note("d'abord la question sur le jeu", recue == "no" and "game update postponed by the player" in lire(journal)
             and "declined" not in lire(journal))
        poser_et_repondre(p, 10, False, deja=1)
        time.sleep(2)
        note("puis la question sur la nouvelle version", "update 9.9.9 declined" in lire(journal))
        note("rien ne démarre, rien n'est installé", not os.path.exists(m_cons) and not os.path.exists(m_inst))
        fermer(p)
    finally:
        if "--garder" in sys.argv:
            print("dossier gardé :", travail)
        else:
            time.sleep(1)
            shutil.rmtree(travail, ignore_errors=True)
    print(f"{sum(bilan)} contrôles sur {len(bilan)} passés")
    sys.exit(0 if all(bilan) else 1)


if __name__ == "__main__":
    main()

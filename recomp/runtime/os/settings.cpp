// Réglages de l'exécutable (voir settings.h) : recomp.ini, variables RECOMP_*, ligne de commande.
#include "settings.h"

#include "hos.h"

#ifdef _WIN32
#include <windows.h>
#else
#include "plat_linux.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

namespace hos {
namespace {

Settings g_settings;
std::string g_file_label = "recomp.ini";
const char* g_src[3] = {"default", "default", "default"};   // fréquence, résolution, rendu interne
bool g_echelle_donnee = false;   // render_scale lu quelque part (sinon native_resolution = false vaut 67 %)

// Contenu du fichier créé au premier lancement (le launcher pourra le réécrire).
const char* const kTemplate = R"(; Réglages de recomp_main.exe, lus à chaque lancement.
; Ce fichier est prévu pour être écrit par le launcher ; il peut aussi être modifié à la main.
; Priorité : ce fichier < variables d'environnement RECOMP_* < options de la ligne de commande
; (--refresh, --resolution, --native-res, --config <fichier>).

[display]
; Fréquence d'images en Hz : 60, 90 ou 120 (30 à 240 acceptés, autres valeurs non testées).
; Le jeu se cale sur un écran virtuel à cette fréquence ; la simulation n'est pas accélérée.
refresh_rate = 60

; Résolution de sortie : auto (d'après l'écran principal), 720, 1080, 1440 ou 2160.
resolution = auto

; Rendu interne à la résolution de sortie (true),
; ou à 50 % puis agrandi comme sur la console (false).
native_resolution = true

; Échelle du rendu 3D, en % de la résolution de sortie : 100 (natif), 85, 75 ou 67. En dessous de 100 (mise à
; l'échelle expérimentale), le jeu rend plus petit puis agrandit l'image : plus rapide, un peu moins net (utile sur
; les petites cartes graphiques).
; 3D rendering scale, in % of the output resolution: 100 (native), 85, 75 or 67. Below 100 (experimental upscaling),
; the game renders smaller then enlarges the image: faster, slightly less sharp (useful on small graphics cards).
render_scale = 100

; Fenêtre : borderless (plein écran sans bordure, image pixel pour pixel) ou windowed (fenêtré).
; Alt+Entrée bascule pendant le jeu.
window_mode = borderless

[game]
; Langue du jeu : auto (celle de Windows), fr, en-US, en-GB, de, es, es-419, it, nl, pt, pt-BR, ja, ko,
; zh-Hans ou zh-Hant (codes de la console ; le jeu prend la plus proche de celles qu'il contient).
language = auto

[input]
; Vibrations de la manette, en % : 0 (aucune) à 100. Manettes Xbox, PlayStation 4 et 5, Pro de la Switch
; (SDL3), sinon Xbox seulement (XInput).
vibration = 100

)";

// Réglages apparus en 1.0.3 : à la suite du modèle, et ajoutés à la fin d'un recomp.ini d'une version précédente
// (load_file), pour que le joueur les trouve.
const char* const kSectionMemoire = R"([memory]
; Économie de mémoire : true (la mémoire du jeu est prise au fur et à mesure) ou false (comme jusqu'à la
; version 1.0.2 : tout est pris d'avance, plusieurs Go de plus). Mettre false si le jeu se comporte moins bien
; depuis la version 1.0.3 (images manquantes, arrêts).
; Memory saver: true (game memory is taken as needed) or false (as up to version 1.0.2: everything is taken
; up front, several GB more). Set it to false if the game misbehaves since version 1.0.3.
memory_saver = true
)";
const char* const kCleTextures = R"(
; Données des textures rendues à Windows une fois envoyées à la carte graphique : true ou false. Elles sont gardées
; dans un fichier temporaire (quelques Go dans le dossier temporaire de Windows, supprimé à la fermeture du jeu) et
; remises en place si le jeu les relit. Sans effet si memory_saver = false.
; Texture data given back to Windows once sent to the graphics card: true or false. It is kept in a temporary file
; (a few GB in the Windows temp folder, deleted when the game closes) and put back if the game reads it again.
; No effect if memory_saver = false.
texture_release = true
)";
const char* const kCleShaders = R"(
; Shaders préparés à leur premier usage (true), ou tous au démarrage du jeu comme jusqu'à la version 1.0.2 (false).
; Sans effet si memory_saver = false.
; Shaders prepared at first use (true), or all of them at game start as up to version 1.0.2 (false).
; No effect if memory_saver = false.
shaders_on_demand = true
)";
const char* const kCleVides = R"(
; Pages vides de la mémoire du jeu (que des zéros) rendues à Windows pendant la partie : true ou false. Le jeu les
; retrouve à zéro s'il y revient. Sans effet si memory_saver = false.
; Empty pages of game memory (only zeros) given back to Windows during the game: true or false. The game finds them
; as zeros if it comes back to them. No effect if memory_saver = false.
zero_page_release = true
)";
const char* const kCleFroides = R"(
; Pages pleines de la mémoire du jeu que le jeu n'a pas touchées depuis plusieurs minutes, rangées dans le fichier
; temporaire des textures : true ou false. Le jeu les retrouve telles quelles s'il y revient (courte attente). Sans
; effet si memory_saver, texture_release ou zero_page_release vaut false.
; Full pages of game memory the game has not touched for several minutes, put in the temporary file of the
; textures: true or false. The game finds them unchanged if it comes back to them (short wait). No effect if
; memory_saver, texture_release or zero_page_release is false.
cold_page_release = true
)";

const char* const kCleDlss = R"(
; DLSS de NVIDIA à la place de l'anticrénelage du jeu : off, dlaa (rendu natif), ou, en mise à l'échelle
; expérimentale, quality (rendu 3D à 67 %), balanced (58 %), performance (50 %) ou ultra_performance (33 %) : le DLSS
; agrandit alors l'image et render_scale ne sert pas. Cartes NVIDIA RTX seulement (voir gpu), avec nvngx_dlss.dll à
; côté de recomp_main.exe ; sinon le jeu garde son anticrénelage. dlss_model : dlss4 (modèle K) ou dlss45 (modèle M,
; plus fin, plus coûteux).
; NVIDIA DLSS instead of the game's anti-aliasing: off, dlaa (native rendering), or, as experimental upscaling,
; quality (3D rendering at 67%), balanced (58%), performance (50%) or ultra_performance (33%): DLSS then enlarges the
; image and render_scale is not used. NVIDIA RTX cards only (see gpu), with nvngx_dlss.dll next to recomp_main.exe;
; otherwise the game keeps its anti-aliasing. dlss_model: dlss4 (model K) or dlss45 (model M, sharper, costlier).
dlss = off
dlss_model = dlss4
)";

// Apparu après la 1.0.3 (optimisations du rendu du 2026-11-12)
const char* const kCleRendu = R"(
; Optimisations du rendu (dessins plus rapides à préparer, travail envoyé au GPU par morceaux) : true ou false. À
; mettre sur false seulement si un défaut d'image ou un plantage est apparu après une mise à jour : le jeu dessine
; alors comme la version 1.0.3, un peu moins vite dans les scènes chargées.
; Rendering optimizations (draws faster to prepare, work sent to the GPU in parts): true or false. Set it to false
; only if a picture defect or a crash appeared after an update: the game then draws like version 1.0.3, a little
; slower in heavy scenes.
fast_rendering = true
)";

// Apparu en 1.2.0 (cadence régulière, 2026-11-19, issue n° 41)
const char* const kCleCadence = R"(
; Cadence régulière : true ou false. true : chaque image est montrée à heure fixe (toutes les 16,7 ms à 60 Hz), comme
; sur la console ; le mouvement est régulier, surtout sur un écran à 120 Hz et plus. false : chaque image est montrée
; dès qu'elle est prête (quelques millisecondes plus tôt, mais à intervalles irréguliers), comme jusqu'à la 1.1.0.
; Regular frame pacing: true or false. true: each frame is shown at a fixed instant (every 16.7 ms at 60 Hz), like
; on the console; motion is even, above all on a 120 Hz or faster screen. false: each frame is shown as soon as it
; is ready (a few milliseconds earlier, but at uneven intervals), as up to version 1.1.0.
frame_pacing = true
)";

// Apparu en 1.2.0 (PR 51 : mode souris du jeu, hle_kbm.cpp)
const char* const kCleSouris = R"(
; Souris et clavier : true ou false. La manette émulée devient une paire de Joy-Con 2 et la souris du PC le Joy-Con 2
; droit posé comme une souris : le jeu passe dans son mode souris (sensibilité réglable dans ses options). Le curseur
; de Windows est retenu dans l'image du jeu (en fenêtré, après un clic dedans) ; mouse_release (F9 par défaut, 0x43)
; ou Alt+Tab le rendent. Le clavier pilote la manette comme d'habitude (touches key_...). Réglages facultatifs, page
; Contrôles du launcher, onglet Souris : mouse_speed, vitesse de la souris en % (10 à 1000, 100 par défaut) ;
; mouse_left (r par défaut), mouse_right (zr), mouse_middle (rstick), mouse_back, mouse_forward, mouse_wheel_up,
; mouse_wheel_down : action de la manette de chaque entrée de la souris (a, b, x, y, l, r, zl, zr, plus, minus, lstick,
; rstick, up, down, left, right, ls_up, ls_down, ls_left, ls_right, rs_up, rs_down, rs_left, rs_right, left_sl,
; left_sr, right_sl, right_sr, ou none).
; Mouse and keyboard: true or false. The emulated controller becomes a pair of Joy-Con 2 and the PC mouse the right
; Joy-Con 2 laid down as a mouse: the game enters its mouse mode (sensitivity set in its options). The Windows cursor is
; held in the game's picture (windowed: after a click in it); mouse_release (F9 by default, 0x43) or Alt+Tab releases
; it. The keyboard drives the controller as usual (key_... keys). Optional settings, launcher's Controls page, Mouse
; tab: mouse_speed, mouse speed in % (10 to 1000, 100 by default); mouse_left (r by default), mouse_right (zr),
; mouse_middle (rstick), mouse_back, mouse_forward, mouse_wheel_up, mouse_wheel_down: controller action of each mouse
; input (a, b, x, y, l, r, zl, zr, plus, minus, lstick, rstick, up, down, left, right, ls_up, ls_down, ls_left,
; ls_right, rs_up, rs_down, rs_left, rs_right, left_sl, left_sr, right_sl, right_sr, or none).
mouse_keyboard = false
)";

// Apparu après la 1.0.3 (FSR d'AMD, 2026-11-12)
const char* const kCleFsr = R"(
; FSR d'AMD à la place de l'anticrénelage du jeu, sur toute carte graphique (AMD, NVIDIA, Intel) : off, native (rendu
; natif), ou, en mise à l'échelle expérimentale, quality (rendu 3D à 67 %), balanced (59 %), performance (50 %) ou
; ultra_performance (33 %) : le FSR agrandit alors l'image et render_scale ne sert pas. fsr_version : auto (la version
; la plus récente que la carte accepte : FSR 4 sur les cartes AMD qui le proposent, sinon FSR 3.1), fsr3 ou fsr4.
; Si le DLSS est demandé aussi et que la carte est une NVIDIA RTX, c'est le DLSS qui sert. Il faut les fichiers d'AMD
; (amd_fidelityfx_loader_vk.dll et amd_fidelityfx_upscaler_vk.dll) dans outils\fsr ; sinon le jeu garde son
; anticrénelage. fsr_sharpness : netteté ajoutée par le FSR, de 0 (aucune) à 100.
; AMD FSR instead of the game's anti-aliasing, on any graphics card (AMD, NVIDIA, Intel): off, native (native
; rendering), or, as experimental upscaling, quality (3D rendering at 67%), balanced (59%), performance (50%) or
; ultra_performance (33%): FSR then enlarges the image and render_scale is not used. fsr_version: auto (the newest
; version the card accepts: FSR 4 on the AMD cards that offer it, FSR 3.1 otherwise), fsr3 or fsr4. If DLSS is asked
; too and the card is an NVIDIA RTX, DLSS is used. AMD's files (amd_fidelityfx_loader_vk.dll and
; amd_fidelityfx_upscaler_vk.dll) are needed in outils\fsr; otherwise the game keeps its anti-aliasing.
; fsr_sharpness: sharpening added by FSR, from 0 (none) to 100.
fsr = off
fsr_version = auto
fsr_sharpness = 0
)";

// Apparu après la 1.0.3 (choix de la carte graphique dans le launcher)
const char* const kCleGpu = R"(
; Carte graphique : auto (la carte dédiée qui a le plus de mémoire vidéo), ou le nom de la carte voulue (choisie dans
; la page Graphismes du launcher ; un morceau du nom suffit, AMD par exemple). Le DLSS ne sert que sur une carte
; NVIDIA RTX. Changer de carte refait la préparation des shaders au lancement suivant.
; Graphics card: auto (the dedicated card with the most video memory), or the name of the wanted card (chosen in the
; launcher's Graphics page; part of the name is enough, AMD for example). DLSS only works on an NVIDIA RTX card.
; Changing the card redoes the shader preparation at the next launch.
gpu = auto
)";

// Apparu en 1.2.0 (PR 54 : transit des textures et mémoire vidéo)
const char* const kClePerf = R"(
[performance]
; Transit des textures vers la carte graphique et mémoire vidéo. Les valeurs par défaut conviennent à presque tous les
; PC ; les baisser aide un PC qui manque de mémoire.
; staging_arena_mb : tampon de transit préparé d'avance pour les chargements (auto = 512 Mo, off = aucun, ou n Mo) ;
; créé seulement au-delà de 5 Go de mémoire libre. staging_pool_mb : réserve de tampons de transit gardés entre deux
; chargements, au-delà de 5 Go libres (128 = taille jusqu'à la 1.1.0). deswizzle_threads : fils qui préparent les
; textures (auto = la moitié des processeurs, 8 au plus). vertex_buffers_vram : essai, sommets lus depuis la mémoire
; vidéo (plus rapide dans les scènes chargées, Windows seulement), avec vertex_buffers_vram_mb Mo au plus.
; Texture transfer to the graphics card and video memory. The defaults suit almost every PC; lowering them helps a PC
; short of memory.
; staging_arena_mb: staging buffer prepared in advance for loads (auto = 512 MB, off = none, or n MB); only created
; with more than 5 GB of free memory. staging_pool_mb: staging buffers kept between loads, above 5 GB free (128 = size
; up to 1.1.0). deswizzle_threads: threads that prepare textures (auto = half of the processors, 8 at most).
; vertex_buffers_vram: test, vertices read from video memory (faster in heavy scenes, Windows only), with
; vertex_buffers_vram_mb MB at most.
staging_arena_mb = auto
staging_pool_mb = 512
deswizzle_threads = auto
vertex_buffers_vram = false
vertex_buffers_vram_mb = 2048
)";

// (kNomsActions, PAD_ENTREES, kLanguages : dans settings.h depuis le menu en jeu, partagés avec settings_ecrire.cpp)

// Apparu en 1.2.0 (PR 48 : menu en jeu, F1)
const char* const kCleMenu = R"(
[menu]
; Menu en jeu (F1, ou Inser ; à la manette : bouton central, ou Back et Start ensemble) : il change ces réglages
; pendant la partie. show_fps : compteur d'images par seconde dans le coin de l'image, true ou false. menu_size :
; taille du menu, small, medium ou large.
; In-game menu (F1, or Insert; on a controller: the centre button, or Back and Start together): it changes these
; settings while playing. show_fps: frames-per-second counter in the corner of the picture, true or false. menu_size:
; size of the menu, small, medium or large.
show_fps = false
menu_size = medium
)";

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
std::string lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

bool parse_int(const std::string& v, long* out) {   // tolère un suffixe (« 90 Hz », « 1080p »)
    std::string t = trim(v);
    char* end = nullptr;
    long n = strtol(t.c_str(), &end, 10);
    if (end == t.c_str()) return false;
    *out = n;
    return true;
}
bool parse_refresh(const std::string& v, int* out) {
    long n;
    if (!parse_int(v, &n) || n < 30 || n > 240) return false;
    *out = (int)n;
    return true;
}
bool parse_resolution(const std::string& v0, int* out) {
    std::string v = lower(trim(v0));
    if (v == "auto" || v == "0") {
        *out = 0;
        return true;
    }
    if (v == "4k" || v == "uhd") {
        *out = 2160;
        return true;
    }
    long n;
    if (!parse_int(v, &n) || (n != 720 && n != 1080 && n != 1440 && n != 2160)) return false;
    *out = (int)n;
    return true;
}
// Touche du clavier : « ctrl+shift+0x11 » (modificateurs ctrl, shift, alt, puis le scancode ; 0 : aucune touche)
bool parse_touche(const std::string& v, unsigned* out) {
    std::string r = lower(trim(v));
    unsigned mods = 0;
    for (bool vu = true; vu;) {
        vu = false;
        for (auto [nom, bit] : {std::pair<const char*, unsigned>{"ctrl+", Settings::MODIF_CTRL},
                                {"shift+", Settings::MODIF_SHIFT}, {"alt+", Settings::MODIF_ALT}})
            if (r.rfind(nom, 0) == 0) r = trim(r.substr(strlen(nom))), mods |= bit, vu = true;
    }
    char* fin = nullptr;
    const unsigned long sc = strtoul(r.c_str(), &fin, 0);
    if (fin == r.c_str() || *fin || !(sc <= 0xFF || (sc >= 0xE000 && sc <= 0xE0FF))) return false;
    *out = (unsigned)sc | (sc ? mods : 0);
    return true;
}
bool parse_bool(const std::string& v0, bool* out) {
    std::string v = lower(trim(v0));
    if (v == "1" || v == "true" || v == "yes" || v == "on" || v == "oui" || v == "vrai") {
        *out = true;
        return true;
    }
    if (v == "0" || v == "false" || v == "no" || v == "off" || v == "non" || v == "faux") {
        *out = false;
        return true;
    }
    return false;
}

void apply(const std::string& key, const std::string& val, const char* src) {
    std::string k = lower(trim(key));
    std::string v = trim(val);
    if (k == "refresh_rate") {
        int r;
        if (parse_refresh(v, &r)) g_settings.refresh_rate = r, g_src[0] = src;
        else HLOG("cfg", "%s: refresh_rate = '%s' invalid (30 to 240 Hz): ignored", src, v.c_str());
    } else if (k == "resolution") {
        int r;
        if (parse_resolution(v, &r)) g_settings.resolution = r, g_src[1] = src;
        else HLOG("cfg", "%s: resolution = '%s' invalid (auto, 720, 1080, 1440, 2160): ignored", src, v.c_str());
    } else if (k == "native_resolution") {
        bool b;
        if (parse_bool(v, &b)) g_settings.native_resolution = b, g_src[2] = src;
        else HLOG("cfg", "%s: native_resolution = '%s' invalid (true / false): ignored", src, v.c_str());
    } else if (k == "render_scale") {
        const int e = atoi(v.c_str());
        if (e >= 50 && e <= 100) {
            g_settings.render_scale = std::max(e, 67), g_echelle_donnee = true, g_src[2] = src;
            if (e < 67) HLOG("cfg", "%s: render_scale = %d: 67 at least (below, the game takes the console's DLSS path)", src, e);
        } else {
            HLOG("cfg", "%s: render_scale = '%s' invalid (67 to 100): ignored", src, v.c_str());
        }
    } else if (k == "dlss") {
        const char* const* modes = kModesDlss;
        const std::string m = lower(v);
        int i = 0;
        while (i < 6 && m != modes[i]) i++;
        if (i < 6) g_settings.dlss = i;
        else if (m == "0" || m == "false" || m == "non" || m == "no") g_settings.dlss = 0;
        else if (m == "on" || m == "true" || m == "native") g_settings.dlss = 1;
        else if (m == "ultra_perf" || m == "ultraperformance") g_settings.dlss = 5;
        else HLOG("cfg", "%s: dlss = '%s' invalid (off, dlaa, quality, balanced, performance, ultra_performance): ignored",
                  src, v.c_str());
    } else if (k == "dlss_model") {
        const std::string m = lower(v);
        if (m == "dlss4" || m == "4" || m == "k") g_settings.dlss_modele = 1;
        else if (m == "dlss45" || m == "dlss4.5" || m == "4.5" || m == "45" || m == "m") g_settings.dlss_modele = 2;
        else HLOG("cfg", "%s: dlss_model = '%s' invalid (dlss4, dlss45): ignored", src, v.c_str());
    } else if (k == "fsr") {
        const char* const* modes = kModesFsr;
        const std::string m = lower(v);
        int i = 0;
        while (i < 6 && m != modes[i]) i++;
        if (i < 6) g_settings.fsr = i;
        else if (m == "0" || m == "false" || m == "non" || m == "no") g_settings.fsr = 0;
        else if (m == "on" || m == "true" || m == "nativeaa" || m == "native_aa" || m == "aa") g_settings.fsr = 1;
        else if (m == "ultra_perf" || m == "ultraperformance") g_settings.fsr = 5;
        else HLOG("cfg", "%s: fsr = '%s' invalid (off, native, quality, balanced, performance, ultra_performance): ignored",
                  src, v.c_str());
    } else if (k == "fsr_version") {
        const std::string m = lower(v);
        if (m == "auto" || m.empty()) g_settings.fsr_version = 0;
        else if (m == "fsr3" || m == "3" || m == "fsr3.1" || m == "3.1") g_settings.fsr_version = 3;
        else if (m == "fsr4" || m == "4" || m == "fsr4.1" || m == "4.1") g_settings.fsr_version = 4;
        else HLOG("cfg", "%s: fsr_version = '%s' invalid (auto, fsr3, fsr4): ignored", src, v.c_str());
    } else if (k == "fsr_sharpness") {
        const int n = atoi(v.c_str());
        if (n >= 0 && n <= 100 && !v.empty() && isdigit((unsigned char)v[0])) g_settings.fsr_sharpness = n;
        else HLOG("cfg", "%s: fsr_sharpness = '%s' invalid (0 to 100): ignored", src, v.c_str());
    } else if (k == "gpu") {   // texte cherché dans le nom des cartes (vk_render.cpp, choisir_carte)
        g_settings.gpu = v.empty() || lower(v) == "auto" ? std::string("auto") : v;
    } else if (k == "window_mode") {
        std::string m = lower(v);
        if (m == "borderless" || m == "fullscreen" || m == "plein_ecran") g_settings.borderless = true;
        else if (m == "windowed" || m == "window" || m == "fenetre") g_settings.borderless = false;
        else HLOG("cfg", "%s: window_mode = '%s' invalid (borderless / windowed): ignored", src, v.c_str());
    } else if (k == "language") {
        const std::string l = lower(v);
        bool ok = l == "auto";
        if (ok) g_settings.language = "auto";
        for (const char* c : kLanguages)
            if (!ok && lower(c) == l) g_settings.language = c, ok = true;
        if (!ok) HLOG("cfg", "%s: language = '%s' invalid (auto, fr, en-US, ja...): ignored", src, v.c_str());
    } else if (k == "vibration") {
        long n;
        bool b;
        if (parse_int(v, &n) && n >= 0 && n <= 100) g_settings.vibration = (int)n;
        else if (parse_bool(v, &b)) g_settings.vibration = b ? 100 : 0;
        else HLOG("cfg", "%s: vibration = '%s' invalid (0 to 100): ignored", src, v.c_str());
    } else if (k == "volumetric_fog") {
        bool b;
        if (parse_bool(v, &b)) g_settings.volumetric_fog = b;
        else HLOG("cfg", "%s: volumetric_fog = '%s' invalid (true or false): ignored", src, v.c_str());
    } else if (k == "memory_saver") {
        bool b;
        if (parse_bool(v, &b)) g_settings.memory_saver = b;
        else HLOG("cfg", "%s: memory_saver = '%s' invalid (true or false): ignored", src, v.c_str());
    } else if (k == "zero_page_release") {
        bool b;
        if (parse_bool(v, &b)) g_settings.zero_page_release = b;
        else HLOG("cfg", "%s: zero_page_release = '%s' invalid (true or false): ignored", src, v.c_str());
    } else if (k == "frame_pacing") {
        bool b;
        if (parse_bool(v, &b)) g_settings.frame_pacing = b;
        else HLOG("cfg", "%s: frame_pacing = '%s' invalid (true or false): ignored", src, v.c_str());
    } else if (k == "fast_rendering") {
        bool b;
        if (parse_bool(v, &b)) g_settings.fast_rendering = b;
        else HLOG("cfg", "%s: fast_rendering = '%s' invalid (true or false): ignored", src, v.c_str());
    } else if (k == "mouse_keyboard") {
        bool b;
        if (parse_bool(v, &b)) g_settings.mouse_keyboard = b;
        else HLOG("cfg", "%s: mouse_keyboard = '%s' invalid (true or false): ignored", src, v.c_str());
#ifndef _WIN32
        // capture de la souris (hle_kbm.cpp) écrite pour Windows seulement : le mode reste coupé ailleurs
        if (g_settings.mouse_keyboard) {
            g_settings.mouse_keyboard = false;
            HLOG("cfg", "%s: mouse_keyboard needs Windows for now: off", src);
        }
#endif
    } else if (k == "mouse_speed") {   // hors limites : ramené dedans, comme le launcher
        long n;
        if (parse_int(v, &n)) {
            g_settings.mouse_speed = (int)std::clamp<long>(n, 10, 1000);
            if (n < 10 || n > 1000) HLOG("cfg", "%s: mouse_speed = %ld out of 10 to 1000: %d", src, n, g_settings.mouse_speed);
        } else {
            HLOG("cfg", "%s: mouse_speed = '%s' invalid (10 to 1000): ignored", src, v.c_str());
        }
    } else if (k == "mouse_release") {
        unsigned t = 0;
        if (parse_touche(v, &t)) g_settings.mouse_release = t;
        else HLOG("cfg", "%s: mouse_release = '%s' invalid (key scancode such as 0x43, ctrl+0x43; 0: none): ignored",
                  src, v.c_str());
    } else if (k.rfind("mouse_", 0) == 0) {   // action de la manette d'une entrée de la souris
        static const char* const souris[Settings::M_NB] = {"left", "right", "middle", "back", "forward", "wheel_up",
                                                           "wheel_down"};
        int i = 0, b = 0;
        while (i < Settings::M_NB && k.compare(6, std::string::npos, souris[i]) != 0) i++;
        const std::string m = lower(v);
        while (b < Settings::K_NB && m != kNomsActions[b]) b++;
        if (i >= Settings::M_NB) HLOG("cfg", "%s: unknown key '%s' ignored", src, k.c_str());
        else if (b < Settings::K_NB || m == "none") g_settings.mouse_buttons[i] = (int8_t)(b < Settings::K_NB ? b : -1);
        else HLOG("cfg", "%s: %s = '%s' invalid (a, b, x, y, l, r, zl, zr, plus, minus, lstick, rstick, up, down, "
                         "left, right, ls_up … rs_right, left_sl, left_sr, right_sl, right_sr, none): ignored", src,
                  k.c_str(), v.c_str());
    } else if (k == "show_fps") {
        bool b;
        if (parse_bool(v, &b)) g_settings.show_fps = b;
        else HLOG("cfg", "%s: show_fps = '%s' invalid (true or false): ignored", src, v.c_str());
    } else if (k == "menu_size") {
        const std::string m = lower(v);
        if (m == "small" || m == "1" || m == "petit") g_settings.menu_size = 1;
        else if (m == "medium" || m == "2" || m == "moyen") g_settings.menu_size = 2;
        else if (m == "large" || m == "3" || m == "grand") g_settings.menu_size = 3;
        else HLOG("cfg", "%s: menu_size = '%s' invalid (small, medium, large): ignored", src, v.c_str());
    } else if (k == "cold_page_release") {
        bool b;
        if (parse_bool(v, &b)) g_settings.cold_page_release = b;
        else HLOG("cfg", "%s: cold_page_release = '%s' invalid (true or false): ignored", src, v.c_str());
    } else if (k == "staging_arena_mb") {
        long n;
        const std::string m = lower(trim(v));
        if (m == "auto") g_settings.staging_arena_mb = -1;
        else if (m == "off" || m == "false" || m == "none") g_settings.staging_arena_mb = 0;
        else if (parse_int(v, &n) && n >= 0 && n <= 4096) g_settings.staging_arena_mb = (int)n;
        else HLOG("cfg", "%s: staging_arena_mb = '%s' invalid (auto, off, 0 to 4096): ignored", src, v.c_str());
    } else if (k == "staging_pool_mb") {
        long n;
        if (parse_int(v, &n) && n >= 0 && n <= 4096) g_settings.staging_pool_mb = (int)n;
        else HLOG("cfg", "%s: staging_pool_mb = '%s' invalid (0 to 4096): ignored", src, v.c_str());
    } else if (k == "deswizzle_threads") {
        long n;
        if (lower(trim(v)) == "auto") g_settings.deswizzle_threads = 0;
        else if (parse_int(v, &n) && n >= 1 && n <= 16) g_settings.deswizzle_threads = (int)n;
        else HLOG("cfg", "%s: deswizzle_threads = '%s' invalid (auto, 1 to 16): ignored", src, v.c_str());
    } else if (k == "vertex_buffers_vram") {
        bool b;
        if (parse_bool(v, &b)) g_settings.vertex_buffers_vram = b;
        else HLOG("cfg", "%s: vertex_buffers_vram = '%s' invalid (true or false): ignored", src, v.c_str());
    } else if (k == "vertex_buffers_vram_mb") {
        long n;
        if (parse_int(v, &n) && n >= 32 && n <= 65536) g_settings.vertex_buffers_vram_mb = (int)n;
        else HLOG("cfg", "%s: vertex_buffers_vram_mb = '%s' invalid (32 to 65536): ignored", src, v.c_str());
    } else if (k == "texture_release" || k == "shaders_on_demand") {
        bool b;
        if (parse_bool(v, &b)) (k == "texture_release" ? g_settings.texture_release : g_settings.shaders_on_demand) = b;
        else HLOG("cfg", "%s: %s = '%s' invalid (true or false): ignored", src, k.c_str(), v.c_str());
    } else if (k.rfind("pad_", 0) == 0) {   // bouton de manette d'une action (page Contrôles du launcher, onglet Manette)
        int i = 0, e = 0;
        while (i < Settings::PAD_NB && k.compare(4, std::string::npos, kNomsActions[i]) != 0) i++;
        const std::string m = lower(v);
        while (e < Settings::P_NB && m != PAD_ENTREES[e]) e++;
        if (i < Settings::PAD_NB && e < Settings::P_NB) g_settings.pad[i] = (uint8_t)e;
        else if (i >= Settings::PAD_NB) HLOG("cfg", "%s: unknown key '%s' ignored (pad_a ... pad_right)", src, k.c_str());
        else HLOG("cfg", "%s: %s = '%s' invalid (controller input such as east, south, lefttrigger; set it from the "
                         "launcher's Controls page): ignored", src, k.c_str(), v.c_str());
    } else if (k.rfind("key_", 0) == 0) {   // touche du clavier d'une action de la manette (page Contrôles du launcher)
        int i = 0;
        while (i < Settings::K_NB && k.compare(4, std::string::npos, kNomsActions[i]) != 0) i++;
        unsigned t = 0;
        if (i < Settings::K_NB && parse_touche(v, &t)) g_settings.keys[i] = t;
        else HLOG("cfg", "%s: %s = '%s' invalid (key scancode such as 0x11, shift+0x11; set it from the launcher's "
                         "Controls page): ignored", src, k.c_str(), v.c_str());
    } else {
        HLOG("cfg", "%s: unknown key '%s' ignored", src, k.c_str());
    }
}

void load_file(const std::string& path, bool create) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        if (!create) {
            HLOG("cfg", "settings file not found: %s (default values)", path.c_str());
            return;
        }
        if (FILE* w = fopen(path.c_str(), "wb")) {
            fputs(kTemplate, w);
            fputs(kSectionMemoire, w);
            fputs(kCleTextures, w);
            fputs(kCleShaders, w);
            fputs(kCleVides, w);
            fputs(kCleFroides, w);
            fputs(kCleDlss, w);
            fputs(kCleFsr, w);
            fputs(kCleRendu, w);
            fputs(kCleCadence, w);
            fputs(kCleGpu, w);
            fputs(kCleSouris, w);
            fputs(kClePerf, w);   // (sections [performance] puis [menu] : après les réglages sans section)
            fputs(kCleMenu, w);
            fclose(w);
            HLOG("cfg", "%s created with default values", path.c_str());
        }
        return;
    }
    char line[1024];
    int n = 0;
    std::set<std::string> vues;   // clés présentes dans le fichier
    while (fgets(line, sizeof line, f)) {
        std::string s = line;
        if (n++ == 0 && s.size() >= 3 && (uint8_t)s[0] == 0xEF && (uint8_t)s[1] == 0xBB && (uint8_t)s[2] == 0xBF)
            s = s.substr(3);   // BOM UTF-8
        size_t c = s.find_first_of(";#");
        if (c != std::string::npos) s = s.substr(0, c);
        s = trim(s);
        if (s.empty() || s[0] == '[') continue;   // sections : simple rangement, ignorées
        size_t eq = s.find('=');
        if (eq == std::string::npos) {
            HLOG("cfg", "%s, line %d: expected 'key = value', ignored", g_file_label.c_str(), n);
            continue;
        }
        vues.insert(lower(trim(s.substr(0, eq))));
        apply(s.substr(0, eq), s.substr(eq + 1), g_file_label.c_str());
    }
    fclose(f);
    // recomp.ini d'une version précédente : les réglages apparus depuis sont ajoutés à la fin, avec leur explication ;
    // les lignes existantes ne sont pas touchées (pas pour un fichier donné par --config)
    if (create) {
        std::string ajout;
        if (!vues.count("memory_saver")) ajout += std::string("\n") + kSectionMemoire;
        if (!vues.count("texture_release")) ajout += kCleTextures;
        if (!vues.count("shaders_on_demand")) ajout += kCleShaders;
        if (!vues.count("zero_page_release")) ajout += kCleVides;
        if (!vues.count("cold_page_release")) ajout += kCleFroides;
        if (!vues.count("dlss")) ajout += kCleDlss;
        if (!vues.count("fsr")) ajout += kCleFsr;
        if (!vues.count("fast_rendering")) ajout += kCleRendu;
        if (!vues.count("frame_pacing")) ajout += kCleCadence;
        if (!vues.count("gpu")) ajout += kCleGpu;
        if (!vues.count("mouse_keyboard")) ajout += kCleSouris;
        if (!vues.count("staging_arena_mb")) ajout += kClePerf;
        if (!vues.count("show_fps")) ajout += kCleMenu;
        if (!ajout.empty()) {
            if (FILE* w = fopen(path.c_str(), "ab")) {
                fputs(ajout.c_str(), w);
                fclose(w);
                HLOG("cfg", "%s: settings added since the previous version appended at the end", g_file_label.c_str());
            }
        }
    }
}

}  // namespace

const Settings& settings() { return g_settings; }
Settings& settings_vivants() { return g_settings; }

bool settings_volume_enregistrer(int volume) {
    // lanceur.ini du launcher qui a lancé le jeu (lanceur.cpp, lancer_jeu) : il relit son volume à la fin de la partie
#ifdef _WIN32
    const wchar_t* ini = _wgetenv(L"RECOMP_LANCEUR_INI");
    if (!ini || !*ini) return false;
    return WritePrivateProfileStringW(L"lanceur", L"volume", std::to_wstring(std::max(0, std::min(100, volume))).c_str(), ini) != 0;
#else
    // Linux : le launcher tient ce fichier en UTF-8, « [section] » et « clé=valeur » (launcher/win32_linux.cpp,
    // WritePrivateProfileStringW). La ligne « volume=… » de la section [lanceur] est remplacée, ou ajoutée sous son
    // titre ; le reste est gardé. Écrit à côté puis mis en place d'un seul coup.
    const char* ini = getenv("RECOMP_LANCEUR_INI");
    if (!ini || !*ini) return false;
    std::string t;
    FILE* f = fopen(ini, "rb");
    if (!f) return false;
    char b[4096];
    for (size_t n; (n = fread(b, 1, sizeof b, f)) > 0;) t.append(b, n);
    fclose(f);
    if (t.size() >= 2 && (unsigned char)t[0] == 0xFF && (unsigned char)t[1] == 0xFE) return false;   // UTF-16 : pas ici
    std::vector<std::string> li;
    for (size_t i = 0; i < t.size();) {
        size_t j = t.find('\n', i);
        if (j == std::string::npos) j = t.size();
        std::string l = t.substr(i, j - i);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        li.push_back(l);
        i = j + 1;
    }
    const std::string ligne = "volume=" + std::to_string(std::max(0, std::min(100, volume)));
    int titre = -1, cle = -1;
    bool dans = false;
    for (int i = 0; i < (int)li.size() && cle < 0; i++) {
        const std::string l = lower(trim(li[i]));
        if (!l.empty() && l[0] == '[') {
            dans = l.compare(0, 9, "[lanceur]") == 0;
            if (dans && titre < 0) titre = i;
            continue;
        }
        const size_t eq = l.find('=');
        if (dans && eq != std::string::npos && trim(l.substr(0, eq)) == "volume") cle = i;
    }
    if (cle >= 0) li[cle] = ligne;
    else if (titre >= 0) li.insert(li.begin() + titre + 1, ligne);
    else li.push_back("[lanceur]"), li.push_back(ligne);
    std::string r;
    for (const std::string& l : li) r += l + "\n";
    const std::string part = std::string(ini) + ".part";
    FILE* w = fopen(part.c_str(), "wb");
    if (!w) return false;
    const bool ecrit = fwrite(r.data(), 1, r.size(), w) == r.size();
    if (fclose(w) != 0 || !ecrit || rename(part.c_str(), ini) != 0) {
        remove(part.c_str());
        return false;
    }
    return true;
#endif
}

std::string language_code() {
    if (g_settings.language != "auto") return g_settings.language;
#ifndef _WIN32
    // Linux : langue de la session (LC_ALL, LC_MESSAGES, LANG, p. ex. « fr_CA.UTF-8 ») → code de la console le plus proche
    std::string l;
    for (const char* v : {"LC_ALL", "LC_MESSAGES", "LANG"})
        if (const char* e = getenv(v); e && *e && strcmp(e, "C") && strcmp(e, "POSIX")) {
            l = e;
            break;
        }
    const std::string lg = l.substr(0, 2), pays = l.size() >= 5 ? l.substr(3, 2) : "";
    if (lg == "fr") return pays == "CA" ? "fr-CA" : "fr";
    if (lg == "en") return pays == "GB" || pays == "AU" || pays == "NZ" || pays == "IE" ? "en-GB" : "en-US";
    if (lg == "de") return "de";
    if (lg == "it") return "it";
    if (lg == "es") return pays.empty() || pays == "ES" ? "es" : "es-419";
    if (lg == "nl") return "nl";
    if (lg == "pt") return pays == "BR" ? "pt-BR" : "pt";
    if (lg == "ru") return "ru";
    if (lg == "ja") return "ja";
    if (lg == "ko") return "ko";
    if (lg == "zh") return pays == "CN" || pays == "SG" ? "zh-Hans" : "zh-Hant";
    return "en-US";
#else
    // langue de l'interface de Windows → code de la console le plus proche
    const LANGID id = GetUserDefaultUILanguage();
    const int sub = SUBLANGID(id);
    switch (PRIMARYLANGID(id)) {
    case LANG_FRENCH: return sub == SUBLANG_FRENCH_CANADIAN ? "fr-CA" : "fr";
    case LANG_ENGLISH: return sub == SUBLANG_ENGLISH_UK || sub == SUBLANG_ENGLISH_AUS || sub == SUBLANG_ENGLISH_NZ ||
                                      sub == SUBLANG_ENGLISH_EIRE ? "en-GB" : "en-US";
    case LANG_GERMAN: return "de";
    case LANG_ITALIAN: return "it";
    case LANG_SPANISH: return sub == SUBLANG_SPANISH || sub == SUBLANG_SPANISH_MODERN ? "es" : "es-419";
    case LANG_DUTCH: return "nl";
    case LANG_PORTUGUESE: return sub == SUBLANG_PORTUGUESE_BRAZILIAN ? "pt-BR" : "pt";
    case LANG_RUSSIAN: return "ru";
    case LANG_JAPANESE: return "ja";
    case LANG_KOREAN: return "ko";
    case LANG_CHINESE: return sub == SUBLANG_CHINESE_SIMPLIFIED || sub == SUBLANG_CHINESE_SINGAPORE ? "zh-Hans" : "zh-Hant";
    default: return "en-US";
    }
#endif
}

void settings_init(int argc, char** argv) {
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string dir = exe;
    dir = dir.substr(0, dir.find_last_of("\\/") + 1);
    std::string cfg;
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--config")) cfg = argv[i + 1];
    bool explicit_cfg = !cfg.empty();
    if (!explicit_cfg) cfg = dir + "recomp.ini";
    g_settings.file = cfg;
    g_file_label = cfg.substr(cfg.find_last_of("\\/") + 1);
    load_file(cfg, !explicit_cfg);
    if (const char* e = getenv("RECOMP_REFRESH")) apply("refresh_rate", e, "RECOMP_REFRESH");
    if (const char* e = getenv("RECOMP_RESOLUTION")) apply("resolution", e, "RECOMP_RESOLUTION");
    if (const char* e = getenv("RECOMP_NATIVE_RES")) apply("native_resolution", e, "RECOMP_NATIVE_RES");
    if (const char* e = getenv("RECOMP_RENDER_SCALE")) apply("render_scale", e, "RECOMP_RENDER_SCALE");
    if (const char* e = getenv("RECOMP_DLSS")) apply("dlss", e, "RECOMP_DLSS");
    if (const char* e = getenv("RECOMP_DLSS_MODEL")) apply("dlss_model", e, "RECOMP_DLSS_MODEL");
    if (const char* e = getenv("RECOMP_FSR")) apply("fsr", e, "RECOMP_FSR");
    if (const char* e = getenv("RECOMP_FSR_VERSION")) apply("fsr_version", e, "RECOMP_FSR_VERSION");
    if (const char* e = getenv("RECOMP_FSR_SHARPNESS")) apply("fsr_sharpness", e, "RECOMP_FSR_SHARPNESS");
    if (const char* e = getenv("RECOMP_GPU")) apply("gpu", e, "RECOMP_GPU");
    if (const char* e = getenv("RECOMP_MOUSE_KEYBOARD")) apply("mouse_keyboard", e, "RECOMP_MOUSE_KEYBOARD");
    if (const char* e = getenv("RECOMP_WINDOW")) apply("window_mode", e, "RECOMP_WINDOW");
    if (const char* e = getenv("RECOMP_LANGUAGE")) apply("language", e, "RECOMP_LANGUAGE");
    if (const char* e = getenv("RECOMP_VIBRATION")) apply("vibration", e, "RECOMP_VIBRATION");
    if (const char* e = getenv("RECOMP_MEMORY_SAVER")) apply("memory_saver", e, "RECOMP_MEMORY_SAVER");
    if (getenv("RECOMP_EAGER_COMMIT")) apply("memory_saver", "false", "RECOMP_EAGER_COMMIT");   // ancien nom (essais)
    // section [performance] : les variables d'essai d'avant passent devant recomp.ini
    if (const char* e = getenv("RECOMP_STAGING_ARENA_MB")) apply("staging_arena_mb", e, "RECOMP_STAGING_ARENA_MB");
    if (getenv("RECOMP_NO_STAGING_ARENA")) apply("staging_arena_mb", "off", "RECOMP_NO_STAGING_ARENA");
    if (const char* e = getenv("RECOMP_STAGING_POOL_MB")) apply("staging_pool_mb", e, "RECOMP_STAGING_POOL_MB");
    if (const char* e = getenv("RECOMP_DESWIZZLE_THREADS")) apply("deswizzle_threads", e, "RECOMP_DESWIZZLE_THREADS");
    if (const char* e = getenv("RECOMP_VB_VRAM")) apply("vertex_buffers_vram", e, "RECOMP_VB_VRAM");
    if (const char* e = getenv("RECOMP_VB_VRAM_MO")) apply("vertex_buffers_vram_mb", e, "RECOMP_VB_VRAM_MO");
    if (const char* e = getenv("RECOMP_TEX_RELEASE")) apply("texture_release", e, "RECOMP_TEX_RELEASE");
    if (const char* e = getenv("RECOMP_SHADERS_ON_DEMAND")) apply("shaders_on_demand", e, "RECOMP_SHADERS_ON_DEMAND");
    if (const char* e = getenv("RECOMP_ZERO_PAGE_RELEASE")) apply("zero_page_release", e, "RECOMP_ZERO_PAGE_RELEASE");
    if (const char* e = getenv("RECOMP_COLD_PAGE_RELEASE")) apply("cold_page_release", e, "RECOMP_COLD_PAGE_RELEASE");
    if (const char* e = getenv("RECOMP_FAST_RENDERING")) apply("fast_rendering", e, "RECOMP_FAST_RENDERING");
    if (const char* e = getenv("RECOMP_FRAME_PACING")) apply("frame_pacing", e, "RECOMP_FRAME_PACING");
    if (const char* e = getenv("RECOMP_VOLUMETRIC_FOG")) apply("volumetric_fog", e, "RECOMP_VOLUMETRIC_FOG");
    if (const char* e = getenv("RECOMP_SHOW_FPS")) apply("show_fps", e, "RECOMP_SHOW_FPS");
    for (int i = 1; i + 1 < argc; i++) {
        std::string a = argv[i];
        if (a == "--refresh") apply("refresh_rate", argv[++i], "command line");
        else if (a == "--resolution") apply("resolution", argv[++i], "command line");
        else if (a == "--native-res") apply("native_resolution", argv[++i], "command line");
        else if (a == "--window") apply("window_mode", argv[++i], "command line");
        else if (a == "--language") apply("language", argv[++i], "command line");
    }
    // ancien « Comme la console » (50 %, scène 3D noire) : 67 %, l'échelle la plus basse qui s'affiche
    if (!g_settings.native_resolution && !g_echelle_donnee) g_settings.render_scale = 67;
    const Settings& s = g_settings;
    char res[16];
    char echelle[24];
    if (s.render_scale >= 100) snprintf(echelle, sizeof echelle, "native");
    else snprintf(echelle, sizeof echelle, "%d %%", s.render_scale);
    if (s.resolution) snprintf(res, sizeof res, "%d", s.resolution);
    else snprintf(res, sizeof res, "auto");
    HLOG("cfg", "settings: %d Hz [%s], resolution %s [%s], internal rendering %s [%s], %s, language %s (%s), rumble %d %% (file %s)",
         s.refresh_rate, g_src[0], res, g_src[1], echelle, g_src[2],
         s.borderless ? "borderless fullscreen" : "windowed", s.language.c_str(), language_code().c_str(), s.vibration,
         s.file.c_str());
    if (!s.volumetric_fog) HLOG("cfg", "volumetric fog off");
    if (s.dlss) {
        static const char* const modes[] = {"off", "DLAA", "Quality (67 %)", "Balanced (58 %)", "Performance (50 %)",
                                            "Ultra Performance (33 %)"};
        HLOG("cfg", "DLSS requested: %s, %s", modes[s.dlss], s.dlss_modele == 2 ? "DLSS 4.5 (model M)" : "DLSS 4 (model K)");
    }
    if (s.fsr) {
        static const char* const modes[] = {"off", "Native AA", "Quality (67 %)", "Balanced (59 %)", "Performance (50 %)",
                                            "Ultra Performance (33 %)"};
        HLOG("cfg", "AMD FSR requested: %s, version %s, sharpening %d %%", modes[s.fsr],
             s.fsr_version == 3 ? "FSR 3.1" : s.fsr_version == 4 ? "FSR 4" : "automatic", s.fsr_sharpness);
    }
    if (s.gpu != "auto") HLOG("cfg", "graphics card requested: '%s'", s.gpu.c_str());
    if (s.mouse_keyboard) {
        auto nom = [](int8_t b) { return b < 0 ? "none" : kNomsActions[b]; };
        HLOG("cfg", "mouse mode: Joy-Con 2 pair, mouse = right Joy-Con 2 mouse at %d %%; buttons left %s, right %s, "
             "middle %s, back %s, forward %s; wheel up %s, down %s; release key 0x%X", s.mouse_speed,
             nom(s.mouse_buttons[0]), nom(s.mouse_buttons[1]), nom(s.mouse_buttons[2]), nom(s.mouse_buttons[3]),
             nom(s.mouse_buttons[4]), nom(s.mouse_buttons[5]), nom(s.mouse_buttons[6]), s.mouse_release);
    }
    {   // touches du clavier changées par le joueur (page Contrôles du launcher)
        const Settings defaut;
        int n = 0;
        for (int i = 0; i < Settings::K_NB; i++) n += s.keys[i] != defaut.keys[i];
        if (n) HLOG("cfg", "keyboard: %d key(s) changed from the default layout (recomp.ini, key_...)", n);
        n = 0;
        for (int i = 0; i < Settings::PAD_NB; i++) n += s.pad[i] != defaut.pad[i];
        if (n) HLOG("cfg", "controller: %d button(s) changed from the default layout (recomp.ini, pad_...)", n);
    }
    // memory_saver = false : tout comme en 1.0.2, les deux autres économies comprises
    if (!g_settings.memory_saver)
        g_settings.texture_release = g_settings.shaders_on_demand = g_settings.zero_page_release = false;
    // pages oubliées : dans le fichier des textures, par le balayage des pages vides
    if (!g_settings.texture_release || !g_settings.zero_page_release) g_settings.cold_page_release = false;
    if (!g_settings.fast_rendering) {
        // Chaque optimisation lit sa variable d'essai à son premier usage, après ce chargement : les poser ici les
        // coupe toutes (vk_render.cpp, nvn_cmd.cpp, runtime.cpp).
        for (const char* v : {"RECOMP_NO_BANK_CACHE", "RECOMP_NO_DYN_CACHE", "RECOMP_NO_REC_CACHE", "RECOMP_NO_CALL_CACHE",
                              "RECOMP_NO_BIND_SKIP"})
            _putenv_s(v, "1");
        _putenv_s("RECOMP_PARTIAL_SUBMIT", "0");
        _putenv_s("RECOMP_SPIN_US", "0");
        HLOG("cfg", "fast_rendering = false: the rendering optimizations of 2026-11-12 are all off");
    }
    HLOG("cfg", "frame pacing: %s", s.frame_pacing ? "on (each frame shown at a fixed instant)"
                                                   : "off (each frame shown as soon as it is ready, as up to 1.1.0)");
    HLOG("cfg", "memory saver: %s", s.memory_saver ? "on (game memory committed on demand, pools imported by windows)"
                                                   : "off (as in 1.0.2: full commit, whole pools imported)");
    HLOG("cfg", "texture data released after upload: %s; shaders prepared at first use: %s; empty game pages given "
                "back: %s; forgotten game pages put in the temporary file: %s", s.texture_release ? "on" : "off",
         s.shaders_on_demand ? "on" : "off", s.zero_page_release ? "on" : "off", s.cold_page_release ? "on" : "off");
    {   // section [performance] : une ligne quand un réglage n'a pas sa valeur par défaut
        const Settings d;
        if (s.staging_arena_mb != d.staging_arena_mb || s.staging_pool_mb != d.staging_pool_mb ||
            s.deswizzle_threads != d.deswizzle_threads || s.vertex_buffers_vram != d.vertex_buffers_vram ||
            s.vertex_buffers_vram_mb != d.vertex_buffers_vram_mb) {
            char arene[16], fils[16];
            if (s.staging_arena_mb < 0) snprintf(arene, sizeof arene, "auto");
            else if (s.staging_arena_mb == 0) snprintf(arene, sizeof arene, "off");
            else snprintf(arene, sizeof arene, "%d MB", s.staging_arena_mb);
            if (s.deswizzle_threads) snprintf(fils, sizeof fils, "%d", s.deswizzle_threads);
            else snprintf(fils, sizeof fils, "auto");
            HLOG("cfg", "performance: staging arena %s, staging pool %d MB, deswizzle threads %s, vertex buffers in video "
                        "memory %s (%d MB)", arene, s.staging_pool_mb, fils, s.vertex_buffers_vram ? "on" : "off",
                 s.vertex_buffers_vram_mb);
        }
    }
    if (s.refresh_rate != 60 && s.refresh_rate != 90 && s.refresh_rate != 120)
        HLOG("cfg", "%d Hz: untested value (60, 90 and 120 Hz are verified)", s.refresh_rate);
}

}  // namespace hos

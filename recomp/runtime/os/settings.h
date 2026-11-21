// Réglages de l'exécutable, prévus pour être écrits par le futur launcher.
//
// Sources, par priorité croissante :
//   1. valeurs par défaut ;
//   2. recomp.ini à côté de l'exécutable (ou --config <fichier>) ; créé avec les valeurs par défaut
//      s'il n'existe pas ;
//   3. variables d'environnement RECOMP_REFRESH, RECOMP_RESOLUTION, RECOMP_NATIVE_RES, RECOMP_WINDOW,
//      RECOMP_LANGUAGE (développement) ;
//   4. options --refresh <Hz>, --resolution <auto|720|1080|1440|2160>, --native-res <0|1>, --window, --language.
// Les réglages sont lus une fois au lancement : un changement s'applique au lancement suivant. Sauf ceux que le menu en
// jeu (menu.cpp, F1) change pendant la partie : fenêtre, vibrations, touches du clavier, boutons de la manette, netteté
// du FSR, compteur d'images, taille du menu (settings_vivants) ; il écrit aussi les autres dans recomp.ini, pour le
// lancement suivant (settings_enregistrer).
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace hos {

struct Settings {
    int refresh_rate = 60;           // Hz de l'écran virtuel : 60, 90 ou 120 (30 à 240 acceptés)
    int resolution = 0;              // 0 = auto (d'après l'écran de l'hôte), sinon 720 / 1080 / 1440 / 2160
    bool native_resolution = true;   // rendu interne à la résolution de sortie (sinon 50 % + agrandissement)
    // Échelle du rendu 3D en % de la résolution de sortie (67 à 100) : le moteur rend plus petit, fait son
    // anticrénelage temporel à cette taille, puis agrandit l'image (upsample.frag, bilinéaire). native_resolution =
    // false (ancien « Comme la console », scène 3D noire jusqu'au 2026-11-12 : à 50 %, réseaux DLSS et NDSR de la
    // console, intraduisibles, désormais coupés dans recomp_cvars) vaut 67 si render_scale n'est pas donné.
    int render_scale = 100;
    // DLSS de NVIDIA à la place de l'anticrénelage temporel du jeu (dlss.cpp) : 0 non, 1 DLAA (rendu natif), 2 qualité
    // (67 %), 3 équilibré (58 %), 4 performances (50 %), 5 ultra performances (33 %). En super résolution (2 à 5),
    // l'échelle du rendu 3D suit le mode au lieu de render_scale. Cartes NVIDIA RTX seulement, avec nvngx_dlss.dll à
    // côté de l'exécutable ; sinon le jeu garde son anticrénelage (à render_scale).
    int dlss = 0;
    int dlss_modele = 1;   // 1 : DLSS 4 (modèle K), 2 : DLSS 4.5 (modèle M)
    // FSR d'AMD à la place de l'anticrénelage temporel du jeu (fsr.cpp), sur toute carte : 0 non, 1 natif, 2 qualité
    // (67 %), 3 équilibré (59 %), 4 performances (50 %), 5 ultra performances (33 %). Le DLSS passe devant s'il est
    // demandé aussi et que la carte est une NVIDIA RTX. fsr_version : 0 auto (la plus récente que la carte accepte :
    // FSR 4 sur les cartes AMD qui le proposent, sinon FSR 3.1), 3 ou 4 imposé.
    int fsr = 0;
    int fsr_version = 0;
    int fsr_sharpness = 0;   // netteté ajoutée par le FSR (passe RCAS), en % : 0 aucune (par défaut) à 100
    // Carte graphique (clé gpu, écrite par la page Graphismes du launcher ; RECOMP_GPU pour les essais) : « auto », ou
    // un texte contenu dans le nom de la carte voulue (le launcher écrit le nom complet). Choix dans vk_render.cpp
    // (choisir_carte) : la première carte dont le nom le contient ; sinon la carte dédiée qui a le plus de mémoire vidéo.
    // Le DLSS ne sert que sur une carte NVIDIA RTX (dlss.cpp, carte_dlss).
    std::string gpu = "auto";
    bool borderless = true;         // plein écran sans bordure (image pixel pour pixel) ; sinon fenêtré (Alt+Entrée bascule)
    std::string language = "auto";  // langue du jeu (code Switch : fr, en-US, ja…), auto = celle de Windows
    int vibration = 100;             // intensité des vibrations de la manette, en % (0 : aucune)
    // Touches du clavier, une par action de la manette (recomp.ini, clés key_a, key_ls_up… écrites par la page
    // Contrôles du launcher) : scancodes, c'est-à-dire la position physique de la touche (0xE0xx : touche étendue, les
    // flèches par exemple) ; 0 = aucune touche. Valeurs par défaut : celles du jeu depuis la 1.0.0, par position
    // (W A S D sur un clavier QWERTY, Z Q S D sur un AZERTY). Même ordre que TOUCHES_CLES (launcher/lanceur.cpp).
    // Puis SL et SR de chaque Joy-Con (K_LSL … K_RSR, aucune touche par défaut). Une touche peut demander des
    // modificateurs (MODIF_CTRL, MODIF_SHIFT, MODIF_ALT, bits 16 à 18 ; « shift+0x11 » dans recomp.ini) : Maj+W et W peuvent
    // faire deux actions, la combinaison la plus précise l'emporte (input.cpp, touches_actives).
    enum { K_A, K_B, K_X, K_Y, K_L, K_R, K_ZL, K_ZR, K_PLUS, K_MINUS, K_LSTICK, K_RSTICK, K_UP, K_DOWN, K_LEFT, K_RIGHT,
           K_LS_UP, K_LS_DOWN, K_LS_LEFT, K_LS_RIGHT, K_RS_UP, K_RS_DOWN, K_RS_LEFT, K_RS_RIGHT, K_LSL, K_LSR, K_RSL,
           K_RSR, K_NB };
    static constexpr unsigned MODIF_CTRL = 1u << 16, MODIF_SHIFT = 1u << 17, MODIF_ALT = 1u << 18,
                              MODIF_MASK = MODIF_CTRL | MODIF_SHIFT | MODIF_ALT;
    unsigned keys[K_NB] = {0x2E, 0x2D, 0x2F, 0x2C, 0x10, 0x12, 0x13, 0x14, 0x32, 0x31, 0x21, 0x23,
                           0xE048, 0xE050, 0xE04B, 0xE04D, 0x11, 0x1F, 0x1E, 0x20, 0x17, 0x25, 0x24, 0x26, 0, 0, 0, 0};
    // Boutons de la manette (recomp.ini, clés pad_a… écrites par la page Contrôles du launcher, onglet Manette) : pour
    // chaque bouton de la Switch, dans l'ordre K_A … K_RIGHT, l'entrée de la manette qui le fait (input.cpp), repérée
    // par sa position comme SDL le fait : P_SUD est le bouton du bas (A d'une manette Xbox, ✕ d'une PlayStation, B
    // d'une Nintendo). Valeurs de recomp.ini : PAD_ENTREES (settings.cpp), même ordre que ENTREES_PAD du launcher.
    // Par défaut, la disposition par position de toujours : le bouton de droite fait A, celui du bas B…
    enum { P_AUCUNE, P_SUD, P_EST, P_OUEST, P_NORD, P_BACK, P_GUIDE, P_START, P_STICK_G, P_STICK_D, P_EPAULE_G,
           P_EPAULE_D, P_HAUT, P_BAS, P_GAUCHE, P_DROITE, P_MISC1, P_PALETTE_D1, P_PALETTE_G1, P_PALETTE_D2,
           P_PALETTE_G2, P_PAVE, P_GACHETTE_G, P_GACHETTE_D, P_NB };
    static constexpr int PAD_NB = 16;   // boutons K_A … K_RIGHT (les sticks restent les sticks)
    uint8_t pad[PAD_NB] = {P_EST, P_SUD, P_NORD, P_OUEST, P_EPAULE_G, P_EPAULE_D, P_GACHETTE_G, P_GACHETTE_D,
                           P_START, P_BACK, P_STICK_G, P_STICK_D, P_HAUT, P_BAS, P_GAUCHE, P_DROITE};
    // Économie de mémoire (depuis la 1.0.3) : mémoire du jeu engagée à la demande (memory.cpp) et pools du jeu importés
    // par fenêtres (vk_render.cpp). false : comportement des versions 1.0.0 à 1.0.2 (tout engagé à la réservation,
    // pools importés en entier, tous au premier calcul à mémoire globale) : recours si un joueur a un problème.
    bool memory_saver = true;
    // Depuis la 1.0.3 aussi, et sans effet si memory_saver est faux (settings_init les met alors à faux) :
    // données des textures rendues à Windows après leur envoi au GPU, gardées dans un fichier temporaire (memory.cpp,
    // mem_release) ;
    bool texture_release = true;
    // bibliothèques de shaders créées au premier dessin quand pipelines.cache est complet, au lieu de toutes au
    // démarrage (vk_render.cpp, ensure_init).
    bool shaders_on_demand = true;
    // pages de la mémoire du jeu qui ne contiennent que des zéros, rendues à Windows pendant la partie (memory.cpp,
    // vides_balayer) ; le jeu les retrouve à zéro s'il y revient.
    bool zero_page_release = true;
    // pages pleines de la mémoire du jeu que le jeu n'a pas touchées depuis 4 minutes, rangées dans le fichier
    // temporaire des textures (memory.cpp, vides_balayer) ; le jeu les retrouve telles quelles s'il y revient. Faux
    // aussi sans texture_release ou zero_page_release (settings_init).
    bool cold_page_release = true;
    // Transit des textures et mémoire vidéo (section [performance] de recomp.ini, 2026-11-20 ; avant : variables
    // d'environnement seulement, qui passent toujours devant) :
    int staging_arena_mb = -1;          // arène de transit préchauffée (vk_render.cpp) : -1 auto (512 Mo), 0 aucune, n Mo
    int staging_pool_mb = 512;          // réserve des tampons de transit au-delà de 5 Go validables libres
    bool vertex_buffers_vram = false;   // sommets et index lus en mémoire vidéo (essai ; ignoré sous Linux)
    int vertex_buffers_vram_mb = 2048;  // plafond de mémoire vidéo de vertex_buffers_vram
    int deswizzle_threads = 0;          // fils du désentrelacement des textures : 0 auto (moitié des processeurs, 8 au plus)
    // Optimisations du 2026-11-12 (jeux de descripteurs repris d'un dessin à l'autre, état dynamique gardé, étages de
    // textures sautés, caches d'enregistrement et d'appels indirects, attente active entre fils du jeu) : false les
    // coupe toutes d'un coup (comme la 1.0.3), à essayer si un défaut d'image ou un plantage apparaît.
    bool fast_rendering = true;
    // Cadence régulière (2026-11-19, issue n° 41) : chaque image est montrée à l'instant de son vblank virtuel, comme
    // dans la file d'affichage de la console, au lieu de l'être dès qu'elle est prête (nvn_gpu.cpp, Item::Present).
    bool frame_pacing = true;
    // Brouillard volumétrique du moteur (volumetric_fog, 2026-11-15) : false coupe render.volumetric_fog_enabled et sa
    // variante portable (recomp_cvars, svc_fs.cpp). Son calcul (volumetric_fog_light.comp) prend 25 ms par image sur
    // le Steam Deck, 82 % du GPU dans la cinématique du prologue ; coupé, celle-ci passe de 13-22 à 55-60 img/s.
    bool volumetric_fog = true;
    // Mode souris du jeu (hle_kbm.cpp) : la manette émulée devient une paire de Joy-Con 2 (svc_hid.cpp) et la souris
    // du PC devient le capteur souris du Joy-Con 2 droit, que le jeu suit dans son mode souris. Le clavier pilote
    // toujours la manette (keys) ; les boutons de la souris appuient sur des boutons de la manette (mouse_buttons).
    bool mouse_keyboard = false;
    int mouse_speed = 100;   // déplacements de la souris envoyés au jeu, en % (10 à 1000)
    // action de la manette (indice K_A … K_RSR : bouton, direction d'un stick, SL ou SR ; -1 : aucune) de chaque entrée
    // de la souris : boutons gauche, droit, milieu, arrière (X1), avant (X2), molette vers le haut et vers le bas (un
    // appui bref par cran). Par défaut R et ZR, les deux boutons sous les doigts du Joy-Con 2 tenu en souris.
    enum { M_GAUCHE, M_DROIT, M_MILIEU, M_ARRIERE, M_AVANT, M_MOLETTE_HAUT, M_MOLETTE_BAS, M_NB };
    int8_t mouse_buttons[M_NB] = {K_R, K_ZR, K_RSTICK, -1, -1, -1, -1};
    // touche qui rend le curseur de Windows (scancode et modificateurs, comme keys ; 0 : aucune) ; un clic dans l'image
    // le reprend. F9 par défaut.
    unsigned mouse_release = 0x43;
    // Menu en jeu (menu.cpp) : compteur d'images par seconde dans le coin de l'image, et taille du menu (1 petit,
    // 2 moyen, 3 grand : sur un téléviseur, de loin).
    bool show_fps = false;
    int menu_size = 2;
    std::string file;                // fichier de réglages utilisé
};

// Écriture de recomp.ini, la même dans le jeu (settings.cpp, settings_ecrire.cpp) et dans le launcher (lanceur.cpp, qui
// a ses propres copies : TOUCHES_CLES, PAD_CLES, ENTREES_PAD, DLSS_MODES, FSR_MODES).
// noms des actions dans les clés key_… et pad_… (ordre de Settings::keys ; les 16 premières sont les boutons, puis les
// directions des sticks, puis SL et SR de chaque Joy-Con)
inline const char* const kNomsActions[Settings::K_NB] = {
    "a", "b", "x", "y", "l", "r", "zl", "zr", "plus", "minus", "lstick", "rstick", "up", "down", "left", "right",
    "ls_up", "ls_down", "ls_left", "ls_right", "rs_up", "rs_down", "rs_left", "rs_right", "left_sl", "left_sr",
    "right_sl", "right_sr"};
// entrées des manettes (Settings::P_*), valeurs des clés pad_… ; même ordre que ENTREES_PAD (launcher/lanceur.cpp)
inline const char* const PAD_ENTREES[Settings::P_NB] = {
    "none", "south", "east", "west", "north", "back", "guide", "start", "leftstick", "rightstick", "leftshoulder",
    "rightshoulder", "dpad_up", "dpad_down", "dpad_left", "dpad_right", "misc1", "right_paddle1", "left_paddle1",
    "right_paddle2", "left_paddle2", "touchpad", "lefttrigger", "righttrigger"};
// codes de langue de la console (nn::settings::LanguageCode), écriture canonique
inline const char* const kLanguages[] = {"ja", "en-US", "en-GB", "fr", "fr-CA", "de", "it", "es", "es-419", "nl",
                                         "pt", "pt-BR", "ru", "ko", "zh-Hans", "zh-Hant"};
constexpr int kNbLanguages = (int)(sizeof kLanguages / sizeof kLanguages[0]);
// valeurs des clés dlss et fsr, dans l'ordre de Settings::dlss et Settings::fsr
inline const char* const kModesDlss[6] = {"off", "dlaa", "quality", "balanced", "performance", "ultra_performance"};
inline const char* const kModesFsr[6] = {"off", "native", "quality", "balanced", "performance", "ultra_performance"};

const Settings& settings();
void settings_init(int argc, char** argv);   // à appeler tôt dans main(), après l'ouverture du journal
// Code de langue rendu au jeu (nn::oe::GetDesiredLanguage) : réglage, ou langue de Windows si « auto »
std::string language_code();
// ---- menu en jeu (menu.cpp) ----
// Les réglages en service, modifiables : le menu y écrit ceux qui s'appliquent tout de suite (des entiers et des
// booléens lus à chaque usage par les autres fils ; jamais les chaînes gpu et language, lues au démarrage seulement).
Settings& settings_vivants();
// Lignes « clé = valeur » de recomp.ini pour ces réglages (settings_ecrire.cpp) : toutes celles que le menu et le
// launcher changent, touches (key_…) et boutons (pad_…) compris.
std::vector<std::pair<std::string, std::string>> settings_lignes(const Settings& s);
// `contenu` (le texte de recomp.ini) avec ces clés : chaque ligne « clé = … » est remplacée, une clé absente est
// ajoutée à la fin (touches et boutons sous leur section, comme le launcher) ; commentaires, ordre et fins de ligne
// gardés.
std::string ini_remplacer(const std::string& contenu, const std::vector<std::pair<std::string, std::string>>& cles);
// Écrit ces clés dans le fichier de réglages utilisé (settings().file), d'un seul coup (fichier à côté, puis mis en
// place). Faux si le fichier n'a pas pu être écrit (il reste alors tel qu'il était).
bool settings_enregistrer(const std::vector<std::pair<std::string, std::string>>& cles);
// Volume du jeu (0 à 100), réglage du launcher (lanceur.ini, RECOMP_VOLUME) : écrit dans le fichier du launcher quand
// c'est lui qui a lancé le jeu (RECOMP_LANCEUR_INI). Faux sinon : le volume ne vaut alors que pour cette partie.
bool settings_volume_enregistrer(int volume);


}  // namespace hos

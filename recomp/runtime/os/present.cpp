// Présentation hôte provisoire : fenêtre Win32 (GDI) qui affiche la couleur d'effacement de la
// dernière image présentée par le jeu et le compteur d'images. Sert à vérifier que la boucle de
// rendu tourne ; sera remplacée par le moteur de rendu (Vulkan) quand les commandes NVN seront
// traduites. Fermer la fenêtre termine le processus.
#ifdef _WIN32
#include "gfx.h"
#include "settings.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace hos {
namespace vkr {
void save_caches();   // vk_render.cpp : écrit pipelines.cache
}
namespace {

std::once_flag g_once;
HWND g_hwnd = nullptr;
std::atomic<bool> g_vulkan{false};
std::mutex g_lk;
float g_color[4] = {0, 0, 0, 1};
uint64_t g_frame = 0;
int g_w = 1280, g_h = 720;
std::chrono::steady_clock::time_point g_last;
std::chrono::steady_clock::time_point g_fps_t0;
uint64_t g_fps_f0 = 0;
double g_fps = 0;

// Mise à l'échelle de Windows de l'écran principal (1 = 100 %, 1,5 = 150 %). Le jeu est compatible DPI depuis le
// 2026-11-12 (host_main.cpp, dpi_compatible) : ses tailles sont en pixels réels. RECOMP_DPI=<ppp> (essais : 144 pour
// 150 %) simule une mise à l'échelle pour le choix de resolution = auto et la taille de la fenêtre.
double echelle_ecran() {
    static const double k = [] {
        UINT ppp = GetDpiForSystem();
        if (const char* e = getenv("RECOMP_DPI")) ppp = (UINT)atoi(e);
        return ppp >= 48 ? ppp / 96.0 : 1.0;
    }();
    return k;
}

// Plein écran sans bordure (recomp.ini : window_mode) : fenêtre sans cadre couvrant l'écran où elle se
// trouve, pour présenter l'image pixel pour pixel. Une fenêtre à cadre de la taille de l'écran n'a
// qu'une zone utile plus petite (2560×1421 sur un écran 1440p) : l'image y était réduite, donc floue.
bool g_borderless = false;
WINDOWPLACEMENT g_wp_windowed{sizeof(WINDOWPLACEMENT)};
void set_borderless(HWND h, bool on) {
    if (on == g_borderless) return;
    if (on) {
        GetWindowPlacement(h, &g_wp_windowed);
        MONITORINFO mi{sizeof mi};
        GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLongW(h, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(h, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        SetWindowLongW(h, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPlacement(h, &g_wp_windowed);
        SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
    g_borderless = on;
    HLOG("gfx", "window: %s", on ? "borderless fullscreen" : "windowed");
}

LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        if (g_vulkan) {   // la swapchain Vulkan dessine la fenêtre
            ValidateRect(h, nullptr);
            return 0;
        }
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        float c[4];
        uint64_t f;
        double fps;
        {
            std::lock_guard<std::mutex> lk(g_lk);
            memcpy(c, g_color, sizeof c);
            f = g_frame;
            fps = g_fps;
        }
        auto to8 = [](float v) { return (int)(std::min(1.0f, std::max(0.0f, v)) * 255.0f + 0.5f); };
        RECT rc;
        GetClientRect(h, &rc);
        HBRUSH br = CreateSolidBrush(RGB(to8(c[0]), to8(c[1]), to8(c[2])));
        FillRect(dc, &rc, br);
        DeleteObject(br);
        char txt[160];
        snprintf(txt, sizeof txt, "frame %llu  -  %.1f fps  -  NVN rendering not translated (clear color)",
                 (unsigned long long)f, fps);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutA(dc, 12, 12, txt, (int)strlen(txt));
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SYSKEYDOWN:   // Alt+Entrée : plein écran sans bordure ↔ fenêtré
        if (wp == VK_RETURN && (lp & (1 << 29))) {
            set_borderless(h, !g_borderless);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (wp == VK_F11) {
            set_borderless(h, !g_borderless);
            return 0;
        }
        break;
    case WM_DPICHANGED: {   // fenêtre passée sur un écran à une autre mise à l'échelle (compatible DPI par écran)
        const RECT* r = (const RECT*)lp;
        if (g_borderless) {   // plein écran : couvrir le nouvel écran, pixel pour pixel
            MONITORINFO mi{sizeof mi};
            GetMonitorInfoW(MonitorFromRect(r, MONITOR_DEFAULTTONEAREST), &mi);
            SetWindowPos(h, nullptr, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOZORDER | SWP_NOACTIVATE);
        } else {   // fenêtré : taille apparente gardée (rectangle proposé par Windows)
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        HLOG("gfx", "window moved to a screen with %u %% display scaling", (unsigned)(LOWORD(wp) * 100 / 96));
        return 0;
    }
    case WM_CLOSE:
        HLOG("gfx", "window closed: process exiting");
        vkr::save_caches();   // pipelines créés pendant la partie
        fflush(nullptr);
        ExitProcess(0);
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void window_thread() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    // icône du jeu, tirée de la copie du joueur et intégrée à l'exécutable à l'édition de liens (link.py)
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    wc.lpszClassName = L"recomp_main";
    RegisterClassExW(&wc);
    {   // fenêtré : taille apparente d'avant la compatibilité DPI (2/3 de la sortie, en points), passée en pixels réels.
        // À 100 %, rien ne change : trop grande pour l'écran, Windows la réduit à sa création, comme avant (2560×1421 sur
        // un écran 1440p). Sur un écran mis à l'échelle, Windows ne réduisait que sa hauteur (3840×1421 sur un écran
        // 2560×1440 à 150 %, plus large que l'écran) : elle tient maintenant dans la zone de travail, cadre compris,
        // proportions gardées.
        const double e = echelle_ecran();
        if (e != 1.0) {
            g_w = (int)lround(g_w * e);
            g_h = (int)lround(g_h * e);
            RECT zt{}, cadre{0, 0, 0, 0};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &zt, 0);
            AdjustWindowRectExForDpi(&cadre, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForSystem());
            const int mw = (zt.right - zt.left) - (cadre.right - cadre.left), mh = (zt.bottom - zt.top) - (cadre.bottom - cadre.top);
            if (mw > 0 && mh > 0 && (g_w > mw || g_h > mh)) {
                const double r = std::min((double)mw / g_w, (double)mh / g_h);
                g_w = (int)lround(g_w * r);
                g_h = (int)lround(g_h * r);
            }
        }
        HLOG("gfx", "host window %dx%d (Windows display scaling %.0f %%)", g_w, g_h, e * 100);
    }
    RECT r{0, 0, g_w, g_h};
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForSystem());
    g_hwnd = CreateWindowW(L"recomp_main", L"StarFox Remake [recompiled]", WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr,
                           nullptr, inst, nullptr);
    // RECOMP_WINDOW_HIDDEN=1 : la fenêtre existe (Vulkan y présente ses images) mais n'est jamais montrée, ni dans la
    // barre des tâches. Sert au relevé des shaders de la construction (launcher/preparer.py) : voir le jeu s'ouvrir
    // tout seul pendant la première construction déroutait (demande de l'utilisateur, 2026-11-11).
    static const bool cachee = getenv("RECOMP_WINDOW_HIDDEN") != nullptr;
    if (cachee) {
        HLOG("gfx", "window hidden (RECOMP_WINDOW_HIDDEN)");
    } else {
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
        if (settings().borderless) set_borderless(g_hwnd, true);
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

}  // namespace

void gfx_open_window(int width, int height) {
    std::call_once(g_once, [&] {
        // fenêtre à l'échelle 2/3 pour une sortie 1080p
        g_w = width >= 1920 ? width * 2 / 3 : width;
        g_h = width >= 1920 ? height * 2 / 3 : height;
        g_last = g_fps_t0 = std::chrono::steady_clock::now();
        std::thread(window_thread).detach();   // taille en pixels réels : window_thread
    });
}

void* gfx_window_handle() { return g_hwnd; }

void gfx_status(const char* text) {
    char title[160];
    snprintf(title, sizeof title, "StarFox Remake [recompiled]  -  %s", text);
    if (g_hwnd) SetWindowTextA(g_hwnd, title);
}

void gfx_screen_size(int* w, int* h) {
    // en points (pixels réels ÷ mise à l'échelle de Windows) : resolution = auto garde le choix d'avant la compatibilité
    // DPI, par exemple 1080p pour un écran 4K à 200 %, plutôt qu'un rendu plus lourd après une mise à jour
    const double k = echelle_ecran();
    *w = (int)lround(GetSystemMetrics(SM_CXSCREEN) / k);
    *h = (int)lround(GetSystemMetrics(SM_CYSCREEN) / k);
}

void gfx_count_frame(uint64_t frame) {
    using namespace std::chrono;
    g_vulkan = true;
    auto now = steady_clock::now();
    std::lock_guard<std::mutex> lk(g_lk);
    g_frame = frame;
    double dt = duration<double>(now - g_fps_t0).count();
    if (dt >= 1.0) {
        g_fps = (frame - g_fps_f0) / dt;
        g_fps_t0 = now;
        g_fps_f0 = frame;
        static int n = 0;
        if (++n % 10 == 0) HLOG("gfx", "%.1f fps (frame %llu, Vulkan)", g_fps, (unsigned long long)frame);
        char title[96];
        snprintf(title, sizeof title, "StarFox Remake [recompiled]  -  %.0f fps", g_fps);
        if (g_hwnd) SetWindowTextA(g_hwnd, title);
    }
}

bool gfx_window_focused() {
    HWND f = GetForegroundWindow();
    return g_hwnd && f == g_hwnd;
}

void gfx_present(const float rgba[4], uint64_t frame) {
    using namespace std::chrono;
    auto now = steady_clock::now();
    {
        std::lock_guard<std::mutex> lk(g_lk);
        memcpy(g_color, rgba, sizeof g_color);
        g_frame = frame;
        double dt = duration<double>(now - g_fps_t0).count();
        if (dt >= 1.0) {
            g_fps = (frame - g_fps_f0) / dt;
            g_fps_t0 = now;
            g_fps_f0 = frame;
            static int n = 0;
            if (++n % 10 == 0) HLOG("gfx", "%.1f fps (frame %llu)", g_fps, (unsigned long long)frame);
        }
    }
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
    // synchro verticale simulée : au plus 60 images par seconde, attente au minuteur haute
    // résolution (précis à ~0,5 ms quelle que soit la résolution du minuteur système)
    static const bool novsync = getenv("RECOMP_NOVSYNC") != nullptr;   // diagnostic
    auto next = g_last + microseconds(16667);
    if (now < next && !novsync) {
        thread_local HANDLE tm = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                                        TIMER_ALL_ACCESS);
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(duration_cast<nanoseconds>(next - now).count() / 100);
        if (tm && SetWaitableTimer(tm, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(tm, INFINITE);
        else std::this_thread::sleep_until(next);
    }
    // pas de rattrapage en rafale après un retard : la cadence repart de maintenant
    g_last = std::max(next, now);
}

}  // namespace hos
#else  // !_WIN32
// ---------------------------------------------------------------- Linux : fenêtre SDL3
// La vidéo de SDL est initialisée et ses événements traités sur le fil principal du processus (gfx_boucle_principale,
// appelée par run_main_thread pendant que le jeu tourne sur son propre fil) : c'est ce que SDL demande sous X11 et
// Wayland. Les autres fils lui passent leurs demandes (ouverture de la fenêtre, titre, plein écran) par un événement
// utilisateur. La surface Vulkan est créée par SDL (gfx_vk_surface), sur le fil du rendu.
#include "gfx.h"
#include "settings.h"
#include "plat_linux.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>

// Icône du jeu (JPEG tiré de la copie du joueur par preparer.py) : décodée par stb_image (domaine public / MIT,
// third_party/stb_image.h, v2.32), JPEG seulement.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_NO_LINEAR
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "stb_image.h"
#pragma GCC diagnostic pop

namespace hos {
namespace vkr {
void save_caches();   // vk_render.cpp : écrit pipelines.cache
}
namespace {

SDL_Surface* g_icone_jeu = nullptr;   // icône du jeu (fenêtre), nullptr sans elle

// Icône du jeu : build/full/icone_jeu.jpg, à côté de recomp_main (preparer.py, tirée de votre copie du jeu ; elle
// reste sur ce PC). C'est l'icône de recomp_main.exe sous Windows. nullptr si elle manque ou ne se décode pas.
SDL_Surface* icone_jeu() {
    const char* base = SDL_GetBasePath();
    if (!base) return nullptr;
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load((std::string(base) + "icone_jeu.jpg").c_str(), &w, &h, &n, 4);
    if (!px) return nullptr;
    SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    if (s)
        for (int y = 0; y < h; y++)
            memcpy((uint8_t*)s->pixels + (size_t)y * s->pitch, px + (size_t)y * w * 4, (size_t)w * 4);
    stbi_image_free(px);
    return s;
}

bool lire_fichier(const std::string& p, std::string& out) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    char b[65536];
    size_t n;
    out.clear();
    while ((n = fread(b, 1, sizeof b, f)) > 0) out.append(b, n);
    fclose(f);
    return true;
}
bool ecrire_si_change(const std::string& p, const std::string& contenu) {   // vrai si le fichier a changé
    std::string actuel;
    if (lire_fichier(p, actuel) && actuel == contenu) return false;
    const std::string tmp = p + ".new";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = fwrite(contenu.data(), 1, contenu.size(), f) == contenu.size();
    if (fclose(f) != 0 || !ok || rename(tmp.c_str(), p.c_str()) != 0) {
        remove(tmp.c_str());
        return false;
    }
    return true;
}
void creer_dossiers(const std::string& d) {   // mkdir -p
    for (size_t i = 1; i <= d.size(); i++)
        if (i == d.size() || d[i] == '/') mkdir(d.substr(0, i).c_str(), 0755);
}

// Entrée de bureau du jeu (AppImage seulement, comme celle du launcher posée par AppRun) : GNOME (Wayland) ne prend
// l'icône d'une fenêtre que dans le .desktop au nom de son identifiant ; sans lui, un engrenage dans le dock. Entrée
// cachée du menu (NoDisplay : le menu lance le launcher), refaite si l'icône ou la place de l'AppImage ont changé,
// retirée par DÉSINSTALLER (desinstaller_linux.py). Jamais bloquante.
void integrer_bureau(SDL_Surface* icone) {
    const char* appimage = getenv("APPIMAGE");
    if (!appimage || !*appimage) return;
    const char* xdg = getenv("XDG_DATA_HOME");
    const char* home = getenv("HOME");
    std::string donnees;
    if (xdg && *xdg) donnees = xdg;
    else if (home && *home) donnees = std::string(home) + "/.local/share";
    else return;
    const std::string theme = donnees + "/icons/hicolor", apps = donnees + "/applications";
    const std::string png = theme + "/256x256/apps/spaceanimalsrecomp-game.png";
    creer_dossiers(theme + "/256x256/apps");
    creer_dossiers(apps);
    // icône : PNG 256 × 256 (taille du dossier hicolor)
    SDL_Surface* s = icone->w == 256 && icone->h == 256 ? icone : SDL_ScaleSurface(icone, 256, 256, SDL_SCALEMODE_LINEAR);
    if (s) {
        const std::string tmp = png + ".tmp";
        std::string donnees_png;
        if (SDL_SavePNG(s, tmp.c_str()) && lire_fichier(tmp, donnees_png) && ecrire_si_change(png, donnees_png)) {
            // cache d'icônes plus ancien que l'icône : rendu périmé (GTK ne s'y fie que s'il est plus récent que le
            // dossier hicolor, que l'ajout d'une icône dans un sous-dossier ne date pas ; voir AppRun)
            struct stat c{};
            if (stat((theme + "/icon-theme.cache").c_str(), &c) == 0) utimensat(AT_FDCWD, theme.c_str(), nullptr, 0);
        }
        remove(tmp.c_str());
        if (s != icone) SDL_DestroySurface(s);
    }
    // Exec : chemin entre guillemets, échappé selon la spécification Desktop Entry (comme AppRun)
    std::string exec;
    for (const char* c = appimage; *c; c++) {
        if (*c == '\\') exec += "\\\\\\\\";
        else if (*c == '"' || *c == '`' || *c == '$') exec += "\\\\", exec += *c;
        else if (*c == '%') exec += "%%";
        else exec += *c;
    }
    ecrire_si_change(apps + "/spaceanimalsrecomp-game.desktop",
                     "[Desktop Entry]\n"
                     "Type=Application\n"
                     "Name=StarFox Remake [recompiled]\n"
                     "Comment=Native port of Star Fox (Switch 2), built on your PC from your own copy\n"
                     "Exec=\"" + exec + "\"\n"
                     "Icon=spaceanimalsrecomp-game\n"
                     "StartupWMClass=spaceanimalsrecomp-game\n"
                     "Categories=Game;\n"
                     "NoDisplay=true\n"
                     "Terminal=false\n");
}

std::mutex g_lk;
std::condition_variable g_cv;
bool g_video = false, g_video_echec = false;   // SDL_INIT_VIDEO fait (ou refusé)
std::atomic<SDL_Window*> g_win{nullptr};
std::atomic<bool> g_vulkan{false}, g_focus{false};
bool g_demande = false;   // ouverture de la fenêtre demandée
std::string g_titre;      // titre à poser (fil principal)
bool g_titre_neuf = false;
uint32_t g_ev_reveil = 0;   // événement utilisateur : une demande attend le fil principal
float g_color[4] = {0, 0, 0, 1};
uint64_t g_frame = 0;
int g_w = 1280, g_h = 720;
std::chrono::steady_clock::time_point g_last, g_fps_t0;
uint64_t g_fps_f0 = 0;
double g_fps = 0;
bool g_borderless = false;

void reveiller() {
    if (!g_ev_reveil) return;
    SDL_Event e{};
    e.type = g_ev_reveil;
    SDL_PushEvent(&e);
}

void set_borderless(SDL_Window* w, bool on) {
    if (on == g_borderless) return;
    SDL_SetWindowFullscreenMode(w, nullptr);   // plein écran « bureau » : sans changement de mode de l'écran
    SDL_SetWindowFullscreen(w, on);
    g_borderless = on;
    HLOG("gfx", "window: %s", on ? "borderless fullscreen" : "windowed");
}

void creer_fenetre() {   // fil principal
    static const bool cachee = getenv("RECOMP_WINDOW_HIDDEN") != nullptr;
    SDL_WindowFlags f = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (cachee) f |= SDL_WINDOW_HIDDEN;
    // fenêtré : la fenêtre tient dans la zone utilisable de l'écran, barre de titre comprise, proportions gardées
    // (comme sous Windows, window_thread). Pour une sortie 3840×2160 elle mesurait 2560×1440, plus que l'écran 1080p :
    // GNOME la ramenait à la taille de l'écran (on aurait dit du plein écran), et sous Wayland l'image disparaissait.
    // Tailles en points (l'échelle de l'écran est appliquée par SDL : SDL_WINDOW_HIGH_PIXEL_DENSITY).
    {
        SDL_Rect zt{};
        if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &zt) && zt.w > 0 && zt.h > 0) {
            const int cadre_h = 48, marge = 16;   // barre de titre du bureau (libdecor, GNOME) et bords
            const int mw = zt.w - marge, mh = zt.h - cadre_h - marge;
            if (mw > 0 && mh > 0 && (g_w > mw || g_h > mh)) {
                const double r = std::min((double)mw / g_w, (double)mh / g_h);
                g_w = (int)lround(g_w * r);
                g_h = (int)lround(g_h * r);
            }
        }
    }
    SDL_Window* w = SDL_CreateWindow("StarFox Remake [recompiled]", g_w, g_h, f);
    if (!w) {
        HLOG("gfx", "SDL_CreateWindow failed: %s", SDL_GetError());
        return;
    }
    HLOG("gfx", "host window %dx%d (SDL %s video driver)", g_w, g_h, SDL_GetCurrentVideoDriver());
    if (cachee) HLOG("gfx", "window hidden (RECOMP_WINDOW_HIDDEN)");
    else if (settings().borderless) set_borderless(w, true);
    // icône de la fenêtre : celle du jeu (icone_jeu, comme recomp_main.exe sous Windows), à défaut lanceur.png de
    // l'installation (recomp_main est dans <installation>/recomp/build/full). X11 et KDE la montrent ; GNOME prend
    // celle du .desktop au nom de l'identifiant de la fenêtre (integrer_bureau, ou spaceanimalsrecomp d'AppRun)
    if (g_icone_jeu) {
        SDL_SetWindowIcon(w, g_icone_jeu);
    } else if (const char* base = SDL_GetBasePath()) {
        if (SDL_Surface* icone = SDL_LoadPNG((std::string(base) + "../../../lanceur.png").c_str())) {
            SDL_SetWindowIcon(w, icone);
            SDL_DestroySurface(icone);
        }
    }
    g_focus = (SDL_GetWindowFlags(w) & SDL_WINDOW_INPUT_FOCUS) != 0;
    g_win = w;
}

}  // namespace

// Fil principal : vidéo de SDL et boucle d'événements jusqu'à la fin du jeu (fini(arg) vrai)
void gfx_boucle_principale(bool (*fini)(void*), void* arg) {
    SDL_SetHint(SDL_HINT_APP_NAME, "StarFox Remake [recompiled]");
    // Le jeu a son icône et son identifiant (spaceanimalsrecomp-game), le launcher les siens (spaceanimalsrecomp) :
    // le dock montre l'icône de l'application active, comme la barre des tâches de Windows. Sans icône du jeu,
    // l'identifiant du launcher (son entrée de menu existe, pas d'engrenage dans le dock).
    g_icone_jeu = icone_jeu();
    if (g_icone_jeu) integrer_bureau(g_icone_jeu);
    SDL_SetHint(SDL_HINT_APP_ID, g_icone_jeu ? "spaceanimalsrecomp-game" : "spaceanimalsrecomp");
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "0");
    const bool ok = SDL_InitSubSystem(SDL_INIT_VIDEO) && SDL_Vulkan_LoadLibrary(nullptr);
    {
        std::lock_guard<std::mutex> lk(g_lk);
        g_video = ok;
        g_video_echec = !ok;
        if (ok) g_ev_reveil = SDL_RegisterEvents(1);
    }
    g_cv.notify_all();
    if (!ok) {
        HLOG("gfx", "SDL video unavailable (%s): no window", SDL_GetError());
        while (!fini(arg)) Sleep(50);
        return;
    }
    while (!fini(arg)) {
        {
            std::unique_lock<std::mutex> lk(g_lk);
            if (g_demande && !g_win.load()) {
                g_demande = false;
                lk.unlock();
                creer_fenetre();
                g_cv.notify_all();
                lk.lock();
            }
            if (g_titre_neuf && g_win.load()) {
                g_titre_neuf = false;
                SDL_SetWindowTitle(g_win.load(), g_titre.c_str());
            }
        }
        SDL_Event e;
        if (!SDL_WaitEventTimeout(&e, 50)) continue;
        do {
            SDL_Window* w = g_win.load();
            switch (e.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                HLOG("gfx", "window closed: process exiting");
                vkr::save_caches();   // pipelines créés pendant la partie
                fflush(nullptr);
                _exit(0);
            case SDL_EVENT_WINDOW_FOCUS_GAINED: g_focus = true; break;
            case SDL_EVENT_WINDOW_FOCUS_LOST: g_focus = false; break;
            case SDL_EVENT_KEY_DOWN:   // Alt+Entrée, F11 : plein écran sans bordure ↔ fenêtré
                if (w && !e.key.repeat &&
                    (e.key.scancode == SDL_SCANCODE_F11 ||
                     ((e.key.scancode == SDL_SCANCODE_RETURN || e.key.scancode == SDL_SCANCODE_KP_ENTER) &&
                      (e.key.mod & SDL_KMOD_ALT))))
                    set_borderless(w, !g_borderless);
                break;
            default: break;
            }
        } while (SDL_PollEvent(&e));
    }
}

void gfx_open_window(int width, int height) {
    std::unique_lock<std::mutex> lk(g_lk);
    if (g_demande || g_win.load()) return;
    // fenêtre à l'échelle 2/3 pour une sortie 1080p
    g_w = width >= 1920 ? width * 2 / 3 : width;
    g_h = width >= 1920 ? height * 2 / 3 : height;
    g_last = g_fps_t0 = std::chrono::steady_clock::now();
    g_cv.wait(lk, [] { return g_video || g_video_echec; });
    if (!g_video) return;
    g_demande = true;
    lk.unlock();
    reveiller();
}

void* gfx_window_handle() { return g_win.load(); }

bool gfx_vk_extensions(std::vector<const char*>& ext) {
    Uint32 n = 0;
    const char* const* e = SDL_Vulkan_GetInstanceExtensions(&n);
    if (!e) return false;
    for (Uint32 i = 0; i < n; i++) ext.push_back(e[i]);
    return n > 0;
}

bool gfx_vk_surface(void* instance, void*, uint64_t* surface) {
    VkSurfaceKHR s = 0;
    if (!g_win.load() || !SDL_Vulkan_CreateSurface(g_win.load(), (VkInstance)instance, nullptr, &s)) return false;
    *surface = (uint64_t)s;
    return true;
}

void gfx_window_pixels(int* w, int* h) {
    *w = *h = 0;
    if (SDL_Window* win = g_win.load()) SDL_GetWindowSizeInPixels(win, w, h);
}

void gfx_status(const char* text) {
    char title[160];
    snprintf(title, sizeof title, "StarFox Remake [recompiled]  -  %s", text);
    {
        std::lock_guard<std::mutex> lk(g_lk);
        g_titre = title;
        g_titre_neuf = true;
    }
    reveiller();
}

void gfx_screen_size(int* w, int* h) {
    *w = 1920, *h = 1080;
    {
        std::unique_lock<std::mutex> lk(g_lk);
        g_cv.wait(lk, [] { return g_video || g_video_echec; });
        if (!g_video) return;
    }
    // en points (pixels ÷ mise à l'échelle de l'écran), comme sous Windows
    const SDL_DisplayID d = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* m = d ? SDL_GetDesktopDisplayMode(d) : nullptr;
    if (!m) return;
    float k = SDL_GetDisplayContentScale(d);
    if (!(k >= 0.5f)) k = 1.0f;
    if (const char* e = getenv("RECOMP_DPI")) k = (float)atoi(e) / 96.0f;
    *w = (int)lround(m->w * m->pixel_density / k);
    *h = (int)lround(m->h * m->pixel_density / k);
}

void gfx_count_frame(uint64_t frame) {
    using namespace std::chrono;
    g_vulkan = true;
    auto now = steady_clock::now();
    bool titre = false;
    {
        std::lock_guard<std::mutex> lk(g_lk);
        g_frame = frame;
        double dt = duration<double>(now - g_fps_t0).count();
        if (dt >= 1.0) {
            g_fps = (frame - g_fps_f0) / dt;
            g_fps_t0 = now;
            g_fps_f0 = frame;
            static int n = 0;
            if (++n % 10 == 0) HLOG("gfx", "%.1f fps (frame %llu, Vulkan)", g_fps, (unsigned long long)frame);
            char t[96];
            snprintf(t, sizeof t, "StarFox Remake [recompiled]  -  %.0f fps", g_fps);
            g_titre = t;
            g_titre_neuf = titre = true;
        }
    }
    if (titre) reveiller();
}

bool gfx_window_focused() { return g_win.load() && g_focus.load(); }

void gfx_present(const float rgba[4], uint64_t frame) {   // sans Vulkan : cadence seule (rien n'est dessiné)
    using namespace std::chrono;
    auto now = steady_clock::now();
    {
        std::lock_guard<std::mutex> lk(g_lk);
        memcpy(g_color, rgba, sizeof g_color);
        g_frame = frame;
    }
    static const bool novsync = getenv("RECOMP_NOVSYNC") != nullptr;   // diagnostic
    auto next = g_last + microseconds(16667);
    if (now < next && !novsync) plat_sleep_ns(duration_cast<nanoseconds>(next - now).count());
    g_last = std::max(next, now);
}

}  // namespace hos
#endif  // _WIN32

// Launcher sous Linux : réalisation de win32_linux.h sur POSIX et SDL3. Voir l'en-tête.
#include "win32_linux.h"

#include "../runtime/os/scancodes.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <thread>

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char** environ;

// ---------------------------------------------------------------- objets
struct PlatObjet {
    enum Type { FICHIER, TUBE, PROCESSUS, FIL, TRAVAIL, RECHERCHE } type;
    int fd = -1;
    pid_t pid = 0;
    std::mutex m;
    bool fini = false;
    int code = 0;
    pid_t groupe = 0;          // TRAVAIL : groupe de processus
    bool tuer_a_la_fin = false;
    std::vector<std::string> noms;   // RECHERCHE
    size_t i = 0;
    explicit PlatObjet(Type t) : type(t) {}
};

static thread_local DWORD t_erreur = 0;
static void erreur() { t_erreur = (DWORD)errno; }
DWORD GetLastError() { return t_erreur; }

// ---------------------------------------------------------------- chaînes
static std::string u8(const wchar_t* w, size_t n) {
    std::string o;
    o.reserve(n);
    for (size_t i = 0; i < n; i++) {
        uint32_t c = (uint32_t)w[i];
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) o += (char)(0xC0 | c >> 6), o += (char)(0x80 | (c & 63));
        else if (c < 0x10000) o += (char)(0xE0 | c >> 12), o += (char)(0x80 | ((c >> 6) & 63)), o += (char)(0x80 | (c & 63));
        else o += (char)(0xF0 | c >> 18), o += (char)(0x80 | ((c >> 12) & 63)), o += (char)(0x80 | ((c >> 6) & 63)), o += (char)(0x80 | (c & 63));
    }
    return o;
}
static std::string u8(const wchar_t* w) { return w ? u8(w, wcslen(w)) : std::string(); }
static std::string u8(const std::wstring& w) { return u8(w.c_str(), w.size()); }
static std::wstring large(const char* s, size_t n) {
    std::wstring o;
    o.reserve(n);
    for (size_t i = 0; i < n;) {
        const unsigned char c = (unsigned char)s[i];
        uint32_t v;
        int k;
        if (c < 0x80) v = c, k = 0;
        else if ((c & 0xE0) == 0xC0) v = c & 0x1F, k = 1;
        else if ((c & 0xF0) == 0xE0) v = c & 0x0F, k = 2;
        else if ((c & 0xF8) == 0xF0) v = c & 0x07, k = 3;
        else v = 0xFFFD, k = 0;
        i++;
        for (int j = 0; j < k && i < n && ((unsigned char)s[i] & 0xC0) == 0x80; j++, i++) v = v << 6 | ((unsigned char)s[i] & 63);
        o += (wchar_t)v;
    }
    return o;
}
static std::wstring large(const std::string& s) { return large(s.data(), s.size()); }
static std::wstring large(const char* s) { return s ? large(s, strlen(s)) : std::wstring(); }

std::string plat_chemin(const std::wstring& w) {
    std::string s = u8(w);
    for (char& c : s)
        if (c == '\\') c = '/';
    // « //x » (dossier + "\\x" quand le dossier finit déjà par « / ») : une seule barre
    std::string o;
    for (char c : s)
        if (!(c == '/' && !o.empty() && o.back() == '/')) o += c;
    return o;
}
std::string plat_chemin(const wchar_t* w) { return plat_chemin(std::wstring(w ? w : L"")); }

int WideCharToMultiByte(UINT, DWORD, const wchar_t* w, int nw, char* s, int ns, const char*, BOOL*) {
    const size_t n = nw < 0 ? wcslen(w) + 1 : (size_t)nw;
    const std::string o = u8(w, n);
    if (!s || !ns) return (int)o.size();
    const int k = std::min<int>((int)o.size(), ns);
    memcpy(s, o.data(), (size_t)k);
    return k;
}
int MultiByteToWideChar(UINT, DWORD, const char* s, int ns, wchar_t* w, int nw) {
    const size_t n = ns < 0 ? strlen(s) + 1 : (size_t)ns;
    const std::wstring o = large(s, n);
    if (!w || !nw) return (int)o.size();
    const int k = std::min<int>((int)o.size(), nw);
    wmemcpy(w, o.data(), (size_t)k);
    return k;
}

static std::mutex g_env_lk;
static std::map<std::wstring, std::wstring> g_env;   // copies larges (adresses stables)
const wchar_t* _wgetenv(const wchar_t* nom) {
    const char* v = getenv(u8(nom).c_str());
    if (!v) return nullptr;
    std::lock_guard<std::mutex> l(g_env_lk);
    std::wstring& w = g_env[nom];
    const std::wstring nv = large(v);
    if (w != nv) w = nv;   // (une valeur changée : nouvelle chaîne ; l'ancienne peut avoir été gardée par l'appelant)
    return w.c_str();
}
BOOL SetEnvironmentVariableW(const wchar_t* nom, const wchar_t* v) {
    return (v ? setenv(u8(nom).c_str(), u8(v).c_str(), 1) : unsetenv(u8(nom).c_str())) == 0;
}
wchar_t* GetEnvironmentStringsW() {
    std::wstring b;
    for (char** e = environ; *e; e++) b += large(*e), b += L'\0';
    b += L'\0';
    wchar_t* r = (wchar_t*)malloc(b.size() * sizeof(wchar_t));
    wmemcpy(r, b.data(), b.size());
    return r;
}
BOOL FreeEnvironmentStringsW(wchar_t* e) {
    free(e);
    return TRUE;
}

FILE* _wfopen(const wchar_t* chemin, const wchar_t* mode) {
    FILE* f = fopen(plat_chemin(chemin).c_str(), u8(mode).c_str());
    if (!f) erreur();
    return f;
}

// ---------------------------------------------------------------- système
ULONGLONG GetTickCount64() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (ULONGLONG)t.tv_sec * 1000 + (ULONGLONG)t.tv_nsec / 1000000;
}
void Sleep(DWORD ms) { usleep((useconds_t)ms * 1000); }
void GetLocalTime(SYSTEMTIME* s) {
    timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    tm l;
    localtime_r(&t.tv_sec, &l);
    *s = {(WORD)(l.tm_year + 1900), (WORD)(l.tm_mon + 1), (WORD)l.tm_wday, (WORD)l.tm_mday, (WORD)l.tm_hour,
          (WORD)l.tm_min, (WORD)l.tm_sec, (WORD)(t.tv_nsec / 1000000)};
}
void GetSystemInfo(SYSTEM_INFO* s) { s->dwNumberOfProcessors = (DWORD)std::max(1L, sysconf(_SC_NPROCESSORS_ONLN)); }
BOOL GlobalMemoryStatusEx(MEMORYSTATUSEX* m) {
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) return FALSE;
    std::map<std::string, ULONGLONG> v;
    char k[64];
    unsigned long long n;
    while (fscanf(f, "%63[^:]: %llu kB\n", k, &n) == 2) v[k] = n * 1024;
    fclose(f);
    m->ullTotalPhys = v["MemTotal"];
    m->ullAvailPhys = v["MemAvailable"];
    // Linux surengage : « validable » = disponible + échange libre (pas de limite d'engagement stricte)
    m->ullTotalPageFile = v["MemTotal"] + v["SwapTotal"];
    m->ullAvailPageFile = v["MemAvailable"] + v["SwapFree"];
    m->dwMemoryLoad = m->ullTotalPhys ? (DWORD)(100 - m->ullAvailPhys * 100 / m->ullTotalPhys) : 0;
    return TRUE;
}
WORD GetUserDefaultUILanguage() {
    for (const char* n : {"LC_ALL", "LC_MESSAGES", "LANG", "LANGUAGE"})
        if (const char* v = getenv(n); v && *v && strcmp(v, "C") && strcmp(v, "POSIX")) return !strncmp(v, "fr", 2) ? 0x040c : 0x0409;
    return 0x0409;
}
DWORD GetCurrentProcessId() { return (DWORD)getpid(); }
DWORD GetModuleFileNameW(HMODULE, wchar_t* b, DWORD n) {
    char p[4096];
    const ssize_t k = readlink("/proc/self/exe", p, sizeof p - 1);
    if (k <= 0) return 0;
    p[k] = 0;
    const std::wstring w = large(p);
    const size_t c = std::min<size_t>(w.size(), n - 1);
    wmemcpy(b, w.data(), c);
    b[c] = 0;
    return (DWORD)c;
}
static std::string normaliser(std::string p) {   // absolu, sans « . » ni « .. »
    if (p.empty() || p[0] != '/') {
        char c[4096];
        if (getcwd(c, sizeof c)) p = std::string(c) + "/" + p;
    }
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= p.size()) {
        size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        const std::string s = p.substr(i, j - i);
        if (s == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!s.empty() && s != ".") {
            parts.push_back(s);
        }
        i = j + 1;
    }
    std::string o;
    for (const auto& s : parts) o += "/" + s;
    return o.empty() ? "/" : o;
}
DWORD GetFullPathNameW(const wchar_t* p, DWORD n, wchar_t* b, wchar_t**) {
    const std::wstring w = large(normaliser(plat_chemin(p)));
    if (w.size() + 1 > n) return (DWORD)w.size() + 1;
    wmemcpy(b, w.c_str(), w.size() + 1);
    return (DWORD)w.size();
}
DWORD GetTempPathW(DWORD n, wchar_t* b) {
    const char* t = getenv("TMPDIR");
    std::wstring w = large(t && *t ? t : "/tmp");
    if (w.back() != L'/') w += L'/';
    if (w.size() + 1 > n) return 0;
    wmemcpy(b, w.c_str(), w.size() + 1);
    return (DWORD)w.size();
}
DWORD SearchPathW(const wchar_t*, const wchar_t* nom, const wchar_t*, DWORD n, wchar_t* b, wchar_t**) {
    const char* path = getenv("PATH");
    if (!path) return 0;
    const std::string nm = u8(nom), ps = path;
    size_t i = 0;
    while (i <= ps.size()) {
        size_t j = ps.find(':', i);
        if (j == std::string::npos) j = ps.size();
        const std::string c = ps.substr(i, j - i) + "/" + nm;
        if (j > i && access(c.c_str(), X_OK) == 0) {
            const std::wstring w = large(c);
            if (w.size() + 1 > n) return 0;
            wmemcpy(b, w.c_str(), w.size() + 1);
            return (DWORD)w.size();
        }
        i = j + 1;
    }
    return 0;
}
static std::string nom_bibliotheque(const wchar_t* nom) {
    std::string s = u8(nom);
    if (s == "vulkan-1.dll") return "libvulkan.so.1";
    return plat_chemin(nom);
}
HMODULE LoadLibraryW(const wchar_t* nom) {
    void* h = dlopen(nom_bibliotheque(nom).c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h && u8(nom) == "vulkan-1.dll") h = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    return h;
}
HMODULE LoadLibraryExW(const wchar_t* nom, HANDLE, DWORD) { return LoadLibraryW(nom); }
void* GetProcAddress(HMODULE m, const char* nom) { return dlsym(m ? m : RTLD_DEFAULT, nom); }
BOOL FreeLibrary(HMODULE m) { return m && m != RTLD_DEFAULT ? dlclose(m) == 0 : TRUE; }

// ---------------------------------------------------------------- fichiers
static FILETIME ft(const timespec& t) {
    const ULONGLONG v = ((ULONGLONG)t.tv_sec + 11644473600ull) * 10000000ull + (ULONGLONG)t.tv_nsec / 100;
    return {(DWORD)(uint32_t)v, (DWORD)(uint32_t)(v >> 32)};
}
DWORD GetFileAttributesW(const wchar_t* p) {
    struct stat st;
    if (stat(plat_chemin(p).c_str(), &st) != 0) return erreur(), INVALID_FILE_ATTRIBUTES;
    return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}
BOOL GetFileAttributesExW(const wchar_t* p, GET_FILEEX_INFO_LEVELS, void* d) {
    struct stat st;
    if (stat(plat_chemin(p).c_str(), &st) != 0) return erreur(), FALSE;
    auto* a = (WIN32_FILE_ATTRIBUTE_DATA*)d;
    a->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    a->ftCreationTime = a->ftLastWriteTime = ft(st.st_mtim);
    a->ftLastAccessTime = ft(st.st_atim);
    a->nFileSizeHigh = (DWORD)(uint32_t)((uint64_t)st.st_size >> 32);
    a->nFileSizeLow = (DWORD)(uint32_t)st.st_size;
    return TRUE;
}
BOOL MoveFileExW(const wchar_t* de, const wchar_t* vers, DWORD) {
    if (rename(plat_chemin(de).c_str(), plat_chemin(vers).c_str()) != 0) return erreur(), FALSE;
    return TRUE;
}
BOOL DeleteFileW(const wchar_t* p) {
    if (unlink(plat_chemin(p).c_str()) != 0) return erreur(), FALSE;
    return TRUE;
}
BOOL CreateDirectoryW(const wchar_t* p, void*) {
    if (mkdir(plat_chemin(p).c_str(), 0755) != 0) return erreur(), FALSE;
    return TRUE;
}
int SHCreateDirectoryExW(HWND, const wchar_t* p, void*) {
    const std::string s = plat_chemin(p);
    for (size_t i = 1; i <= s.size(); i++)
        if (i == s.size() || s[i] == '/') mkdir(s.substr(0, i).c_str(), 0755);
    return 0;
}
// Joker dans le dernier élément seulement, comme Windows : `*` et `?`, rien d'autre. Le dossier est ouvert tel quel
// (opendir) : avec glob() sur tout le chemin, un dossier dont le nom contient [ ] * ? (« Star Fox [EU] ») ne se
// trouvait plus (copie du jeu introuvable, shaders jamais comptés). [ ] et \ du motif sont échappés pour fnmatch, où
// ils ont un sens qu'ils n'ont pas sous Windows. Comme glob() avant : noms triés, `*` ne prend pas les noms cachés.
HANDLE FindFirstFileW(const wchar_t* motif, WIN32_FIND_DATAW* d) {
    const std::string m = plat_chemin(motif);
    const size_t k = m.find_last_of('/');
    const std::string dossier = k == std::string::npos ? std::string() : m.substr(0, k + 1);
    std::string nom;
    for (char c : k == std::string::npos ? m : m.substr(k + 1)) {
        if (c == '[' || c == ']' || c == '\\') nom += '\\';
        nom += c;
    }
    DIR* dir = opendir(dossier.empty() ? "." : dossier.c_str());
    if (!dir) return erreur(), INVALID_HANDLE_VALUE;
    std::vector<std::string> noms;
    while (dirent* e = readdir(dir))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..") && !fnmatch(nom.c_str(), e->d_name, FNM_PERIOD))
            noms.push_back(dossier + e->d_name);
    closedir(dir);
    if (noms.empty()) return INVALID_HANDLE_VALUE;
    std::sort(noms.begin(), noms.end());
    auto* h = new PlatObjet(PlatObjet::RECHERCHE);
    h->noms = std::move(noms);
    if (!FindNextFileW(h, d)) {
        delete h;
        return INVALID_HANDLE_VALUE;
    }
    return h;
}
BOOL FindNextFileW(HANDLE h, WIN32_FIND_DATAW* d) {
    if (!h || h == INVALID_HANDLE_VALUE || h->i >= h->noms.size()) return FALSE;
    const std::string& p = h->noms[h->i++];
    struct stat st{};
    stat(p.c_str(), &st);
    d->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    d->nFileSizeHigh = (DWORD)(uint32_t)((uint64_t)st.st_size >> 32);
    d->nFileSizeLow = (DWORD)(uint32_t)st.st_size;
    const std::wstring n = large(p.substr(p.find_last_of('/') + 1));
    const size_t k = std::min<size_t>(n.size(), sizeof d->cFileName / sizeof(wchar_t) - 1);
    wmemcpy(d->cFileName, n.data(), k);
    d->cFileName[k] = 0;
    return TRUE;
}
BOOL FindClose(HANDLE h) {
    if (h && h != INVALID_HANDLE_VALUE) delete h;
    return TRUE;
}
HANDLE CreateFileW(const wchar_t* p, DWORD acces, DWORD, SECURITY_ATTRIBUTES*, DWORD creation, DWORD, HANDLE) {
    int fl = O_CLOEXEC;
    fl |= (acces & GENERIC_WRITE) ? ((acces & GENERIC_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
    if (creation == CREATE_NEW) fl |= O_CREAT | O_EXCL;
    const int fd = open(plat_chemin(p).c_str(), fl, 0644);
    if (fd < 0) return erreur(), INVALID_HANDLE_VALUE;
    auto* h = new PlatObjet(PlatObjet::FICHIER);
    h->fd = fd;
    return h;
}
BOOL ReadFile(HANDLE h, void* b, DWORD n, DWORD* lu, void*) {
    if (!h || h == INVALID_HANDLE_VALUE) return FALSE;
    ssize_t k;
    do k = read(h->fd, b, n);
    while (k < 0 && errno == EINTR);
    if (k < 0) {
        erreur();
        if (lu) *lu = 0;
        return FALSE;
    }
    if (lu) *lu = (DWORD)k;
    return h->type == PlatObjet::TUBE ? k > 0 : TRUE;   // tube : fin (écrivain fermé) = échec, comme ERROR_BROKEN_PIPE
}
BOOL WriteFile(HANDLE h, const void* b, DWORD n, DWORD* ecrit, void*) {
    if (!h || h == INVALID_HANDLE_VALUE) return FALSE;
    const ssize_t k = write(h->fd, b, n);
    if (k < 0) return erreur(), FALSE;
    if (ecrit) *ecrit = (DWORD)k;
    return TRUE;
}
BOOL SetFilePointerEx(HANDLE h, LARGE_INTEGER d, LARGE_INTEGER* nouveau, DWORD origine) {
    const off_t r = lseek(h->fd, (off_t)d.QuadPart, origine == 1 ? SEEK_CUR : origine == 2 ? SEEK_END : SEEK_SET);
    if (r < 0) return erreur(), FALSE;
    if (nouveau) nouveau->QuadPart = r;
    return TRUE;
}
BOOL CloseHandle(HANDLE h) {
    if (!h || h == INVALID_HANDLE_VALUE) return FALSE;
    if (h->fd >= 0) close(h->fd);
    if (h->type == PlatObjet::TRAVAIL && h->tuer_a_la_fin && h->groupe > 0) kill(-h->groupe, SIGKILL);
    if (h->type == PlatObjet::PROCESSUS && !h->fini) {
        // processus laissé à lui-même : ramassé par un fil à part (pas de zombie)
        const pid_t pid = h->pid;
        std::thread([pid] { waitpid(pid, nullptr, 0); }).detach();
    }
    delete h;
    return TRUE;
}

// lanceur.ini : « [section] » et « clé=valeur » ; lu en UTF-8 (ou UTF-16 avec BOM : fichier d'avant), écrit en UTF-8
static std::mutex g_ini_lk;
static std::vector<std::string> ini_lignes(const std::string& f) {
    std::vector<std::string> v;
    FILE* x = fopen(f.c_str(), "rb");
    if (!x) return v;
    std::string t;
    char b[4096];
    size_t n;
    while ((n = fread(b, 1, sizeof b, x)) > 0) t.append(b, n);
    fclose(x);
    if (t.size() >= 2 && (unsigned char)t[0] == 0xFF && (unsigned char)t[1] == 0xFE) {   // UTF-16LE
        std::wstring w;
        for (size_t i = 2; i + 1 < t.size(); i += 2) w += (wchar_t)((unsigned char)t[i] | (unsigned char)t[i + 1] << 8);
        t = u8(w);
    } else if (t.size() >= 3 && !memcmp(t.data(), "\xEF\xBB\xBF", 3)) {
        t.erase(0, 3);
    }
    size_t i = 0;
    while (i < t.size()) {
        size_t j = t.find('\n', i);
        if (j == std::string::npos) j = t.size();
        std::string l = t.substr(i, j - i);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        v.push_back(l);
        i = j + 1;
    }
    return v;
}
static std::string sans_blancs(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
DWORD GetPrivateProfileStringW(const wchar_t* section, const wchar_t* cle, const wchar_t* def, wchar_t* b, DWORD n,
                               const wchar_t* fichier) {
    std::lock_guard<std::mutex> l(g_ini_lk);
    const std::string sec = u8(section), k = u8(cle);
    std::wstring v = def ? def : L"";
    bool dans = false;
    for (const std::string& li : ini_lignes(plat_chemin(fichier))) {
        const std::string t = sans_blancs(li);
        if (!t.empty() && t[0] == '[') {
            dans = strcasecmp(t.substr(1, t.find(']') - 1).c_str(), sec.c_str()) == 0;
            continue;
        }
        const size_t eq = t.find('=');
        if (dans && eq != std::string::npos && strcasecmp(sans_blancs(t.substr(0, eq)).c_str(), k.c_str()) == 0) {
            v = large(sans_blancs(t.substr(eq + 1)));
            break;
        }
    }
    const size_t c = std::min<size_t>(v.size(), n ? n - 1 : 0);
    wmemcpy(b, v.data(), c);
    b[c] = 0;
    return (DWORD)c;
}
BOOL WritePrivateProfileStringW(const wchar_t* section, const wchar_t* cle, const wchar_t* val, const wchar_t* fichier) {
    std::lock_guard<std::mutex> l(g_ini_lk);
    const std::string f = plat_chemin(fichier), sec = u8(section), k = u8(cle), v = val ? u8(val) : std::string();
    std::vector<std::string> li = ini_lignes(f);
    int debut = -1, fin = (int)li.size(), trouvee = -1;
    for (int i = 0; i < (int)li.size(); i++) {
        const std::string t = sans_blancs(li[i]);
        if (!t.empty() && t[0] == '[') {
            if (debut >= 0 && fin == (int)li.size()) fin = i;
            if (strcasecmp(t.substr(1, t.find(']') - 1).c_str(), sec.c_str()) == 0) debut = i, fin = (int)li.size();
            continue;
        }
        const size_t eq = t.find('=');
        if (debut >= 0 && fin == (int)li.size() && eq != std::string::npos &&
            strcasecmp(sans_blancs(t.substr(0, eq)).c_str(), k.c_str()) == 0)
            trouvee = i;
    }
    if (trouvee >= 0) {
        if (val) li[trouvee] = k + "=" + v;
        else li.erase(li.begin() + trouvee);
    } else if (val) {
        if (debut < 0) {
            li.push_back("[" + sec + "]");
            li.push_back(k + "=" + v);
        } else {
            int pos = fin;
            while (pos > debut + 1 && sans_blancs(li[pos - 1]).empty()) pos--;
            li.insert(li.begin() + pos, k + "=" + v);
        }
    }
    const std::string tmp = f + ".tmp";
    FILE* x = fopen(tmp.c_str(), "wb");
    if (!x) return erreur(), FALSE;
    for (const std::string& s : li) fprintf(x, "%s\n", s.c_str());
    fclose(x);
    return rename(tmp.c_str(), f.c_str()) == 0;
}

// ---------------------------------------------------------------- processus
BOOL CreatePipe(HANDLE* lecture, HANDLE* ecriture, SECURITY_ATTRIBUTES*, DWORD) {
    int p[2];
    if (pipe2(p, O_CLOEXEC) != 0) return erreur(), FALSE;
    *lecture = new PlatObjet(PlatObjet::TUBE);
    (*lecture)->fd = p[0];
    *ecriture = new PlatObjet(PlatObjet::TUBE);
    (*ecriture)->fd = p[1];
    return TRUE;
}
BOOL SetHandleInformation(HANDLE, DWORD, DWORD) { return TRUE; }   // (tout est O_CLOEXEC ; l'enfant reçoit ses tubes par dup2)

// ligne de commande Windows → arguments (règles de CommandLineToArgvW), « \ » des chemins → « / »
static std::vector<std::string> arguments(const std::wstring& cmd) {
    std::vector<std::wstring> a;
    size_t i = 0;
    const size_t n = cmd.size();
    while (i < n) {
        while (i < n && (cmd[i] == L' ' || cmd[i] == L'\t')) i++;
        if (i >= n) break;
        std::wstring s;
        bool guillemets = false;
        while (i < n && (guillemets || (cmd[i] != L' ' && cmd[i] != L'\t'))) {
            if (cmd[i] == L'\\') {
                size_t k = 0;
                while (i < n && cmd[i] == L'\\') k++, i++;
                if (i < n && cmd[i] == L'"') {
                    s.append(k / 2, L'\\');
                    if (k % 2) s += L'"', i++;
                } else {
                    s.append(k, L'\\');
                }
            } else if (cmd[i] == L'"') {
                guillemets = !guillemets;
                i++;
            } else {
                s += cmd[i++];
            }
        }
        a.push_back(s);
    }
    std::vector<std::string> o;
    for (const auto& s : a) o.push_back(plat_chemin(s));
    return o;
}

BOOL CreateProcessW(const wchar_t* appli, wchar_t* cmd, void*, void*, BOOL, DWORD drapeaux, void* env, const wchar_t* dossier,
                    STARTUPINFOW* si, PROCESS_INFORMATION* pi) {
    std::vector<std::string> args = arguments(cmd ? cmd : L"");
    if (appli) {
        if (args.empty()) args.push_back(plat_chemin(appli));
        else args[0] = plat_chemin(appli);
    }
    if (args.empty()) return errno = EINVAL, erreur(), FALSE;
    std::vector<char*> argv;
    for (auto& s : args) argv.push_back(s.data());
    argv.push_back(nullptr);
    std::vector<std::string> envs;
    std::vector<char*> envp;
    if (env) {   // bloc large (CREATE_UNICODE_ENVIRONMENT) : « nom=valeur\0…\0\0 »
        for (const wchar_t* p = (const wchar_t*)env; *p; p += wcslen(p) + 1) envs.push_back(u8(p));
        for (auto& s : envs) envp.push_back(s.data());
        envp.push_back(nullptr);
    }
    const std::string rep = dossier ? plat_chemin(dossier) : std::string();
    const bool tubes = si && (si->dwFlags & STARTF_USESTDHANDLES);
    const int sortie = tubes && si->hStdOutput ? si->hStdOutput->fd : -1, erreurs = tubes && si->hStdError ? si->hStdError->fd : -1;
    const bool detache = drapeaux & CREATE_NEW_PROCESS_GROUP, tache = drapeaux & CREATE_SUSPENDED;
    int p[2];   // échec de l'exec rapporté par ce tube (fermé par l'exec s'il réussit)
    if (pipe2(p, O_CLOEXEC) != 0) return erreur(), FALSE;
    const pid_t parent = getpid();
    const pid_t pid = fork();
    if (pid < 0) {
        erreur();
        close(p[0]), close(p[1]);
        return FALSE;
    }
    if (pid == 0) {
        if (detache) setsid();
        else setpgid(0, 0);   // son propre groupe : TerminateJobObject tue tout l'arbre
        if (tache) {          // tâche du launcher (job « tuer à la fermeture ») : meurt avec lui
            prctl(PR_SET_PDEATHSIG, SIGKILL);
            if (getppid() != parent) _exit(127);
        }
        if (tubes) {
            const int nul = open("/dev/null", O_RDONLY);
            if (nul >= 0) dup2(nul, 0);
            if (sortie >= 0) dup2(sortie, 1);
            if (erreurs >= 0) dup2(erreurs, 2);
        }
        if (!rep.empty() && chdir(rep.c_str()) != 0) {
            const int e = errno;
            (void)!write(p[1], &e, sizeof e);
            _exit(127);
        }
        signal(SIGPIPE, SIG_DFL);
        if (env) environ = envp.data();
        if (strchr(argv[0], '/')) execv(argv[0], argv.data());
        else execvp(argv[0], argv.data());
        const int e = errno;
        (void)!write(p[1], &e, sizeof e);
        _exit(127);
    }
    close(p[1]);
    int e = 0;
    const ssize_t k = read(p[0], &e, sizeof e);
    close(p[0]);
    if (k == sizeof e) {
        waitpid(pid, nullptr, 0);
        errno = e;
        erreur();
        return FALSE;
    }
    auto* h = new PlatObjet(PlatObjet::PROCESSUS);
    h->pid = pid;
    pi->hProcess = h;
    pi->hThread = new PlatObjet(PlatObjet::FIL);
    pi->dwProcessId = pi->dwThreadId = (DWORD)pid;
    return TRUE;
}
DWORD WaitForSingleObject(HANDLE h, DWORD ms) {
    if (!h || h == INVALID_HANDLE_VALUE || h->type != PlatObjet::PROCESSUS) return WAIT_OBJECT_0;
    const ULONGLONG fin = ms == INFINITE ? ~0ull : GetTickCount64() + ms;
    for (;;) {
        {
            std::lock_guard<std::mutex> l(h->m);
            if (h->fini) return WAIT_OBJECT_0;
            int st = 0;
            const pid_t r = waitpid(h->pid, &st, ms == INFINITE ? 0 : WNOHANG);
            if (r == h->pid) {
                h->fini = true;
                h->code = WIFEXITED(st) ? WEXITSTATUS(st) : WIFSIGNALED(st) ? 128 + WTERMSIG(st) : 1;
                return WAIT_OBJECT_0;
            }
            if (r < 0 && errno != EINTR) {   // déjà ramassé ailleurs
                h->fini = true;
                return WAIT_OBJECT_0;
            }
        }
        if (GetTickCount64() >= fin) return WAIT_TIMEOUT;
        usleep(5000);
    }
}
BOOL GetExitCodeProcess(HANDLE h, DWORD* code) {
    if (!h || h == INVALID_HANDLE_VALUE) return FALSE;
    WaitForSingleObject(h, 0);
    std::lock_guard<std::mutex> l(h->m);
    *code = h->fini ? (DWORD)h->code : STILL_ACTIVE;
    return TRUE;
}
DWORD ResumeThread(HANDLE) { return 1; }
BOOL SetPriorityClass(HANDLE h, DWORD classe) {
    if (!h || h->type != PlatObjet::PROCESSUS) return FALSE;
    return setpriority(PRIO_PGRP, (id_t)h->pid, classe == BELOW_NORMAL_PRIORITY_CLASS ? 10 : 0) == 0;
}
HANDLE CreateJobObjectW(void*, const wchar_t*) { return new PlatObjet(PlatObjet::TRAVAIL); }
BOOL SetInformationJobObject(HANDLE job, JOBOBJECTINFOCLASS, void* info, DWORD) {
    job->tuer_a_la_fin = (((JOBOBJECT_EXTENDED_LIMIT_INFORMATION*)info)->BasicLimitInformation.LimitFlags &
                          JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) != 0;
    return TRUE;
}
BOOL AssignProcessToJobObject(HANDLE job, HANDLE proc) {
    job->groupe = proc->pid;   // l'enfant a pris son propre groupe (CreateProcessW)
    return TRUE;
}
BOOL TerminateJobObject(HANDLE job, UINT) {
    if (job->groupe > 0) kill(-job->groupe, SIGKILL);
    return TRUE;
}

// programme du bureau (xdg-open), détaché (l'AppImage ne touche plus LD_LIBRARY_PATH : rien à rétablir). Double fork :
// l'enfant lance xdg-open dans un petit-enfant et sort aussitôt, ramassé ici ; le petit-enfant est repris par init (ou
// le sous-moissonneur du bureau). Avant, chaque clic laissait un zombie, jamais ramassé, jusqu'à la fin du launcher.
static void ouvrir(const std::string& cible) {
    const pid_t pid = fork();
    if (pid == 0) {
        setsid();
        if (fork() == 0) {
            execlp("xdg-open", "xdg-open", cible.c_str(), (char*)nullptr);
            _exit(127);
        }
        _exit(0);
    }
    if (pid > 0)
        while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
}
HANDLE ShellExecuteW(HWND, const wchar_t*, const wchar_t* fichier, const wchar_t*, const wchar_t*, int) {
    ouvrir(plat_chemin(fichier));   // fichier ou dossier, avec le programme du bureau
    return (HANDLE)(intptr_t)33;
}

// ---------------------------------------------------------------- fenêtre
namespace {
WNDPROC g_proc = nullptr;
SDL_Window* g_win = nullptr;
std::mutex g_file_lk;
std::deque<MSG> g_file;
uint32_t g_ev_reveil = 0;
std::atomic<bool> g_question{false};
struct Minuteur { UINT_PTR id; UINT ms; ULONGLONG prochain; };
std::vector<Minuteur> g_minuteurs;
bool g_suivi_souris = false;
int g_verrou = -1;

bool video() {
    static bool ok = [] {
        SDL_SetHint(SDL_HINT_APP_NAME, "StarFox Remake [recompiled]");
        SDL_SetHint(SDL_HINT_APP_ID, "spaceanimalsrecomp");
        SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            fprintf(stderr, "lanceur: SDL video: %s\n", SDL_GetError());
            return false;
        }
        g_ev_reveil = SDL_RegisterEvents(1);
        return true;
    }();
    return ok;
}
float densite() { return g_win ? std::max(0.5f, SDL_GetWindowPixelDensity(g_win)) : 1.f; }

SDL_HitTestResult test_zone(SDL_Window*, const SDL_Point* p, void*) {
    if (!g_proc) return SDL_HITTEST_NORMAL;
    const float k = densite();
    const LRESULT r = g_proc((HWND)g_win, WM_NCHITTEST, 0, MAKELPARAM((int)lroundf(p->x * k), (int)lroundf(p->y * k)));
    return r == HTCAPTION ? SDL_HITTEST_DRAGGABLE : SDL_HITTEST_NORMAL;
}

// événement SDL → message ; false : rien à transmettre
bool traduire(const SDL_Event& e, MSG* m) {
    *m = {(HWND)g_win, 0, 0, 0};
    const float k = densite();
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
        m->message = WM_MOUSEMOVE;
        m->lParam = MAKELPARAM((int)lroundf(e.motion.x * k), (int)lroundf(e.motion.y * k));
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button != SDL_BUTTON_LEFT) return false;
        m->message = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? WM_LBUTTONDOWN : WM_LBUTTONUP;
        m->lParam = MAKELPARAM((int)lroundf(e.button.x * k), (int)lroundf(e.button.y * k));
        return true;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        if (!g_suivi_souris) return false;
        g_suivi_souris = false;
        m->message = WM_MOUSELEAVE;
        return true;
    case SDL_EVENT_KEY_DOWN: {
        m->message = (e.key.mod & SDL_KMOD_ALT) ? WM_SYSKEYDOWN : WM_KEYDOWN;
        switch (e.key.scancode) {
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER: m->wParam = VK_RETURN; break;
        case SDL_SCANCODE_ESCAPE: m->wParam = VK_ESCAPE; break;
        case SDL_SCANCODE_BACKSPACE: m->wParam = VK_BACK; break;
        case SDL_SCANCODE_TAB: m->wParam = VK_TAB; break;
        default: m->wParam = (WPARAM)e.key.key; break;
        }
        const unsigned sc = scancodes::depuis_sdl((int)e.key.scancode);   // position de la touche, comme Windows
        m->lParam = (LPARAM)((sc & 0xFF) << 16) | ((sc & 0xE000) ? (1 << 24) : 0) | (e.key.repeat ? (1 << 30) : 0) | 1;
        return true;
    }
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        m->message = WM_SIZE;
        return true;
    case SDL_EVENT_WINDOW_MINIMIZED:
        m->message = WM_SIZE;
        m->wParam = SIZE_MINIMIZED;
        return true;
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED: {
        static RECT r;   // position actuelle (la taille suit l'échelle du launcher)
        int x = 0, y = 0;
        SDL_GetWindowPosition(g_win, &x, &y);
        r = {x, y, x, y};
        m->message = WM_DPICHANGED;
        const UINT d = GetDpiForWindow(g_win);
        m->wParam = (WPARAM)(d | d << 16);
        m->lParam = (LPARAM)&r;
        return true;
    }
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        m->message = WM_CLOSE;
        return true;
    default: return false;
    }
}
}  // namespace

UINT RegisterClassExW(const WNDCLASSEXW* wc) {
    g_proc = wc->lpfnWndProc;
    return 1;
}
HWND FindWindowW(const wchar_t*, const wchar_t*) {
    // une seule instance : verrou exclusif gardé ouvert jusqu'à la fin ; un autre launcher qui le tient → « trouvé »
    const char* r = getenv("XDG_RUNTIME_DIR");
    const std::string f = std::string(r && *r ? r : "/tmp") + "/spaceanimalsrecomp-lanceur-" + std::to_string(getuid()) + ".lock";
    g_verrou = open(f.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (g_verrou >= 0 && flock(g_verrou, LOCK_EX | LOCK_NB) != 0 && errno == EWOULDBLOCK) {
        close(g_verrou);
        g_verrou = -1;
        return (HWND)1;
    }
    return nullptr;
}
HWND CreateWindowExW(DWORD, const wchar_t*, const wchar_t* titre, DWORD, int x, int y, int w, int h, HWND, void*, HINSTANCE, void*) {
    if (!video()) return nullptr;
    g_win = SDL_CreateWindow(u8(titre).c_str(), w, h, SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN);
    if (!g_win) {
        fprintf(stderr, "lanceur: SDL_CreateWindow: %s\n", SDL_GetError());
        return nullptr;
    }
    SDL_SetWindowPosition(g_win, x, y);
    SDL_SetWindowHitTest(g_win, test_zone, nullptr);
    // icône de la fenêtre (lanceur.png, à côté du launcher) : X11 et KDE la montrent ; GNOME prend celle de
    // l'entrée de menu spaceanimalsrecomp.desktop (posée par AppRun)
    if (const char* base = SDL_GetBasePath()) {
        if (SDL_Surface* icone = SDL_LoadPNG((std::string(base) + "lanceur.png").c_str())) {
            SDL_SetWindowIcon(g_win, icone);
            SDL_DestroySurface(icone);
        }
    }
    return (HWND)g_win;
}
BOOL DestroyWindow(HWND h) {
    if (g_proc) g_proc(h, WM_DESTROY, 0, 0);
    if (g_win) SDL_DestroyWindow(g_win), g_win = nullptr;
    return TRUE;
}
BOOL ShowWindow(HWND, int cmd) {
    if (!g_win) return FALSE;
    switch (cmd) {
    case SW_HIDE: SDL_HideWindow(g_win); break;
    case SW_MINIMIZE: SDL_MinimizeWindow(g_win); break;
    case SW_RESTORE: SDL_RestoreWindow(g_win); SDL_ShowWindow(g_win); break;
    default: SDL_ShowWindow(g_win); break;
    }
    return TRUE;
}
BOOL UpdateWindow(HWND) { return TRUE; }
BOOL SetForegroundWindow(HWND) { return g_win && SDL_RaiseWindow(g_win); }
BOOL SetWindowPos(HWND, HWND, int x, int y, int w, int h, UINT) {
    if (!g_win) return FALSE;
    const float k = densite();   // tailles en pixels, comme Windows
    SDL_SetWindowSize(g_win, (int)lroundf(w / k), (int)lroundf(h / k));
    SDL_SetWindowPosition(g_win, x, y);
    return TRUE;
}
BOOL IsIconic(HWND) { return g_win && (SDL_GetWindowFlags(g_win) & SDL_WINDOW_MINIMIZED); }
BOOL IsWindowVisible(HWND) { return g_win && !(SDL_GetWindowFlags(g_win) & SDL_WINDOW_HIDDEN); }
HWND GetForegroundWindow() { return g_win && (SDL_GetWindowFlags(g_win) & SDL_WINDOW_INPUT_FOCUS) ? (HWND)g_win : nullptr; }
BOOL IsWindowEnabled(HWND) { return !g_question.load(); }
BOOL GetClientRect(HWND, RECT* r) {
    int w = 0, h = 0;
    if (g_win) SDL_GetWindowSizeInPixels(g_win, &w, &h);
    *r = {0, 0, w, h};
    return g_win != nullptr;
}
BOOL ScreenToClient(HWND, POINT*) { return TRUE; }

BOOL PostMessageW(HWND h, UINT m, WPARAM w, LPARAM l) {
    {
        std::lock_guard<std::mutex> lk(g_file_lk);
        g_file.push_back({h, m, w, l});
    }
    if (g_ev_reveil) {
        SDL_Event e{};
        e.type = g_ev_reveil;
        SDL_PushEvent(&e);
    }
    return TRUE;
}
void PostQuitMessage(int code) { PostMessageW(nullptr, WM_QUIT, (WPARAM)code, 0); }
BOOL PeekMessageW(MSG* msg, HWND, UINT, UINT, UINT) {
    {
        std::lock_guard<std::mutex> lk(g_file_lk);
        if (!g_file.empty()) {
            *msg = g_file.front();
            g_file.pop_front();
            return TRUE;
        }
    }
    const ULONGLONG t = GetTickCount64();
    for (Minuteur& m : g_minuteurs)
        if (t >= m.prochain) {
            m.prochain = t + m.ms;
            *msg = {(HWND)g_win, WM_TIMER, m.id, 0};
            return TRUE;
        }
    if (!video()) return FALSE;
    SDL_Event e;
    while (SDL_PollEvent(&e))
        if (traduire(e, msg)) return TRUE;
    return FALSE;
}
BOOL TranslateMessage(const MSG*) { return TRUE; }
LRESULT DispatchMessageW(const MSG* msg) {
    if (!g_proc || msg->message == WM_QUIT) return 0;
    const LRESULT r = g_proc(msg->hwnd, msg->message, msg->wParam, msg->lParam);
    if (msg->message == WM_MOUSEMOVE)   // Windows envoie WM_SETCURSOR à chaque mouvement
        g_proc(msg->hwnd, WM_SETCURSOR, (WPARAM)msg->hwnd, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    return r;
}
LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM) { return 0; }
DWORD MsgWaitForMultipleObjects(DWORD, const HANDLE*, BOOL, DWORD ms, DWORD) {
    if (video()) SDL_WaitEventTimeout(nullptr, (Sint32)ms);
    else Sleep(ms);
    return 0;
}
UINT_PTR SetTimer(HWND, UINT_PTR id, UINT ms, void*) {
    KillTimer(nullptr, id);
    g_minuteurs.push_back({id, ms, GetTickCount64() + ms});
    return id;
}
BOOL KillTimer(HWND, UINT_PTR id) {
    for (size_t i = 0; i < g_minuteurs.size(); i++)
        if (g_minuteurs[i].id == id) {
            g_minuteurs.erase(g_minuteurs.begin() + (long)i);
            return TRUE;
        }
    return FALSE;
}
int MessageBoxW(HWND, const wchar_t* texte, const wchar_t* titre, UINT type) {
    video();
    SDL_MessageBoxButtonData oui_non[2] = {{0, IDNO, "Non / No"}, {0, IDYES, "Oui / Yes"}};
    SDL_MessageBoxButtonData ok[1] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, IDOK, "OK"}};
    const bool question = (type & 0xF) == MB_YESNO;
    if (question) {
        oui_non[0].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT | ((type & MB_DEFBUTTON2) ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0);
        oui_non[1].flags = (type & MB_DEFBUTTON2) ? 0 : SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
    }
    const UINT icone = type & 0xF0;
    const std::string t = u8(texte), ti = u8(titre);
    SDL_MessageBoxData d{};
    d.flags = icone == MB_ICONERROR ? SDL_MESSAGEBOX_ERROR : icone == MB_ICONWARNING ? SDL_MESSAGEBOX_WARNING : SDL_MESSAGEBOX_INFORMATION;
    d.window = g_win && IsWindowVisible(nullptr) ? g_win : nullptr;
    d.title = ti.c_str();
    d.message = t.c_str();
    d.numbuttons = question ? 2 : 1;
    d.buttons = question ? oui_non : ok;
    int r = question ? IDNO : IDOK;
    g_question = true;
    if (!SDL_ShowMessageBox(&d, &r)) {
        fprintf(stderr, "lanceur: %s\n%s\n", ti.c_str(), t.c_str());
        r = question ? IDNO : IDOK;
    }
    g_question = false;
    return r;
}
HCURSOR LoadCursorW(HINSTANCE, const wchar_t* nom) {
    static SDL_Cursor* main = nullptr;
    static SDL_Cursor* fleche = nullptr;
    if (!video()) return nullptr;
    if (nom == IDC_HAND) return main ? main : (main = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER));
    return fleche ? fleche : (fleche = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT));
}
HCURSOR SetCursor(HCURSOR c) {
    static HCURSOR avant = nullptr;
    const HCURSOR a = avant;
    if (c && c != avant) SDL_SetCursor((SDL_Cursor*)c);
    avant = c;
    return a;
}
HWND SetCapture(HWND h) {
    SDL_CaptureMouse(true);
    return h;
}
BOOL ReleaseCapture() { return SDL_CaptureMouse(false); }
BOOL TrackMouseEvent(TRACKMOUSEEVENT*) {
    g_suivi_souris = true;
    return TRUE;
}
// Échelle de l'écran : celle que SDL donne (Xft.dpi sous X11). Sous Wayland, la fenêtre (sans haute densité de
// pixels) est mise à l'échelle par le compositeur : 96. LANCEUR_DPI : imposée.
static UINT dpi_de(SDL_DisplayID d) {
    if (const char* e = getenv("LANCEUR_DPI")) return (UINT)std::max(48, atoi(e));
    if (!video()) return 96;
    const char* pilote = SDL_GetCurrentVideoDriver();
    if (pilote && !strcmp(pilote, "wayland")) return 96;
    const float k = d ? SDL_GetDisplayContentScale(d) : 1.f;
    return (UINT)lroundf(96.f * (k >= 0.5f ? k : 1.f));
}
UINT GetDpiForSystem() { return video() ? dpi_de(SDL_GetPrimaryDisplay()) : 96; }
UINT GetDpiForWindow(HWND) { return g_win ? dpi_de(SDL_GetDisplayForWindow(g_win)) : GetDpiForSystem(); }
static RECT zone(SDL_DisplayID d, bool travail) {
    SDL_Rect r{0, 0, 1280, 800};
    if (video() && d) (travail ? SDL_GetDisplayUsableBounds : SDL_GetDisplayBounds)(d, &r);
    return {r.x, r.y, r.x + r.w, r.y + r.h};
}
BOOL SystemParametersInfoW(UINT action, UINT, void* p, UINT) {
    if (action != SPI_GETWORKAREA) return FALSE;
    *(RECT*)p = zone(video() ? SDL_GetPrimaryDisplay() : 0, true);
    return TRUE;
}
void* MonitorFromWindow(HWND, DWORD) { return (void*)(uintptr_t)(g_win ? SDL_GetDisplayForWindow(g_win) : 0); }
BOOL GetMonitorInfoW(void* m, MONITORINFO* mi) {
    const SDL_DisplayID d = (SDL_DisplayID)(uintptr_t)m;
    if (!d) return FALSE;
    mi->rcMonitor = zone(d, false);
    mi->rcWork = zone(d, true);
    return TRUE;
}
UINT MapVirtualKeyW(UINT code, UINT) {
    if (!video()) return 0;
    SDL_Keycode k = 0;
    if (code >= 'A' && code <= 'Z') k = (SDL_Keycode)(code - 'A' + 'a');
    else if (code >= '0' && code <= '9') k = (SDL_Keycode)code;
    else if (code == VK_OEM_COMMA) k = SDLK_COMMA;
    if (!k) return 0;
    const SDL_Scancode s = SDL_GetScancodeFromKey(k, nullptr);   // selon la disposition du clavier
    return s == SDL_SCANCODE_UNKNOWN ? 0 : scancodes::depuis_sdl((int)s);
}

int GetKeyNameTextW(LONG lparam, wchar_t* b, int n) {
    if (!video() || n <= 0) return 0;
    const unsigned pc = (unsigned)((lparam >> 16) & 0xFF) | ((lparam & (1 << 24)) ? 0xE000u : 0u);
    const int s = scancodes::vers_sdl(pc);
    if (!s) return 0;
    const char* nom = SDL_GetScancodeName((SDL_Scancode)s);   // touches sans caractère (Maj, flèches…)
    const SDL_Keycode k = SDL_GetKeyFromScancode((SDL_Scancode)s, SDL_KMOD_NONE, false);
    if (k >= 0x21 && k < 0x7F00 && !(k & SDLK_SCANCODE_MASK)) nom = SDL_GetKeyName(k);   // lettre de la disposition
    if (!nom || !*nom) return 0;
    const std::wstring w = large(nom);
    const int c = std::min<int>((int)w.size(), n - 1);
    wmemcpy(b, w.data(), (size_t)c);
    b[c] = 0;
    return c;
}

// ---------------------------------------------------------------- propres à Linux
namespace {
struct Choix {
    std::atomic<bool> fini{false};
    std::string chemin;
};
void SDLCALL choisi(void* u, const char* const* liste, int) {
    auto* c = (Choix*)u;
    if (liste && liste[0]) c->chemin = liste[0];
    c->fini = true;
}
std::wstring attendre(Choix& c) {
    g_question = true;
    while (!c.fini) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    g_question = false;
    return large(c.chemin);
}
}  // namespace

std::wstring plat_choisir_fichier(HWND, const wchar_t* titre, const wchar_t* filtre_nom, const char* motif, const std::wstring& dossier) {
    if (!video()) return {};
    Choix c;
    const std::string nom = u8(filtre_nom), dep = dossier.empty() ? std::string() : plat_chemin(dossier) + "/";
    const SDL_DialogFileFilter f[1] = {{nom.c_str(), motif}};
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetPointerProperty(p, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, (void*)f);
    SDL_SetNumberProperty(p, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, 1);
    SDL_SetPointerProperty(p, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, g_win);
    const std::string t = u8(titre);
    SDL_SetStringProperty(p, SDL_PROP_FILE_DIALOG_TITLE_STRING, t.c_str());
    if (!dep.empty()) SDL_SetStringProperty(p, SDL_PROP_FILE_DIALOG_LOCATION_STRING, dep.c_str());
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFILE, choisi, &c, p);
    SDL_DestroyProperties(p);
    return attendre(c);
}
std::wstring plat_choisir_dossier(HWND, const wchar_t* titre) {
    if (!video()) return {};
    Choix c;
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetPointerProperty(p, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, g_win);
    const std::string t = u8(titre);
    SDL_SetStringProperty(p, SDL_PROP_FILE_DIALOG_TITLE_STRING, t.c_str());
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFOLDER, choisi, &c, p);
    SDL_DestroyProperties(p);
    return attendre(c);
}
std::wstring plat_bureau() {
    const char* home = getenv("HOME");
    if (!home) return {};
    std::string d;
    const char* cfg = getenv("XDG_CONFIG_HOME");
    const std::string f = (cfg && *cfg ? std::string(cfg) : std::string(home) + "/.config") + "/user-dirs.dirs";
    if (FILE* x = fopen(f.c_str(), "r")) {   // XDG_DESKTOP_DIR="$HOME/Bureau"
        char l[1024];
        while (fgets(l, sizeof l, x)) {
            std::string s = l;
            if (s.rfind("XDG_DESKTOP_DIR=", 0) != 0) continue;
            s = s.substr(16);
            while (!s.empty() && (s.back() == '\n' || s.back() == '"')) s.pop_back();
            if (!s.empty() && s[0] == '"') s.erase(0, 1);
            if (s.rfind("$HOME", 0) == 0) s = home + s.substr(5);
            d = s;
        }
        fclose(x);
    }
    if (d.empty()) d = std::string(home) + "/Desktop";
    struct stat st;
    return stat(d.c_str(), &st) == 0 && S_ISDIR(st.st_mode) ? large(d) : std::wstring();
}
std::wstring plat_exe_lanceur() {
    if (const char* a = getenv("APPIMAGE"); a && *a) return large(a);
    wchar_t b[4096];
    return GetModuleFileNameW(nullptr, b, 4096) ? std::wstring(b) : std::wstring();
}

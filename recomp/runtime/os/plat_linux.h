// Portage Linux de la couche hôte : équivalents POSIX des quelques fonctions de Windows que le runtime appelle telles
// quelles (temps, sommeil, verrous SRW, fibres, fichiers). Inclus à la place de <windows.h> quand _WIN32 n'est pas
// défini ; les sous-systèmes propres à Windows (mémoire, exceptions, fenêtre, son, manettes XInput) ont leur propre
// code Linux dans chaque fichier (#ifdef _WIN32 … #else … #endif). Sous Windows, ce fichier n'est jamais inclus.
#pragma once
#ifndef _WIN32

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <malloc.h>
#include <pthread.h>
#include <string>
#include <sched.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <setjmp.h>
#include <ucontext.h>
#include <unistd.h>
#include <dirent.h>
#include <fnmatch.h>

#ifndef MAX_PATH
#define MAX_PATH 4096
#endif
#ifndef INFINITE
#define INFINITE 0xFFFFFFFFu
#endif

// ---------------------------------------------------------------- temps
typedef union {
    struct {
        uint32_t LowPart;
        int32_t HighPart;
    };
    int64_t QuadPart;
} LARGE_INTEGER;

inline uint64_t plat_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
inline uint64_t GetTickCount64() { return plat_ns() / 1000000ull; }
inline int QueryPerformanceCounter(LARGE_INTEGER* t) {
    t->QuadPart = (int64_t)plat_ns();
    return 1;
}
inline int QueryPerformanceFrequency(LARGE_INTEGER* f) {
    f->QuadPart = 1000000000ll;
    return 1;
}
// Sommeil précis (CLOCK_MONOTONIC absolu : pas de dérive si un signal interrompt l'attente)
inline void plat_sleep_ns(int64_t ns) {
    if (ns <= 0) return;
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const uint64_t fin = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec + (uint64_t)ns;
    ts.tv_sec = (time_t)(fin / 1000000000ull);
    ts.tv_nsec = (long)(fin % 1000000000ull);
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {}
}
inline void Sleep(uint32_t ms) {
    if (ms == INFINITE)
        for (;;) pause();
    if (!ms) {
        sched_yield();
        return;
    }
    plat_sleep_ns((int64_t)ms * 1000000ll);
}
inline int SwitchToThread() { return sched_yield() == 0; }
#define YieldProcessor() __builtin_ia32_pause()

// ---------------------------------------------------------------- verrous SRW (lecteurs / rédacteur)
typedef pthread_rwlock_t SRWLOCK;
#define SRWLOCK_INIT PTHREAD_RWLOCK_INITIALIZER
inline void AcquireSRWLockShared(SRWLOCK* l) { pthread_rwlock_rdlock(l); }
inline void ReleaseSRWLockShared(SRWLOCK* l) { pthread_rwlock_unlock(l); }
inline void AcquireSRWLockExclusive(SRWLOCK* l) { pthread_rwlock_wrlock(l); }
inline void ReleaseSRWLockExclusive(SRWLOCK* l) { pthread_rwlock_unlock(l); }
inline bool TryAcquireSRWLockExclusive(SRWLOCK* l) { return pthread_rwlock_trywrlock(l) == 0; }

// ---------------------------------------------------------------- mémoire, chaînes, fichiers
inline void* _aligned_malloc(size_t n, size_t a) {
    void* p = nullptr;
    return posix_memalign(&p, a < sizeof(void*) ? sizeof(void*) : a, n ? n : 1) ? nullptr : p;
}
inline void _aligned_free(void* p) { free(p); }
inline size_t _msize(void* p) { return malloc_usable_size(p); }
inline int _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
inline int _strnicmp(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }
inline int _putenv_s(const char* n, const char* v) { return setenv(n, v, 1); }
inline int _mkdir(const char* p) { return mkdir(p, 0755); }
inline int _access(const char* p, int m) { return access(p, m); }
#define _fseeki64 fseeko
#define _ftelli64 ftello
inline int localtime_s(struct tm* r, const time_t* t) { return localtime_r(t, r) ? 0 : errno; }
inline int gmtime_s(struct tm* r, const time_t* t) { return gmtime_r(t, r) ? 0 : errno; }
#define _mkgmtime timegm
inline unsigned long GetCurrentProcessId() { return (unsigned long)getpid(); }

// Dossier de l'exécutable, avec le séparateur final (équivalent de GetModuleFileName + coupe au dernier « \ »)
inline std::string plat_exe_path() {
    char b[MAX_PATH] = {};
    const ssize_t n = readlink("/proc/self/exe", b, sizeof b - 1);
    return n > 0 ? std::string(b, (size_t)n) : std::string();
}
// GetModuleFileNameA(nullptr, …) : seul usage du runtime, le chemin de l'exécutable
inline unsigned GetModuleFileNameA(void*, char* b, unsigned n) {
    const std::string p = plat_exe_path();
    if (!n) return 0;
    snprintf(b, n, "%s", p.c_str());
    return (unsigned)strlen(b);
}
inline bool plat_existe(const char* p) {
    struct stat st;
    return p && *p && stat(p, &st) == 0;
}

// ---------------------------------------------------------------- fibres (nn::os, hle_os.cpp)
// Mêmes appels que les fibres de Windows, sur ucontext : chaque fibre a sa propre pile hôte, réservée par mmap (les
// pages ne prennent de la RAM qu'au premier accès). swapcontext garde aussi l'état flottant (MXCSR, mot de contrôle
// x87), comme FIBER_FLAG_FLOAT_SWITCH.
#define FIBER_FLAG_FLOAT_SWITCH 0x1
#define WINAPI
struct PlatFibre {
    ucontext_t uc;
    void* pile = nullptr;
    size_t taille = 0;
    void (*fn)(void*) = nullptr;
    void* arg = nullptr;
};
inline thread_local PlatFibre* t_plat_fibre = nullptr;
// Fin d'un fil depuis n'importe quelle profondeur (svc ExitThread, fonction de fibre qui revient) : retour par longjmp
// au point de départ du fil (kernel.cpp), comme _endthreadex sous Windows, qui ne déroule pas la pile non plus.
// pthread_exit déroulerait la pile (exception de fin forcée de la glibc) à travers le code recompilé et les piles des
// fibres, et sautait à une adresse de pile (2026-11-15, WSL : plantage au premier ExitThread d'un fil du jeu).
inline thread_local jmp_buf* t_plat_sortie = nullptr;
[[noreturn]] inline void plat_fin_fil() {
    if (t_plat_sortie) longjmp(*t_plat_sortie, 1);
    pthread_exit(nullptr);   // (fil qui n'a pas de point de sortie : ne devrait pas arriver)
}
inline void plat_fibre_depart(unsigned lo, unsigned hi) {
    PlatFibre* f = (PlatFibre*)(((uintptr_t)hi << 32) | lo);
    f->fn(f->arg);
    plat_fin_fil();   // (Windows : une fonction de fibre qui revient termine le fil)
}
inline bool IsThreadAFiber() { return t_plat_fibre != nullptr; }
inline void* GetCurrentFiber() { return t_plat_fibre; }
inline void* ConvertThreadToFiberEx(void*, unsigned) {
    if (!t_plat_fibre) t_plat_fibre = new PlatFibre;
    return t_plat_fibre;
}
inline void* CreateFiberEx(size_t, size_t reserve, unsigned, void (*fn)(void*), void* arg) {
    auto* f = new PlatFibre;
    f->taille = (reserve + 0xffff) & ~(size_t)0xffff;
    f->pile = mmap(nullptr, f->taille, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_STACK, -1, 0);
    if (f->pile == MAP_FAILED) {
        delete f;
        return nullptr;
    }
    f->fn = fn;
    f->arg = arg;
    getcontext(&f->uc);
    f->uc.uc_stack.ss_sp = f->pile;
    f->uc.uc_stack.ss_size = f->taille;
    f->uc.uc_link = nullptr;
    makecontext(&f->uc, (void (*)())plat_fibre_depart, 2, (unsigned)(uintptr_t)f, (unsigned)((uintptr_t)f >> 32));
    return f;
}
inline void SwitchToFiber(void* cible) {
    PlatFibre* de = t_plat_fibre;
    PlatFibre* vers = (PlatFibre*)cible;
    if (!de || de == vers) return;
    t_plat_fibre = vers;
    swapcontext(&de->uc, &vers->uc);
}
inline void DeleteFiber(void* p) {
    PlatFibre* f = (PlatFibre*)p;
    if (!f) return;
    if (f->pile) munmap(f->pile, f->taille);
    delete f;
}
inline unsigned long GetLastError() { return (unsigned long)errno; }

// ---------------------------------------------------------------- types et fichiers (équivalents des appels Win32 du runtime)
typedef unsigned long DWORD;   // (les journaux l'écrivent en %lu / %lx, comme sous Windows ; valeurs sur 32 bits)
typedef int BOOL;
typedef unsigned int UINT;
typedef unsigned long long ULONGLONG;
typedef void* HANDLE;
typedef void* HMODULE;
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#endif
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
#define FILE_ATTRIBUTE_DIRECTORY 0x10u
#define FILE_ATTRIBUTE_NORMAL 0x80u
#define MOVEFILE_REPLACE_EXISTING 0x1u
struct FILETIME {
    DWORD dwLowDateTime, dwHighDateTime;
};
// date en unités de 100 ns depuis 1601, comme Windows (seul l'ordre et l'égalité servent au runtime)
inline FILETIME plat_filetime(const struct timespec& t) {
    const uint64_t v = ((uint64_t)t.tv_sec + 11644473600ull) * 10000000ull + (uint64_t)t.tv_nsec / 100;
    return FILETIME{(DWORD)(uint32_t)v, (DWORD)(uint32_t)(v >> 32)};
}
inline DWORD GetFileAttributesA(const char* p) {
    struct stat st;
    if (!p || stat(p, &st)) return INVALID_FILE_ATTRIBUTES;
    return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}
enum { GetFileExInfoStandard = 0 };
struct WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow;
};
inline BOOL GetFileAttributesExA(const char* p, int, WIN32_FILE_ATTRIBUTE_DATA* d) {
    struct stat st;
    if (!p || stat(p, &st)) return 0;
    d->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    d->ftCreationTime = d->ftLastAccessTime = d->ftLastWriteTime = plat_filetime(st.st_mtim);
    d->nFileSizeHigh = (DWORD)(uint32_t)((uint64_t)st.st_size >> 32);
    d->nFileSizeLow = (DWORD)(uint32_t)st.st_size;
    return 1;
}
inline BOOL DeleteFileA(const char* p) { return unlink(p) == 0; }
inline BOOL MoveFileExA(const char* de, const char* vers, DWORD) { return rename(de, vers) == 0; }
inline BOOL CreateDirectoryA(const char* p, void*) { return mkdir(p, 0755) == 0; }
inline BOOL SetEnvironmentVariableA(const char* n, const char* v) { return (v ? setenv(n, v, 1) : unsetenv(n)) == 0; }

// FindFirstFileA / FindNextFileA : motif « dossier/…*.ext » (joker dans le dernier élément seulement)
struct WIN32_FIND_DATAA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow;
    char cFileName[MAX_PATH];
};
struct PlatRecherche {
    DIR* d;
    std::string dossier, motif;
};
inline bool plat_recherche_suivante(PlatRecherche* r, WIN32_FIND_DATAA* fd) {
    while (dirent* e = readdir(r->d)) {
        if (fnmatch(r->motif.c_str(), e->d_name, 0)) continue;
        struct stat st;
        const std::string p = r->dossier + e->d_name;
        if (stat(p.c_str(), &st)) continue;
        memset(fd, 0, sizeof *fd);
        fd->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        fd->ftCreationTime = fd->ftLastAccessTime = fd->ftLastWriteTime = plat_filetime(st.st_mtim);
        fd->nFileSizeHigh = (DWORD)(uint32_t)((uint64_t)st.st_size >> 32);
        fd->nFileSizeLow = (DWORD)(uint32_t)st.st_size;
        snprintf(fd->cFileName, sizeof fd->cFileName, "%s", e->d_name);
        return true;
    }
    return false;
}
inline HANDLE FindFirstFileA(const char* motif, WIN32_FIND_DATAA* fd) {
    const std::string m = motif;
    const size_t k = m.find_last_of('/');
    auto* r = new PlatRecherche{nullptr, k == std::string::npos ? std::string("./") : m.substr(0, k + 1),
                                k == std::string::npos ? m : m.substr(k + 1)};
    r->d = opendir(r->dossier.c_str());
    if (!r->d || !plat_recherche_suivante(r, fd)) {
        if (r->d) closedir(r->d);
        delete r;
        return INVALID_HANDLE_VALUE;
    }
    return r;
}
inline BOOL FindNextFileA(HANDLE h, WIN32_FIND_DATAA* fd) { return plat_recherche_suivante((PlatRecherche*)h, fd); }
inline BOOL FindClose(HANDLE h) {
    auto* r = (PlatRecherche*)h;
    if (r && r != INVALID_HANDLE_VALUE) {
        closedir(r->d);
        delete r;
    }
    return 1;
}

// ---------------------------------------------------------------- mémoire d'un processus (journal, surveillance)
// GetCurrentProcess() : pseudo-poignée ; un autre processus est désigné par son pid (precompile_enfant, vk_render.cpp)
inline HANDLE GetCurrentProcess() { return (HANDLE)(intptr_t)-1; }
// Fils : priorité d'un fil (le préchauffage de l'arène de transit tourne en priorité basse), par sa valeur nice
#define THREAD_PRIORITY_BELOW_NORMAL (-1)
inline HANDLE GetCurrentThread() { return (HANDLE)(intptr_t)-2; }
inline BOOL SetThreadPriority(HANDLE, int prio) {   // fil courant seulement (seul usage)
    return setpriority(PRIO_PROCESS, (id_t)gettid(), prio < 0 ? 5 : 0) == 0;
}
// Protection de pages (suivi des sommets RECOMP_VB_VRAM) : mprotect, ancienne protection non relue
#define PAGE_READONLY 0x02u
#define PAGE_READWRITE 0x04u
inline BOOL VirtualProtect(void* a, size_t n, DWORD prot, DWORD* avant) {
    if (avant) *avant = PAGE_READWRITE;
    return mprotect(a, n, prot == PAGE_READONLY ? PROT_READ : PROT_READ | PROT_WRITE) == 0;
}
struct PROCESS_MEMORY_COUNTERS {
    DWORD cb;
    DWORD PageFaultCount;
    size_t PeakWorkingSetSize, WorkingSetSize;
    size_t QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage, QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage;
    size_t PagefileUsage, PeakPagefileUsage;
};
struct PROCESS_MEMORY_COUNTERS_EX : PROCESS_MEMORY_COUNTERS {
    size_t PrivateUsage;
};
// RAM du processus (VmRSS), mémoire privée (RssAnon + VmSwap) comme « mémoire engagée », fautes de page (stat)
inline BOOL K32GetProcessMemoryInfo(HANDLE h, PROCESS_MEMORY_COUNTERS* m, DWORD cb) {
    const long pid = h == GetCurrentProcess() ? (long)getpid() : (long)(intptr_t)h;
    char chemin[64];
    snprintf(chemin, sizeof chemin, "/proc/%ld/status", pid);
    FILE* f = fopen(chemin, "r");
    if (!f) return 0;
    unsigned long long rss = 0, hwm = 0, anon = 0, swap = 0, v = 0;
    char l[256];
    while (fgets(l, sizeof l, f)) {
        if (sscanf(l, "VmRSS: %llu", &v) == 1) rss = v;
        else if (sscanf(l, "VmHWM: %llu", &v) == 1) hwm = v;
        else if (sscanf(l, "RssAnon: %llu", &v) == 1) anon = v;
        else if (sscanf(l, "VmSwap: %llu", &v) == 1) swap = v;
    }
    fclose(f);
    memset((char*)m + sizeof(DWORD), 0, cb - sizeof(DWORD));
    m->WorkingSetSize = (size_t)rss << 10;
    m->PeakWorkingSetSize = (size_t)hwm << 10;
    m->PagefileUsage = m->PeakPagefileUsage = (size_t)(anon + swap) << 10;
    if (cb >= sizeof(PROCESS_MEMORY_COUNTERS_EX)) ((PROCESS_MEMORY_COUNTERS_EX*)m)->PrivateUsage = m->PagefileUsage;
    snprintf(chemin, sizeof chemin, "/proc/%ld/stat", pid);
    if (FILE* s = fopen(chemin, "r")) {
        unsigned long mn = 0, mj = 0;
        if (fscanf(s, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %lu %*u %lu", &mn, &mj) == 2) m->PageFaultCount = (DWORD)(mn + mj);
        fclose(s);
    }
    return 1;
}

#endif  // !_WIN32

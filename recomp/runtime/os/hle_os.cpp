// Fibres nn::os → fibres Windows.
//
// Le code recompilé utilise la pile hôte pour ses appels : changer de pile invitée (ce que fait
// nn::os::SwitchToFiber) ne suffit pas, il faut aussi changer de pile hôte. Chaque fibre
// invitée a donc sa fibre Windows (pile hôte propre) et son propre état CPU ; changer de fibre =
// SwitchToFiber hôte, en recopiant les registres propres au thread (TLS, FPCR/FPSR). Une fibre
// peut ainsi être reprise sur un autre thread que celui où elle s'est suspendue.
//
// Remplace InitializeFiber / FinalizeFiber / SwitchToFiber / GetCurrentFiber ; les appels
// internes au SDK (nn::async) passent par le dispatch grâce à recomp/hooks.txt.
#include "hos.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <utility>
#include <mutex>
#include <unordered_map>

namespace hos {
namespace {

struct HFiber {
    void* host = nullptr;      // fibre Windows
    CPUState* ctx = nullptr;   // état CPU invité de la fibre
    uint64_t guest = 0;        // nn::os::FiberType* (0 : contexte d'origine d'un thread)
    uint64_t func = 0, arg = 0, stack = 0, stack_size = 0;
    bool finished = false;
};

std::mutex g_lk;
std::unordered_map<uint64_t, HFiber*> g_fibers;   // FiberType* → fibre
thread_local HFiber* t_thread_fiber = nullptr;    // contexte d'origine du thread hôte
thread_local HFiber* t_current = nullptr;
constexpr size_t HOST_FIBER_STACK = 64ull << 20;

HFiber* lookup(uint64_t guest) {
    std::lock_guard<std::mutex> lk(g_lk);
    auto it = g_fibers.find(guest);
    return it == g_fibers.end() ? nullptr : it->second;
}

void ensure_thread_fiber(CPUState* ctx) {
    if (t_thread_fiber) return;
    auto* f = new HFiber;
    f->host = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
    if (!f->host) panic("ConvertThreadToFiber failed (error %lu)", GetLastError());
    f->ctx = ctx;
    t_thread_fiber = t_current = f;
}

void switch_to(CPUState* cur_ctx, HFiber* target) {
    // registres propres au thread, partagés par toutes ses fibres
    target->ctx->tpidr_el0 = cur_ctx->tpidr_el0;
    target->ctx->tpidrro_el0 = cur_ctx->tpidrro_el0;
    target->ctx->fpcr = cur_ctx->fpcr;
    target->ctx->fpsr = cur_ctx->fpsr;
    t_current = target;
    kernel_set_running_ctx(target->ctx);
    ::SwitchToFiber(target->host);
    // repris ici : celui qui nous a relancés a mis à jour t_current
}

void WINAPI fiber_entry(void* p) {
    HFiber* f = (HFiber*)p;
    CPUState* c = f->ctx;
    c->x[0] = f->arg;
    c->sp = (f->stack + f->stack_size) & ~15ull;
    c->x[29] = 0;
    call_guest(c, f->func);
    // la fonction de fibre renvoie la fibre suivante (nullptr : contexte d'origine du thread)
    uint64_t next = c->x[0];
    f->finished = true;
    HFiber* n = next ? lookup(next) : t_thread_fiber;
    if (!n) panic("fiber 0x%llx finished into an unknown fiber 0x%llx", (unsigned long long)f->guest,
                  (unsigned long long)next);
    switch_to(c, n);
    panic("finished fiber resumed");
}

// nn::os::InitializeFiber(FiberType*, FiberFunction, void* arg, void* stack, size_t size, int flags)
void init_fiber(CPUState* ctx) {
    auto* f = new HFiber;
    f->guest = ctx->x[0];
    f->func = ctx->x[1];
    f->arg = ctx->x[2];
    f->stack = ctx->x[3];
    f->stack_size = ctx->x[4];
    f->ctx = (CPUState*)_aligned_malloc(sizeof(CPUState), 64);
    memset(f->ctx, 0, sizeof(CPUState));
    f->host = CreateFiberEx(64 << 10, HOST_FIBER_STACK, FIBER_FLAG_FLOAT_SWITCH, fiber_entry, f);
    if (!f->host) panic("CreateFiberEx failed (error %lu)", GetLastError());
    HFiber* old = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_lk);
        auto it = g_fibers.find(f->guest);
        if (it != g_fibers.end()) old = it->second;
        g_fibers[f->guest] = f;
    }
    if (old && old != t_current) {
        DeleteFiber(old->host);
        _aligned_free(old->ctx);
        delete old;
    }
    HDBG("fiber", "InitializeFiber 0x%llx: %s, stack 0x%llx+0x%llx", (unsigned long long)f->guest,
         symbolize(f->func).c_str(), (unsigned long long)f->stack, (unsigned long long)f->stack_size);
}

void finalize_fiber(CPUState* ctx) {
    uint64_t g = ctx->x[0];
    HFiber* f = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_lk);
        auto it = g_fibers.find(g);
        if (it == g_fibers.end()) return;
        f = it->second;
        g_fibers.erase(it);
    }
    if (f == t_current) {   // ne devrait pas arriver : on ne détruit pas la fibre qui tourne
        HLOG("fiber", "FinalizeFiber on the current fiber 0x%llx: skipped", (unsigned long long)g);
        return;
    }
    DeleteFiber(f->host);
    _aligned_free(f->ctx);
    delete f;
}

void switch_fiber(CPUState* ctx) {
    ensure_thread_fiber(ctx);
    HFiber* t = lookup(ctx->x[0]);
    if (!t) panic("SwitchToFiber to an unknown fiber 0x%llx (caller %s)",
                  (unsigned long long)ctx->x[0], symbolize(ctx->x[30]).c_str());
    if (t == t_current) return;
    if (t->finished) panic("SwitchToFiber to a finished fiber 0x%llx", (unsigned long long)ctx->x[0]);
    switch_to(ctx, t);
}

void current_fiber(CPUState* ctx) {
    ctx->x[0] = (t_current && t_current != t_thread_fiber) ? t_current->guest : 0;
}

// ---------------------------------------------------------------- coroutines du moteur
// main+0xba33c30 : jump(x0 = &case_sauvegarde, x1 = &case_cible). Version d'origine : empile
// x29/x30, d15..d8, x28..x19 (0xa0 octets), range sp dans *x0, charge sp depuis *x1, dépile et
// « revient » (ret) dans l'autre contexte avec x0 = 0. Ici : chaque contexte a sa fibre Windows
// et son état CPU ; la « poignée » est la valeur de sp rangée dans la case.
std::unordered_map<uint64_t, HFiber*> g_eng;   // poignée (sp suspendu) → contexte suspendu

void write_frame(CPUState* c, uint64_t h) {
    uint64_t* q = (uint64_t*)h;
    q[0] = c->x[29];
    q[1] = c->x[30];
    for (int i = 0; i < 8; i++) q[2 + i] = c->v[15 - i].u64[0];        // d15..d8
    for (int i = 0; i < 10; i++) q[10 + i] = c->x[28 - i];             // x28..x19
}

void read_frame(CPUState* c, uint64_t h) {
    const uint64_t* q = (const uint64_t*)h;
    c->x[29] = q[0];
    c->x[30] = q[1];
    for (int i = 0; i < 8; i++) c->v[15 - i] = V128{}, c->v[15 - i].u64[0] = q[2 + i];
    for (int i = 0; i < 10; i++) c->x[28 - i] = q[10 + i];
    c->sp = h + 0xa0;
    c->x[0] = 0;
}

void WINAPI eng_entry(void* p) {
    HFiber* f = (HFiber*)p;
    CPUState* c = f->ctx;
    // nouveau contexte préparé par le moteur : « retour » à x30 (trampoline : x0 = x19, br x20)
    call_guest(c, c->x[30]);
    panic("engine coroutine finished (entry %s)", symbolize(f->func).c_str());
}

void eng_jump(CPUState* ctx) {
    ensure_thread_fiber(ctx);
    uint64_t save_slot = ctx->x[0], target_slot = ctx->x[1];
    HFiber* cur = t_current;
    uint64_t h = (ctx->sp - 0xa0) & ~15ull;
    write_frame(ctx, h);                       // même empreinte mémoire que l'original
    uint64_t th = *(uint64_t*)target_slot;
    HFiber* t = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_lk);
        g_eng[h] = cur;
        *(uint64_t*)save_slot = h;
        auto it = g_eng.find(th);
        if (it != g_eng.end()) {
            t = it->second;
            g_eng.erase(it);
        }
    }
    if (!t) {   // contexte neuf : registres lus dans le bloc préparé sur sa pile
        t = new HFiber;
        t->ctx = (CPUState*)_aligned_malloc(sizeof(CPUState), 64);
        memset(t->ctx, 0, sizeof(CPUState));
        read_frame(t->ctx, th);
        t->func = t->ctx->x[30];
        t->host = CreateFiberEx(64 << 10, HOST_FIBER_STACK, FIBER_FLAG_FLOAT_SWITCH, eng_entry, t);
        if (!t->host) panic("CreateFiberEx failed (error %lu)", GetLastError());
        HDBG("fiber", "new coroutine: stack 0x%llx, resume %s", (unsigned long long)th,
             symbolize(t->func).c_str());
    }
    if (t == cur) {   // retour sur soi-même
        ctx->x[0] = 0;
        return;
    }
    switch_to(ctx, t);
    ctx->x[0] = 0;   // repris : jump renvoie 0
}

// ---------------------------------------------------------------- bouchons d'API nommés
constexpr int NAPI = 512;
struct ApiStub {
    std::string name;
    std::atomic<uint64_t> calls{0};
};
ApiStub g_api[NAPI];
int g_napi = 0;
void api_stub_call(int i, CPUState* ctx) {
    if (g_api[i].calls++ == 0)
        HLOG("hle", "[stubbed out] %s (caller %s)", g_api[i].name.c_str(), symbolize(ctx->x[30]).c_str());
    ctx->x[0] = 0;
}
template <int I>
void api_stub_fn(CPUState* ctx) { api_stub_call(I, ctx); }
template <int... I>
std::array<RecompFn, sizeof...(I)> make_api_stubs(std::integer_sequence<int, I...>) {
    return {{api_stub_fn<I>...}};
}

}  // namespace

int stub_namespace(const char* prefix, const char* why) {
    static const auto stubs = make_api_stubs(std::make_integer_sequence<int, NAPI>{});
    int n = 0;
    for (auto& m : g_modules) {
        if (m.name != "main") continue;
        for (auto& imp : m.imports) {
            if (imp.compare(0, strlen(prefix), prefix)) continue;
            if (g_napi >= NAPI) panic("API stub pool exhausted");
            int i = g_napi++;
            g_api[i].name = demangle(imp.c_str());
            if (override_export(imp.c_str(), stubs[i])) n++;
        }
    }
    HLOG("hle", "%s stubbed out (%d functions): %s", prefix, n, why);
    return n;
}

// ---------------------------------------------------------------- memmove / memcpy / strcmp du SDK
// Le profil d'une cinématique chargée (2026-11-17) mettait sdk!memmove parmi les premières fonctions des threads de
// travail du jeu : la copie octet par octet ou mot par mot du code recompilé coûte bien plus que celle de la
// bibliothèque C de l'hôte (UCRT sous Windows, glibc sous Linux : C standard, même source). Les appels qui passent
// par le dispatch (imports de main, PLT du SDK) arrivent ici ; les appels directs internes au SDK restent recompilés.
// Mémoire invitée = mémoire hôte : les fautes (pages engagées à la demande, textures rendues, VB_VRAM) sont
// traitées par le gestionnaire d'exceptions quel que soit le code qui les provoque. RECOMP_NO_HOST_LIBC=1 : désactivé.
namespace {
// GPTR de cpu.h, que <windows.h> redéfinit (GMEM_FIXED | GMEM_ZEROINIT) dans ce fichier
inline void* gmem(uint64_t a) { return (void*)(uintptr_t)(a + (uint64_t)RECOMP_MEM_OFFSET); }
// memmove(dst, src, n) → dst : x0 n'est pas modifié (comme le code du SDK)
void host_memmove(CPUState* ctx) {
    if (ctx->x[2]) memmove(gmem(ctx->x[0]), gmem(ctx->x[1]), (size_t)ctx->x[2]);
}
// strcmp : même résultat que sdk!strcmp au bit près (différence des premiers octets non signés qui diffèrent ou
// fin de la première chaîne, sur 32 bits) et mêmes lectures (mots de 8 octets alignés seulement quand les deux
// chaînes ont le même alignement) : pas de lecture au-delà de ce que lit le code du SDK.
void host_strcmp(CPUState* ctx) {
    const uint8_t* a = (const uint8_t*)gmem(ctx->x[0]);
    const uint8_t* b = (const uint8_t*)gmem(ctx->x[1]);
    if ((((uintptr_t)a ^ (uintptr_t)b) & 7) == 0) {
        for (; (uintptr_t)a & 7; a++, b++)
            if (!*a || *a != *b) goto fin;
        for (;; a += 8, b += 8) {
            uint64_t x, y;
            memcpy(&x, a, 8);
            memcpy(&y, b, 8);
            const uint64_t marque = (((x - 0x0101010101010101ull) & ~x) & 0x8080808080808080ull) | (x ^ y);
            if (marque) {
                const unsigned d = (unsigned)__builtin_ctzll(marque) & 0x38;
                ctx->x[0] = (uint32_t)((uint32_t)((x >> d) & 0xff) - (uint32_t)((y >> d) & 0xff));
                return;
            }
        }
    }
    while (*a && *a == *b) a++, b++;
fin:
    ctx->x[0] = (uint32_t)((uint32_t)*a - (uint32_t)*b);
}

int replace_libc() {
    if (const char* e = getenv("RECOMP_NO_HOST_LIBC"); e && *e && *e != '0') {
        HLOG("hle", "memmove / memcpy / strcmp: recompiled SDK code kept (RECOMP_NO_HOST_LIBC)");
        return 0;
    }
    // memcpy du SDK = saut vers __memcpy_aarch64 (par la PLT) ; memmove de l'hôte couvre aussi les recouvrements
    const std::pair<const char*, RecompFn> fns[] = {
        {"memmove", host_memmove}, {"memcpy", host_memmove}, {"__memcpy_aarch64", host_memmove}, {"strcmp", host_strcmp}};
    int n = 0;
    for (auto& [nom, fn] : fns) {
        uint64_t a = find_export(nom);
        if (!a) continue;
        RecompFn f = fn;
        rt_register_functions(&a, &f, 1);
        n++;
    }
    HLOG("hle", "memmove / memcpy / strcmp → host C library (%d functions replaced; RECOMP_NO_HOST_LIBC=1 to keep the SDK's)", n);
    return n;
}
}  // namespace

void hle_os_init() {
    replace_libc();
    int n = 0;
    n += override_export("_ZN2nn2os15InitializeFiberEPNS0_9FiberTypeEPFS2_PvES3_S3_mi", init_fiber);
    n += override_export("_ZN2nn2os13FinalizeFiberEPNS0_9FiberTypeE", finalize_fiber);
    n += override_export("_ZN2nn2os13SwitchToFiberEPNS0_9FiberTypeE", switch_fiber);
    n += override_export("_ZN2nn2os15GetCurrentFiberEv", current_fiber);
    for (auto& m : g_modules)
        if (m.name == "main") {
            uint64_t a = m.base + 0xba33c30;
            RecompFn fn = eng_jump;
            rt_register_functions(&a, &fn, 1);
            n++;
        }
    HLOG("hle", "nn::os fibers and engine coroutines → Windows fibers (%d functions replaced)", n);
    stub_namespace("_ZN2nn4chat", "GameChat relies on the gsv graphics system, which is bypassed");
}

}  // namespace hos

// Sortie audio Windows du HLE nn::audio (hle_audio.cpp) : WASAPI en mode partagé, piloté par événement.
//
// Le fil de rendu demande au mixeur du HLE autant de trames que le tampon du périphérique en accepte :
// l'horloge de la carte son rythme ainsi la consommation des flux du jeu (pas de dérive entre deux
// horloges). Format demandé : 48 kHz, stéréo, flottant 32 bits ; Windows convertit vers le format du
// périphérique (AUTOCONVERTPCM). Périphérique absent ou perdu : nouvel essai toutes les secondes, et le
// HLE revient à son horloge virtuelle, pour que le jeu n'attende jamais une carte son.
//
// Réglages : RECOMP_NO_AUDIO=1 (pas de sortie), RECOMP_VOLUME=0..100 (gain de sortie, 100 par défaut),
// RECOMP_AUDIO_WAV=fichier.wav (enregistre le mixage, avant le volume : vérification sans haut-parleurs).
// Linux : flux SDL3 (SDL_OpenAudioDeviceStream) sur le périphérique par défaut, rempli par son rappel au rythme de la
// carte son (même principe) ; SDL convertit vers le format du périphérique et suit un changement de périphérique.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <propidl.h>
#else
#include "plat_linux.h"
#include <SDL3/SDL.h>
#include <vector>
#endif

#include "audio_out.h"
#include "hos.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#if defined(_WIN32) && !defined(AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM)
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#if defined(_WIN32) && !defined(AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY)
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif

namespace hos {
namespace audio_out {
namespace {

std::atomic<bool> g_active{false};
MixFn g_mix = nullptr;

#ifdef _WIN32
// identifiants COM (définis ici : pas de dépendance à uuid.lib)
const GUID kCLSID_MMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const GUID kIID_IMMDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const GUID kIID_IAudioClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
const GUID kIID_IAudioRenderClient = {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};
const GUID kSUBTYPE_IEEE_FLOAT = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

// Nom du périphérique (PKEY_Device_FriendlyName) pour le journal : on sait ainsi où sort le son.
std::string device_name(IMMDevice* dev) {
    static const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
    std::string out = "?";
    IPropertyStore* ps = nullptr;
    if (FAILED(dev->OpenPropertyStore(STGM_READ, &ps)) || !ps) return out;
    PROPVARIANT v;
    PropVariantInit(&v);
    if (SUCCEEDED(ps->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, v.pwszVal, -1, nullptr, 0, nullptr, nullptr);
        if (n > 1) {
            out.assign((size_t)n - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, v.pwszVal, -1, &out[0], n, nullptr, nullptr);
        }
    }
    PropVariantClear(&v);
    ps->Release();
    return out;
}
#endif  // _WIN32

// ---------------------------------------------------------------- enregistrement WAV (diagnostic)
struct Wav {
    FILE* f = nullptr;
    uint64_t frames = 0, last_hdr = 0;
    void open(const char* path) {
        f = fopen(path, "wb");
        if (!f) {
            HLOG("audio", "RECOMP_AUDIO_WAV: cannot create %s", path);
            return;
        }
        header();
        HLOG("audio", "mix saved to %s (48 kHz, stereo, float)", path);
    }
    void header() {   // tailles réécrites régulièrement : fichier lisible même si le processus est tué
        const uint32_t data = (uint32_t)std::min<uint64_t>(frames * 8, 0xFFFFFF00u);
        struct {
            char riff[4];
            uint32_t size;
            char wave[4], fmt[4];
            uint32_t fmtsize;
            uint16_t tag, ch;
            uint32_t rate, bps;
            uint16_t align, bits;
            char dat[4];
            uint32_t datasize;
        } h = {{'R', 'I', 'F', 'F'}, 36 + data, {'W', 'A', 'V', 'E'}, {'f', 'm', 't', ' '}, 16, 3 /* IEEE float */, 2,
               48000, 48000 * 8, 8, 32, {'d', 'a', 't', 'a'}, data};
        fseek(f, 0, SEEK_SET);
        fwrite(&h, sizeof h, 1, f);
        fseek(f, 0, SEEK_END);
    }
    void write(const float* p, uint32_t n) {
        if (!f) return;
        fwrite(p, 8, n, f);
        frames += n;
        if (frames - last_hdr >= 24000) {   // toutes les 0,5 s
            last_hdr = frames;
            header();
            fflush(f);
        }
    }
};

#ifdef _WIN32
// ---------------------------------------------------------------- périphérique
struct Dev {
    IMMDeviceEnumerator* en = nullptr;
    IMMDevice* dev = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    HANDLE ev = nullptr;
    UINT32 frames = 0;

    void close() {
        g_active = false;
        if (client) client->Stop();
        if (render) render->Release();
        if (client) client->Release();
        if (dev) dev->Release();
        if (en) en->Release();
        if (ev) CloseHandle(ev);
        *this = Dev{};
    }
    bool fail(const char* what, HRESULT hr, bool quiet) {
        if (!quiet) HLOG("audio", "audio output: %s (0x%08lx)", what, (unsigned long)hr);
        close();
        return false;
    }
    bool open(bool quiet) {
        HRESULT hr = CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, kIID_IMMDeviceEnumerator, (void**)&en);
        if (FAILED(hr)) return fail("device enumerator unavailable", hr, quiet);
        hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
        if (FAILED(hr)) return fail("no output device", hr, quiet);
        hr = dev->Activate(kIID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&client);
        if (FAILED(hr)) return fail("audio client activation refused", hr, quiet);
        WAVEFORMATEX* mixf = nullptr;
        uint32_t dev_rate = 0, dev_ch = 0;
        if (SUCCEEDED(client->GetMixFormat(&mixf)) && mixf) {
            dev_rate = mixf->nSamplesPerSec, dev_ch = mixf->nChannels;
            CoTaskMemFree(mixf);
        }
        WAVEFORMATEXTENSIBLE wf{};
        wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wf.Format.nChannels = 2;
        wf.Format.nSamplesPerSec = 48000;
        wf.Format.wBitsPerSample = 32;
        wf.Format.nBlockAlign = 8;
        wf.Format.nAvgBytesPerSec = 48000 * 8;
        wf.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        wf.Samples.wValidBitsPerSample = 32;
        wf.dwChannelMask = 0x3;   // avant gauche + avant droite
        wf.SubFormat = kSUBTYPE_IEEE_FLOAT;
        // 40 ms de tampon côté Windows (période du moteur audio ≈ 10 ms)
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                    AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                400000, 0, &wf.Format, nullptr);
        if (FAILED(hr)) return fail("48 kHz stereo float format refused", hr, quiet);
        ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!ev || FAILED(client->SetEventHandle(ev))) return fail("render event", E_FAIL, quiet);
        if (FAILED(client->GetBufferSize(&frames)) || !frames) return fail("buffer size", E_FAIL, quiet);
        hr = client->GetService(kIID_IAudioRenderClient, (void**)&render);
        if (FAILED(hr)) return fail("service de rendu", hr, quiet);
        BYTE* p = nullptr;
        if (SUCCEEDED(render->GetBuffer(frames, &p))) render->ReleaseBuffer(frames, AUDCLNT_BUFFERFLAGS_SILENT);
        hr = client->Start();
        if (FAILED(hr)) return fail("start", hr, quiet);
        HLOG("audio", "Windows audio output opened on '%s': 48 kHz stereo float → device %u Hz, %u channels (buffer of %u frames)",
             device_name(dev).c_str(), dev_rate, dev_ch, frames);
        g_active = true;
        return true;
    }
};

void render_thread() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    static const char* vol_env = getenv("RECOMP_VOLUME");
    const float vol = vol_env ? std::clamp(atoi(vol_env), 0, 100) / 100.0f : 1.0f;
    Wav wav;
    if (const char* p = getenv("RECOMP_AUDIO_WAV")) wav.open(p);
    Dev d;
    int tries = 0;
    for (;;) {
        if (!d.client) {
            if (!d.open(tries++ > 0)) {   // premier échec journalisé, les nouveaux essais en silence
                Sleep(1000);
                continue;
            }
            tries = 0;
        }
        WaitForSingleObject(d.ev, 100);
        UINT32 pad = 0;
        HRESULT hr = d.client->GetCurrentPadding(&pad);
        if (FAILED(hr)) {   // périphérique débranché ou changé : réouverture (horloge virtuelle en attendant)
            HLOG("audio", "audio output lost (0x%08lx): reopening", (unsigned long)hr);
            d.close();
            continue;
        }
        const UINT32 n = d.frames - std::min(pad, d.frames);
        if (!n) continue;
        BYTE* p = nullptr;
        if (FAILED(d.render->GetBuffer(n, &p)) || !p) {
            d.close();
            continue;
        }
        float* out = (float*)p;
        g_mix(out, n);
        wav.write(out, n);
        if (vol != 1.0f)
            for (UINT32 i = 0; i < 2 * n; i++) out[i] *= vol;
        d.render->ReleaseBuffer(n, 0);
    }
}
#else
// ---------------------------------------------------------------- Linux : SDL3
Wav g_wav;
float g_vol = 1.0f;
std::vector<float> g_tampon;

void SDLCALL remplir(void*, SDL_AudioStream* s, int manque, int) {
    if (manque <= 0) return;
    const uint32_t n = (uint32_t)(manque / 8);   // trames stéréo flottantes
    if (!n) return;
    if (g_tampon.size() < 2 * (size_t)n) g_tampon.resize(2 * (size_t)n);
    float* out = g_tampon.data();
    g_mix(out, n);
    g_wav.write(out, n);
    if (g_vol != 1.0f)
        for (uint32_t i = 0; i < 2 * n; i++) out[i] *= g_vol;
    SDL_PutAudioStreamData(s, out, (int)(n * 8));
}

void ouvrir_sdl() {
    static const char* vol_env = getenv("RECOMP_VOLUME");
    g_vol = vol_env ? std::clamp(atoi(vol_env), 0, 100) / 100.0f : 1.0f;
    if (const char* p = getenv("RECOMP_AUDIO_WAV")) g_wav.open(p);
    for (int essai = 0;; essai++) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 48000};
            // ≈ 10 ms par rappel (comme la période de WASAPI)
            SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "480");
            if (SDL_AudioStream* s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, remplir, nullptr)) {
                SDL_AudioSpec dev{};
                int frames = 0;
                SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(s), &dev, &frames);
                const char* nom = SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(s));
                g_active = true;
                SDL_ResumeAudioStreamDevice(s);
                HLOG("audio", "SDL audio output opened on '%s' (%s): 48 kHz stereo float → device %d Hz, %d channels "
                              "(buffer of %d frames)", nom ? nom : "?", SDL_GetCurrentAudioDriver(), dev.freq, dev.channels,
                     frames);
                return;
            }
        }
        if (!essai) HLOG("audio", "audio output: %s; retrying every second", SDL_GetError());
        Sleep(1000);
    }
}
#endif

}  // namespace

void start(MixFn mix) {
    if (getenv("RECOMP_NO_AUDIO")) {
        HLOG("audio", "RECOMP_NO_AUDIO: no audio output (streams consumed by the virtual clock)");
        return;
    }
    g_mix = mix;
#ifdef _WIN32
    std::thread(render_thread).detach();
#else
    std::thread(ouvrir_sdl).detach();
#endif
}
bool active() { return g_active.load(std::memory_order_relaxed); }

}  // namespace audio_out
}  // namespace hos

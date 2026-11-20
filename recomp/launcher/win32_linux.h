// Launcher sous Linux : le sous-ensemble de l'API Win32 dont se sert le launcher (lanceur.cpp, manette.cpp, dessin.cpp),
// réalisé sur POSIX et SDL3 (win32_linux.cpp). Le code du launcher reste celui de Windows : la fenêtre est une fenêtre
// SDL dont les événements deviennent les messages de la procédure de fenêtre (WM_MOUSEMOVE, WM_KEYDOWN…), les
// processus sont lancés par fork/exec, lanceur.ini est lu et écrit comme par GetPrivateProfileString.
//
// Chemins : le launcher les compose avec le séparateur de la plateforme (ch(), SEP : lanceur.h), donc « / » ici.
// Par sécurité, toute fonction de fichier ou de processus d'ici change encore un « \ » égaré en « / » : un nom de
// fichier Linux peut contenir « \ », mais aucun de ceux du jeu.
#pragma once
#ifdef _WIN32
#error "win32_linux.h : Linux seulement"
#endif

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <mutex>
#include <string>
#include <vector>

// ---------------------------------------------------------------- types
typedef unsigned long DWORD;   // (%lu dans les journaux)
typedef int BOOL;
typedef unsigned int UINT;
typedef unsigned char BYTE;
typedef long LONG;
typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;
typedef uint32_t UINT32;
typedef uintptr_t UINT_PTR;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef long HRESULT;
typedef unsigned short WORD;
typedef wchar_t* PWSTR;
typedef void* HWND;
typedef void* HMODULE;
typedef void* HINSTANCE;
typedef void* HICON;
typedef void* HCURSOR;
typedef void* HBRUSH;
struct PlatObjet;
typedef PlatObjet* HANDLE;
#define TRUE 1
#define FALSE 0
#define WINAPI
#define CALLBACK
#define MAX_PATH 4096
#define INFINITE 0xFFFFFFFFul
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFul
#define FILE_ATTRIBUTE_DIRECTORY 0x10ul
#define FILE_ATTRIBUTE_NORMAL 0x80ul
#define S_OK ((HRESULT)0)
#define E_FAIL ((HRESULT)0x80004005L)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define STILL_ACTIVE 259ul
#define WAIT_OBJECT_0 0ul
#define WAIT_TIMEOUT 258ul

union LARGE_INTEGER {
    struct { DWORD LowPart; LONG HighPart; } u;
    LONGLONG QuadPart;
};
struct RECT { LONG left, top, right, bottom; };
struct POINT { LONG x, y; };
struct MSG { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; };
struct SYSTEMTIME { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; };
struct SYSTEM_INFO { DWORD dwNumberOfProcessors; };
struct MEMORYSTATUSEX {
    DWORD dwLength, dwMemoryLoad;
    ULONGLONG ullTotalPhys, ullAvailPhys, ullTotalPageFile, ullAvailPageFile, ullTotalVirtual, ullAvailVirtual,
        ullAvailExtendedVirtual;
};
struct FILETIME { DWORD dwLowDateTime, dwHighDateTime; };
struct WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow;
};
enum GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard };
struct WIN32_FIND_DATAW {
    DWORD dwFileAttributes;
    DWORD nFileSizeHigh, nFileSizeLow;
    wchar_t cFileName[1024];
};
struct SECURITY_ATTRIBUTES { DWORD nLength; void* lpSecurityDescriptor; BOOL bInheritHandle; };
struct STARTUPINFOW {
    DWORD cb, dwFlags;
    HANDLE hStdInput, hStdOutput, hStdError;
};
struct PROCESS_INFORMATION { HANDLE hProcess, hThread; DWORD dwProcessId, dwThreadId; };
struct JOBOBJECT_BASIC_LIMIT_INFORMATION { DWORD LimitFlags; };
struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION { JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation; };
enum JOBOBJECTINFOCLASS { JobObjectExtendedLimitInformation = 9 };
struct MONITORINFO { DWORD cbSize; RECT rcMonitor, rcWork; DWORD dwFlags; };
struct TRACKMOUSEEVENT { DWORD cbSize, dwFlags; HWND hwndTrack; DWORD dwHoverTime; };
struct MARGINS { int cxLeftWidth, cxRightWidth, cyTopHeight, cyBottomHeight; };
typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);
struct WNDCLASSEXW {
    UINT cbSize, style;
    WNDPROC lpfnWndProc;
    int cbClsExtra, cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    const wchar_t* lpszMenuName;
    const wchar_t* lpszClassName;
    HICON hIconSm;
};

// ---------------------------------------------------------------- constantes
#define CP_UTF8 65001
#define MOVEFILE_REPLACE_EXISTING 1ul
#define GENERIC_READ 0x80000000ul
#define GENERIC_WRITE 0x40000000ul
#define FILE_SHARE_READ 1ul
#define CREATE_NEW 1ul
#define OPEN_EXISTING 3ul
#define FILE_BEGIN 0ul
#define HANDLE_FLAG_INHERIT 1ul
#define STARTF_USESTDHANDLES 0x100ul
#define CREATE_SUSPENDED 0x4ul
#define CREATE_NEW_PROCESS_GROUP 0x200ul
#define CREATE_UNICODE_ENVIRONMENT 0x400ul
#define CREATE_BREAKAWAY_FROM_JOB 0x1000000ul
#define CREATE_NO_WINDOW 0x8000000ul
#define JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE 0x2000ul
#define NORMAL_PRIORITY_CLASS 0x20ul
#define BELOW_NORMAL_PRIORITY_CLASS 0x4000ul
#define MB_OK 0x0u
#define MB_YESNO 0x4u
#define MB_ICONERROR 0x10u
#define MB_ICONQUESTION 0x20u
#define MB_ICONWARNING 0x30u
#define MB_DEFBUTTON2 0x100u
#define IDOK 1
#define IDYES 6
#define IDNO 7
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SW_RESTORE 9
#define SWP_NOZORDER 0x4u
#define SWP_NOACTIVATE 0x10u
#define WS_POPUP 0x80000000ul
#define WS_SYSMENU 0x80000ul
#define WS_MINIMIZEBOX 0x20000ul
#define WS_EX_APPWINDOW 0x40000ul
#define PM_REMOVE 1u
#define QS_ALLINPUT 0x4FFu
#define TME_LEAVE 2ul
#define HTCLIENT 1
#define HTCAPTION 2
#define SIZE_MINIMIZED 1
#define SPI_GETWORKAREA 0x30u
#define SM_CXSMICON 49
#define SM_CYSMICON 50
#define IMAGE_ICON 1
#define BLACK_BRUSH 4
#define MONITOR_DEFAULTTONEAREST 2ul
#define MAPVK_VK_TO_VSC 0u
#define LOAD_WITH_ALTERED_SEARCH_PATH 0x8ul
#define LANG_FRENCH 0x0c
#define PRIMARYLANGID(l) ((WORD)(l) & 0x3ff)
#define IDC_ARROW ((const wchar_t*)32512)
#define IDC_HAND ((const wchar_t*)32649)
#define MAKEINTRESOURCEW(i) ((const wchar_t*)(uintptr_t)(i))
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((void*)-4)
#define LOWORD(l) ((WORD)((uintptr_t)(l) & 0xffff))
#define HIWORD(l) ((WORD)(((uintptr_t)(l) >> 16) & 0xffff))
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#define MAKELPARAM(l, h) ((LPARAM)(((uint32_t)(WORD)(l)) | (((uint32_t)(WORD)(h)) << 16)))

#define WM_DESTROY 0x0002u
#define WM_SIZE 0x0005u
#define WM_CLOSE 0x0010u
#define WM_QUIT 0x0012u
#define WM_SETCURSOR 0x0020u
#define WM_NCHITTEST 0x0084u
#define WM_KEYDOWN 0x0100u
#define WM_SYSKEYDOWN 0x0104u
#define WM_TIMER 0x0113u
#define WM_MOUSEMOVE 0x0200u
#define WM_LBUTTONDOWN 0x0201u
#define WM_LBUTTONUP 0x0202u
#define WM_MOUSELEAVE 0x02A3u
#define WM_DPICHANGED 0x02E0u
#define WM_APP 0x8000u

#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_ESCAPE 0x1B
#define VK_OEM_COMMA 0xBC

// ---------------------------------------------------------------- chaînes (CRT de Microsoft)
#define _wcsicmp wcscasecmp
#define _wcsnicmp wcsncasecmp
#define _vsnwprintf vswprintf
inline int _wtoi(const wchar_t* s) { return (int)wcstol(s, nullptr, 10); }
inline double _wtof(const wchar_t* s) { return wcstod(s, nullptr); }
const wchar_t* _wgetenv(const wchar_t* nom);   // copie large de getenv (gardée jusqu'à la fin)
FILE* _wfopen(const wchar_t* chemin, const wchar_t* mode);
int WideCharToMultiByte(UINT cp, DWORD, const wchar_t* w, int nw, char* s, int ns, const char*, BOOL*);
int MultiByteToWideChar(UINT cp, DWORD, const char* s, int ns, wchar_t* w, int nw);
std::string plat_chemin(const std::wstring& w);   // UTF-8, « \ » → « / »
std::string plat_chemin(const wchar_t* w);

// ---------------------------------------------------------------- système
DWORD GetLastError();
ULONGLONG GetTickCount64();
void Sleep(DWORD ms);
void GetLocalTime(SYSTEMTIME* t);
void GetSystemInfo(SYSTEM_INFO* s);
BOOL GlobalMemoryStatusEx(MEMORYSTATUSEX* m);
WORD GetUserDefaultUILanguage();
DWORD GetCurrentProcessId();
DWORD GetModuleFileNameW(HMODULE, wchar_t* b, DWORD n);
DWORD GetFullPathNameW(const wchar_t* p, DWORD n, wchar_t* b, wchar_t**);
DWORD GetTempPathW(DWORD n, wchar_t* b);
DWORD SearchPathW(const wchar_t*, const wchar_t* nom, const wchar_t*, DWORD n, wchar_t* b, wchar_t**);
BOOL SetEnvironmentVariableW(const wchar_t* nom, const wchar_t* v);
wchar_t* GetEnvironmentStringsW();
BOOL FreeEnvironmentStringsW(wchar_t* e);
inline unsigned timeBeginPeriod(unsigned) { return 0; }
inline unsigned timeEndPeriod(unsigned) { return 0; }
inline HRESULT CoInitializeEx(void*, DWORD) { return S_OK; }
inline void CoUninitialize() {}
inline BOOL SetProcessDpiAwarenessContext(void*) { return TRUE; }
#define COINIT_APARTMENTTHREADED 2ul
#define COINIT_MULTITHREADED 0ul
HMODULE LoadLibraryW(const wchar_t* nom);
HMODULE LoadLibraryExW(const wchar_t* nom, HANDLE, DWORD);
void* GetProcAddress(HMODULE m, const char* nom);
BOOL FreeLibrary(HMODULE m);

// ---------------------------------------------------------------- fichiers
DWORD GetFileAttributesW(const wchar_t* p);
BOOL GetFileAttributesExW(const wchar_t* p, GET_FILEEX_INFO_LEVELS, void* d);
BOOL MoveFileExW(const wchar_t* de, const wchar_t* vers, DWORD);
BOOL DeleteFileW(const wchar_t* p);
BOOL CreateDirectoryW(const wchar_t* p, void*);
int SHCreateDirectoryExW(HWND, const wchar_t* p, void*);   // dossier et ses parents
HANDLE FindFirstFileW(const wchar_t* motif, WIN32_FIND_DATAW* d);
BOOL FindNextFileW(HANDLE h, WIN32_FIND_DATAW* d);
BOOL FindClose(HANDLE h);
HANDLE CreateFileW(const wchar_t* p, DWORD acces, DWORD partage, SECURITY_ATTRIBUTES*, DWORD creation, DWORD, HANDLE);
BOOL ReadFile(HANDLE h, void* b, DWORD n, DWORD* lu, void*);
BOOL WriteFile(HANDLE h, const void* b, DWORD n, DWORD* ecrit, void*);
BOOL SetFilePointerEx(HANDLE h, LARGE_INTEGER d, LARGE_INTEGER* nouveau, DWORD origine);
BOOL CloseHandle(HANDLE h);
DWORD GetPrivateProfileStringW(const wchar_t* section, const wchar_t* cle, const wchar_t* def, wchar_t* b, DWORD n,
                               const wchar_t* fichier);
BOOL WritePrivateProfileStringW(const wchar_t* section, const wchar_t* cle, const wchar_t* v, const wchar_t* fichier);

// ---------------------------------------------------------------- processus
BOOL CreatePipe(HANDLE* lecture, HANDLE* ecriture, SECURITY_ATTRIBUTES*, DWORD);
BOOL SetHandleInformation(HANDLE h, DWORD masque, DWORD drapeaux);
BOOL CreateProcessW(const wchar_t* appli, wchar_t* cmd, void*, void*, BOOL heriter, DWORD drapeaux, void* env,
                    const wchar_t* dossier, STARTUPINFOW* si, PROCESS_INFORMATION* pi);
DWORD WaitForSingleObject(HANDLE h, DWORD ms);
BOOL GetExitCodeProcess(HANDLE h, DWORD* code);
DWORD ResumeThread(HANDLE);
BOOL SetPriorityClass(HANDLE h, DWORD classe);
HANDLE CreateJobObjectW(void*, const wchar_t*);
BOOL SetInformationJobObject(HANDLE, JOBOBJECTINFOCLASS, void*, DWORD);
BOOL AssignProcessToJobObject(HANDLE job, HANDLE proc);
BOOL TerminateJobObject(HANDLE job, UINT code);
HANDLE ShellExecuteW(HWND, const wchar_t* verbe, const wchar_t* fichier, const wchar_t* params, const wchar_t*, int);

// ---------------------------------------------------------------- fenêtre (SDL3)
// La fenêtre SDL est créée par CreateWindowExW ; ses événements sont rendus par PeekMessageW et passés à la procédure
// de la classe par DispatchMessageW. PostMessageW est sûre depuis tout fil (file des messages + réveil de SDL).
UINT RegisterClassExW(const WNDCLASSEXW* wc);
HWND CreateWindowExW(DWORD, const wchar_t* classe, const wchar_t* titre, DWORD, int x, int y, int w, int h, HWND, void*,
                     HINSTANCE, void*);
HWND FindWindowW(const wchar_t* classe, const wchar_t*);   // une seule instance (verrou dans XDG_RUNTIME_DIR)
BOOL DestroyWindow(HWND h);
BOOL ShowWindow(HWND h, int cmd);
BOOL UpdateWindow(HWND);
BOOL SetForegroundWindow(HWND h);
BOOL SetWindowPos(HWND h, HWND, int x, int y, int w, int hh, UINT drapeaux);
BOOL IsIconic(HWND h);
BOOL IsWindowVisible(HWND h);
HWND GetForegroundWindow();   // la fenêtre du launcher quand elle a le clavier, sinon nullptr
BOOL IsWindowEnabled(HWND h);   // faux pendant une question (MessageBoxW)
BOOL GetClientRect(HWND h, RECT* r);
BOOL ScreenToClient(HWND, POINT*);   // (WM_NCHITTEST reçoit déjà des coordonnées de la fenêtre)
BOOL PostMessageW(HWND h, UINT m, WPARAM w, LPARAM l);
void PostQuitMessage(int code);
BOOL PeekMessageW(MSG* msg, HWND, UINT, UINT, UINT);
BOOL TranslateMessage(const MSG*);
LRESULT DispatchMessageW(const MSG* msg);
LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM);
DWORD MsgWaitForMultipleObjects(DWORD, const HANDLE*, BOOL, DWORD ms, DWORD);
UINT_PTR SetTimer(HWND, UINT_PTR id, UINT ms, void*);
BOOL KillTimer(HWND, UINT_PTR id);
int MessageBoxW(HWND h, const wchar_t* texte, const wchar_t* titre, UINT type);
HCURSOR LoadCursorW(HINSTANCE, const wchar_t* nom);
HCURSOR SetCursor(HCURSOR c);
HWND SetCapture(HWND h);
BOOL ReleaseCapture();
BOOL TrackMouseEvent(TRACKMOUSEEVENT*);
UINT GetDpiForSystem();
UINT GetDpiForWindow(HWND h);
BOOL SystemParametersInfoW(UINT action, UINT, void* p, UINT);
void* MonitorFromWindow(HWND h, DWORD);
BOOL GetMonitorInfoW(void* m, MONITORINFO* mi);
UINT MapVirtualKeyW(UINT code, UINT type);
int GetKeyNameTextW(LONG lparam, wchar_t* b, int n);   // nom de la touche (disposition du clavier)
inline HICON LoadIconW(HINSTANCE, const wchar_t*) { return nullptr; }
inline void* LoadImageW(HINSTANCE, const wchar_t*, UINT, int, int, UINT) { return nullptr; }
inline int GetSystemMetrics(int) { return 16; }
inline void* GetStockObject(int) { return nullptr; }
inline HRESULT DwmSetWindowAttribute(HWND, DWORD, const void*, DWORD) { return S_OK; }
inline HRESULT DwmExtendFrameIntoClientArea(HWND, const MARGINS*) { return S_OK; }

// propres à Linux (lanceur.cpp)
std::wstring plat_choisir_fichier(HWND h, const wchar_t* titre, const wchar_t* filtre_nom, const char* motif,
                                  const std::wstring& dossier);   // vide : annulé
std::wstring plat_choisir_dossier(HWND h, const wchar_t* titre);
std::wstring plat_bureau();   // dossier du bureau (XDG_DESKTOP_DIR), vide si inconnu
std::wstring plat_exe_lanceur();   // ce qui lance le launcher : l'AppImage ($APPIMAGE) ou l'exécutable

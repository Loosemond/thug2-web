// user32 on SDL: the game's window is SDL's (sdl_window.cpp hands its SDL_Window* over as the HWND), its input comes
// through SDL's events (PartyMod's platform layer), so what's left here is the message loop's remains, the cursor,
// the screen's size and the keyboard's tables. COM has nothing to create.

#include <windows.h>

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstring>

namespace {

SDL_Window* SdlWindow(HWND window) { return reinterpret_cast<SDL_Window*>(window); }

// Scan code set 1 to virtual key, US layout (0: none).
const BYTE kScanToVk[0x60] = {
    0,    VK_ESCAPE, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 0xBD, 0xBB, VK_BACK, VK_TAB,     // 0x00
    'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',  'O',  'P',  0xDB, 0xDD, VK_RETURN, VK_CONTROL, 'A', 'S', // 0x10
    'D',  'F',  'G',  'H',  'J',  'K',  'L',  0xBA, 0xDE, 0xC0, VK_SHIFT, 0xDC, 'Z', 'X', 'C', 'V',       // 0x20
    'B',  'N',  'M',  0xBC, 0xBE, 0xBF, VK_SHIFT, VK_MULTIPLY, VK_MENU, VK_SPACE, VK_CAPITAL, VK_F1, VK_F2, VK_F3, VK_F4,
    VK_F5,                                                                                                   // 0x30
    VK_F6, VK_F7, VK_F8, VK_F9, VK_F10, VK_NUMLOCK, VK_SCROLL, VK_NUMPAD7, VK_NUMPAD8, VK_NUMPAD9, VK_SUBTRACT,
    VK_NUMPAD4, VK_NUMPAD5, VK_NUMPAD6, VK_ADD, VK_NUMPAD1,                                                  // 0x40
    VK_NUMPAD2, VK_NUMPAD3, VK_NUMPAD0, VK_DECIMAL, 0, 0, 0xE2, VK_F11, VK_F12, 0, 0, 0, 0, 0, 0, 0,         // 0x50
};

// A virtual key's unshifted character, US layout (0: none).
char VkToChar(UINT vk)
{
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z') || vk == ' ')
        return static_cast<char>(vk);
    switch (vk) {
    case 0xBA: return ';';
    case 0xBB: return '=';
    case 0xBC: return ',';
    case 0xBD: return '-';
    case 0xBE: return '.';
    case 0xBF: return '/';
    case 0xC0: return '`';
    case 0xDB: return '[';
    case 0xDC: return '\\';
    case 0xDD: return ']';
    case 0xDE: return '\'';
    case VK_MULTIPLY: return '*';
    case VK_ADD: return '+';
    case VK_SUBTRACT: return '-';
    case VK_DECIMAL: return '.';
    case VK_RETURN: return '\r';
    case VK_TAB: return '\t';
    case VK_BACK: return '\b';
    case VK_ESCAPE: return 0x1B;
    default: return 0;
    }
}

int g_cursorCount = 0;
bool g_quit = false;

} // namespace

extern "C" {

ATOM WINAPI RegisterClassExA(const WNDCLASSEXA*) { return 1; }
BOOL WINAPI UnregisterClassA(LPCSTR, HINSTANCE) { return TRUE; }
HWND WINAPI CreateWindowExA(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID)
{
    return nullptr; // (the SDL window is the game's: sdl_window.cpp)
}
BOOL WINAPI DestroyWindow(HWND) { return TRUE; }
BOOL WINAPI ShowWindow(HWND, int) { return TRUE; }
BOOL WINAPI UpdateWindow(HWND) { return TRUE; }
BOOL WINAPI SetForegroundWindow(HWND) { return TRUE; }
HWND WINAPI GetForegroundWindow(void) { return nullptr; }
BOOL WINAPI BringWindowToTop(HWND) { return TRUE; }
HWND WINAPI FindWindowA(LPCSTR, LPCSTR) { return nullptr; }
HWND WINAPI FindWindowExA(HWND, HWND, LPCSTR, LPCSTR) { return nullptr; } // (no other copy of the game running)
BOOL WINAPI InvalidateRect(HWND, const RECT*, BOOL) { return TRUE; }
BOOL WINAPI ValidateRect(HWND, const RECT*) { return TRUE; }
BOOL WINAPI AdjustWindowRect(LPRECT, DWORD, BOOL) { return TRUE; }
BOOL WINAPI AdjustWindowRectEx(LPRECT, DWORD, BOOL, DWORD) { return TRUE; }

BOOL WINAPI GetClientRect(HWND window, LPRECT rect)
{
    int width = 640, height = 480;
    if (window)
        SDL_GetWindowSize(SdlWindow(window), &width, &height);
    *rect = {0, 0, width, height};
    return TRUE;
}
BOOL WINAPI GetWindowRect(HWND window, LPRECT rect) { return GetClientRect(window, rect); }
BOOL WINAPI SetWindowPos(HWND, HWND, int, int, int, int, UINT) { return TRUE; }
BOOL WINAPI MoveWindow(HWND, int, int, int, int, BOOL) { return TRUE; }
BOOL WINAPI SetWindowTextA(HWND window, LPCSTR text)
{
    if (window)
        SDL_SetWindowTitle(SdlWindow(window), text);
    return TRUE;
}
BOOL WINAPI IsIconic(HWND) { return FALSE; }
BOOL WINAPI IsWindowVisible(HWND) { return TRUE; }
LONG WINAPI SetWindowLongA(HWND, int, LONG) { return 0; }
LONG WINAPI GetWindowLongA(HWND, int) { return 0; }

LRESULT WINAPI DefWindowProcA(HWND, UINT, WPARAM, LPARAM) { return 0; }
LRESULT WINAPI SendMessageA(HWND, UINT, WPARAM, LPARAM) { return 0; }
BOOL WINAPI PostMessageA(HWND, UINT message, WPARAM, LPARAM)
{
    if (message == WM_QUIT)
        g_quit = true;
    return TRUE;
}
void WINAPI PostQuitMessage(int) { g_quit = true; }
BOOL WINAPI PeekMessageA(LPMSG message, HWND, UINT, UINT, UINT remove)
{
    if (!g_quit)
        return FALSE;
    *message = {};
    message->message = WM_QUIT;
    if (remove & PM_REMOVE)
        g_quit = false;
    return TRUE;
}
BOOL WINAPI GetMessageA(LPMSG message, HWND window, UINT first, UINT last)
{
    while (!PeekMessageA(message, window, first, last, PM_REMOVE))
        SDL_Delay(10);
    return message->message != WM_QUIT;
}
BOOL WINAPI TranslateMessage(const MSG*) { return FALSE; }
LRESULT WINAPI DispatchMessageA(const MSG*) { return 0; }

int WINAPI MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT)
{
    std::fprintf(stderr, "[%s] %s\n", caption ? caption : "", text ? text : "");
    return 1; // IDOK
}

HCURSOR WINAPI LoadCursorA(HINSTANCE, LPCSTR) { return nullptr; }
HICON WINAPI LoadIconA(HINSTANCE, LPCSTR) { return nullptr; }
HCURSOR WINAPI SetCursor(HCURSOR) { return nullptr; }
int WINAPI ShowCursor(BOOL show)
{
    g_cursorCount += show ? 1 : -1;
    SDL_ShowCursor(g_cursorCount >= 0 ? SDL_ENABLE : SDL_DISABLE);
    return g_cursorCount;
}
BOOL WINAPI GetCursorPos(LPPOINT point)
{
    int x = 0, y = 0;
    SDL_GetMouseState(&x, &y);
    point->x = x;
    point->y = y;
    return TRUE;
}
BOOL WINAPI SetCursorPos(int, int) { return TRUE; }
BOOL WINAPI ClipCursor(const RECT*) { return TRUE; }
BOOL WINAPI ScreenToClient(HWND, LPPOINT) { return TRUE; }
BOOL WINAPI ClientToScreen(HWND, LPPOINT) { return TRUE; }

UINT WINAPI MapVirtualKeyA(UINT code, UINT type)
{
    switch (type) {
    case MAPVK_VSC_TO_VK:
    case MAPVK_VSC_TO_VK_EX:
        return code < sizeof(kScanToVk) ? kScanToVk[code] : 0;
    case MAPVK_VK_TO_VSC:
        for (UINT scan = 1; scan < sizeof(kScanToVk); ++scan)
            if (kScanToVk[scan] == code)
                return scan;
        return 0;
    case MAPVK_VK_TO_CHAR:
        return static_cast<BYTE>(VkToChar(code));
    default:
        return 0;
    }
}

SHORT WINAPI GetAsyncKeyState(int) { return 0; }
SHORT WINAPI GetKeyState(int) { return 0; }
int WINAPI GetKeyNameTextA(LONG, LPSTR buffer, int size)
{
    if (size > 0)
        buffer[0] = 0;
    return 0;
}

int WINAPI GetSystemMetrics(int index)
{
    SDL_DisplayMode mode;
    if (SDL_GetDesktopDisplayMode(0, &mode) != 0)
        mode.w = 1920, mode.h = 1080;
    switch (index) {
    case SM_CXSCREEN: return mode.w;
    case SM_CYSCREEN: return mode.h;
    default: return 0;
    }
}

BOOL WINAPI SystemParametersInfoA(UINT, UINT, PVOID pvParam, UINT)
{
    if (pvParam)
        *static_cast<UINT*>(pvParam) = 0;
    return TRUE;
}

BOOL WINAPI EnumDisplaySettingsA(LPCSTR, DWORD mode, DEVMODEA* devMode)
{
    if (mode != 0 && mode != static_cast<DWORD>(-1) /* ENUM_CURRENT_SETTINGS */)
        return FALSE;
    devMode->dmPelsWidth = static_cast<DWORD>(GetSystemMetrics(SM_CXSCREEN));
    devMode->dmPelsHeight = static_cast<DWORD>(GetSystemMetrics(SM_CYSCREEN));
    devMode->dmBitsPerPel = 32;
    devMode->dmDisplayFrequency = 60;
    return TRUE;
}
LONG WINAPI ChangeDisplaySettingsA(DEVMODEA*, DWORD) { return 0; }
HDC WINAPI GetDC(HWND) { return nullptr; }
int WINAPI ReleaseDC(HWND, HDC) { return 1; }

BOOL WINAPI OpenClipboard(HWND) { return FALSE; }
BOOL WINAPI CloseClipboard(void) { return TRUE; }
BOOL WINAPI EmptyClipboard(void) { return TRUE; }
HANDLE WINAPI GetClipboardData(UINT) { return nullptr; }
HANDLE WINAPI SetClipboardData(UINT, HANDLE) { return nullptr; }

HRESULT WINAPI CoInitialize(LPVOID) { return S_OK; }
HRESULT WINAPI CoInitializeEx(LPVOID, DWORD) { return S_OK; }
void WINAPI CoUninitialize(void) {}
HRESULT WINAPI CoCreateInstance(REFCLSID, IUnknown*, DWORD, REFIID, LPVOID* object)
{
    *object = nullptr;
    return static_cast<HRESULT>(0x80040154); // REGDB_E_CLASSNOTREG
}
void WINAPI CoTaskMemFree(LPVOID memory) { std::free(memory); }

} // extern "C"

// SDL's video side stood in for when running headless under Node (thug2-node; the Emscripten SDL port has no dummy
// video driver): a window that's only a size, no display but a 640x480 one - and no input but the key presses the
// environment variable THUG2_KEYS scripts, by frame: "600:Down 700:Return" (SDL key names; each held 6 frames).
// A frame is a Present (d3d9/device.cpp counts them).

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

extern "C" unsigned thug2_frames_presented;

namespace {
int g_window; // (its address is the window)
Uint8 g_keys[SDL_NUM_SCANCODES];

struct ScriptedKey {
    unsigned frame;
    SDL_Scancode scancode;
    bool down;
};
std::vector<ScriptedKey> g_script; // (by frame)
size_t g_next = 0;
std::deque<SDL_Event> g_events;

void ReadScript()
{
    const char* text = std::getenv("THUG2_KEYS");
    if (!text)
        return;
    std::string all = text;
    for (size_t at = 0; at < all.size();) {
        size_t end = all.find(' ', at);
        std::string item = all.substr(at, end == std::string::npos ? std::string::npos : end - at);
        at = end == std::string::npos ? all.size() : end + 1;
        size_t colon = item.find(':');
        if (colon == std::string::npos)
            continue;
        unsigned frame = static_cast<unsigned>(std::strtoul(item.c_str(), nullptr, 10));
        SDL_Scancode scancode = SDL_GetScancodeFromName(item.c_str() + colon + 1);
        if (scancode == SDL_SCANCODE_UNKNOWN) {
            std::fprintf(stderr, "THUG2_KEYS: no key %s\n", item.c_str() + colon + 1);
            continue;
        }
        g_script.push_back({frame, scancode, true});
        g_script.push_back({frame + 6, scancode, false});
    }
    std::stable_sort(g_script.begin(), g_script.end(),
                     [](const ScriptedKey& a, const ScriptedKey& b) { return a.frame < b.frame; });
}

// The keys due this frame: the keyboard's state and events.
void Play()
{
    while (g_next < g_script.size() && g_script[g_next].frame <= thug2_frames_presented) {
        const ScriptedKey& key = g_script[g_next++];
        g_keys[key.scancode] = key.down ? 1 : 0;
        SDL_Event event = {};
        event.key.type = key.down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.timestamp = SDL_GetTicks();
        event.key.windowID = 1;
        event.key.state = key.down ? SDL_PRESSED : SDL_RELEASED;
        event.key.keysym.scancode = key.scancode;
        event.key.keysym.sym = SDL_SCANCODE_TO_KEYCODE(key.scancode);
        if (key.scancode == SDL_SCANCODE_RETURN) event.key.keysym.sym = SDLK_RETURN;
        if (key.scancode == SDL_SCANCODE_ESCAPE) event.key.keysym.sym = SDLK_ESCAPE;
        if (key.scancode == SDL_SCANCODE_SPACE) event.key.keysym.sym = SDLK_SPACE;
        if (key.scancode >= SDL_SCANCODE_A && key.scancode <= SDL_SCANCODE_Z)
            event.key.keysym.sym = 'a' + (key.scancode - SDL_SCANCODE_A);
        std::fprintf(stderr, "THUG2_KEYS: frame %u %s %s\n", thug2_frames_presented, SDL_GetScancodeName(key.scancode),
                     key.down ? "down" : "up");
        g_events.push_back(event);
    }
}
} // namespace

extern "C" {

int __real_SDL_Init(Uint32 flags);
int __wrap_SDL_Init(Uint32 flags)
{
    return __real_SDL_Init(flags & ~(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC |
                                     SDL_INIT_EVENTS | SDL_INIT_AUDIO));
}
SDL_Window* __wrap_SDL_CreateWindow(const char*, int, int, int, int, Uint32)
{
    return reinterpret_cast<SDL_Window*>(&g_window);
}
void __wrap_SDL_GetWindowSize(SDL_Window*, int* width, int* height)
{
    if (width)
        *width = 640;
    if (height)
        *height = 480;
}
int __wrap_SDL_ShowCursor(int) { return 0; }
int __wrap_SDL_PollEvent(SDL_Event* event)
{
    static bool read = (ReadScript(), true);
    (void)read;
    if (g_events.empty()) {
        Play(); // (for the next polls)
        return 0;
    }
    if (event)
        *event = g_events.front();
    g_events.pop_front();
    return 1;
}
int __wrap_SDL_GetDesktopDisplayMode(int, SDL_DisplayMode* mode)
{
    *mode = {SDL_PIXELFORMAT_RGB888, 640, 480, 60, nullptr};
    return 0;
}
int __wrap_SDL_SetRelativeMouseMode(SDL_bool) { return 0; }
SDL_Window* __wrap_SDL_GetMouseFocus() { return nullptr; }
SDL_Window* __wrap_SDL_GetWindowFromID(Uint32) { return reinterpret_cast<SDL_Window*>(&g_window); }
void __wrap_SDL_SetWindowTitle(SDL_Window*, const char*) {}
int __wrap_SDL_NumJoysticks() { return 0; }
Uint32 __wrap_SDL_GetMouseState(int* x, int* y)
{
    if (x)
        *x = 0;
    if (y)
        *y = 0;
    return 0;
}
SDL_bool __wrap_SDL_GetWindowWMInfo(SDL_Window*, SDL_SysWMinfo*) { return SDL_FALSE; }
const Uint8* __wrap_SDL_GetKeyboardState(int* count)
{
    if (count)
        *count = SDL_NUM_SCANCODES;
    return g_keys;
}
} // extern "C"

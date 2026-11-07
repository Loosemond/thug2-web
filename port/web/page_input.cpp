// SDL's video side and input in the page. The game's thread is a worker, without the DOM: the page's thread takes the
// keyboard and mouse events (web/index.html) and hands them over here (thug2_page_* below, called on the page's
// thread), as the keyboard's state SDL_GetKeyboardState returns and as events for SDL_PollEvent. The window is the
// canvas, its size the one the page gave. The page's touch controls and the browser's gamepads are one game
// controller, the pad: the SDL_GameController calls the game makes are answered from its state.

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <emscripten/emscripten.h>

#include <atomic>
#include <deque>
#include <mutex>

namespace {

int g_window; // (its address is the window)
std::atomic<int> g_width{1280}, g_height{720};

Uint8 g_keys[SDL_NUM_SCANCODES];
std::atomic<int> g_mouseX{0}, g_mouseY{0};
std::atomic<Uint32> g_mouseButtons{0};

std::mutex g_lock;
std::deque<SDL_Event> g_events;

// The pad: index 0 and instance ID 0; it arrives (SDL_CONTROLLERDEVICEADDED) the first time the page sends it.
constexpr SDL_JoystickID kPad = 0;
int g_pad; // (its address is the controller)
std::atomic<bool> g_padAttached{false};
std::atomic<Uint32> g_padButtons{0}; // (bit per SDL_GameControllerButton)
std::atomic<Sint16> g_padAxes[SDL_CONTROLLER_AXIS_MAX];
std::atomic<int> g_padPlayer{-1};

void Push(const SDL_Event& event)
{
    std::lock_guard lock(g_lock);
    if (g_events.size() < 1024)
        g_events.push_back(event);
}

SDL_Window* Window() { return reinterpret_cast<SDL_Window*>(&g_window); }
SDL_GameController* Pad() { return reinterpret_cast<SDL_GameController*>(&g_pad); }

void PushPadEvent(Uint32 type, Uint8 button, Uint8 state)
{
    SDL_Event event = {};
    event.cbutton.type = type;
    event.cbutton.timestamp = SDL_GetTicks();
    event.cbutton.which = kPad;
    event.cbutton.button = button;
    event.cbutton.state = state;
    Push(event);
}

} // namespace

extern "C" {

// --- From the page (its thread) --------------------------------------------------------------------------------------

EMSCRIPTEN_KEEPALIVE void thug2_page_size(int width, int height)
{
    g_width = width;
    g_height = height;
}

// A key, by SDL scan code (the page maps KeyboardEvent.code), with its key code and the modifiers (SDL_Keymod).
EMSCRIPTEN_KEEPALIVE void thug2_page_key(int scancode, int keycode, int down, int repeat, int modifiers)
{
    if (scancode <= 0 || scancode >= SDL_NUM_SCANCODES)
        return;
    g_keys[scancode] = down ? 1 : 0;
    SDL_Event event = {};
    event.key.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.timestamp = SDL_GetTicks();
    event.key.windowID = 1;
    event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    event.key.repeat = repeat ? 1 : 0;
    event.key.keysym.scancode = static_cast<SDL_Scancode>(scancode);
    event.key.keysym.sym = keycode;
    event.key.keysym.mod = static_cast<Uint16>(modifiers);
    Push(event);
}

// Typed text (UTF-8, up to 31 bytes), for the game's text entry.
EMSCRIPTEN_KEEPALIVE void thug2_page_text(const char* text)
{
    SDL_Event event = {};
    event.text.type = SDL_TEXTINPUT;
    event.text.timestamp = SDL_GetTicks();
    event.text.windowID = 1;
    SDL_strlcpy(event.text.text, text, sizeof(event.text.text));
    Push(event);
}

// The mouse: its position on the canvas (in the game's pixels) and buttons (SDL_BUTTON masks); a button that changed
// (1 left, 2 middle, 3 right; 0 none) as an event.
EMSCRIPTEN_KEEPALIVE void thug2_page_mouse(int x, int y, int buttons, int button, int down)
{
    int dx = x - g_mouseX.exchange(x), dy = y - g_mouseY.exchange(y);
    g_mouseButtons = static_cast<Uint32>(buttons);
    SDL_Event event = {};
    if (button) {
        event.button.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        event.button.timestamp = SDL_GetTicks();
        event.button.windowID = 1;
        event.button.button = static_cast<Uint8>(button);
        event.button.state = down ? SDL_PRESSED : SDL_RELEASED;
        event.button.clicks = 1;
        event.button.x = x;
        event.button.y = y;
    } else {
        event.motion.type = SDL_MOUSEMOTION;
        event.motion.timestamp = SDL_GetTicks();
        event.motion.windowID = 1;
        event.motion.state = static_cast<Uint32>(buttons);
        event.motion.x = x;
        event.motion.y = y;
        event.motion.xrel = dx;
        event.motion.yrel = dy;
    }
    Push(event);
}

EMSCRIPTEN_KEEPALIVE void thug2_page_wheel(int y)
{
    SDL_Event event = {};
    event.wheel.type = SDL_MOUSEWHEEL;
    event.wheel.timestamp = SDL_GetTicks();
    event.wheel.windowID = 1;
    event.wheel.y = y;
    Push(event);
}

// The pad: its buttons (bit per SDL_GameControllerButton) and axes (SDL's ranges: sticks -32768..32767, triggers
// 0..32767), sent whenever they change.
EMSCRIPTEN_KEEPALIVE void thug2_page_pad(Uint32 buttons, int leftX, int leftY, int rightX, int rightY, int leftTrigger,
                                         int rightTrigger)
{
    if (!g_padAttached.exchange(true)) {
        SDL_Event event = {};
        event.cdevice.type = SDL_CONTROLLERDEVICEADDED;
        event.cdevice.timestamp = SDL_GetTicks();
        event.cdevice.which = 0; // (the device index)
        Push(event);
    }
    const int axes[SDL_CONTROLLER_AXIS_MAX] = {leftX, leftY, rightX, rightY, leftTrigger, rightTrigger};
    for (int i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i) {
        auto value = static_cast<Sint16>(SDL_clamp(axes[i], -32768, 32767));
        if (g_padAxes[i].exchange(value) == value)
            continue;
        SDL_Event event = {};
        event.caxis.type = SDL_CONTROLLERAXISMOTION;
        event.caxis.timestamp = SDL_GetTicks();
        event.caxis.which = kPad;
        event.caxis.axis = static_cast<Uint8>(i);
        event.caxis.value = value;
        Push(event);
    }
    Uint32 changed = g_padButtons.exchange(buttons) ^ buttons;
    for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; ++i) {
        if (changed & (1u << i)) {
            bool down = buttons & (1u << i);
            PushPadEvent(down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP, static_cast<Uint8>(i),
                         down ? SDL_PRESSED : SDL_RELEASED);
        }
    }
}

// The page gained or lost the focus: the keys are let go when it's lost (their key-ups go elsewhere).
EMSCRIPTEN_KEEPALIVE void thug2_page_focus(int focused)
{
    if (!focused)
        for (Uint8& key : g_keys)
            key = 0;
    SDL_Event event = {};
    event.window.type = SDL_WINDOWEVENT;
    event.window.timestamp = SDL_GetTicks();
    event.window.windowID = 1;
    event.window.event = focused ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST;
    Push(event);
}

// --- SDL, for the game's thread --------------------------------------------------------------------------------------

int __real_SDL_Init(Uint32 flags);
int __wrap_SDL_Init(Uint32 flags)
{
    return __real_SDL_Init(flags & ~(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC |
                                     SDL_INIT_EVENTS | SDL_INIT_AUDIO));
}
SDL_Window* __wrap_SDL_CreateWindow(const char*, int, int, int, int, Uint32) { return Window(); }
void __wrap_SDL_GetWindowSize(SDL_Window*, int* width, int* height)
{
    if (width)
        *width = g_width;
    if (height)
        *height = g_height;
}
int __wrap_SDL_ShowCursor(int) { return 0; }
int __wrap_SDL_PollEvent(SDL_Event* event)
{
    std::lock_guard lock(g_lock);
    if (g_events.empty())
        return 0;
    if (event) {
        *event = g_events.front();
        g_events.pop_front();
    }
    return 1;
}
const Uint8* __wrap_SDL_GetKeyboardState(int* count)
{
    if (count)
        *count = SDL_NUM_SCANCODES;
    return g_keys;
}
int __wrap_SDL_GetDesktopDisplayMode(int, SDL_DisplayMode* mode)
{
    *mode = {SDL_PIXELFORMAT_RGB888, g_width, g_height, 60, nullptr};
    return 0;
}
int __wrap_SDL_SetRelativeMouseMode(SDL_bool) { return 0; }
SDL_Window* __wrap_SDL_GetMouseFocus() { return Window(); }
SDL_Window* __wrap_SDL_GetWindowFromID(Uint32) { return Window(); }
void __wrap_SDL_SetWindowTitle(SDL_Window*, const char*) {}
int __wrap_SDL_NumJoysticks() { return 0; }
Uint32 __wrap_SDL_GetMouseState(int* x, int* y)
{
    if (x)
        *x = g_mouseX;
    if (y)
        *y = g_mouseY;
    return g_mouseButtons;
}
SDL_bool __wrap_SDL_GetWindowWMInfo(SDL_Window*, SDL_SysWMinfo*) { return SDL_FALSE; }

// The pad, as SDL's game controller (SDL's own joystick side is off: SDL_Init above). SDL_NumJoysticks stays 0 - the
// pad arrives as an event.
SDL_bool __wrap_SDL_IsGameController(int index) { return index == 0 ? SDL_TRUE : SDL_FALSE; }
const char* __wrap_SDL_GameControllerNameForIndex(int) { return "Page pad"; }
const char* __wrap_SDL_JoystickNameForIndex(int) { return "Page pad"; }
const char* __wrap_SDL_GameControllerName(SDL_GameController*) { return "Page pad"; }
SDL_GameController* __wrap_SDL_GameControllerOpen(int index) { return index == 0 ? Pad() : nullptr; }
SDL_GameController* __wrap_SDL_GameControllerFromInstanceID(SDL_JoystickID id) { return id == kPad ? Pad() : nullptr; }
void __wrap_SDL_GameControllerClose(SDL_GameController*) {}
SDL_bool __wrap_SDL_GameControllerGetAttached(SDL_GameController* controller)
{
    return controller == Pad() && g_padAttached ? SDL_TRUE : SDL_FALSE;
}
void __wrap_SDL_GameControllerSetPlayerIndex(SDL_GameController* controller, int player)
{
    if (controller == Pad())
        g_padPlayer = player;
}
int __wrap_SDL_GameControllerGetPlayerIndex(SDL_GameController* controller)
{
    return controller == Pad() ? g_padPlayer.load() : -1;
}
Uint8 __wrap_SDL_GameControllerGetButton(SDL_GameController* controller, SDL_GameControllerButton button)
{
    if (controller != Pad() || button < 0 || button >= SDL_CONTROLLER_BUTTON_MAX)
        return 0;
    return (g_padButtons >> button) & 1;
}
Sint16 __wrap_SDL_GameControllerGetAxis(SDL_GameController* controller, SDL_GameControllerAxis axis)
{
    if (controller != Pad() || axis < 0 || axis >= SDL_CONTROLLER_AXIS_MAX)
        return 0;
    return g_padAxes[axis];
}
SDL_Joystick* __wrap_SDL_GameControllerGetJoystick(SDL_GameController* controller)
{
    return reinterpret_cast<SDL_Joystick*>(controller);
}
int __wrap_SDL_JoystickRumble(SDL_Joystick*, Uint16, Uint16, Uint32) { return -1; }

} // extern "C"

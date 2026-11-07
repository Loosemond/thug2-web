#pragma once
// thug2-web: Win32 on Emscripten. Types and COM come from DXVK native; what the game uses beyond them is added here.
// (DXVK's MEMORYSTATUS has only two of its fields: renamed out of the way, the full one is in win32.h)
#define MEMORYSTATUS thug2web_dxvk_MEMORYSTATUS
#include_next <windows.h>
#undef MEMORYSTATUS
#include "thug2web/win32.h"

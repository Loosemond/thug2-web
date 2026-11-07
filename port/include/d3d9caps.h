#pragma once
// thug2-web: Wine's d3d9caps.h packs its structures to 4 bytes only on i386; wasm32 needs the same 32-bit Windows layout.
#pragma pack(push, 4)
#include_next <d3d9caps.h>
#pragma pack(pop)

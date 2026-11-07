#pragma once
// thug2-web: MSVC's _fsopen share modes. Sharing means nothing here.
#include <cstdio>
#define _SH_DENYRW 0x10
#define _SH_DENYWR 0x20
#define _SH_DENYRD 0x30
#define _SH_DENYNO 0x40
inline FILE* _fsopen(const char* name, const char* mode, int) { return thug2web_fopen(name, mode); }

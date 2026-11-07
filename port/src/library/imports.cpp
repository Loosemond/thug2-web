// The exe's import thunks the source still calls by address (0x005BEAAE-0x005BEB1A: `jmp [IAT]` to Winsock; Direct3DCreate9's is
// port/src/d3d9's), at those addresses (REPLACE: hook/game.h), and the GameSpy SDK functions the single-player game
// reaches (0x005F8160-0x00622E80, not decompiled). GameSpy's servers are long gone and online play isn't part of
// this build: the availability check says the back end is down.

#include <arpa/inet.h>
#include <netdb.h>

#include "hook/hook.h"

namespace {

unsigned long __stdcall InetAddr(const char* text) { return inet_addr(text); }
const char* __stdcall InetNtoa(in_addr address) { return inet_ntoa(address); }
void* __stdcall GetHostByName(const char*) { return nullptr; } // (no name lookups from the browser)

void __cdecl GsiStartAvailableCheck(const char*) {}
int __cdecl GsiAvailableCheckThink() { return 2; } // GSIACUnavailable
void __cdecl Msleep(unsigned) {}

} // namespace

REPLACE(0x005BEAAE, InetAddr);
REPLACE(0x005BEAB4, InetNtoa);
REPLACE(0x005BEABA, GetHostByName);

REPLACE(0x005F8160, Msleep);
REPLACE(0x005F8690, GsiStartAvailableCheck);
REPLACE(0x005F8840, GsiAvailableCheckThink);

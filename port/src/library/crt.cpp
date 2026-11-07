// The game's own C runtime (MSVC 7.1, statically linked at 0x00622F80-0x00640000, not decompiled) where the source still
// calls it by address, at those addresses (REPLACE: hook/game.h), on this build's C library. rand / srand are the MSVC
// runtime's generator, so the game draws the same sequence from a seed.

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <strings.h>

#include "hook/hook.h"

namespace {

uint32_t g_randState = 1;

void __cdecl Srand(unsigned seed) { g_randState = seed; }
int __cdecl Rand()
{
    g_randState = g_randState * 214013u + 2531011u;
    return static_cast<int>((g_randState >> 16) & 0x7FFF);
}

int __cdecl Sprintf(char* out, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int n = std::vsprintf(out, format, args);
    va_end(args);
    return n;
}

void* __cdecl Malloc(size_t size) { return std::malloc(size); }
void __cdecl Free(void* block) { std::free(block); }
void __cdecl Qsort(void* base, size_t count, size_t size, int(__cdecl* compare)(const void*, const void*))
{
    std::qsort(base, count, size, compare);
}

FILE* __cdecl Fopen(const char* name, const char* mode) { return thug2web_fopen(name, mode); }
int __cdecl Fclose(FILE* file) { return std::fclose(file); }
size_t __cdecl Fread(void* to, size_t size, size_t count, FILE* file) { return std::fread(to, size, count, file); }
int __cdecl Fseek(FILE* file, long offset, int origin) { return std::fseek(file, offset, origin); }
long __cdecl Ftell(FILE* file) { return std::ftell(file); }
double __cdecl Atof(const char* text) { return std::atof(text); }
int __cdecl Stricmp(const char* a, const char* b) { return strcasecmp(a, b); }

// time() with the 32-bit time_t of the original's runtime.
unsigned __cdecl Time(unsigned* out)
{
    auto now = static_cast<unsigned>(std::time(nullptr));
    if (out)
        *out = now;
    return now;
}

void __cdecl Exit(int code) { std::exit(code); }
void __cdecl ExitNow(int code) { std::_Exit(code); }
void __cdecl CExit() { std::fflush(nullptr); }

// MSVC 7.1's std::string (its allocator's byte, a 16-byte buffer or the pointer to the text, size, capacity) - the
// exe's at 0x0067E958 in .data - assigned a C string (0x004B7880).
struct MsvcString {
    uint32_t allocator;
    union {
        char buffer[16];
        char* text;
    };
    uint32_t size;
    uint32_t capacity;
    char* Text() { return capacity >= 16 ? text : buffer; }
};
static_assert(sizeof(MsvcString) == 0x1C);

MsvcString* __thiscall AssignString(MsvcString* string, const char* from)
{
    uint32_t length = static_cast<uint32_t>(std::strlen(from));
    if (length > string->capacity) {
        uint32_t capacity = length | 15;
        auto* text = static_cast<char*>(std::malloc(capacity + 1));
        if (string->capacity >= 16)
            std::free(string->text);
        string->text = text;
        string->capacity = capacity;
    }
    std::memmove(string->Text(), from, length + 1);
    string->size = length;
    return string;
}

} // namespace

REPLACE(0x004B7880, AssignString); // std::basic_string::operator=(const char*)
REPLACE(0x004D3BC0, Malloc);       // (a jump to malloc)
REPLACE(0x006234FE, Sprintf);
REPLACE(0x00623B96, Srand);
REPLACE(0x00623BA3, Rand);
REPLACE(0x00623E04, Fclose);
REPLACE(0x00623F3E, Fread);
REPLACE(0x00624031, Malloc);
REPLACE(0x006240D2, Fseek);
REPLACE(0x0062411B, Free);
REPLACE(0x006241E8, Fopen);
REPLACE(0x006242A0, Qsort);
REPLACE(0x00624AFE, Exit);
REPLACE(0x00624B0F, ExitNow);
REPLACE(0x00624B20, CExit);
REPLACE(0x00625138, Time);
REPLACE(0x00625388, Ftell);
REPLACE(0x006253C9, Atof);
REPLACE(0x00626EFB, Stricmp);

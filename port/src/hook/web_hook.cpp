// The web build's side of upstream's src/hook: what the game source calls there (the log, thug2-decomp.ini, the test
// runner's stages, the crash hooks), and the map from the original's addresses to our functions that the REPLACE
// lines fill (hook/game.h's GameFunction calls go through it).

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <emscripten/emscripten.h>

#include "hook/crash.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "thug2web/paths.h"
#include "hook/web_hook.h"
#include "hook/game_global.h"

// --- The log: stdout, which the page shows in the browser's console --------------------------------------------------

namespace {

constexpr unsigned kTailLines = 32;
constexpr size_t kTailWidth = 320;
char g_tail[kTailLines][kTailWidth];
unsigned g_tailCount = 0;
void (*g_listener)(const char*) = nullptr;

} // namespace

void LogOpen() {}
void LogToConsole() {}

void Log(const char* fmt, ...)
{
    char* line = g_tail[g_tailCount++ % kTailLines];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, kTailWidth, fmt, args);
    va_end(args);
    if (g_listener)
        g_listener(line);
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::putchar('\n');
}

std::vector<std::string> LogTail(size_t lines)
{
    size_t n = std::min<size_t>({lines, g_tailCount, kTailLines});
    std::vector<std::string> out;
    for (size_t i = n; i > 0; --i) {
        const char* line = g_tail[(g_tailCount - i) % kTailLines];
        out.emplace_back(line, strnlen(line, kTailWidth));
    }
    return out;
}

void SetLogListener(void (*listener)(const char* line)) { g_listener = listener; }
void SetCrashHook(void (*)()) {}
void* LogFileHandle() { return reinterpret_cast<void*>(intptr_t(-1)); }

std::string GameFolderPath(const char* file_name) { return std::string(Thug2Web::GameFolder()) + "/" + file_name; }

// thug2-decomp.ini: its defaults (nothing to read in the browser yet)
int ConfigInt(const char*, const char*, int fallback) { return fallback; }
bool ConfigSetInt(const char*, const char*, int) { return false; }

// --- The test runner (not in this build) -------------------------------------------------------------------------

void RegisterSelfTest(TestStage, const char*, void (*)(), void (*)()) {}
void RunStageOnce(TestStage) {}

// --- The original's addresses -> our functions -------------------------------------------------------------------
//
// A wasm function pointer is an index into the function table; the original's code addresses (a GameFunction call, an
// entry of one of the exe's vtables or tables loaded with its data) aren't. So every indirect call goes through
// thug2_code_slot first - port/tools/route_indirect_calls.mjs puts the call in front of each call_indirect after the
// link - which turns an address in the original's code (0x00401000-0x00644ED2) into the function a REPLACE line put
// there, and leaves the build's own function pointers (small indices) as they are. The map is one, in the shared
// memory, for every thread. (A table grown to the original's addresses instead, one per thread, cost Safari 320 MB
// each - more than a phone's browser gives a page.)

extern "C" uintptr_t thug2_code_offset = 0;
extern "C" uint32_t thug2_slots_filled[0x00645000 / 32] = {}; // (a bit per slot that has a function)

namespace {

constexpr uint32_t kCodeStart = 0x00401000;
constexpr uint32_t kCodeEnd = 0x00645000;

uint32_t g_codeSlots[kCodeEnd - kCodeStart]; // (the function at each code address, 0 for none)
std::mutex g_placeLock;
thread_local uint32_t t_trippedAt;

std::unordered_map<uint32_t, const char*>& Names()
{
    static std::unordered_map<uint32_t, const char*> names(16384);
    return names;
}

bool Filled(uint32_t address) { return (thug2_slots_filled[address >> 5] >> (address & 31)) & 1; }

// Under g_placeLock: the function at the address, for every thread.
void Add(uint32_t address, uint32_t function)
{
    g_codeSlots[address - kCodeStart] = function;
    thug2_slots_filled[address >> 5] |= 1u << (address & 31);
}

// A call through a code address with no function: named, then the trap (the old one, an empty table slot, said only
// "function signature mismatch"). The cast emulation lets any call reach it.
void Tripwire()
{
    std::fprintf(stderr, "tripwire: a call through 0x%08X, a code address with no function\n",
                 static_cast<unsigned>(t_trippedAt));
    __builtin_trap();
}

} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE uint32_t thug2_code_slot(uint32_t target)
{
    uint32_t slot = target - kCodeStart;
    if (slot >= kCodeEnd - kCodeStart)
        return target;
    if (uint32_t function = g_codeSlots[slot])
        return function;
    t_trippedAt = target;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&Tripwire));
}

bool Thug2Web::FunctionPlaced(uint32_t address)
{
    return address < kCodeEnd && ((thug2_slots_filled[address >> 5] >> (address & 31)) & 1);
}


// REPLACE lines that couldn't be placed (no function index: a virtual or adjusted member function), for the report.
std::vector<std::pair<uint32_t, const char*>>& Dropped()
{
    static std::vector<std::pair<uint32_t, const char*>> dropped;
    return dropped;
}

void RegisterReplacement(const Replacement& r)
{
    auto function = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(r.function));
    if (!function || r.address < kCodeStart || r.address >= kCodeEnd || function >= kCodeStart) {
        Dropped().push_back({r.address, r.name}); // (a virtual member function has no address of its own)
        return;
    }
    if (!Names().emplace(r.address, r.name).second)
        return; // (the first line for an address wins)
    std::lock_guard lock(g_placeLock);
    Add(r.address, function);
}

const std::vector<std::pair<uint32_t, const char*>>& Thug2Web::DroppedReplacements() { return Dropped(); }

// A call to an address with no function: a stand-in of the call's type (returning zero), from now on.
extern "C" void thug2_place_stub(uintptr_t address, void* stub, const char* type)
{
    std::lock_guard lock(g_placeLock);
    if (Filled(static_cast<uint32_t>(address))) // (another thread's call placed one first)
        return;
    if (address < kCodeStart || address >= kCodeEnd) {
        std::fprintf(stderr, "GameFunction 0x%08X: not a code address\n", static_cast<unsigned>(address));
        std::abort();
    }
    const char* signature = std::strstr(type, "Fn = ");
    std::fprintf(stderr, "GameFunction 0x%08X has no source: a stand-in that returns 0 (%.*s)\n",
                 static_cast<unsigned>(address), signature ? static_cast<int>(std::strcspn(signature + 5, "]")) : 1,
                 signature ? signature + 5 : "?");
    Add(static_cast<uint32_t>(address), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stub)));
}

const char* GameFunctionName(uintptr_t address)
{
    auto it = Names().find(static_cast<uint32_t>(address));
    return it != Names().end() ? it->second : nullptr;
}

void Thug2Web::ForEachReplacement(void (*visit)(uint32_t address, const char* name, void* context), void* context)
{
    for (const auto& [address, name] : Names())
        visit(address, name, context);
}

// --- The globals' start values (hook/game_global.h) ------------------------------------------------------------------
//
// Written at their addresses during start-up (after the exe's data: exe_image.cpp runs first), each range noted for
// the report of what the source covers of an encrypted .data.

namespace {

std::vector<Thug2Web::PlacedGlobal>& Placed()
{
    static std::vector<Thug2Web::PlacedGlobal> placed;
    return placed;
}

} // namespace

void PlaceGlobalStart(uint32_t address, const void* value, uint32_t size, const char* name)
{
    if (address < 0x00645000 || address + size > 0x007DD000) {
        std::fprintf(stderr, "GAME_GLOBAL %s at 0x%08X: outside the exe's data - not placed\n", name,
                     static_cast<unsigned>(address));
        return;
    }
    std::memcpy(reinterpret_cast<void*>(uintptr_t{address}), value, size);
    Placed().push_back({address, size, name});
}

const std::vector<Thug2Web::PlacedGlobal>& Thug2Web::PlacedGlobals() { return Placed(); }

// --- Writes over the exe's bytes ---------------------------------------------------------------------------------
//
// Into its data, loaded at its addresses (a script function table's entry, a host name); its code isn't there.
bool WriteGameCode(uint32_t address, const void* bytes, size_t size)
{
    if (address < 0x00645000 || address + size > 0x007DD000) {
        std::fprintf(stderr, "WriteGameCode 0x%08X (%u bytes): in the exe's code, not loaded - ignored\n",
                     static_cast<unsigned>(address), static_cast<unsigned>(size));
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(uintptr_t{address}), bytes, size);
    return true;
}

// --- Crash tests (the automation's "crash <kind>": not in this build) --------------------------------------------

bool ParseTestFault(const std::string&, TestFault*) { return false; }
const char* TestFaultName(TestFault) { return "?"; }
const char* TestFaultNames() { return "(none in the web build)"; }
bool CauseTestFault(TestFault) { return false; }

extern "C" void thug2_decomp_before_exit() { std::fflush(nullptr); }

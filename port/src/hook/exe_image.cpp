// The original exe's read-only data and data (.rdata, .data), loaded from the player's THUG2.exe at their own
// addresses (0x00645000-0x007DCA80) before anything else runs - from the game folder, or in the page from the bytes it
// fetched before starting the build (Module.thug2Exe: the folder is mounted later, on the game's thread). The source still reads some globals and tables there by
// address (GameVariable, raw string and table addresses); wasm memory is a flat space from 0, and the link keeps the low
// 8 MB free (-sGLOBAL_BASE=0x800000), so those addresses work unchanged. Only data is loaded: the code (.text) is
// SafeDisc-encrypted in the retail exe and never used.
//
// The retail exe's .data is encrypted too (the decomp's reference build has it readable): then it starts zeroed, and
// the source's own start values (GAME_GLOBAL) and the tables rebuilt from the source fill it (exe_data.cpp).

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <emscripten/em_js.h>
#include <emscripten/stack.h>

#include "thug2web/exe_data.h"
#include "thug2web/paths.h"

extern "C" char __global_base;

namespace {

constexpr uint32_t kImageBase = 0x00400000;
constexpr uint32_t kLimit = 0x00800000; // where the build's own memory starts

bool g_dataEncrypted = false;

// Bits per byte over the bytes: ~8 for encrypted (or compressed) data, well under for a program's data.
double Entropy(const uint8_t* bytes, size_t size)
{
    size_t counts[256] = {};
    for (size_t i = 0; i < size; ++i)
        ++counts[bytes[i]];
    double bits = 0;
    for (size_t count : counts)
        if (count) {
            double p = static_cast<double>(count) / static_cast<double>(size);
            bits -= p * __builtin_log2(p);
        }
    return bits;
}

template <typename T>
T Read(const std::vector<uint8_t>& file, size_t offset)
{
    T value;
    std::memcpy(&value, file.data() + offset, sizeof(value));
    return value;
}

EM_JS(int, PageExeSize, (), { return Module.thug2Exe ? Module.thug2Exe.byteLength : 0; });
EM_JS(void, TakePageExe, (void* to), {
    HEAPU8.set(new Uint8Array(Module.thug2Exe), to);
    delete Module.thug2Exe;
});

[[noreturn]] void Fail(const char* what)
{
    std::fprintf(stderr, "THUG2.exe: %s\n", what);
    std::abort();
}

__attribute__((constructor(101))) void LoadExeData()
{
    if (reinterpret_cast<uintptr_t>(&__global_base) < kLimit || emscripten_stack_get_end() < kLimit)
        Fail("the build's memory starts below 8 MB (link with -sGLOBAL_BASE=0x800000)");

    std::vector<uint8_t> file;
    size_t got = 0;
    if (int size = PageExeSize()) {
        file.resize(static_cast<size_t>(size));
        TakePageExe(file.data());
        got = file.size();
    } else {
        std::string path = std::string(Thug2Web::GameFolder()) + "/THUG2.exe";
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f)
            Fail("not found in the game folder");
        std::fseek(f, 0, SEEK_END);
        file.resize(static_cast<size_t>(std::ftell(f)));
        std::fseek(f, 0, SEEK_SET);
        got = std::fread(file.data(), 1, file.size(), f);
        std::fclose(f);
    }
    if (got != file.size() || file.size() < 0x400 || Read<uint16_t>(file, 0) != 0x5A4D)
        Fail("not an exe");

    uint32_t pe = Read<uint32_t>(file, 0x3C);
    if (Read<uint32_t>(file, pe) != 0x00004550 || Read<uint32_t>(file, pe + 24 + 28) != kImageBase)
        Fail("not the game's exe (no PE header, or not based at 0x400000)");
    uint16_t sections = Read<uint16_t>(file, pe + 6);
    size_t table = pe + 24 + Read<uint16_t>(file, pe + 20);

    int loaded = 0;
    for (uint16_t i = 0; i < sections; ++i) {
        size_t header = table + 40 * i;
        char name[9] = {};
        std::memcpy(name, file.data() + header, 8);
        if (std::strcmp(name, ".rdata") != 0 && std::strcmp(name, ".data") != 0)
            continue;
        uint32_t size = Read<uint32_t>(file, header + 8);
        uint32_t address = kImageBase + Read<uint32_t>(file, header + 12);
        uint32_t rawSize = Read<uint32_t>(file, header + 16);
        uint32_t rawOffset = Read<uint32_t>(file, header + 20);
        if (address + size > kLimit || rawOffset + std::min(rawSize, size) > file.size())
            Fail("a data section outside the expected range");
        auto* to = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(address));
        std::memset(to, 0, size);
        uint32_t initialised = std::min(rawSize, size);
        if (std::strcmp(name, ".data") == 0 && Entropy(file.data() + rawOffset, initialised) > 7.5) {
            g_dataEncrypted = true;
            std::fprintf(stderr, "THUG2.exe: its .data is encrypted (a retail CD build): starting from the source's "
                                 "values instead\n");
        } else {
            std::memcpy(to, file.data() + rawOffset, initialised);
        }
        ++loaded;
    }
    if (loaded != 2)
        Fail("no .rdata / .data sections");
}

} // namespace

bool Thug2Web::ExeDataEncrypted() { return g_dataEncrypted; }

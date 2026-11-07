#pragma once

#include <cstdint>

// A slot of the exe's import address table (in its .rdata, loaded at its own address - port/src/hook/exe_image.cpp)
// that the source calls through, given our function: IMPORT_SLOT(0x006452E8, BinkOpen). (SafeDisc's loader fills the
// original's slots; the file only has its own.) Runs after the exe's data is loaded (that constructor runs first).
namespace Thug2Web {
struct ImportSlot {
    template <typename F>
    ImportSlot(uint32_t slot, F* function)
    {
        *reinterpret_cast<uint32_t*>(uintptr_t{slot}) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(function));
    }
};
} // namespace Thug2Web

#define IMPORT_SLOT_CONCAT2(a, b) a##b
#define IMPORT_SLOT_CONCAT(a, b) IMPORT_SLOT_CONCAT2(a, b)
#define IMPORT_SLOT(slot, function) static Thug2Web::ImportSlot IMPORT_SLOT_CONCAT(g_importSlot_, __LINE__)(slot, &function)

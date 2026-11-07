#pragma once

// What port/src/hook/web_hook.cpp keeps that the rest of the port's hook side reads.

#include <cstdint>
#include <utility>
#include <vector>

namespace Thug2Web {

// Every REPLACE line's original address and its function's name as written ("Obj::CFunc_WaitForEvent").
void ForEachReplacement(void (*visit)(uint32_t address, const char* name, void* context), void* context);

// The globals whose start values were written at their addresses (GAME_GLOBAL), in the order written.
struct PlacedGlobal {
    uint32_t address;
    uint32_t size;
    const char* name;
};
const std::vector<PlacedGlobal>& PlacedGlobals();

// Whether the function table has a function at the original's code address.
bool FunctionPlaced(uint32_t address);
// REPLACE lines whose function couldn't be placed (no function index of its own: virtual or adjusted members).
const std::vector<std::pair<uint32_t, const char*>>& DroppedReplacements();

} // namespace Thug2Web

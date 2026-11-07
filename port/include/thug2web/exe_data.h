#pragma once

#include <cstdint>

namespace Thug2Web {

// Whether THUG2.exe's .data was encrypted (a retail CD build) and so left zeroed. (port/src/hook/exe_image.cpp)
bool ExeDataEncrypted();

// With the .data encrypted: what the source knows of it put in place - the script function table rebuilt from the
// REPLACE lines' CFunc_ functions, the member function names stood in for - and how much of the initialised .data the
// source's start values covered, logged. On the game's thread before WinMain. (port/src/hook/exe_data.cpp)
void RebuildEncryptedData();

// What the exe's static initialisers did before WinMain (the memory manager, ...), as far as the source has them.
void RunStaticInitialisers();

// The game code the exe's read-only data points at with no function placed (a call through it traps), counted - and
// listed to `path` (with the REPLACE lines that couldn't be placed) when given.
void ReportUnplacedCode(const char* path);

} // namespace Thug2Web

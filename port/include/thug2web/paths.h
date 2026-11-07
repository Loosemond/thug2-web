#pragma once

namespace Thug2Web {

// Where the game folder (THUG2.exe, Data/, ...) is in the build's file system: "/game", where the page mounts it - or,
// under Node, the THUG2_GAME environment variable's folder on the host.
const char* GameFolder();

// The game folder made ready, on the game's thread before WinMain: the page mounts its files (port/web), Node's are
// the host's already (port/node).
void PrepareGameFolder();

// Where the start-up report of unplaced code goes (Node: the THUG2_REPORT environment variable), or null.
const char* ReportPath();

} // namespace Thug2Web

#pragma once

#include <string>

namespace Win32 {

// A Windows path (backslashes, a drive, any case) as the path of what's on disk: "C:\THUG2\..." is the game folder,
// others are relative to the working directory; each name is matched in any case where it exists.
std::string PosixPath(const char* windowsPath);
// The other way, for paths the game is told (the working directory).
std::string WindowsPath(const std::string& posixPath);

} // namespace Win32

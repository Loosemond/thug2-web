#pragma once

#include <string>

namespace Thug2Web {

// The game folder (manifest.txt and the files, at baseUrl) mounted at `folder`; its files are fetched when first
// read, which only the game's threads (workers) may do.
bool MountGameFiles(const char* folder, const char* baseUrl);

// The URL a path under the game folder is fetched from (empty for one outside it).
std::string GameFileUrl(const std::string& posixPath);

// The files fetched since the last call and the time the game waited for them (ms) - for the page's stats.
void TakeFetchStats(unsigned& files, double& ms);

} // namespace Thug2Web

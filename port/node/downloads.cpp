#include "thug2web/downloads.h"

// (none under Node: the host's files are read as they're asked for)
std::shared_ptr<Thug2Web::Download> Thug2Web::DownloadGameFile(const std::string&) { return nullptr; }
std::shared_ptr<Thug2Web::Download> Thug2Web::DownloadStreamEntry(uint32_t, uint32_t) { return nullptr; }

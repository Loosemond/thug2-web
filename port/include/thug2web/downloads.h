#pragma once

// Game files fetched in the background: the page's own thread fetches them, so no game thread waits - for what the
// game asks for while it plays (a song, a voice line) and the original read from disk in no time. Where there are none
// (Node: the host's files, read as they're asked for) the functions return null.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Thug2Web {

class Download {
public:
    // The bytes are in (Failed: they won't come).
    bool Ready() const { return state.load(std::memory_order_acquire) == 1; }
    bool Failed() const { return state.load(std::memory_order_acquire) < 0; }

    std::string url;
    std::vector<uint8_t> bytes;    // (written by the page's thread until it's ready)
    std::atomic<int32_t> state{0}; // 0 on its way, 1 in, -1 failed
};

// The game file at `posixPath` (as PosixPath gives it), whole.
std::shared_ptr<Download> DownloadGameFile(const std::string& posixPath);

// pcm.wad's entry `checksum` (`size` bytes): a voice line or a sound stream, served as a file of its own
// (Data/streams/pcm/entries/<checksum>.bik - web/serve.py).
std::shared_ptr<Download> DownloadStreamEntry(uint32_t checksum, uint32_t size);

} // namespace Thug2Web

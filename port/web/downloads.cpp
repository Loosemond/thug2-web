// Game files fetched in the background (thug2web/downloads.h): the page's thread fetches them - fetch(), straight into
// the download's bytes in the wasm memory - while the game goes on. The browser keeps them as it keeps the others.

#include "thug2web/downloads.h"

#include <emscripten.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <mutex>

#include "thug2web/paths.h"
#include "web/game_files.h"

namespace {

EM_JS_DEPS(downloads, "$UTF8ToString");

// What's on its way: held until it's in, as the page's thread writes into it.
std::mutex g_lock;
std::vector<std::shared_ptr<Thug2Web::Download>> g_coming;

std::shared_ptr<Thug2Web::Download> Start(std::string url, size_t size)
{
    auto download = std::make_shared<Thug2Web::Download>();
    download->url = std::move(url);
    download->bytes.resize(size);
    {
        std::lock_guard lock(g_lock);
        g_coming.erase(std::remove_if(g_coming.begin(), g_coming.end(),
                                      [](const auto& coming) { return coming->state.load() != 0; }),
                       g_coming.end());
        g_coming.push_back(download);
    }
    MAIN_THREAD_ASYNC_EM_ASM({
        var url = UTF8ToString($0);
        var at = $1;
        var size = $2;
        var state = $3 >> 2;
        fetch(url).then(response => {
            if (!response.ok)
                throw new Error("HTTP " + response.status);
            return response.arrayBuffer();
        }).then(buffer => {
            var bytes = new Uint8Array(buffer);
            HEAPU8.set(bytes.subarray(0, size), at);
            Atomics.store(HEAP32, state, bytes.length >= size ? 1 : -1);
        }).catch(error => {
            console.warn(url + ": " + error);
            Atomics.store(HEAP32, state, -1);
        });
    }, download->url.c_str(), download->bytes.data(), static_cast<int>(size), &download->state);
    return download;
}

} // namespace

std::shared_ptr<Thug2Web::Download> Thug2Web::DownloadGameFile(const std::string& posixPath)
{
    std::string path = posixPath;
    char folder[512];
    if (!path.empty() && path[0] != '/' && getcwd(folder, sizeof(folder))) // (the game's paths are from its folder)
        path = std::string(folder) + "/" + path;
    std::string url = GameFileUrl(path);
    struct stat info;
    if (url.empty() || stat(path.c_str(), &info) != 0)
        return nullptr;
    return Start(std::move(url), static_cast<size_t>(info.st_size));
}

std::shared_ptr<Thug2Web::Download> Thug2Web::DownloadStreamEntry(uint32_t checksum, uint32_t size)
{
    char name[64];
    std::snprintf(name, sizeof(name), "/Data/streams/pcm/entries/%08x.bik", checksum);
    std::string url = GameFileUrl(GameFolder() + std::string(name));
    return url.empty() ? nullptr : Start(std::move(url), size);
}

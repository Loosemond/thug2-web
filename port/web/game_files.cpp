// The game folder in the browser: a WasmFS backend whose tree comes from the manifest (port/tools/make_manifest.py,
// served next to the page) and whose files fetch their bytes, whole, the first time they're read - a synchronous
// request, which the game's threads (all workers: the game runs off the page's thread) may make. What the game asks
// for while it plays - the songs, the voice lines - comes in the background instead (thug2web/downloads.h). What the
// game writes (its settings, saves) lives in memory beside them.

#include <emscripten.h>
#include <emscripten/wasmfs.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "memory_backend.h" // (WasmFS's own: system/lib/wasmfs)
#include "wasmfs.h"

#include "web/game_files.h"

namespace {

EM_JS_DEPS(game_files, "$UTF8ToString,$stringToNewUTF8");

std::atomic<unsigned> g_fetches{0};
std::atomic<uint64_t> g_fetchMicroseconds{0};

// The URL's bytes into `destination` (up to `size`); returns the whole length, or -1.
EM_JS(int, FetchInto, (const char* url, void* destination, int size), {
    var request = new XMLHttpRequest();
    request.open('GET', UTF8ToString(url), false);
    request.responseType = 'arraybuffer';
    request.send(null);
    if (request.status != 200)
        return -1;
    var bytes = new Uint8Array(request.response);
    HEAPU8.set(bytes.subarray(0, Math.min(bytes.length, size)), destination);
    return bytes.length;
});

// The URL's text (malloc'd), or null.
EM_JS(char*, FetchText, (const char* url), {
    var request = new XMLHttpRequest();
    request.open('GET', UTF8ToString(url), false);
    request.send(null);
    return request.status == 200 ? stringToNewUTF8(request.responseText) : 0;
});

// A file of the game's: its size from the manifest, its bytes from the server once read (then kept, and writable).
class GameFile final : public wasmfs::DataFile {
public:
    GameFile(std::string url, off_t size, wasmfs::backend_t backend)
        : DataFile(S_IFREG | 0666, backend), m_url(std::move(url)), m_size(size)
    {
    }

private:
    int open(wasmfs::oflags_t) override { return 0; }
    int close() override { return 0; }
    int flush() override { return 0; }
    off_t getSize() override { return m_loaded ? static_cast<off_t>(m_data.size()) : m_size; }
    int setSize(off_t size) override
    {
        if (int error = Load())
            return error;
        m_data.resize(size);
        return 0;
    }
    ssize_t read(uint8_t* buffer, size_t length, off_t offset) override
    {
        if (int error = Load())
            return error;
        if (offset >= static_cast<off_t>(m_data.size()))
            return 0;
        length = std::min(length, m_data.size() - static_cast<size_t>(offset));
        std::memcpy(buffer, m_data.data() + offset, length);
        return static_cast<ssize_t>(length);
    }
    ssize_t write(const uint8_t* buffer, size_t length, off_t offset) override
    {
        if (int error = Load())
            return error;
        if (offset + length > m_data.size())
            m_data.resize(offset + length);
        std::memcpy(m_data.data() + offset, buffer, length);
        return static_cast<ssize_t>(length);
    }

    int Load()
    {
        if (m_loaded)
            return 0;
        m_data.resize(m_size);
        double start = emscripten_get_now();
        int length = FetchInto(m_url.c_str(), m_data.data(), static_cast<int>(m_size));
        ++g_fetches;
        g_fetchMicroseconds += static_cast<uint64_t>((emscripten_get_now() - start) * 1000);
        if (length < 0)
            return -EIO;
        if (length != m_size) { // (the manifest is out of date: the server's file wins)
            m_data.resize(length);
            if (length > m_size)
                FetchInto(m_url.c_str(), m_data.data(), length);
        }
        m_loaded = true;
        return 0;
    }

    std::string m_url;
    off_t m_size;
    std::vector<uint8_t> m_data;
    bool m_loaded = false;
};

class GameDirectory final : public wasmfs::MemoryDirectory {
public:
    using MemoryDirectory::MemoryDirectory;
    void Add(const std::string& name, std::shared_ptr<wasmfs::File> child) { insertChild(name, std::move(child)); }
};

class GameBackend final : public wasmfs::Backend {
public:
    explicit GameBackend(std::string baseUrl) : m_baseUrl(std::move(baseUrl)) {}

    std::shared_ptr<wasmfs::DataFile> createFile(mode_t mode) override
    {
        return std::make_shared<wasmfs::MemoryDataFile>(mode, this);
    }
    std::shared_ptr<wasmfs::Directory> createDirectory(mode_t mode) override
    {
        auto directory = std::make_shared<GameDirectory>(mode, this);
        if (!m_populated) { // the first is the mount point: the game folder
            m_populated = true;
            Populate(*directory);
        }
        return directory;
    }
    std::shared_ptr<wasmfs::Symlink> createSymlink(std::string target) override
    {
        return std::make_shared<wasmfs::MemorySymlink>(target, this);
    }

private:
    // Each manifest line: "<size>\t<path>".
    void Populate(GameDirectory& root)
    {
        char* text = FetchText((m_baseUrl + "/manifest.txt").c_str());
        if (!text) {
            emscripten_err("game files: no manifest.txt");
            return;
        }
        std::map<std::string, GameDirectory*> directories{{"", &root}};
        std::vector<std::shared_ptr<GameDirectory>> keep; // (owned by their parents once added)
        auto directoryOf = [&](const std::string& path, auto& self) -> GameDirectory* {
            if (auto it = directories.find(path); it != directories.end())
                return it->second;
            size_t slash = path.rfind('/');
            GameDirectory* parent = self(slash == std::string::npos ? "" : path.substr(0, slash), self);
            auto directory = std::make_shared<GameDirectory>(S_IFDIR | 0777, this);
            parent->Add(slash == std::string::npos ? path : path.substr(slash + 1), directory);
            return directories[path] = directory.get();
        };
        size_t files = 0;
        for (char* line = text; *line;) {
            char* end = std::strchr(line, '\n');
            if (end)
                *end = 0;
            char* tab = std::strchr(line, '\t');
            if (tab) {
                std::string path(tab + 1);
                size_t slash = path.rfind('/');
                GameDirectory* parent = directoryOf(slash == std::string::npos ? "" : path.substr(0, slash),
                                                    directoryOf);
                std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
                parent->Add(name, std::make_shared<GameFile>(m_baseUrl + "/" + path, std::atoll(line), this));
                ++files;
            }
            if (!end)
                break;
            line = end + 1;
        }
        std::free(text);
        emscripten_errf("game files: %zu in the manifest", files);
    }

    std::string m_baseUrl;
    bool m_populated = false;
};

// Where it's mounted and where its files come from.
std::string g_folder, g_baseUrl;

} // namespace

namespace Thug2Web {

bool MountGameFiles(const char* folder, const char* baseUrl)
{
    g_folder = folder;
    g_baseUrl = baseUrl;
    wasmfs::backend_t backend = wasmfs::wasmFS.addBackend(std::make_unique<GameBackend>(baseUrl));
    return wasmfs_create_directory(folder, 0777, reinterpret_cast<::backend_t>(backend)) == 0;
}

std::string GameFileUrl(const std::string& posixPath)
{
    if (g_folder.empty() || posixPath.compare(0, g_folder.size(), g_folder) != 0 || posixPath.size() <= g_folder.size() ||
        posixPath[g_folder.size()] != '/')
        return {};
    return g_baseUrl + posixPath.substr(g_folder.size());
}

void TakeFetchStats(unsigned& files, double& ms)
{
    files = g_fetches.exchange(0);
    ms = static_cast<double>(g_fetchMicroseconds.exchange(0)) / 1000;
}

} // namespace Thug2Web

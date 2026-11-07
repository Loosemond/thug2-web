// The registry, in memory (keys and value names in any case), and .ini files ("profile" strings) on disk. The game
// keeps its settings under HKCU\Software\Activision\Tony Hawk's Underground 2 and writes defaults for what's missing.

#include <windows.h>

#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "win32/objects.h"
#include "win32/paths.h"

namespace {

std::string Lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

struct Value {
    DWORD type;
    std::vector<BYTE> data;
};

std::mutex g_lock;
std::map<std::string, std::map<std::string, Value>> g_keys; // by lower-case path ("hkcu\software\...")

struct Key : Win32::Object {
    std::string path;
};

std::string RootName(HKEY key)
{
    switch (reinterpret_cast<uintptr_t>(key)) {
    case 0x80000000u: return "hkcr";
    case 0x80000001u: return "hkcu";
    case 0x80000002u: return "hklm";
    case 0x80000003u: return "hku";
    default: return {};
    }
}

// The full path of `subKey` under `key` (a root or one of ours); empty if `key` is neither.
std::string PathOf(HKEY key, LPCSTR subKey)
{
    std::string base = RootName(key);
    if (base.empty()) {
        auto* ours = dynamic_cast<Key*>(Win32::ObjectOf(key));
        if (!ours)
            return {};
        base = ours->path;
    }
    if (subKey && *subKey)
        base += "\\" + Lower(subKey);
    while (!base.empty() && base.back() == '\\')
        base.pop_back();
    return base;
}

LSTATUS Open(HKEY key, LPCSTR subKey, HKEY* result, bool create)
{
    std::string path = PathOf(key, subKey);
    if (path.empty())
        return ERROR_INVALID_HANDLE;
    std::lock_guard lock(g_lock);
    if (!g_keys.count(path)) {
        if (!create)
            return ERROR_FILE_NOT_FOUND;
        g_keys[path];
    }
    auto* opened = new Key;
    opened->path = path;
    *result = reinterpret_cast<HKEY>(static_cast<Win32::Object*>(opened));
    return ERROR_SUCCESS;
}

LSTATUS Query(const std::string& path, LPCSTR name, LPDWORD type, LPBYTE data, LPDWORD size)
{
    std::lock_guard lock(g_lock);
    auto key = g_keys.find(path);
    if (key == g_keys.end())
        return ERROR_FILE_NOT_FOUND;
    auto value = key->second.find(Lower(name ? name : ""));
    if (value == key->second.end())
        return ERROR_FILE_NOT_FOUND;
    if (type)
        *type = value->second.type;
    DWORD needed = static_cast<DWORD>(value->second.data.size());
    if (data) {
        if (!size || *size < needed) {
            if (size)
                *size = needed;
            return ERROR_MORE_DATA;
        }
        std::memcpy(data, value->second.data.data(), needed);
    }
    if (size)
        *size = needed;
    return ERROR_SUCCESS;
}

} // namespace

extern "C" {

LSTATUS WINAPI RegOpenKeyExA(HKEY key, LPCSTR subKey, DWORD, REGSAM, HKEY* result)
{
    return Open(key, subKey, result, false);
}

LSTATUS WINAPI RegCreateKeyExA(HKEY key, LPCSTR subKey, DWORD, LPSTR, DWORD, REGSAM, SECURITY_ATTRIBUTES*,
                               HKEY* result, LPDWORD disposition)
{
    if (disposition)
        *disposition = 1; // REG_CREATED_NEW_KEY
    return Open(key, subKey, result, true);
}

LSTATUS WINAPI RegCreateKeyA(HKEY key, LPCSTR subKey, HKEY* result) { return Open(key, subKey, result, true); }

LSTATUS WINAPI RegQueryValueExA(HKEY key, LPCSTR name, LPDWORD, LPDWORD type, LPBYTE data, LPDWORD size)
{
    std::string path = PathOf(key, nullptr);
    return path.empty() ? ERROR_INVALID_HANDLE : Query(path, name, type, data, size);
}

LSTATUS WINAPI RegGetValueA(HKEY key, LPCSTR subKey, LPCSTR value, DWORD, LPDWORD type, PVOID data, LPDWORD size)
{
    std::string path = PathOf(key, subKey);
    return path.empty() ? ERROR_INVALID_HANDLE : Query(path, value, type, static_cast<LPBYTE>(data), size);
}

LSTATUS WINAPI RegSetValueExA(HKEY key, LPCSTR name, DWORD, DWORD type, const BYTE* data, DWORD size)
{
    std::string path = PathOf(key, nullptr);
    if (path.empty())
        return ERROR_INVALID_HANDLE;
    std::lock_guard lock(g_lock);
    g_keys[path][Lower(name ? name : "")] = {type, std::vector<BYTE>(data, data + size)};
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegDeleteValueA(HKEY key, LPCSTR name)
{
    std::string path = PathOf(key, nullptr);
    std::lock_guard lock(g_lock);
    auto it = g_keys.find(path);
    if (it == g_keys.end() || !it->second.erase(Lower(name ? name : "")))
        return ERROR_FILE_NOT_FOUND;
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegCloseKey(HKEY key)
{
    if (RootName(key).empty())
        delete dynamic_cast<Key*>(Win32::ObjectOf(key));
    return ERROR_SUCCESS;
}

// --- .ini files ---------------------------------------------------------------------------------------------------

namespace {

// The value of `key` in `section` of `file`, if it's there.
bool ProfileString(LPCSTR section, LPCSTR key, LPCSTR file, std::string* out)
{
    std::ifstream in(Win32::PosixPath(file));
    std::string line, current;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == ';')
            continue;
        if (line[start] == '[') {
            size_t end = line.find(']', start);
            current = Lower(line.substr(start + 1, end == std::string::npos ? std::string::npos : end - start - 1));
            continue;
        }
        size_t equals = line.find('=', start);
        if (equals == std::string::npos || current != Lower(section))
            continue;
        std::string name = line.substr(start, equals - start);
        name.erase(name.find_last_not_of(" \t") + 1);
        if (Lower(name) != Lower(key))
            continue;
        std::string value = line.substr(equals + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        *out = value;
        return true;
    }
    return false;
}

} // namespace

UINT WINAPI GetPrivateProfileIntA(LPCSTR section, LPCSTR key, INT fallback, LPCSTR file)
{
    std::string value;
    if (!ProfileString(section, key, file, &value))
        return static_cast<UINT>(fallback);
    return static_cast<UINT>(std::strtol(value.c_str(), nullptr, 10));
}

DWORD WINAPI GetPrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR fallback, LPSTR buffer, DWORD size,
                                      LPCSTR file)
{
    std::string value;
    if (!section || !key || !ProfileString(section, key, file, &value))
        value = fallback ? fallback : "";
    if (size == 0)
        return 0;
    size_t n = std::min<size_t>(value.size(), size - 1);
    std::memcpy(buffer, value.data(), n);
    buffer[n] = 0;
    return static_cast<DWORD>(n);
}

BOOL WINAPI WritePrivateProfileStringA(LPCSTR, LPCSTR, LPCSTR, LPCSTR) { return FALSE; } // (read-only for now)

DWORD WINAPI GetPrivateProfileSectionA(LPCSTR, LPSTR buffer, DWORD size, LPCSTR)
{
    if (size >= 2)
        buffer[0] = buffer[1] = 0;
    return 0;
}

} // extern "C"

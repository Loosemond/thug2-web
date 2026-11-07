// Win32 files on POSIX: Windows paths (backslashes, any case) resolved against the real file system, file handles,
// directory searches, attributes. The game folder is the game's "C:\THUG2" (GetModuleFileNameA says the exe is
// there) and the working directory starts in it.

#include <windows.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "thug2web/paths.h"
#include "win32/objects.h"
#include "win32/paths.h"

namespace Win32 {

namespace {

constexpr char kWindowsGameFolder[] = "C:\\THUG2";

std::string Lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// Directory listings by lower-case name, for matching names in any case. Kept until something in the directory is
// created or removed.
std::mutex g_listingsLock;
std::unordered_map<std::string, std::unordered_map<std::string, std::string>> g_listings;

const std::unordered_map<std::string, std::string>& Listing(const std::string& directory)
{
    auto it = g_listings.find(directory);
    if (it != g_listings.end())
        return it->second;
    auto& names = g_listings[directory];
    if (DIR* dir = opendir(directory.empty() ? "." : directory.c_str())) {
        while (dirent* entry = readdir(dir))
            names.emplace(Lower(entry->d_name), entry->d_name);
        closedir(dir);
    }
    return names;
}

void Forget(const std::string& posixPath)
{
    std::lock_guard lock(g_listingsLock);
    size_t slash = posixPath.rfind('/');
    g_listings.erase(slash == std::string::npos ? std::string() : posixPath.substr(0, slash == 0 ? 1 : slash));
}

} // namespace

std::string PosixPath(const char* windowsPath)
{
    std::string path = windowsPath ? windowsPath : "";
    std::replace(path.begin(), path.end(), '\\', '/');
    std::string lower = Lower(path);
    std::string prefix = Lower(kWindowsGameFolder);
    std::replace(prefix.begin(), prefix.end(), '\\', '/');
    if (lower.compare(0, prefix.size(), prefix) == 0 && (lower.size() == prefix.size() || lower[prefix.size()] == '/'))
        path = Thug2Web::GameFolder() + path.substr(prefix.size());
    else if (path.size() >= 2 && path[1] == ':') // (another drive: under /drive_<letter>)
        path = "/drive_" + Lower(path.substr(0, 1)) + path.substr(2);

    // Each component as it is on disk, in any case.
    std::lock_guard lock(g_listingsLock);
    std::string resolved = path[0] == '/' ? "/" : "";
    size_t start = path[0] == '/' ? 1 : 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos)
            end = path.size();
        std::string part = path.substr(start, end - start);
        start = end + 1;
        if (part.empty() || part == ".")
            continue;
        std::string directory = resolved.empty() ? "" : (resolved == "/" ? "/" : resolved.substr(0, resolved.size() - 1));
        if (part != "..") {
            const auto& names = Listing(directory);
            auto it = names.find(Lower(part));
            if (it != names.end())
                part = it->second;
        }
        resolved += part + "/";
    }
    if (resolved.size() > 1)
        resolved.pop_back();
    return resolved.empty() ? "." : resolved;
}

std::string WindowsPath(const std::string& posixPath)
{
    std::string path = posixPath;
    std::string folder = Thug2Web::GameFolder();
    if (path.compare(0, folder.size(), folder) == 0 && (path.size() == folder.size() || path[folder.size()] == '/'))
        path = kWindowsGameFolder + path.substr(folder.size());
    std::replace(path.begin(), path.end(), '/', '\\');
    return path;
}

namespace {

struct File : Object {
    int fd;
    std::string path;
    ~File() override { close(fd); }
};

struct Find : Object {
    std::vector<WIN32_FIND_DATAA> entries;
    size_t next = 0;
};

FILETIME FileTime(const timespec& time)
{
    uint64_t ticks = (static_cast<uint64_t>(time.tv_sec) + 11644473600ull) * 10000000ull + time.tv_nsec / 100;
    return {static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
}

DWORD Attributes(const struct stat& info)
{
    DWORD attributes = S_ISDIR(info.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    if (!(info.st_mode & S_IWUSR))
        attributes |= FILE_ATTRIBUTE_READONLY;
    return attributes;
}

DWORD ErrorOf(int error)
{
    switch (error) {
    case ENOENT: return ERROR_FILE_NOT_FOUND;
    case ENOTDIR: return ERROR_PATH_NOT_FOUND;
    case EACCES: case EPERM: return ERROR_ACCESS_DENIED;
    case EEXIST: return ERROR_ALREADY_EXISTS;
    default: return ERROR_INVALID_PARAMETER;
    }
}

bool Matches(const char* pattern, const char* name)
{
    for (; *pattern; ++pattern, ++name) {
        if (*pattern == '*') {
            while (pattern[1] == '*')
                ++pattern;
            if (!pattern[1])
                return true;
            for (; *name; ++name)
                if (Matches(pattern + 1, name))
                    return true;
            return false;
        }
        if (!*name)
            return false;
        if (*pattern != '?' && std::tolower(static_cast<unsigned char>(*pattern)) !=
                                   std::tolower(static_cast<unsigned char>(*name)))
            return false;
    }
    return !*name;
}

} // namespace

} // namespace Win32

using namespace Win32;

const char* thug2web_path(const char* windowsPath, char* buffer, size_t size)
{
    std::snprintf(buffer, size, "%s", PosixPath(windowsPath).c_str());
    return buffer;
}

extern "C" {

HANDLE WINAPI CreateFileA(LPCSTR name, DWORD access, DWORD, SECURITY_ATTRIBUTES*, DWORD disposition, DWORD, HANDLE)
{
    std::string path = PosixPath(name);
    int flags = (access & GENERIC_WRITE) ? ((access & GENERIC_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
    switch (disposition) {
    case CREATE_NEW: flags |= O_CREAT | O_EXCL; break;
    case CREATE_ALWAYS: flags |= O_CREAT | O_TRUNC; break;
    case OPEN_ALWAYS: flags |= O_CREAT; break;
    case TRUNCATE_EXISTING: flags |= O_TRUNC; break;
    default: break;
    }
    int fd = open(path.c_str(), flags, 0644);
    if (fd < 0) {
        SetLastError(ErrorOf(errno));
        return INVALID_HANDLE_VALUE;
    }
    if (flags & O_CREAT)
        Forget(path);
    auto* file = new File;
    file->fd = fd;
    file->path = path;
    SetLastError(0);
    return file;
}

// Overlapped reads and writes complete at once (the game's async loader only uses them on Vista and later, and this
// says XP): the result is in the OVERLAPPED and its event is set.
BOOL WINAPI ReadFile(HANDLE handle, LPVOID buffer, DWORD size, LPDWORD read, LPOVERLAPPED overlapped)
{
    auto* file = dynamic_cast<File*>(ObjectOf(handle));
    if (!file)
        return FALSE;
    ssize_t got = overlapped ? pread(file->fd, buffer, size, (static_cast<off_t>(overlapped->OffsetHigh) << 32) |
                                                                 overlapped->Offset)
                             : ::read(file->fd, buffer, size);
    if (got < 0) {
        SetLastError(ErrorOf(errno));
        return FALSE;
    }
    if (read)
        *read = static_cast<DWORD>(got);
    if (overlapped) {
        overlapped->Internal = 0;
        overlapped->InternalHigh = static_cast<ULONG_PTR>(got);
        if (overlapped->hEvent)
            SetEvent(overlapped->hEvent);
    }
    return TRUE;
}

BOOL WINAPI WriteFile(HANDLE handle, LPCVOID buffer, DWORD size, LPDWORD written, LPOVERLAPPED overlapped)
{
    auto* file = dynamic_cast<File*>(ObjectOf(handle));
    if (!file)
        return FALSE;
    ssize_t put = overlapped ? pwrite(file->fd, buffer, size, (static_cast<off_t>(overlapped->OffsetHigh) << 32) |
                                                                  overlapped->Offset)
                             : ::write(file->fd, buffer, size);
    if (put < 0) {
        SetLastError(ErrorOf(errno));
        return FALSE;
    }
    if (written)
        *written = static_cast<DWORD>(put);
    if (overlapped) {
        overlapped->Internal = 0;
        overlapped->InternalHigh = static_cast<ULONG_PTR>(put);
        if (overlapped->hEvent)
            SetEvent(overlapped->hEvent);
    }
    return TRUE;
}

BOOL WINAPI GetOverlappedResult(HANDLE, LPOVERLAPPED overlapped, LPDWORD transferred, BOOL)
{
    *transferred = static_cast<DWORD>(overlapped->InternalHigh);
    return TRUE;
}

BOOL WINAPI CancelIo(HANDLE) { return TRUE; }

DWORD WINAPI SetFilePointer(HANDLE handle, LONG distance, PLONG distanceHigh, DWORD method)
{
    auto* file = dynamic_cast<File*>(ObjectOf(handle));
    if (!file)
        return INVALID_SET_FILE_POINTER;
    off_t offset = distanceHigh ? (static_cast<off_t>(*distanceHigh) << 32) | static_cast<DWORD>(distance) : distance;
    off_t at = lseek(file->fd, offset, method == FILE_CURRENT ? SEEK_CUR : method == FILE_END ? SEEK_END : SEEK_SET);
    if (at < 0) {
        SetLastError(ErrorOf(errno));
        return INVALID_SET_FILE_POINTER;
    }
    if (distanceHigh)
        *distanceHigh = static_cast<LONG>(at >> 32);
    return static_cast<DWORD>(at);
}

BOOL WINAPI SetFilePointerEx(HANDLE handle, LARGE_INTEGER distance, LARGE_INTEGER* position, DWORD method)
{
    auto* file = dynamic_cast<File*>(ObjectOf(handle));
    if (!file)
        return FALSE;
    off_t at = lseek(file->fd, distance.QuadPart,
                     method == FILE_CURRENT ? SEEK_CUR : method == FILE_END ? SEEK_END : SEEK_SET);
    if (at < 0)
        return FALSE;
    if (position)
        position->QuadPart = at;
    return TRUE;
}

BOOL WINAPI SetEndOfFile(HANDLE handle)
{
    auto* file = dynamic_cast<File*>(ObjectOf(handle));
    return file && ftruncate(file->fd, lseek(file->fd, 0, SEEK_CUR)) == 0;
}

DWORD WINAPI GetFileSize(HANDLE handle, LPDWORD sizeHigh)
{
    auto* file = dynamic_cast<File*>(ObjectOf(handle));
    struct stat info;
    if (!file || fstat(file->fd, &info) != 0)
        return INVALID_FILE_SIZE;
    if (sizeHigh)
        *sizeHigh = static_cast<DWORD>(static_cast<uint64_t>(info.st_size) >> 32);
    return static_cast<DWORD>(info.st_size);
}

BOOL WINAPI GetFileSizeEx(HANDLE handle, LARGE_INTEGER* size)
{
    auto* file = dynamic_cast<File*>(ObjectOf(handle));
    struct stat info;
    if (!file || fstat(file->fd, &info) != 0)
        return FALSE;
    size->QuadPart = info.st_size;
    return TRUE;
}

BOOL WINAPI GetFileTime(HANDLE handle, LPFILETIME creation, LPFILETIME access, LPFILETIME write)
{
    auto* file = dynamic_cast<File*>(ObjectOf(handle));
    struct stat info;
    if (!file || fstat(file->fd, &info) != 0)
        return FALSE;
    if (creation)
        *creation = FileTime(info.st_mtim);
    if (access)
        *access = FileTime(info.st_atim);
    if (write)
        *write = FileTime(info.st_mtim);
    return TRUE;
}

BOOL WINAPI FlushFileBuffers(HANDLE handle)
{
    auto* file = dynamic_cast<File*>(ObjectOf(handle));
    return file && fsync(file->fd) == 0;
}

BOOL WINAPI DeleteFileA(LPCSTR name)
{
    std::string path = PosixPath(name);
    Forget(path);
    return unlink(path.c_str()) == 0;
}

BOOL WINAPI MoveFileExA(LPCSTR from, LPCSTR to, DWORD)
{
    std::string a = PosixPath(from), b = PosixPath(to);
    Forget(a);
    Forget(b);
    return rename(a.c_str(), b.c_str()) == 0;
}
BOOL WINAPI MoveFileA(LPCSTR from, LPCSTR to) { return MoveFileExA(from, to, 0); }

BOOL WINAPI CopyFileA(LPCSTR from, LPCSTR to, BOOL failIfExists)
{
    std::string a = PosixPath(from), b = PosixPath(to);
    int in = open(a.c_str(), O_RDONLY);
    if (in < 0)
        return FALSE;
    int out = open(b.c_str(), O_WRONLY | O_CREAT | (failIfExists ? O_EXCL : O_TRUNC), 0644);
    if (out < 0) {
        close(in);
        return FALSE;
    }
    char buffer[65536];
    ssize_t n;
    bool ok = true;
    while ((n = ::read(in, buffer, sizeof(buffer))) > 0)
        ok &= ::write(out, buffer, n) == n;
    close(in);
    close(out);
    Forget(b);
    return ok && n == 0;
}

BOOL WINAPI CreateDirectoryA(LPCSTR name, SECURITY_ATTRIBUTES*)
{
    std::string path = PosixPath(name);
    Forget(path);
    if (mkdir(path.c_str(), 0755) == 0)
        return TRUE;
    SetLastError(ErrorOf(errno));
    return FALSE;
}

BOOL WINAPI RemoveDirectoryA(LPCSTR name)
{
    std::string path = PosixPath(name);
    Forget(path);
    return rmdir(path.c_str()) == 0;
}

DWORD WINAPI GetFileAttributesA(LPCSTR name)
{
    struct stat info;
    if (stat(PosixPath(name).c_str(), &info) != 0) {
        SetLastError(ErrorOf(errno));
        return INVALID_FILE_ATTRIBUTES;
    }
    return Attributes(info);
}

BOOL WINAPI SetFileAttributesA(LPCSTR, DWORD) { return TRUE; }

BOOL WINAPI GetFileAttributesExA(LPCSTR name, GET_FILEEX_INFO_LEVELS, LPVOID out)
{
    struct stat info;
    if (stat(PosixPath(name).c_str(), &info) != 0) {
        SetLastError(ErrorOf(errno));
        return FALSE;
    }
    auto* data = static_cast<WIN32_FILE_ATTRIBUTE_DATA*>(out);
    data->dwFileAttributes = Attributes(info);
    data->ftCreationTime = FileTime(info.st_mtim);
    data->ftLastAccessTime = FileTime(info.st_atim);
    data->ftLastWriteTime = FileTime(info.st_mtim);
    data->nFileSizeHigh = static_cast<DWORD>(static_cast<uint64_t>(info.st_size) >> 32);
    data->nFileSizeLow = static_cast<DWORD>(info.st_size);
    return TRUE;
}

HANDLE WINAPI FindFirstFileA(LPCSTR pattern, LPWIN32_FIND_DATAA data)
{
    std::string windows = pattern;
    std::replace(windows.begin(), windows.end(), '/', '\\');
    size_t slash = windows.rfind('\\');
    std::string directory = slash == std::string::npos ? "." : PosixPath(windows.substr(0, slash).c_str());
    std::string wanted = slash == std::string::npos ? windows : windows.substr(slash + 1);
    auto* find = new Find;
    if (DIR* dir = opendir(directory.c_str())) {
        while (dirent* entry = readdir(dir)) {
            if (!Matches(wanted.c_str(), entry->d_name))
                continue;
            struct stat info;
            if (stat((directory + "/" + entry->d_name).c_str(), &info) != 0)
                continue;
            WIN32_FIND_DATAA found = {};
            found.dwFileAttributes = Attributes(info);
            found.ftCreationTime = found.ftLastWriteTime = FileTime(info.st_mtim);
            found.ftLastAccessTime = FileTime(info.st_atim);
            found.nFileSizeHigh = static_cast<DWORD>(static_cast<uint64_t>(info.st_size) >> 32);
            found.nFileSizeLow = static_cast<DWORD>(info.st_size);
            std::strncpy(found.cFileName, entry->d_name, MAX_PATH - 1);
            find->entries.push_back(found);
        }
        closedir(dir);
    }
    if (find->entries.empty()) {
        delete find;
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    *data = find->entries[find->next++];
    return find;
}

BOOL WINAPI FindNextFileA(HANDLE handle, LPWIN32_FIND_DATAA data)
{
    auto* find = dynamic_cast<Find*>(ObjectOf(handle));
    if (!find || find->next >= find->entries.size()) {
        SetLastError(ERROR_NO_MORE_FILES);
        return FALSE;
    }
    *data = find->entries[find->next++];
    return TRUE;
}

BOOL WINAPI FindClose(HANDLE handle)
{
    delete dynamic_cast<Find*>(ObjectOf(handle));
    return TRUE;
}

DWORD WINAPI GetModuleFileNameA(HMODULE, LPSTR buffer, DWORD size)
{
    std::string path = std::string(kWindowsGameFolder) + "\\THUG2.exe";
    std::snprintf(buffer, size, "%s", path.c_str());
    return static_cast<DWORD>(std::min<size_t>(path.size(), size));
}

DWORD WINAPI GetCurrentDirectoryA(DWORD size, LPSTR buffer)
{
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd)))
        return 0;
    std::string path = WindowsPath(cwd);
    if (path.size() + 1 > size)
        return static_cast<DWORD>(path.size() + 1);
    std::memcpy(buffer, path.c_str(), path.size() + 1);
    return static_cast<DWORD>(path.size());
}

BOOL WINAPI SetCurrentDirectoryA(LPCSTR path) { return chdir(PosixPath(path).c_str()) == 0; }

DWORD WINAPI GetFullPathNameA(LPCSTR name, DWORD size, LPSTR buffer, LPSTR* filePart)
{
    std::string path = name;
    if (!(path.size() >= 2 && path[1] == ':') && !(path.size() && (path[0] == '\\' || path[0] == '/'))) {
        char cwd[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, cwd);
        path = std::string(cwd) + "\\" + path;
    }
    std::replace(path.begin(), path.end(), '/', '\\');
    if (path.size() + 1 > size)
        return static_cast<DWORD>(path.size() + 1);
    std::memcpy(buffer, path.c_str(), path.size() + 1);
    if (filePart) {
        char* slash = std::strrchr(buffer, '\\');
        *filePart = slash ? slash + 1 : buffer;
    }
    return static_cast<DWORD>(path.size());
}

UINT WINAPI GetDriveTypeA(LPCSTR) { return DRIVE_FIXED; }
BOOL WINAPI GetVolumeInformationA(LPCSTR, LPSTR, DWORD, LPDWORD, LPDWORD, LPDWORD, LPSTR, DWORD) { return FALSE; } // (no CD)
DWORD WINAPI GetLogicalDrives(void) { return 1u << 2; } // C:
BOOL WINAPI GetDiskFreeSpaceExA(LPCSTR, PULARGE_INTEGER freeToCaller, PULARGE_INTEGER total, PULARGE_INTEGER free)
{
    for (PULARGE_INTEGER out : {freeToCaller, total, free})
        if (out)
            out->QuadPart = 8ull << 30;
    return TRUE;
}
UINT WINAPI GetWindowsDirectoryA(LPSTR buffer, UINT size) { return std::snprintf(buffer, size, "C:\\WINDOWS"); }
DWORD WINAPI GetTempPathA(DWORD size, LPSTR buffer) { return std::snprintf(buffer, size, "C:\\TEMP\\"); }

} // extern "C"

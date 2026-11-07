// The rest of kernel32's process and system calls the game makes: the version (Windows XP SP2, so the game takes its
// XP paths), modules (there are none to load), memory, the debugger, the process.

#include <windows.h>
#include <psapi.h>

#include <emscripten/em_js.h>
#include <emscripten/heap.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
const auto g_processStart = std::chrono::system_clock::now();

// The command line: "THUG2.exe" and the arguments - under Node the process's, in the page the ones it was opened
// with (Module.thug2Args, from ?args= - web/index.html). Read before main (static initialisers of the game's read it).
EM_JS_DEPS(thug2CommandLine, "$stringToNewUTF8");
EM_JS(char*, CommandLine, (), {
    var args = typeof process == 'object' && process.argv ? process.argv.slice(2) : (Module['thug2Args'] || []);
    return stringToNewUTF8(['THUG2.exe'].concat(args).join(' '));
});
}

extern "C" {

BOOL WINAPI GetVersionExA(LPOSVERSIONINFOA info)
{
    info->dwMajorVersion = 5;
    info->dwMinorVersion = 1;
    info->dwBuildNumber = 2600;
    info->dwPlatformId = VER_PLATFORM_WIN32_NT;
    std::snprintf(info->szCSDVersion, sizeof(info->szCSDVersion), "Service Pack 2");
    return TRUE;
}

void WINAPI OutputDebugStringA(LPCSTR text) { std::fputs(text, stderr); }
BOOL WINAPI IsDebuggerPresent(void) { return FALSE; }
void WINAPI DebugBreak(void) { __builtin_trap(); }
EXECUTION_STATE WINAPI SetThreadExecutionState(EXECUTION_STATE) { return 0; }
BOOL WINAPI AllocConsole(void) { return TRUE; }
BOOL WINAPI FreeConsole(void) { return TRUE; }

HMODULE WINAPI GetModuleHandleA(LPCSTR name) { return name ? nullptr : reinterpret_cast<HMODULE>(0x00400000); }
BOOL WINAPI GetModuleHandleExA(DWORD, LPCSTR, HMODULE* module)
{
    *module = reinterpret_cast<HMODULE>(0x00400000);
    return TRUE;
}
HMODULE WINAPI LoadLibraryA(LPCSTR) { return nullptr; }
FARPROC WINAPI GetProcAddress(HMODULE, LPCSTR) { return nullptr; }
BOOL WINAPI FreeLibrary(HMODULE) { return TRUE; }

HANDLE WINAPI GetCurrentProcess(void) { return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1)); }
DWORD WINAPI GetCurrentProcessId(void) { return 1; }
void WINAPI ExitProcess(UINT code) { std::exit(static_cast<int>(code)); }
BOOL WINAPI TerminateProcess(HANDLE, UINT code) { std::_Exit(static_cast<int>(code)); }

BOOL WINAPI GetProcessTimes(HANDLE, FILETIME* created, FILETIME* exited, FILETIME* kernel, FILETIME* user)
{
    auto since = std::chrono::duration_cast<std::chrono::nanoseconds>(g_processStart.time_since_epoch()).count();
    uint64_t ticks = 11644473600ull * 10000000ull + static_cast<uint64_t>(since) / 100;
    *created = {static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
    *exited = *kernel = *user = {};
    return TRUE;
}

LPSTR WINAPI GetCommandLineA(void)
{
    static char* line = CommandLine();
    return line;
}

DWORD WINAPI GetEnvironmentVariableA(LPCSTR name, LPSTR buffer, DWORD size)
{
    const char* value = std::getenv(name);
    if (!value)
        return 0;
    size_t length = std::strlen(value);
    if (length + 1 > size)
        return static_cast<DWORD>(length + 1);
    std::memcpy(buffer, value, length + 1);
    return static_cast<DWORD>(length);
}
BOOL WINAPI SetEnvironmentVariableA(LPCSTR name, LPCSTR value)
{
    return (value ? setenv(name, value, 1) : unsetenv(name)) == 0;
}

int WINAPI wvsprintfA(LPSTR buffer, LPCSTR format, va_list arguments)
{
    return std::vsnprintf(buffer, 1024, format, arguments); // (wsprintf's own limit)
}
int WINAPIV wsprintfA(LPSTR buffer, LPCSTR format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int n = wvsprintfA(buffer, format, arguments);
    va_end(arguments);
    return n;
}

// --- Memory ---

LPVOID WINAPI VirtualAlloc(LPVOID address, SIZE_T size, DWORD, DWORD)
{
    if (address)
        return address; // (committing reserved memory: it's all there)
    void* memory = std::aligned_alloc(65536, (size + 65535) & ~SIZE_T(65535));
    if (memory)
        std::memset(memory, 0, size);
    return memory;
}
BOOL WINAPI VirtualFree(LPVOID address, SIZE_T, DWORD type)
{
    if (type & 0x8000) // MEM_RELEASE
        std::free(address);
    return TRUE;
}
BOOL WINAPI VirtualProtect(LPVOID, SIZE_T, DWORD protect, PDWORD oldProtect)
{
    if (oldProtect)
        *oldProtect = protect;
    return TRUE;
}
SIZE_T WINAPI VirtualQuery(LPCVOID, PMEMORY_BASIC_INFORMATION info, SIZE_T)
{
    std::memset(info, 0, sizeof(*info));
    return 0;
}
BOOL WINAPI FlushInstructionCache(HANDLE, LPCVOID, SIZE_T) { return TRUE; }

void WINAPI GlobalMemoryStatus(MEMORYSTATUS* status)
{
    size_t total = emscripten_get_heap_max();
    *status = {};
    status->dwLength = sizeof(*status);
    status->dwTotalPhys = static_cast<SIZE_T>(std::min<size_t>(total, 0x7FFFFFFF));
    status->dwAvailPhys = static_cast<SIZE_T>(std::min<size_t>(total - emscripten_get_heap_size(), 0x7FFFFFFF));
    status->dwTotalVirtual = status->dwTotalPhys;
    status->dwAvailVirtual = status->dwAvailPhys;
    status->dwTotalPageFile = status->dwTotalPhys;
    status->dwAvailPageFile = status->dwAvailPhys;
}

HGLOBAL WINAPI GlobalAlloc(UINT, SIZE_T size) { return std::calloc(1, size); }
HGLOBAL WINAPI GlobalFree(HGLOBAL memory)
{
    std::free(memory);
    return nullptr;
}
LPVOID WINAPI GlobalLock(HGLOBAL memory) { return memory; }
BOOL WINAPI GlobalUnlock(HGLOBAL) { return TRUE; }

BOOL WINAPI K32GetProcessMemoryInfo(HANDLE, PPROCESS_MEMORY_COUNTERS counters, DWORD)
{
    std::memset(counters, 0, sizeof(*counters));
    counters->cb = sizeof(*counters);
    counters->PagefileUsage = counters->PeakPagefileUsage = counters->WorkingSetSize =
        static_cast<SIZE_T>(emscripten_get_heap_size());
    return TRUE;
}
BOOL WINAPI GetProcessMemoryInfo(HANDLE process, PPROCESS_MEMORY_COUNTERS counters, DWORD size)
{
    return K32GetProcessMemoryInfo(process, counters, size);
}

} // extern "C"

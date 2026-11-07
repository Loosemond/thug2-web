// Win32 clocks: the performance counter at 10 MHz (modern Windows'), tick counts, and calendar times.

#include <windows.h>

#include <chrono>
#include <ctime>

namespace {

using Clock = std::chrono::steady_clock;
const Clock::time_point g_start = Clock::now();

uint64_t Since(Clock::duration unit) { return (Clock::now() - g_start) / unit; }

FILETIME FromTicks(uint64_t ticks) { return {static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)}; }
uint64_t Ticks(const FILETIME& time) { return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime; }

constexpr uint64_t kUnixEpoch = 11644473600ull * 10000000ull; // 1601 to 1970 in 100 ns

uint64_t NowTicks()
{
    auto since = std::chrono::system_clock::now().time_since_epoch();
    return kUnixEpoch + std::chrono::duration_cast<std::chrono::nanoseconds>(since).count() / 100;
}

SYSTEMTIME SystemTimeOf(const std::tm& tm, unsigned milliseconds)
{
    SYSTEMTIME time;
    time.wYear = static_cast<WORD>(tm.tm_year + 1900);
    time.wMonth = static_cast<WORD>(tm.tm_mon + 1);
    time.wDayOfWeek = static_cast<WORD>(tm.tm_wday);
    time.wDay = static_cast<WORD>(tm.tm_mday);
    time.wHour = static_cast<WORD>(tm.tm_hour);
    time.wMinute = static_cast<WORD>(tm.tm_min);
    time.wSecond = static_cast<WORD>(tm.tm_sec);
    time.wMilliseconds = static_cast<WORD>(milliseconds);
    return time;
}

} // namespace

extern "C" {

BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER* count)
{
    count->QuadPart = static_cast<LONGLONG>(Since(std::chrono::nanoseconds(100)));
    return TRUE;
}

BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER* frequency)
{
    frequency->QuadPart = 10000000;
    return TRUE;
}

DWORD WINAPI GetTickCount(void) { return static_cast<DWORD>(Since(std::chrono::milliseconds(1))); }
ULONGLONG WINAPI GetTickCount64(void) { return Since(std::chrono::milliseconds(1)); }

void WINAPI GetSystemTimeAsFileTime(LPFILETIME time) { *time = FromTicks(NowTicks()); }

void WINAPI GetSystemTime(LPSYSTEMTIME time)
{
    uint64_t ticks = NowTicks();
    std::time_t seconds = static_cast<std::time_t>((ticks - kUnixEpoch) / 10000000);
    std::tm tm;
    gmtime_r(&seconds, &tm);
    *time = SystemTimeOf(tm, static_cast<unsigned>(ticks / 10000 % 1000));
}

void WINAPI GetLocalTime(LPSYSTEMTIME time)
{
    uint64_t ticks = NowTicks();
    std::time_t seconds = static_cast<std::time_t>((ticks - kUnixEpoch) / 10000000);
    std::tm tm;
    localtime_r(&seconds, &tm);
    *time = SystemTimeOf(tm, static_cast<unsigned>(ticks / 10000 % 1000));
}

BOOL WINAPI FileTimeToSystemTime(const FILETIME* fileTime, LPSYSTEMTIME systemTime)
{
    uint64_t ticks = Ticks(*fileTime);
    std::time_t seconds = static_cast<std::time_t>((ticks - kUnixEpoch) / 10000000);
    std::tm tm;
    gmtime_r(&seconds, &tm);
    *systemTime = SystemTimeOf(tm, static_cast<unsigned>(ticks / 10000 % 1000));
    return TRUE;
}

BOOL WINAPI FileTimeToLocalFileTime(const FILETIME* fileTime, LPFILETIME localTime)
{
    std::time_t now = std::time(nullptr);
    std::tm tm;
    localtime_r(&now, &tm);
    *localTime = FromTicks(Ticks(*fileTime) + static_cast<int64_t>(tm.tm_gmtoff) * 10000000);
    return TRUE;
}

BOOL WINAPI SystemTimeToFileTime(const SYSTEMTIME* systemTime, LPFILETIME fileTime)
{
    std::tm tm = {};
    tm.tm_year = systemTime->wYear - 1900;
    tm.tm_mon = systemTime->wMonth - 1;
    tm.tm_mday = systemTime->wDay;
    tm.tm_hour = systemTime->wHour;
    tm.tm_min = systemTime->wMinute;
    tm.tm_sec = systemTime->wSecond;
    std::time_t seconds = timegm(&tm);
    *fileTime = FromTicks(kUnixEpoch + static_cast<uint64_t>(seconds) * 10000000 + systemTime->wMilliseconds * 10000ull);
    return TRUE;
}

LONG WINAPI CompareFileTime(const FILETIME* a, const FILETIME* b)
{
    uint64_t x = Ticks(*a), y = Ticks(*b);
    return x < y ? -1 : x > y ? 1 : 0;
}

} // extern "C"

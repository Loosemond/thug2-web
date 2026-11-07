// winmm's multimedia timers on a thread each: the game's loading screen is drawn from one (timeSetEvent, 29 ms).

#include <windows.h>
#include <mmsystem.h>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <thread>


namespace {

struct Timer {
    std::atomic<bool> killed{false};
    std::thread thread;
};

std::mutex g_lock;
std::map<UINT, std::shared_ptr<Timer>> g_timers;
UINT g_nextId = 1;

const auto g_start = std::chrono::steady_clock::now();

} // namespace

extern "C" {

MMRESULT WINAPI timeSetEvent(UINT delay, UINT, LPTIMECALLBACK callback, DWORD_PTR user, UINT flags)
{
    auto timer = std::make_shared<Timer>();
    std::lock_guard lock(g_lock);
    UINT id = g_nextId++;
    g_timers[id] = timer;
    timer->thread = std::thread([timer, id, delay, callback, user, flags] {
        auto next = std::chrono::steady_clock::now();
        do {
            next += std::chrono::milliseconds(delay ? delay : 1);
            std::this_thread::sleep_until(next);
            if (timer->killed)
                break;
            callback(id, 0, user, 0, 0);
        } while (flags & TIME_PERIODIC);
    });
    timer->thread.detach();
    return id;
}

MMRESULT WINAPI timeKillEvent(UINT id)
{
    std::lock_guard lock(g_lock);
    auto it = g_timers.find(id);
    if (it == g_timers.end())
        return TIMERR_NOCANDO;
    it->second->killed = true;
    g_timers.erase(it);
    return TIMERR_NOERROR;
}

MMRESULT WINAPI timeBeginPeriod(UINT) { return TIMERR_NOERROR; }
MMRESULT WINAPI timeEndPeriod(UINT) { return TIMERR_NOERROR; }
DWORD WINAPI timeGetTime(void)
{
    return static_cast<DWORD>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_start).count());
}

} // extern "C"

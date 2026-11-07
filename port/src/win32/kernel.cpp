// Win32 kernel objects on pthreads: events, semaphores, mutexes, threads, critical sections, slim locks, and the waits
// on them. One lock and condition variable for every waitable object keeps WaitForMultipleObjects simple; the game
// waits rarely enough for that not to matter.

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <thread>

#include "win32/objects.h"

namespace Win32 {

std::mutex g_waitLock;
std::condition_variable g_signalled;

namespace {

struct Event : Waitable {
    bool manualReset;
    bool set;
    bool Ready() override { return set; }
    void Acquire() override
    {
        if (!manualReset)
            set = false;
    }
};

struct Semaphore : Waitable {
    LONG count;
    LONG maximum;
    bool Ready() override { return count > 0; }
    void Acquire() override { --count; }
};

struct Mutex : Waitable {
    std::thread::id owner;
    int recursion = 0;
    bool Ready() override { return recursion == 0 || owner == std::this_thread::get_id(); }
    void Acquire() override
    {
        owner = std::this_thread::get_id();
        ++recursion;
    }
};

struct Thread : Waitable {
    LPTHREAD_START_ROUTINE start;
    void* parameter;
    bool suspended;
    bool done = false;
    DWORD exitCode = STILL_ACTIVE;
    DWORD id;
    bool Ready() override { return done; }
};

std::atomic<DWORD> g_nextThreadId{2};
thread_local DWORD t_threadId = 1; // (the main thread's)
thread_local DWORD t_lastError = 0;

void Run(Thread* thread)
{
    t_threadId = thread->id;
    {
        std::unique_lock lock(g_waitLock);
        g_signalled.wait(lock, [thread] { return !thread->suspended; });
    }
    DWORD code = static_cast<DWORD>(thread->start(thread->parameter));
    std::lock_guard lock(g_waitLock);
    thread->exitCode = code;
    thread->done = true;
    g_signalled.notify_all();
}

// A wait's deadline from its timeout in milliseconds (INFINITE: none).
bool WaitUntil(std::unique_lock<std::mutex>& lock, DWORD milliseconds, auto ready)
{
    if (milliseconds == INFINITE) {
        g_signalled.wait(lock, ready);
        return true;
    }
    return g_signalled.wait_for(lock, std::chrono::milliseconds(milliseconds), ready);
}

} // namespace

Object* ObjectOf(HANDLE handle)
{
    if (!handle || handle == INVALID_HANDLE_VALUE)
        return nullptr;
    return static_cast<Object*>(handle);
}

} // namespace Win32

using namespace Win32;

extern "C" {

DWORD WINAPI GetLastError(void) { return t_lastError; }
void WINAPI SetLastError(DWORD error) { t_lastError = error; }

HANDLE WINAPI CreateEventA(SECURITY_ATTRIBUTES*, BOOL manualReset, BOOL initialState, LPCSTR)
{
    auto* event = new Event;
    event->manualReset = manualReset;
    event->set = initialState;
    return event;
}

BOOL WINAPI SetEvent(HANDLE handle)
{
    auto* event = dynamic_cast<Event*>(ObjectOf(handle));
    if (!event)
        return FALSE;
    std::lock_guard lock(g_waitLock);
    event->set = true;
    g_signalled.notify_all();
    return TRUE;
}

BOOL WINAPI ResetEvent(HANDLE handle)
{
    auto* event = dynamic_cast<Event*>(ObjectOf(handle));
    if (!event)
        return FALSE;
    std::lock_guard lock(g_waitLock);
    event->set = false;
    return TRUE;
}

HANDLE WINAPI CreateSemaphoreA(SECURITY_ATTRIBUTES*, LONG initial, LONG maximum, LPCSTR)
{
    auto* semaphore = new Semaphore;
    semaphore->count = initial;
    semaphore->maximum = maximum;
    return semaphore;
}

BOOL WINAPI ReleaseSemaphore(HANDLE handle, LONG count, LPLONG previous)
{
    auto* semaphore = dynamic_cast<Semaphore*>(ObjectOf(handle));
    if (!semaphore)
        return FALSE;
    std::lock_guard lock(g_waitLock);
    if (previous)
        *previous = semaphore->count;
    if (semaphore->count + count > semaphore->maximum)
        return FALSE;
    semaphore->count += count;
    g_signalled.notify_all();
    return TRUE;
}

HANDLE WINAPI CreateMutexA(SECURITY_ATTRIBUTES*, BOOL initialOwner, LPCSTR)
{
    auto* mutex = new Mutex;
    if (initialOwner)
        mutex->Acquire();
    return mutex;
}

BOOL WINAPI ReleaseMutex(HANDLE handle)
{
    auto* mutex = dynamic_cast<Mutex*>(ObjectOf(handle));
    if (!mutex)
        return FALSE;
    std::lock_guard lock(g_waitLock);
    if (mutex->recursion == 0 || mutex->owner != std::this_thread::get_id())
        return FALSE;
    if (--mutex->recursion == 0)
        g_signalled.notify_all();
    return TRUE;
}

DWORD WINAPI WaitForSingleObject(HANDLE handle, DWORD milliseconds)
{
    auto* object = dynamic_cast<Waitable*>(ObjectOf(handle));
    if (!object)
        return WAIT_FAILED;
    std::unique_lock lock(g_waitLock);
    if (!WaitUntil(lock, milliseconds, [object] { return object->Ready(); }))
        return WAIT_TIMEOUT;
    object->Acquire();
    return WAIT_OBJECT_0;
}

DWORD WINAPI WaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD milliseconds)
{
    Waitable* objects[MAXIMUM_WAIT_OBJECTS];
    if (count == 0 || count > MAXIMUM_WAIT_OBJECTS)
        return WAIT_FAILED;
    for (DWORD i = 0; i < count; ++i)
        if (!(objects[i] = dynamic_cast<Waitable*>(ObjectOf(handles[i]))))
            return WAIT_FAILED;
    std::unique_lock lock(g_waitLock);
    DWORD which = 0;
    auto ready = [&] {
        if (waitAll) {
            for (DWORD i = 0; i < count; ++i)
                if (!objects[i]->Ready())
                    return false;
            return true;
        }
        for (DWORD i = 0; i < count; ++i)
            if (objects[i]->Ready()) {
                which = i;
                return true;
            }
        return false;
    };
    if (!WaitUntil(lock, milliseconds, ready))
        return WAIT_TIMEOUT;
    if (waitAll) {
        for (DWORD i = 0; i < count; ++i)
            objects[i]->Acquire();
        return WAIT_OBJECT_0;
    }
    objects[which]->Acquire();
    return WAIT_OBJECT_0 + which;
}

HANDLE WINAPI CreateThread(SECURITY_ATTRIBUTES*, SIZE_T, LPTHREAD_START_ROUTINE start, LPVOID parameter, DWORD flags,
                           LPDWORD id)
{
    auto* thread = new Thread;
    thread->start = start;
    thread->parameter = parameter;
    thread->suspended = (flags & CREATE_SUSPENDED) != 0;
    thread->id = g_nextThreadId++;
    if (id)
        *id = thread->id;
    std::thread(Run, thread).detach();
    return thread;
}

DWORD WINAPI ResumeThread(HANDLE handle)
{
    auto* thread = dynamic_cast<Thread*>(ObjectOf(handle));
    if (!thread)
        return static_cast<DWORD>(-1);
    std::lock_guard lock(g_waitLock);
    DWORD was = thread->suspended ? 1 : 0;
    thread->suspended = false;
    g_signalled.notify_all();
    return was;
}

DWORD WINAPI SuspendThread(HANDLE) { return static_cast<DWORD>(-1); } // (not possible on pthreads; the game doesn't)
BOOL WINAPI SetThreadPriority(HANDLE, int) { return TRUE; }
int WINAPI GetThreadPriority(HANDLE) { return THREAD_PRIORITY_NORMAL; }
DWORD_PTR WINAPI SetThreadAffinityMask(HANDLE, DWORD_PTR) { return 1; }
HANDLE WINAPI GetCurrentThread(void) { return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-2)); }
DWORD WINAPI GetCurrentThreadId(void) { return t_threadId; }

BOOL WINAPI GetExitCodeThread(HANDLE handle, LPDWORD code)
{
    auto* thread = dynamic_cast<Thread*>(ObjectOf(handle));
    if (!thread)
        return FALSE;
    std::lock_guard lock(g_waitLock);
    *code = thread->exitCode;
    return TRUE;
}

BOOL WINAPI SwitchToThread(void)
{
    std::this_thread::yield();
    return TRUE;
}

void WINAPI Sleep(DWORD milliseconds) { std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds)); }
DWORD WINAPI SleepEx(DWORD milliseconds, BOOL)
{
    Sleep(milliseconds);
    return 0;
}

BOOL WINAPI CloseHandle(HANDLE handle)
{
    Object* object = ObjectOf(handle);
    if (!object)
        return FALSE;
    // (threads keep running after their handle is closed: theirs is never freed)
    if (!dynamic_cast<Thread*>(object))
        delete object;
    return TRUE;
}

// Critical sections: a recursive mutex of ours in the 24-byte structure's DebugInfo.
void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION section)
{
    *section = {};
    section->DebugInfo = new std::recursive_mutex;
}
BOOL WINAPI InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION section, DWORD)
{
    InitializeCriticalSection(section);
    return TRUE;
}
void WINAPI EnterCriticalSection(LPCRITICAL_SECTION section)
{
    static_cast<std::recursive_mutex*>(section->DebugInfo)->lock();
}
BOOL WINAPI TryEnterCriticalSection(LPCRITICAL_SECTION section)
{
    return static_cast<std::recursive_mutex*>(section->DebugInfo)->try_lock();
}
void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION section)
{
    static_cast<std::recursive_mutex*>(section->DebugInfo)->unlock();
}
void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION section)
{
    delete static_cast<std::recursive_mutex*>(section->DebugInfo);
    section->DebugInfo = nullptr;
}

// Slim reader/writer locks: a pointer-sized structure, zero to start - a shared mutex of ours made on first use.
namespace {
std::shared_mutex* SharedMutexOf(PSRWLOCK lock)
{
    auto* slot = reinterpret_cast<std::atomic<std::shared_mutex*>*>(&lock->Ptr);
    std::shared_mutex* mutex = slot->load();
    if (!mutex) {
        auto* made = new std::shared_mutex;
        if (slot->compare_exchange_strong(mutex, made))
            return made;
        delete made;
    }
    return mutex;
}
} // namespace

void WINAPI InitializeSRWLock(PSRWLOCK lock) { lock->Ptr = nullptr; }
void WINAPI AcquireSRWLockExclusive(PSRWLOCK lock) { SharedMutexOf(lock)->lock(); }
void WINAPI ReleaseSRWLockExclusive(PSRWLOCK lock) { SharedMutexOf(lock)->unlock(); }
void WINAPI AcquireSRWLockShared(PSRWLOCK lock) { SharedMutexOf(lock)->lock_shared(); }
void WINAPI ReleaseSRWLockShared(PSRWLOCK lock) { SharedMutexOf(lock)->unlock_shared(); }
BOOLEAN WINAPI TryAcquireSRWLockExclusive(PSRWLOCK lock) { return SharedMutexOf(lock)->try_lock(); }

} // extern "C"

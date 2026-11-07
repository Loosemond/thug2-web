#pragma once

#include <windows.h>

#include <condition_variable>
#include <mutex>

// What a HANDLE points to: one of these (kernel.cpp, files.cpp).
namespace Win32 {

struct Object {
    virtual ~Object() = default;
};

// Something WaitForSingleObject / WaitForMultipleObjects can wait for. Ready and Acquire are called with g_waitLock
// held; whatever makes an object ready notifies g_signalled under it.
struct Waitable : Object {
    virtual bool Ready() = 0;
    virtual void Acquire() {}
};

extern std::mutex g_waitLock;
extern std::condition_variable g_signalled;

// The object behind a handle, null for null / INVALID_HANDLE_VALUE.
Object* ObjectOf(HANDLE handle);

} // namespace Win32

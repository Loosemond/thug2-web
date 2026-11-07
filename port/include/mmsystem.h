#pragma once
// thug2-web: winmm's multimedia timers.
#include <windows.h>
typedef UINT MMRESULT;
typedef void(CALLBACK* LPTIMECALLBACK)(UINT timer, UINT message, DWORD_PTR user, DWORD_PTR, DWORD_PTR);
#define TIMERR_NOERROR 0
#define TIMERR_NOCANDO 97
#define TIME_ONESHOT 0x0000
#define TIME_PERIODIC 0x0001
#define TIME_CALLBACK_FUNCTION 0x0000
extern "C" {
MMRESULT WINAPI timeSetEvent(UINT delay, UINT resolution, LPTIMECALLBACK callback, DWORD_PTR user, UINT flags);
MMRESULT WINAPI timeKillEvent(UINT timer);
MMRESULT WINAPI timeBeginPeriod(UINT period);
MMRESULT WINAPI timeEndPeriod(UINT period);
DWORD WINAPI timeGetTime(void);
}

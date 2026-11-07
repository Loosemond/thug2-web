#pragma once
// thug2-web: the Win32 API the game calls, beyond the types and COM that DXVK native's windows_base.h gives.
// Struct layouts follow the 32-bit Windows SDK, since game structs embed some of them. Implemented in port/src/win32/.

#include <cstdarg>
#include <cstdint>

#define CALLBACK
#define WINAPIV
#define APIENTRY
#define WINBASEAPI
#define NTAPI

typedef LONG_PTR LRESULT;
typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef ULONG_PTR DWORD_PTR;
typedef DWORD_PTR* PDWORD_PTR;
typedef LONG LSTATUS;
typedef WORD ATOM;
typedef BYTE* LPBYTE;
typedef BOOL* LPBOOL;
typedef const BYTE* LPCBYTE;
typedef DWORD REGSAM;
typedef DWORD EXECUTION_STATE;
typedef HANDLE HGLOBAL;
typedef HANDLE HICON;
typedef HANDLE HCURSOR;
typedef HANDLE HBRUSH;
typedef HANDLE HMENU;
typedef HANDLE HGDIOBJ;
typedef HANDLE HRSRC;
typedef void* FARPROC;
typedef LONG* PLONG;
typedef DWORD* PDWORD;
typedef BYTE BOOLEAN;

// (unsigned long, as Windows' DWORD: the game's thread functions are declared with it; DXVK's DWORD is uint32_t)
typedef unsigned long(WINAPI* LPTHREAD_START_ROUTINE)(LPVOID parameter);
typedef LRESULT(CALLBACK* WNDPROC)(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

#define MAKEINTRESOURCEA(i) ((LPSTR)((ULONG_PTR)((WORD)(i))))
#define LOWORD(l) ((WORD)(((DWORD_PTR)(l)) & 0xffff))
#define HIWORD(l) ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))
#define LOBYTE(w) ((BYTE)(((DWORD_PTR)(w)) & 0xff))
#define HIBYTE(w) ((BYTE)((((DWORD_PTR)(w)) >> 8) & 0xff))
#define MAKELONG(a, b) ((LONG)(((WORD)(((DWORD_PTR)(a)) & 0xffff)) | ((DWORD)((WORD)(((DWORD_PTR)(b)) & 0xffff))) << 16))
#define UNREFERENCED_PARAMETER(p) (void)(p)

typedef union _ULARGE_INTEGER {
    struct {
        DWORD LowPart;
        DWORD HighPart;
    };
    struct {
        DWORD LowPart;
        DWORD HighPart;
    } u;
    ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;

typedef struct _MEMORYSTATUS {
    DWORD dwLength;
    DWORD dwMemoryLoad;
    SIZE_T dwTotalPhys;
    SIZE_T dwAvailPhys;
    SIZE_T dwTotalPageFile;
    SIZE_T dwAvailPageFile;
    SIZE_T dwTotalVirtual;
    SIZE_T dwAvailVirtual;
} MEMORYSTATUS, *LPMEMORYSTATUS;

typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

typedef struct _SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;

typedef struct _OVERLAPPED {
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    union {
        struct {
            DWORD Offset;
            DWORD OffsetHigh;
        };
        PVOID Pointer;
    };
    HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;

typedef struct _WIN32_FIND_DATAA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    CHAR cFileName[MAX_PATH];
    CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA, *PWIN32_FIND_DATAA, *LPWIN32_FIND_DATAA;
typedef WIN32_FIND_DATAA WIN32_FIND_DATA;

typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA, *LPWIN32_FILE_ATTRIBUTE_DATA;

typedef enum _GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard, GetFileExMaxInfoLevel } GET_FILEEX_INFO_LEVELS;

typedef struct _MEMORY_BASIC_INFORMATION {
    PVOID BaseAddress;
    PVOID AllocationBase;
    DWORD AllocationProtect;
    SIZE_T RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
} MEMORY_BASIC_INFORMATION, *PMEMORY_BASIC_INFORMATION;

typedef struct _OSVERSIONINFOA {
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    CHAR szCSDVersion[128];
} OSVERSIONINFOA, *POSVERSIONINFOA, *LPOSVERSIONINFOA;

// 24 bytes on 32-bit Windows; the implementation keeps its own lock behind the first pointer.
typedef struct _CRITICAL_SECTION {
    PVOID DebugInfo;
    LONG LockCount;
    LONG RecursionCount;
    HANDLE OwningThread;
    HANDLE LockSemaphore;
    ULONG_PTR SpinCount;
} CRITICAL_SECTION, *LPCRITICAL_SECTION, *PCRITICAL_SECTION;

typedef struct _RTL_SRWLOCK {
    PVOID Ptr;
} SRWLOCK, *PSRWLOCK;
#define SRWLOCK_INIT {0}

typedef struct tagWNDCLASSEXA {
    UINT cbSize;
    UINT style;
    WNDPROC lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCSTR lpszMenuName;
    LPCSTR lpszClassName;
    HICON hIconSm;
} WNDCLASSEXA, *LPWNDCLASSEXA;

typedef struct tagMSG {
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
} MSG, *LPMSG;

typedef struct _devicemodeA {
    BYTE dmDeviceName[32];
    WORD dmSpecVersion;
    WORD dmDriverVersion;
    WORD dmSize;
    WORD dmDriverExtra;
    DWORD dmFields;
    union {
        struct {
            short dmOrientation;
            short dmPaperSize;
            short dmPaperLength;
            short dmPaperWidth;
            short dmScale;
            short dmCopies;
            short dmDefaultSource;
            short dmPrintQuality;
        };
        struct {
            POINT dmPosition;
            DWORD dmDisplayOrientation;
            DWORD dmDisplayFixedOutput;
        };
    };
    short dmColor;
    short dmDuplex;
    short dmYResolution;
    short dmTTOption;
    short dmCollate;
    BYTE dmFormName[32];
    WORD dmLogPixels;
    DWORD dmBitsPerPel;
    DWORD dmPelsWidth;
    DWORD dmPelsHeight;
    union {
        DWORD dmDisplayFlags;
        DWORD dmNup;
    };
    DWORD dmDisplayFrequency;
    DWORD dmICMMethod;
    DWORD dmICMIntent;
    DWORD dmMediaType;
    DWORD dmDitherType;
    DWORD dmReserved1;
    DWORD dmReserved2;
    DWORD dmPanningWidth;
    DWORD dmPanningHeight;
} DEVMODEA, *PDEVMODEA, *LPDEVMODEA;

// Files
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x1
#define FILE_SHARE_WRITE 0x2
#define FILE_SHARE_DELETE 0x4
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_ATTRIBUTE_READONLY 0x1
#define FILE_ATTRIBUTE_HIDDEN 0x2
#define FILE_ATTRIBUTE_SYSTEM 0x4
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_ARCHIVE 0x20
#define FILE_ATTRIBUTE_NORMAL 0x80
#define FILE_FLAG_WRITE_THROUGH 0x80000000u
#define FILE_FLAG_OVERLAPPED 0x40000000u
#define FILE_FLAG_NO_BUFFERING 0x20000000u
#define FILE_FLAG_RANDOM_ACCESS 0x10000000u
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000u
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define INVALID_FILE_SIZE ((DWORD)0xFFFFFFFF)
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#define MOVEFILE_REPLACE_EXISTING 0x1
#define MOVEFILE_COPY_ALLOWED 0x2
#define MOVEFILE_WRITE_THROUGH 0x8
#define DRIVE_UNKNOWN 0
#define DRIVE_NO_ROOT_DIR 1
#define DRIVE_REMOVABLE 2
#define DRIVE_FIXED 3
#define DRIVE_REMOTE 4
#define DRIVE_CDROM 5
#define DRIVE_RAMDISK 6

// Errors
#define ERROR_SUCCESS 0L
#define NO_ERROR 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_NOT_ENOUGH_MEMORY 8L
#define ERROR_NO_MORE_FILES 18L
#define ERROR_HANDLE_EOF 38L
#define ERROR_FILE_EXISTS 80L
#define ERROR_INVALID_PARAMETER 87L
#define ERROR_INSUFFICIENT_BUFFER 122L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_MORE_DATA 234L
#define ERROR_IO_INCOMPLETE 996L
#define ERROR_IO_PENDING 997L

// Threads and processes
#define CREATE_SUSPENDED 0x4
#define MAXIMUM_WAIT_OBJECTS 64
#define STILL_ACTIVE 0x103
#define THREAD_PRIORITY_IDLE -15
#define THREAD_PRIORITY_LOWEST -2
#define THREAD_PRIORITY_BELOW_NORMAL -1
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_PRIORITY_TIME_CRITICAL 15
#define ES_SYSTEM_REQUIRED 0x1
#define ES_DISPLAY_REQUIRED 0x2
#define ES_CONTINUOUS 0x80000000u
#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define VER_PLATFORM_WIN32_WINDOWS 1
#define VER_PLATFORM_WIN32_NT 2

// Memory
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_DECOMMIT 0x4000
#define MEM_RELEASE 0x8000
#define MEM_FREE 0x10000
#define MEM_PRIVATE 0x20000
#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40

// Registry
#define HKEY_CLASSES_ROOT ((HKEY)(ULONG_PTR)0x80000000u)
#define HKEY_CURRENT_USER ((HKEY)(ULONG_PTR)0x80000001u)
#define HKEY_LOCAL_MACHINE ((HKEY)(ULONG_PTR)0x80000002u)
#define KEY_QUERY_VALUE 0x0001
#define KEY_SET_VALUE 0x0002
#define KEY_CREATE_SUB_KEY 0x0004
#define KEY_ENUMERATE_SUB_KEYS 0x0008
#define KEY_READ 0x20019
#define KEY_WRITE 0x20006
#define KEY_ALL_ACCESS 0xF003F
#define REG_NONE 0
#define REG_SZ 1
#define REG_BINARY 3
#define REG_DWORD 4
#define REG_OPTION_NON_VOLATILE 0

// Windows and input
#define WS_OVERLAPPED 0x00000000u
#define WS_POPUP 0x80000000u
#define WS_VISIBLE 0x10000000u
#define WS_CAPTION 0x00C00000u
#define WS_SYSMENU 0x00080000u
#define WS_THICKFRAME 0x00040000u
#define WS_MINIMIZEBOX 0x00020000u
#define WS_MAXIMIZEBOX 0x00010000u
#define WS_OVERLAPPEDWINDOW 0x00CF0000u
#define WS_EX_TOPMOST 0x00000008u
#define WS_EX_APPWINDOW 0x00040000u
#define CS_VREDRAW 0x0001
#define CS_HREDRAW 0x0002
#define CS_OWNDC 0x0020
#define CS_CLASSDC 0x0040
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_SHOWMINIMIZED 2
#define SW_SHOWMAXIMIZED 3
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SW_RESTORE 9
#define SW_MAXIMIZE 3
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SPI_GETSCREENSAVETIMEOUT 0x000E
#define SPI_SETSCREENSAVETIMEOUT 0x000F
#define SPI_GETSCREENSAVEACTIVE 0x0010
#define SPI_SETSCREENSAVEACTIVE 0x0011
#define MB_OK 0x0
#define MB_ICONERROR 0x10
#define MB_ICONWARNING 0x30
#define PM_NOREMOVE 0x0
#define PM_REMOVE 0x1
#define WM_DESTROY 0x0002
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WA_INACTIVE 0
#define WA_ACTIVE 1
#define WA_CLICKACTIVE 2
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ACTIVATEAPP 0x001C
#define WM_SETCURSOR 0x0020
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_SYSCOMMAND 0x0112
#define SC_KEYMENU 0xF100
#define SC_SCREENSAVE 0xF140
#define SC_MONITORPOWER 0xF170
#define WM_POWERBROADCAST 0x0218
#define WM_MOUSEMOVE 0x0200
#define MAPVK_VK_TO_VSC 0
#define MAPVK_VSC_TO_VK 1
#define MAPVK_VK_TO_CHAR 2
#define MAPVK_VSC_TO_VK_EX 3
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_MBUTTON 0x04
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_SEPARATOR 0x6C
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define IDC_ARROW MAKEINTRESOURCEA(32512)
#define CDS_FULLSCREEN 0x4
#define DISP_CHANGE_SUCCESSFUL 0

// COM
#define CLSCTX_INPROC_SERVER 0x1
#define CLSCTX_INPROC_HANDLER 0x2
#define CLSCTX_LOCAL_SERVER 0x4
#define CLSCTX_REMOTE_SERVER 0x10
#define CLSCTX_ALL (CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER)
#define COINIT_MULTITHREADED 0x0
#define COINIT_APARTMENTTHREADED 0x2

#ifdef __cplusplus
extern "C" {
#endif

// Files
HANDLE WINAPI CreateFileA(LPCSTR name, DWORD access, DWORD share, SECURITY_ATTRIBUTES* security, DWORD disposition,
                          DWORD flags, HANDLE templateFile);
BOOL WINAPI ReadFile(HANDLE file, LPVOID buffer, DWORD size, LPDWORD read, LPOVERLAPPED overlapped);
BOOL WINAPI WriteFile(HANDLE file, LPCVOID buffer, DWORD size, LPDWORD written, LPOVERLAPPED overlapped);
BOOL WINAPI GetOverlappedResult(HANDLE file, LPOVERLAPPED overlapped, LPDWORD transferred, BOOL wait);
BOOL WINAPI CancelIo(HANDLE file);
BOOL WINAPI CloseHandle(HANDLE handle);
DWORD WINAPI SetFilePointer(HANDLE file, LONG distance, PLONG distanceHigh, DWORD method);
BOOL WINAPI SetFilePointerEx(HANDLE file, LARGE_INTEGER distance, LARGE_INTEGER* position, DWORD method);
BOOL WINAPI SetEndOfFile(HANDLE file);
DWORD WINAPI GetFileSize(HANDLE file, LPDWORD sizeHigh);
BOOL WINAPI GetFileSizeEx(HANDLE file, LARGE_INTEGER* size);
BOOL WINAPI GetFileTime(HANDLE file, LPFILETIME creation, LPFILETIME access, LPFILETIME write);
BOOL WINAPI FlushFileBuffers(HANDLE file);
BOOL WINAPI DeleteFileA(LPCSTR name);
BOOL WINAPI MoveFileA(LPCSTR from, LPCSTR to);
BOOL WINAPI MoveFileExA(LPCSTR from, LPCSTR to, DWORD flags);
BOOL WINAPI CopyFileA(LPCSTR from, LPCSTR to, BOOL failIfExists);
BOOL WINAPI CreateDirectoryA(LPCSTR name, SECURITY_ATTRIBUTES* security);
BOOL WINAPI RemoveDirectoryA(LPCSTR name);
DWORD WINAPI GetFileAttributesA(LPCSTR name);
BOOL WINAPI SetFileAttributesA(LPCSTR name, DWORD attributes);
BOOL WINAPI GetFileAttributesExA(LPCSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info);
HANDLE WINAPI FindFirstFileA(LPCSTR pattern, LPWIN32_FIND_DATAA data);
BOOL WINAPI FindNextFileA(HANDLE find, LPWIN32_FIND_DATAA data);
BOOL WINAPI FindClose(HANDLE find);
BOOL WINAPI GetDiskFreeSpaceExA(LPCSTR directory, PULARGE_INTEGER freeToCaller, PULARGE_INTEGER total,
                                PULARGE_INTEGER totalFree);
UINT WINAPI GetDriveTypeA(LPCSTR root);
DWORD WINAPI GetLogicalDrives(void);
BOOL WINAPI GetVolumeInformationA(LPCSTR root, LPSTR volumeName, DWORD volumeNameSize, LPDWORD serial,
                                  LPDWORD maxComponentLength, LPDWORD flags, LPSTR fileSystemName,
                                  DWORD fileSystemNameSize);
UINT WINAPI GetWindowsDirectoryA(LPSTR buffer, UINT size);
DWORD WINAPI GetTempPathA(DWORD size, LPSTR buffer);
DWORD WINAPI GetModuleFileNameA(HMODULE module, LPSTR buffer, DWORD size);
DWORD WINAPI GetCurrentDirectoryA(DWORD size, LPSTR buffer);
BOOL WINAPI SetCurrentDirectoryA(LPCSTR path);
DWORD WINAPI GetFullPathNameA(LPCSTR name, DWORD size, LPSTR buffer, LPSTR* filePart);

// Profiles (.ini) and the registry
UINT WINAPI GetPrivateProfileIntA(LPCSTR section, LPCSTR key, INT fallback, LPCSTR file);
DWORD WINAPI GetPrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR fallback, LPSTR buffer, DWORD size,
                                      LPCSTR file);
BOOL WINAPI WritePrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR value, LPCSTR file);
DWORD WINAPI GetPrivateProfileSectionA(LPCSTR section, LPSTR buffer, DWORD size, LPCSTR file);
LSTATUS WINAPI RegOpenKeyExA(HKEY key, LPCSTR subKey, DWORD options, REGSAM access, HKEY* result);
LSTATUS WINAPI RegCreateKeyExA(HKEY key, LPCSTR subKey, DWORD reserved, LPSTR className, DWORD options,
                               REGSAM access, SECURITY_ATTRIBUTES* security, HKEY* result, LPDWORD disposition);
LSTATUS WINAPI RegQueryValueExA(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size);
LSTATUS WINAPI RegSetValueExA(HKEY key, LPCSTR name, DWORD reserved, DWORD type, const BYTE* data, DWORD size);
LSTATUS WINAPI RegDeleteValueA(HKEY key, LPCSTR name);
LSTATUS WINAPI RegCloseKey(HKEY key);
LSTATUS WINAPI RegCreateKeyA(HKEY key, LPCSTR subKey, HKEY* result);
#define RRF_RT_REG_DWORD 0x00000010
LSTATUS WINAPI RegGetValueA(HKEY key, LPCSTR subKey, LPCSTR value, DWORD flags, LPDWORD type, PVOID data,
                            LPDWORD size);

// Modules, process, environment
HMODULE WINAPI GetModuleHandleA(LPCSTR name);
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004
BOOL WINAPI GetModuleHandleExA(DWORD flags, LPCSTR name, HMODULE* module);
HMODULE WINAPI LoadLibraryA(LPCSTR name);
FARPROC WINAPI GetProcAddress(HMODULE module, LPCSTR name);
BOOL WINAPI FreeLibrary(HMODULE module);
HANDLE WINAPI GetCurrentProcess(void);
BOOL WINAPI GetProcessTimes(HANDLE process, FILETIME* created, FILETIME* exited, FILETIME* kernel, FILETIME* user);
DWORD WINAPI GetCurrentProcessId(void);
void WINAPI ExitProcess(UINT code);
BOOL WINAPI TerminateProcess(HANDLE process, UINT code);
LPSTR WINAPI GetCommandLineA(void);
DWORD WINAPI GetEnvironmentVariableA(LPCSTR name, LPSTR buffer, DWORD size);
BOOL WINAPI SetEnvironmentVariableA(LPCSTR name, LPCSTR value);
DWORD WINAPI GetLastError(void);
void WINAPI SetLastError(DWORD error);
BOOL WINAPI GetVersionExA(LPOSVERSIONINFOA info);
void WINAPI OutputDebugStringA(LPCSTR text);
BOOL WINAPI IsDebuggerPresent(void);
void WINAPI DebugBreak(void);
EXECUTION_STATE WINAPI SetThreadExecutionState(EXECUTION_STATE flags);
BOOL WINAPI AllocConsole(void);
BOOL WINAPI FreeConsole(void);
int WINAPIV wsprintfA(LPSTR buffer, LPCSTR format, ...);
int WINAPI wvsprintfA(LPSTR buffer, LPCSTR format, va_list arguments);

// Time
void WINAPI Sleep(DWORD milliseconds);
DWORD WINAPI SleepEx(DWORD milliseconds, BOOL alertable);
DWORD WINAPI GetTickCount(void);
ULONGLONG WINAPI GetTickCount64(void);
BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER* count);
BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER* frequency);
void WINAPI GetLocalTime(LPSYSTEMTIME time);
void WINAPI GetSystemTime(LPSYSTEMTIME time);
void WINAPI GetSystemTimeAsFileTime(LPFILETIME time);
BOOL WINAPI FileTimeToSystemTime(const FILETIME* fileTime, LPSYSTEMTIME systemTime);
BOOL WINAPI FileTimeToLocalFileTime(const FILETIME* fileTime, LPFILETIME localTime);
BOOL WINAPI SystemTimeToFileTime(const SYSTEMTIME* systemTime, LPFILETIME fileTime);
LONG WINAPI CompareFileTime(const FILETIME* a, const FILETIME* b);

// Threads and synchronisation
HANDLE WINAPI CreateThread(SECURITY_ATTRIBUTES* security, SIZE_T stackSize, LPTHREAD_START_ROUTINE start,
                           LPVOID parameter, DWORD flags, LPDWORD threadId);
DWORD WINAPI ResumeThread(HANDLE thread);
DWORD WINAPI SuspendThread(HANDLE thread);
BOOL WINAPI SetThreadPriority(HANDLE thread, int priority);
int WINAPI GetThreadPriority(HANDLE thread);
DWORD_PTR WINAPI SetThreadAffinityMask(HANDLE thread, DWORD_PTR mask);
HANDLE WINAPI GetCurrentThread(void);
DWORD WINAPI GetCurrentThreadId(void);
BOOL WINAPI GetExitCodeThread(HANDLE thread, LPDWORD code);
BOOL WINAPI TerminateThread(HANDLE thread, DWORD code);
void WINAPI ExitThread(DWORD code);
BOOL WINAPI SwitchToThread(void);
HANDLE WINAPI CreateMutexA(SECURITY_ATTRIBUTES* security, BOOL initialOwner, LPCSTR name);
BOOL WINAPI ReleaseMutex(HANDLE mutex);
HANDLE WINAPI CreateSemaphoreA(SECURITY_ATTRIBUTES* security, LONG initial, LONG maximum, LPCSTR name);
BOOL WINAPI ReleaseSemaphore(HANDLE semaphore, LONG count, LPLONG previous);
HANDLE WINAPI CreateEventA(SECURITY_ATTRIBUTES* security, BOOL manualReset, BOOL initialState, LPCSTR name);
BOOL WINAPI SetEvent(HANDLE event);
BOOL WINAPI ResetEvent(HANDLE event);
DWORD WINAPI WaitForSingleObject(HANDLE handle, DWORD milliseconds);
DWORD WINAPI WaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD milliseconds);
void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION section);
BOOL WINAPI InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION section, DWORD spinCount);
void WINAPI EnterCriticalSection(LPCRITICAL_SECTION section);
BOOL WINAPI TryEnterCriticalSection(LPCRITICAL_SECTION section);
void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION section);
void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION section);
void WINAPI InitializeSRWLock(PSRWLOCK lock);
void WINAPI AcquireSRWLockExclusive(PSRWLOCK lock);
void WINAPI ReleaseSRWLockExclusive(PSRWLOCK lock);
void WINAPI AcquireSRWLockShared(PSRWLOCK lock);
void WINAPI ReleaseSRWLockShared(PSRWLOCK lock);
BOOLEAN WINAPI TryAcquireSRWLockExclusive(PSRWLOCK lock);

// Memory
LPVOID WINAPI VirtualAlloc(LPVOID address, SIZE_T size, DWORD type, DWORD protect);
BOOL WINAPI VirtualFree(LPVOID address, SIZE_T size, DWORD type);
BOOL WINAPI VirtualProtect(LPVOID address, SIZE_T size, DWORD protect, PDWORD oldProtect);
SIZE_T WINAPI VirtualQuery(LPCVOID address, PMEMORY_BASIC_INFORMATION info, SIZE_T size);
void WINAPI GlobalMemoryStatus(MEMORYSTATUS* status);
HGLOBAL WINAPI GlobalAlloc(UINT flags, SIZE_T size);
HGLOBAL WINAPI GlobalFree(HGLOBAL memory);
LPVOID WINAPI GlobalLock(HGLOBAL memory);
BOOL WINAPI GlobalUnlock(HGLOBAL memory);
BOOL WINAPI FlushInstructionCache(HANDLE process, LPCVOID address, SIZE_T size);

// Windows, the desktop, input
ATOM WINAPI RegisterClassExA(const WNDCLASSEXA* windowClass);
BOOL WINAPI UnregisterClassA(LPCSTR className, HINSTANCE instance);
HWND WINAPI CreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR title, DWORD style, int x, int y, int width,
                            int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter);
BOOL WINAPI DestroyWindow(HWND window);
BOOL WINAPI ShowWindow(HWND window, int command);
BOOL WINAPI UpdateWindow(HWND window);
BOOL WINAPI SetForegroundWindow(HWND window);
HWND WINAPI GetForegroundWindow(void);
BOOL WINAPI BringWindowToTop(HWND window);
HWND WINAPI FindWindowA(LPCSTR className, LPCSTR title);
HWND WINAPI FindWindowExA(HWND parent, HWND after, LPCSTR className, LPCSTR title);
BOOL WINAPI InvalidateRect(HWND window, const RECT* rect, BOOL erase);
BOOL WINAPI ValidateRect(HWND window, const RECT* rect);
BOOL WINAPI AdjustWindowRect(LPRECT rect, DWORD style, BOOL menu);
BOOL WINAPI AdjustWindowRectEx(LPRECT rect, DWORD style, BOOL menu, DWORD exStyle);
BOOL WINAPI GetClientRect(HWND window, LPRECT rect);
BOOL WINAPI GetWindowRect(HWND window, LPRECT rect);
BOOL WINAPI SetWindowPos(HWND window, HWND after, int x, int y, int width, int height, UINT flags);
BOOL WINAPI MoveWindow(HWND window, int x, int y, int width, int height, BOOL repaint);
BOOL WINAPI SetWindowTextA(HWND window, LPCSTR text);
BOOL WINAPI IsIconic(HWND window);
BOOL WINAPI IsWindowVisible(HWND window);
LONG WINAPI SetWindowLongA(HWND window, int index, LONG value);
LONG WINAPI GetWindowLongA(HWND window, int index);
LRESULT WINAPI DefWindowProcA(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI SendMessageA(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
BOOL WINAPI PostMessageA(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
BOOL WINAPI PeekMessageA(LPMSG message, HWND window, UINT first, UINT last, UINT remove);
BOOL WINAPI GetMessageA(LPMSG message, HWND window, UINT first, UINT last);
BOOL WINAPI TranslateMessage(const MSG* message);
LRESULT WINAPI DispatchMessageA(const MSG* message);
void WINAPI PostQuitMessage(int code);
int WINAPI MessageBoxA(HWND window, LPCSTR text, LPCSTR caption, UINT type);
HCURSOR WINAPI LoadCursorA(HINSTANCE instance, LPCSTR name);
HICON WINAPI LoadIconA(HINSTANCE instance, LPCSTR name);
HCURSOR WINAPI SetCursor(HCURSOR cursor);
int WINAPI ShowCursor(BOOL show);
BOOL WINAPI GetCursorPos(LPPOINT point);
BOOL WINAPI SetCursorPos(int x, int y);
BOOL WINAPI ClipCursor(const RECT* rect);
BOOL WINAPI ScreenToClient(HWND window, LPPOINT point);
BOOL WINAPI ClientToScreen(HWND window, LPPOINT point);
UINT WINAPI MapVirtualKeyA(UINT code, UINT type);
SHORT WINAPI GetAsyncKeyState(int key);
SHORT WINAPI GetKeyState(int key);
int WINAPI GetKeyNameTextA(LONG lParam, LPSTR buffer, int size);
int WINAPI GetSystemMetrics(int index);
BOOL WINAPI SystemParametersInfoA(UINT action, UINT uiParam, PVOID pvParam, UINT winIni);
BOOL WINAPI EnumDisplaySettingsA(LPCSTR device, DWORD mode, DEVMODEA* devMode);
LONG WINAPI ChangeDisplaySettingsA(DEVMODEA* devMode, DWORD flags);
HDC WINAPI GetDC(HWND window);
int WINAPI ReleaseDC(HWND window, HDC dc);
BOOL WINAPI OpenClipboard(HWND owner);
BOOL WINAPI CloseClipboard(void);
BOOL WINAPI EmptyClipboard(void);
HANDLE WINAPI GetClipboardData(UINT format);
HANDLE WINAPI SetClipboardData(UINT format, HANDLE memory);

// COM
HRESULT WINAPI CoInitialize(LPVOID reserved);
HRESULT WINAPI CoInitializeEx(LPVOID reserved, DWORD flags);
void WINAPI CoUninitialize(void);
HRESULT WINAPI CoCreateInstance(REFCLSID clsid, IUnknown* outer, DWORD context, REFIID iid, LPVOID* object);
void WINAPI CoTaskMemFree(LPVOID memory);

#ifdef __cplusplus
}
#endif

// Interlocked operations, inline on the compiler's atomics
inline LONG InterlockedExchange(LONG volatile* target, LONG value) { return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST); }
inline LONG InterlockedIncrement(LONG volatile* target) { return __atomic_add_fetch(target, 1, __ATOMIC_SEQ_CST); }
inline LONG InterlockedDecrement(LONG volatile* target) { return __atomic_sub_fetch(target, 1, __ATOMIC_SEQ_CST); }
inline LONG InterlockedExchangeAdd(LONG volatile* target, LONG value) { return __atomic_fetch_add(target, value, __ATOMIC_SEQ_CST); }
inline LONG InterlockedCompareExchange(LONG volatile* target, LONG exchange, LONG comparand)
{
    __atomic_compare_exchange_n(target, &comparand, exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return comparand;
}
inline PVOID InterlockedExchangePointer(PVOID volatile* target, PVOID value) { return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST); }
inline PVOID InterlockedCompareExchangePointer(PVOID volatile* target, PVOID exchange, PVOID comparand)
{
    __atomic_compare_exchange_n(target, &comparand, exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return comparand;
}

#ifdef __cplusplus
// Thread functions declared with DXVK's DWORD (uint32_t) rather than Windows' unsigned long: the same function in wasm.
inline HANDLE CreateThread(SECURITY_ATTRIBUTES* security, SIZE_T stackSize, DWORD(WINAPI* start)(LPVOID), LPVOID parameter,
                           DWORD flags, LPDWORD id)
{
    return CreateThread(security, stackSize, reinterpret_cast<LPTHREAD_START_ROUTINE>(start), parameter, flags, id);
}
#endif

#pragma once
// thug2-web: force-included into every game source (-include). The MSVC C runtime's extras, on the C library.
#ifdef __cplusplus
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <strings.h>

#define _TRUNCATE ((size_t)-1)
#define _MAX_PATH 260
#define __forceinline inline __attribute__((always_inline))
#define __debugbreak() __builtin_trap()

inline int _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
inline int _strnicmp(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }
inline int stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
inline int strnicmp(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }
inline char* _strupr(char* s) { for (char* p = s; *p; ++p) *p = (char)toupper((unsigned char)*p); return s; }
inline char* _strlwr(char* s) { for (char* p = s; *p; ++p) *p = (char)tolower((unsigned char)*p); return s; }
inline int _strupr_s(char* s, size_t) { _strupr(s); return 0; }
template <size_t N> inline int _strupr_s(char (&s)[N]) { _strupr(s); return 0; }
inline char* _strdup(const char* s) { return strdup(s); }
inline int _snprintf(char* buffer, size_t size, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int n = vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return n < 0 || (size_t)n >= size ? -1 : n; // MSVC: -1 when truncated
}
inline int _vsnprintf(char* buffer, size_t size, const char* format, va_list arguments)
{
    int n = vsnprintf(buffer, size, format, arguments);
    return n < 0 || (size_t)n >= size ? -1 : n;
}
inline int vsprintf_s(char* buffer, size_t size, const char* format, va_list arguments) { return vsnprintf(buffer, size, format, arguments); }
inline int sprintf_s(char* buffer, size_t size, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int n = vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return n;
}
template <size_t N> inline int sprintf_s(char (&buffer)[N], const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int n = vsnprintf(buffer, N, format, arguments);
    va_end(arguments);
    return n;
}
inline int _snprintf_s(char* buffer, size_t size, size_t count, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    size_t limit = count == _TRUNCATE || count + 1 > size ? size : count + 1;
    int n = vsnprintf(buffer, limit, format, arguments);
    va_end(arguments);
    return n < 0 || (size_t)n >= limit ? -1 : n;
}
inline int strcpy_s(char* to, size_t size, const char* from) { snprintf(to, size, "%s", from); return 0; }
template <size_t N> inline int strcpy_s(char (&to)[N], const char* from) { return strcpy_s(to, N, from); }
inline int strncpy_s(char* to, size_t size, const char* from, size_t count)
{
    size_t n = strnlen(from, count == _TRUNCATE ? size - 1 : count);
    if (n >= size) n = size - 1;
    memcpy(to, from, n);
    to[n] = 0;
    return 0;
}
template <size_t N> inline int strncpy_s(char (&to)[N], const char* from, size_t count) { return strncpy_s(to, N, from, count); }
inline int strcat_s(char* to, size_t size, const char* from)
{
    size_t used = strnlen(to, size);
    snprintf(to + used, size - used, "%s", from);
    return 0;
}
template <size_t N> inline int strcat_s(char (&to)[N], const char* from) { return strcat_s(to, N, from); }
inline char* strtok_s(char* s, const char* delimiters, char** context) { return strtok_r(s, delimiters, context); }
// A Windows path (backslashes, any case) as the file system's (port/src/win32/files.cpp), into `buffer`.
const char* thug2web_path(const char* windowsPath, char* buffer, size_t size);
inline FILE* thug2web_fopen(const char* name, const char* mode)
{
    char path[1024];
    return fopen(thug2web_path(name, path, sizeof(path)), mode);
}
inline int fopen_s(FILE** file, const char* name, const char* mode)
{
    *file = thug2web_fopen(name, mode);
    return *file ? 0 : errno;
}
inline int freopen_s(FILE** file, const char* name, const char* mode, FILE* stream)
{
    *file = freopen(name, mode, stream);
    return *file ? 0 : errno;
}
inline int _fileno(FILE* file) { return fileno(file); }
inline int _fseeki64(FILE* file, long long offset, int origin) { return fseeko(file, offset, origin); }
inline long long _ftelli64(FILE* file) { return ftello(file); }
inline int localtime_s(struct tm* out, const time_t* time) { return localtime_r(time, out) ? 0 : EINVAL; }

// wchar_t is 2 bytes here, as on Windows (-fshort-wchar); the C library's wide functions assume 4. The ones the game
// uses are these (Latin-1 conversions, as the game's text is).
inline wchar_t* thug2web_wcscpy(wchar_t* to, const wchar_t* from)
{
    wchar_t* out = to;
    while ((*out++ = *from++) != 0) {
    }
    return to;
}
inline size_t thug2web_wcstombs(char* to, const wchar_t* from, size_t count)
{
    size_t n = 0;
    for (; from[n] && n < count; ++n)
        to[n] = static_cast<char>(from[n]);
    if (n < count)
        to[n] = 0;
    return n;
}
inline size_t thug2web_mbstowcs(wchar_t* to, const char* from, size_t count)
{
    size_t n = 0;
    for (; from[n] && n < count; ++n)
        to[n] = static_cast<unsigned char>(from[n]);
    if (n < count)
        to[n] = 0;
    return n;
}
namespace std {
using ::thug2web_mbstowcs;
using ::thug2web_wcscpy;
using ::thug2web_wcstombs;
} // namespace std
#define wcscpy thug2web_wcscpy
#define wcstombs thug2web_wcstombs
#define mbstowcs thug2web_mbstowcs
template <size_t N>
inline int mbstowcs_s(size_t* converted, wchar_t (&to)[N], const char* from, size_t count)
{
    size_t n = thug2web_mbstowcs(to, from, count == _TRUNCATE || count >= N ? N - 1 : count);
    to[n] = 0;
    if (converted)
        *converted = n + 1;
    return 0;
}

// sscanf_s: %s, %c and %[ take a buffer size after the pointer. Every use in the game is checked against this.
int thug2web_sscanf_s(const char* text, const char* format, ...);
#define sscanf_s thug2web_sscanf_s
#endif

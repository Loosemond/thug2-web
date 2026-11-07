// MSVC's sscanf_s on the C library's sscanf: the buffer sizes that follow %s, %c and %[ pointers are taken off the
// argument list (the game's formats give widths for those, so the sizes aren't needed). Up to 16 conversions.

#include <cstdarg>
#include <cstdio>

int thug2web_sscanf_s(const char* text, const char* format, ...)
{
    void* p[16] = {};
    int n = 0;
    va_list args;
    va_start(args, format);
    for (const char* f = format; *f; ++f) {
        if (*f != '%')
            continue;
        ++f;
        if (*f == '%')
            continue;
        bool assigned = *f != '*';
        if (!assigned)
            ++f;
        while ((*f >= '0' && *f <= '9') || *f == 'h' || *f == 'l' || *f == 'L' || *f == 'z' || *f == 'j' ||
               *f == 't')
            ++f;
        char conversion = *f;
        if (conversion == '[') { // (a scan set: up to its closing bracket, which may come first)
            ++f;
            if (*f == '^')
                ++f;
            if (*f == ']')
                ++f;
            while (*f && *f != ']')
                ++f;
        }
        if (!*f)
            break;
        if (!assigned)
            continue;
        if (n < 16)
            p[n++] = va_arg(args, void*);
        if (conversion == 's' || conversion == 'c' || conversion == '[')
            (void)va_arg(args, unsigned);
    }
    va_end(args);
    return std::sscanf(text, format, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12],
                       p[13], p[14], p[15]);
}

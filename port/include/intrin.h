#pragma once
// thug2-web: the MSVC intrinsics the game uses.
#include <chrono>
#include <cstdint>
#include <cstring>
// The game reads it as a cycle counter for timing only; nanoseconds keep it monotonic.
inline uint64_t __rdtsc()
{
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
inline void __movsb(unsigned char* to, const unsigned char* from, size_t count) { memcpy(to, from, count); }
inline void __movsd(unsigned long* to, const unsigned long* from, size_t count) { memcpy(to, from, count * 4); }
inline void __stosb(unsigned char* to, unsigned char value, size_t count) { memset(to, value, count); }

// The sound's mixer (mixer.h): its thread, the ring the page plays, and the gains.

#include "dsound/mixer.h"

#include <emscripten/emscripten.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

namespace Mixer {

namespace {

using Clock = std::chrono::steady_clock;

// The ring the page's AudioWorklet reads (web/index.html - keep the layout in step): the frames written and read so
// far (each side moves only its own count, both wrap), the capacity in frames, the frames the worklet found none for
// once sound had started (its count, wrapping: what was heard as silence), then the frames.
constexpr uint32_t kCapacity = 16384;
struct Ring {
    std::atomic<uint32_t> written{0};
    std::atomic<uint32_t> read{0};
    uint32_t capacity = kCapacity;
    std::atomic<uint32_t> missed{0};
    float frames[kCapacity * 2] = {};
};
static_assert(sizeof(std::atomic<uint32_t>) == 4, "the worklet reads the counts as 32-bit integers");
alignas(16) Ring g_ring;

// The page's output rate while its sound runs (0: not running - mixed at the clock's pace and dropped).
std::atomic<int> g_rate{0};
constexpr int kClockRate = 44100;
// (what's kept queued ahead of the page: enough for a late mix, little enough that a sound starts promptly - more
// where the page's output takes its sound in bigger bites than the ring held, up to kMostAhead)
constexpr double kAhead = 0.05;
constexpr double kMostAhead = 0.25;

std::mutex g_lock; // (the sources; held for a whole mix)
std::vector<Source*> g_sources;
std::atomic<float> g_master{1.0f};
std::once_flag g_started;

// THUG2_SOUND_RAW=<file>, under Node: what's mixed at the clock's pace, appended as raw stereo floats at 44.1 kHz (with
// Node's own fs, from this thread - the C library's would wait on the main thread).
EM_JS(void, SaveMix, (const float* samples, int count), {
    if (typeof process == "undefined" || !process.env || !process.env.THUG2_SOUND_RAW)
        return;
    require("fs").appendFileSync(process.env.THUG2_SOUND_RAW, HEAPU8.slice(samples, samples + count * 4));
});

// The time spent mixing and the streams' gaps (microseconds) - for the page's stats.
std::atomic<uint64_t> g_mixMicroseconds{0};
std::atomic<uint64_t> g_dryMicroseconds{0};

void MixInto(std::vector<float>& mix, int frames, int rate)
{
    auto start = Clock::now();
    mix.assign(static_cast<size_t>(frames) * 2, 0.0f);
    {
        std::lock_guard lock(g_lock);
        for (Source* source : g_sources)
            source->Mix(mix.data(), frames, rate);
    }
    float master = g_master.load(std::memory_order_relaxed);
    for (float& sample : mix)
        sample = std::clamp(sample * master, -1.0f, 1.0f);
    g_mixMicroseconds += static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count());
}

void Run()
{
    std::vector<float> mix;
    auto last = Clock::now();
    double carry = 0;
    uint32_t lastRead = g_ring.read.load();
    auto readMoved = Clock::now();
    double ahead = kAhead;
    uint32_t lastMissed = g_ring.missed.load();
    for (;;) {
        int rate = g_rate.load();
        auto now = Clock::now();
        if (rate) {
            // The page's pace: topped up to kAhead queued. If its worklet stops reading (a suspended context), the
            // clock's pace until it reads again.
            uint32_t read = g_ring.read.load(std::memory_order_acquire);
            uint32_t written = g_ring.written.load(std::memory_order_relaxed);
            if (read != lastRead) {
                lastRead = read;
                readMoved = now;
            }
            bool stalled = written != read && now - readMoved > std::chrono::milliseconds(300);
            if (uint32_t missed = g_ring.missed.load(std::memory_order_relaxed); missed != lastMissed) {
                lastMissed = missed; // (the page had nothing to play: more ahead)
                ahead = std::min(ahead * 1.5, kMostAhead);
            }
            if (!stalled) {
                uint32_t queued = written - read;
                uint32_t target = static_cast<uint32_t>(rate * ahead);
                if (queued >= target) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    last = now;
                    continue;
                }
                int frames = static_cast<int>(target - queued);
                MixInto(mix, frames, rate);
                for (int i = 0; i < frames; ++i) {
                    uint32_t at = (written + i) % kCapacity;
                    g_ring.frames[at * 2] = mix[i * 2];
                    g_ring.frames[at * 2 + 1] = mix[i * 2 + 1];
                }
                g_ring.written.store(written + frames, std::memory_order_release);
                last = now;
                carry = 0;
                continue;
            }
        } else {
            rate = kClockRate;
        }
        double due = std::chrono::duration<double>(now - last).count() * rate + carry;
        int frames = static_cast<int>(due);
        if (frames < 256) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        frames = std::min(frames, rate / 4); // (after a long pause, not the whole of it at once)
        carry = due - static_cast<int>(due);
        last = now;
        MixInto(mix, frames, rate);
        SaveMix(mix.data(), static_cast<int>(mix.size()));
    }
}

} // namespace

void Add(Source* source)
{
    std::call_once(g_started, [] { std::thread(Run).detach(); });
    std::lock_guard lock(g_lock);
    g_sources.push_back(source);
}

void Remove(Source* source)
{
    std::lock_guard lock(g_lock);
    g_sources.erase(std::remove(g_sources.begin(), g_sources.end(), source), g_sources.end());
}

void SetMasterVolume(long volume)
{
    g_master = volume <= -10000 ? 0.0f : std::pow(10.0f, static_cast<float>(std::min(volume, 0L)) / 2000.0f);
}

void Gains(long volume, long pan, float& left, float& right)
{
    float gain = volume <= -10000 ? 0.0f : std::pow(10.0f, static_cast<float>(std::min(volume, 0L)) / 2000.0f);
    pan = std::clamp(pan, -10000L, 10000L);
    left = gain * (pan > 0 ? std::pow(10.0f, static_cast<float>(-pan) / 2000.0f) : 1.0f);
    right = gain * (pan < 0 ? std::pow(10.0f, static_cast<float>(pan) / 2000.0f) : 1.0f);
}

void NoteDry(int frames, int rate)
{
    if (rate > 0)
        g_dryMicroseconds += static_cast<uint64_t>(frames) * 1000000 / static_cast<uint64_t>(rate);
}

} // namespace Mixer

// From the page (web/index.html): where the ring is; the page's sound running at `rate` (or stopped: 0).
extern "C" EMSCRIPTEN_KEEPALIVE void* thug2_audio_ring() { return &Mixer::g_ring; }
extern "C" EMSCRIPTEN_KEEPALIVE void thug2_audio_start(int rate) { Mixer::g_rate = rate; }
// The time spent mixing since the last call (ms) - for the stats.
extern "C" double thug2_audio_mix_ms() { return static_cast<double>(Mixer::g_mixMicroseconds.exchange(0)) / 1000; }
// The streams' gaps since the last call (ms) - for the page's stats line.
extern "C" EMSCRIPTEN_KEEPALIVE double thug2_audio_dry_ms()
{
    return static_cast<double>(Mixer::g_dryMicroseconds.exchange(0)) / 1000;
}

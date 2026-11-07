// The Bink SDK (binkw32.dll), which the game calls through its import table (0x006452E8-0x00645320): movies and the
// music and voice streams. The music (data\streams\music, opened by name) and the streams (pcm.wad's entries, opened
// from memory) are played for their sound - decoded (bink_audio.h) frame by frame as the game steps through them
// (BinkWait / BinkDoFrame / BinkNextFrame on its stream thread) into a queue the mixer plays (dsound/mixer.h), so the
// game's frames keep pace with the sound as with Bink's own. In the page a stream's file comes in the background
// (thug2web/downloads.h) - the game opens its streams on its own thread, which a download would hold up - and the
// game waits for it as for sound not yet due. Movies aren't opened (no video decoder): as a missing file, the game
// goes on without them.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <emscripten/emscripten.h>

#include "dsound/mixer.h"
#include "library/bink_audio.h"
#include "thug2web/downloads.h"
#include "thug2web/imports.h"
#include "win32/paths.h"

namespace {

constexpr uint32_t kOpenIoSize = 0x01000000;     // BinkOpen: a file name (with the I/O size set)
constexpr uint32_t kOpenFromMemory = 0x04000000; // BinkOpen: the file's bytes in memory
// (the sound decoded ahead of what's played: enough to ride out the game's stream thread running late - what's queued
// when the game closes a stream that ended is still played)
constexpr double kAhead = 0.4;

// (Under Node a file read on another thread waits on the main thread, which the game keeps busy: a stream's file is
// read whole when it's opened there.)
EM_JS(bool, UnderNode, (), { return typeof process != "undefined" && !!process.versions && !!process.versions.node; });

// pcm.wad's entries in the page come in the background once they're opened: upstream's LoadStreamFile puts this where
// it would have read one (thug2web_mark_stream_entry), BinkOpen downloads it. (It starts as the file does, "BIKi",
// which the game keeps; a file has its size where the mark is.)
struct EntryMark {
    char tag[4];
    uint32_t mark;
    uint32_t checksum;
    uint32_t size;
};
constexpr uint32_t kEntryMark = 0xFFFFFFFFu;
// (a stream's frame count until its file is in: none it would reach)
constexpr uint32_t kFramesUnknown = 1u << 30;

// The SDK's BINK as the game reads it (game/gfx/movie.h's Nx::Bink).
struct BinkFields {
    uint32_t width, height;
    uint32_t frames;
    uint32_t frameNum; // the next frame (from 1)
    uint32_t lastFrameNum;
    uint32_t frameRate, frameRateDiv;
};

class Stream final : public Mixer::Source {
public:
    BinkAudio::Header header;
    std::string name; // (for the log)

    enum class State { Coming, Ready, Failed };

    bool OpenFile(const char* name)
    {
        if ((m_download = Thug2Web::DownloadGameFile(Win32::PosixPath(name))))
            return true;
        m_file = thug2web_fopen(name, "rb");
        if (!m_file)
            return false;
        uint8_t start[0x2C];
        if (std::fread(start, 1, sizeof(start), m_file) != sizeof(start))
            return false;
        size_t size = BinkAudio::Header::Size(start, sizeof(start));
        if (!size)
            return false;
        if (UnderNode()) {
            std::fseek(m_file, 0, SEEK_END);
            m_whole.resize(static_cast<size_t>(std::ftell(m_file)));
            std::fseek(m_file, 0, SEEK_SET);
            if (std::fread(m_whole.data(), 1, m_whole.size(), m_file) != m_whole.size())
                return false;
            return OpenMemory(m_whole.data());
        }
        std::vector<uint8_t> all(size);
        std::fseek(m_file, 0, SEEK_SET);
        return std::fread(all.data(), 1, size, m_file) == size && Ready(all.data(), size);
    }
    bool OpenMemory(const uint8_t* data)
    {
        EntryMark mark;
        std::memcpy(&mark, data, sizeof(mark));
        if (mark.mark == kEntryMark && std::memcmp(mark.tag, "BIKi", 4) == 0) {
            m_download = Thug2Web::DownloadStreamEntry(mark.checksum, mark.size);
            return m_download != nullptr;
        }
        m_memory = data;
        size_t size = BinkAudio::Header::Size(data, 0x2C);
        return size && Ready(data, size);
    }
    ~Stream() override
    {
        if (m_file)
            std::fclose(m_file);
    }

    // Where the file is: in, or still coming (its header read and the decoder ready once it's in) - or not coming, a
    // silent frame then (the game goes on). On the game's threads.
    State Poll()
    {
        if (m_state == State::Coming && (m_download->Ready() || m_download->Failed())) {
            std::lock_guard lock(m_lock);
            if (!m_download->Ready() || !OpenMemory(m_download->bytes.data())) {
                header.frames = 1;
                header.frameRate = 30;
                header.frameRateDiv = 1;
                m_state = State::Failed;
            }
        }
        return m_state;
    }

    // The frame's sound (frames from 1) decoded onto the queue.
    void DecodeFrame(uint32_t frame)
    {
        const uint8_t* packet = nullptr;
        size_t packetSize = 0;
        if (m_state == State::Ready && frame >= 1 && frame <= header.frames) {
            uint32_t start = header.offsets[frame - 1], size = header.offsets[frame] - start;
            const uint8_t* bytes = m_memory ? m_memory + start : nullptr;
            if (!bytes) {
                m_frame.resize(size);
                if (std::fseek(m_file, start, SEEK_SET) == 0 && std::fread(m_frame.data(), 1, size, m_file) == size)
                    bytes = m_frame.data();
            }
            if (bytes)
                packet = BinkAudio::TrackPacket(bytes, size, packetSize);
        }
        std::lock_guard lock(m_lock);
        size_t before = m_queue.size();
        if (packet)
            m_decoder.Decode(packet, packetSize, m_queue);
        m_silentFrames = m_queue.size() > before ? 0 : m_silentFrames + 1;
        m_lastFrame = Clock::now();
        // (after the last frame what's queued runs out by design)
        m_flowing = m_silentFrames == 0 && frame < header.frames;
    }
    // Enough is queued: the game waits before its next frame. (Frames without sound go at the file's frame rate - the
    // game steps through them in a loop while this says the next is due.)
    bool Ahead()
    {
        std::lock_guard lock(m_lock);
        if (Queued() >= kAhead * header.sampleRate)
            return true;
        if (m_silentFrames < 2 || !header.frameRate)
            return false;
        auto frame = std::chrono::duration<double>(static_cast<double>(header.frameRateDiv) / header.frameRate);
        return Clock::now() - m_lastFrame < frame;
    }
    void Restart()
    {
        std::lock_guard lock(m_lock);
        m_decoder.Reset();
        m_queue.clear();
        m_read = 0;
        m_fraction = 0;
        m_flowing = false;
    }
    void SetVolume(int volume)
    {
        std::lock_guard lock(m_lock);
        m_volume = std::max(volume, 0) / 32768.0f;
    }
    void SetPan(int pan)
    {
        std::lock_guard lock(m_lock);
        m_pan = std::clamp(pan, 0, 65536);
    }
    void SetPaused(bool paused) { m_paused = paused; }
    void SetSoundOn(bool on) { m_soundOn = on; }
    // Closed by the game: what's queued is played out first (unless paused or silent).
    void Close() { m_closed = true; }
    bool Done()
    {
        std::lock_guard lock(m_lock);
        return m_closed && (m_paused || !m_soundOn || Queued() < 1);
    }

private:
    bool Ready(const uint8_t* header, size_t size)
    {
        if (!this->header.Parse(header, size) || this->header.tracks == 0 ||
            !m_decoder.Init(this->header.sampleRate, this->header.Channels(), this->header.flags))
            return false;
        m_state = State::Ready;
        return true;
    }
    size_t Queued() const
    {
        int channels = m_decoder.Channels(); // (none until the file is in)
        return channels ? (m_queue.size() - m_read) / channels : 0;
    }

    // The queue's sound at its rate resampled (linearly) to the mixer's, at the volume and pan; what's played dropped.
    void Mix(float* out, int frames, int rate) override
    {
        if (m_paused || !m_soundOn)
            return;
        std::lock_guard lock(m_lock);
        const int channels = m_decoder.Channels();
        if (!channels)
            return;
        const float* q = m_queue.data();
        const size_t queued = Queued();
        float left = m_volume * std::min(1.0f, (65536 - m_pan) / 32768.0f);
        float right = m_volume * std::min(1.0f, m_pan / 32768.0f);
        const double step = static_cast<double>(header.sampleRate) / rate;
        size_t frame = 0;
        double fraction = m_fraction;
        int i = 0;
        for (; i < frames && frame + 1 < queued; ++i) {
            const float* a = q + m_read + frame * channels;
            const float* b = a + channels;
            float f = static_cast<float>(fraction);
            float l = a[0] + (b[0] - a[0]) * f;
            float r = channels == 2 ? a[1] + (b[1] - a[1]) * f : l;
            out[i * 2] += l * left;
            out[i * 2 + 1] += r * right;
            fraction += step;
            auto whole = static_cast<size_t>(fraction);
            fraction -= static_cast<double>(whole);
            frame += whole;
        }
        if (m_closed && frame + 1 >= queued)
            frame = queued; // (the last sample, at the end)
        else if (i < frames && m_flowing)
            Mixer::NoteDry(frames - i, rate); // (the game's stream thread is late: a gap)
        m_read += frame * channels;
        m_fraction = fraction;
        if (m_read > 1 << 16 && m_read * 2 > m_queue.size()) {
            m_queue.erase(m_queue.begin(), m_queue.begin() + static_cast<std::ptrdiff_t>(m_read));
            m_read = 0;
        }
    }

    using Clock = std::chrono::steady_clock;

    FILE* m_file = nullptr;
    const uint8_t* m_memory = nullptr;
    std::vector<uint8_t> m_whole;
    std::shared_ptr<Thug2Web::Download> m_download;
    State m_state = State::Coming;
    int m_silentFrames = 0;
    bool m_flowing = false; // (sound is being decoded: running out is a gap)
    Clock::time_point m_lastFrame = Clock::now();
    std::vector<uint8_t> m_frame;
    BinkAudio::Decoder m_decoder;
    std::mutex m_lock;
    std::vector<float> m_queue; // (interleaved, at the file's rate; played from m_read)
    size_t m_read = 0;
    double m_fraction = 0;
    float m_volume = 1.0f;
    int m_pan = 32768;
    std::atomic<bool> m_paused{false}, m_soundOn{true}, m_closed{false};
};

// What the game holds: the BINK it reads, then the stream.
struct Bink {
    BinkFields fields;
    Stream* stream;
    bool coming; // (the fields wait for the stream's file)
};
static_assert(offsetof(Bink, fields) == 0, "the game reads the BINK's fields from its start");

// Streams closed while still sounding: freed once played out (on the game's threads - not the mixer's).
std::mutex g_closingLock;
std::vector<Stream*> g_closing;

void FreeFinished()
{
    std::lock_guard lock(g_closingLock);
    for (auto it = g_closing.begin(); it != g_closing.end();) {
        if ((*it)->Done()) {
            Mixer::Remove(*it);
            delete *it;
            it = g_closing.erase(it);
        } else {
            ++it;
        }
    }
}

Stream* Of(void* bink) { return bink ? static_cast<Bink*>(bink)->stream : nullptr; }

// Once the stream's file is in, the BINK's fields from its header (the frame the game went to kept); false till then.
bool Arrived(Bink& bink)
{
    if (!bink.coming)
        return true;
    Stream::State state = bink.stream->Poll();
    if (state == Stream::State::Coming)
        return false;
    const BinkAudio::Header& h = bink.stream->header;
    BinkFields& f = bink.fields;
    f.width = h.width;
    f.height = h.height;
    f.frames = h.frames;
    f.frameNum = std::clamp<uint32_t>(f.frameNum, 1, std::max<uint32_t>(h.frames, 1));
    f.frameRate = h.frameRate;
    f.frameRateDiv = h.frameRateDiv;
    bink.coming = false;
    static int logged = 0;
    if (state == Stream::State::Failed)
        std::fprintf(stderr, "bink: %s didn't come\n", bink.stream->name.c_str());
    else if (logged++ < 4)
        std::fprintf(stderr, "bink: %s, %u Hz, %d channel(s), %u frames\n", bink.stream->name.c_str(), h.sampleRate,
                     h.Channels(), h.frames);
    return true;
}

void* __stdcall BinkOpen(const void* name, uint32_t flags)
{
    FreeFinished();
    if (!(flags & (kOpenIoSize | kOpenFromMemory)))
        return nullptr; // (a movie)
    auto stream = std::make_unique<Stream>();
    stream->name = flags & kOpenFromMemory ? "a stream" : static_cast<const char*>(name);
    bool open = flags & kOpenFromMemory ? stream->OpenMemory(static_cast<const uint8_t*>(name))
                                        : stream->OpenFile(static_cast<const char*>(name));
    if (!open)
        return nullptr;
    auto* bink = new Bink{{0, 0, kFramesUnknown, 1, 0, 30, 1}, stream.release(), true};
    Arrived(*bink); // (at once, read here)
    Mixer::Add(bink->stream);
    return bink;
}

void __stdcall BinkClose(void* bink)
{
    if (!bink)
        return;
    Stream* stream = Of(bink);
    stream->Close();
    delete static_cast<Bink*>(bink);
    {
        std::lock_guard lock(g_closingLock);
        g_closing.push_back(stream);
    }
    FreeFinished();
}

// 0 when the next frame is due: when the sound queued runs short (none is while the file is coming).
int __stdcall BinkWait(void* bink)
{
    Stream* stream = Of(bink);
    if (!stream)
        return 0;
    return !Arrived(*static_cast<Bink*>(bink)) || stream->Ahead() ? 1 : 0;
}
int __stdcall BinkDoFrame(void* bink)
{
    if (Stream* stream = Of(bink); stream && Arrived(*static_cast<Bink*>(bink)))
        stream->DecodeFrame(static_cast<Bink*>(bink)->fields.frameNum);
    return 0;
}
// On to the next frame - after the last, the first again (as Bink plays on).
void __stdcall BinkNextFrame(void* bink)
{
    if (!bink)
        return;
    BinkFields& f = static_cast<Bink*>(bink)->fields;
    f.lastFrameNum = f.frameNum;
    f.frameNum = f.frameNum >= f.frames ? 1 : f.frameNum + 1;
}
void __stdcall BinkGoto(void* bink, uint32_t frame, int)
{
    if (!bink)
        return;
    BinkFields& f = static_cast<Bink*>(bink)->fields;
    f.frameNum = std::clamp<uint32_t>(frame, 1, std::max<uint32_t>(f.frames, 1));
    Of(bink)->Restart();
}

// Volume 0-32768 (32768 as recorded), pan 0 (left) - 32768 - 65536 (right); one sound track.
void __stdcall BinkSetVolume(void* bink, uint32_t, int volume)
{
    if (Stream* stream = Of(bink))
        stream->SetVolume(volume);
}
void __stdcall BinkSetPan(void* bink, uint32_t, int pan)
{
    if (Stream* stream = Of(bink))
        stream->SetPan(pan);
}
int __stdcall BinkPause(void* bink, int pause)
{
    if (Stream* stream = Of(bink))
        stream->SetPaused(pause != 0);
    return pause;
}
int __stdcall BinkSetSoundOnOff(void* bink, int on)
{
    if (Stream* stream = Of(bink))
        stream->SetSoundOn(on != 0);
    return on;
}

void __stdcall BinkSetIOSize(uint32_t) {}
int __stdcall BinkSetVideoOnOff(void*, int) { return 0; }
int __stdcall BinkOpenDirectSound(uintptr_t) { return 1; }
int __stdcall BinkSetSoundSystem(void*, void*) { return 1; }
int __stdcall BinkCopyToBuffer(void*, void*, int, uint32_t, uint32_t, uint32_t, uint32_t) { return 0; }

} // namespace

// Upstream's LoadStreamFile (game/pcm/bink_stream_files.cpp), in the page: pcm.wad's entry `checksum` marked where it
// would have been read - BinkOpen downloads it. False under Node (it's read).
extern "C" bool thug2web_mark_stream_entry(uint32_t checksum, size_t size, void* data)
{
    if (UnderNode() || size < sizeof(EntryMark))
        return false;
    EntryMark mark{{'B', 'I', 'K', 'i'}, kEntryMark, checksum, static_cast<uint32_t>(size)};
    std::memcpy(data, &mark, sizeof(mark));
    return true;
}

IMPORT_SLOT(0x006452E8, BinkOpen);
IMPORT_SLOT(0x006452EC, BinkDoFrame);
IMPORT_SLOT(0x006452F0, BinkSetPan);
IMPORT_SLOT(0x006452F4, BinkGoto);
IMPORT_SLOT(0x006452F8, BinkSetIOSize);
IMPORT_SLOT(0x006452FC, BinkSetVideoOnOff);
IMPORT_SLOT(0x00645300, BinkOpenDirectSound);
IMPORT_SLOT(0x00645304, BinkSetSoundSystem);
IMPORT_SLOT(0x00645308, BinkCopyToBuffer);
IMPORT_SLOT(0x0064530C, BinkSetSoundOnOff);
IMPORT_SLOT(0x00645310, BinkSetVolume);
IMPORT_SLOT(0x00645314, BinkPause);
IMPORT_SLOT(0x00645318, BinkWait);
IMPORT_SLOT(0x0064531C, BinkClose);
IMPORT_SLOT(0x00645320, BinkNextFrame);

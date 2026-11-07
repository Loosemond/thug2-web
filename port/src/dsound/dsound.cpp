// DirectSound for the web build: each secondary buffer is a source of the mixer (mixer.h), which plays it at its
// frequency, volume and pan and moves its play cursor - so the cursor is where the sound is mixed up to, and the game
// writes streams behind it as with real output. The primary buffer only carries the master volume.

#include <windows.h>
#include <dsound.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#include "dsound/mixer.h"

namespace {

class SoundBuffer final : public IDirectSoundBuffer, public Mixer::Source {
public:
    SoundBuffer(const DSBUFFERDESC& desc) : m_flags(desc.dwFlags)
    {
        if (desc.lpwfxFormat)
            m_format = *desc.lpwfxFormat;
        else
            m_format = {WAVE_FORMAT_PCM, 2, 44100, 44100 * 4, 4, 16, 0};
        m_frequency = m_format.nSamplesPerSec;
        m_data = std::make_shared<std::vector<uint8_t>>(desc.dwBufferBytes ? desc.dwBufferBytes
                                                                           : m_format.nAvgBytesPerSec);
        if (!(m_flags & DSBCAPS_PRIMARYBUFFER))
            Mixer::Add(this);
    }
    // (DuplicateSoundBuffer: the same sound, played on its own)
    SoundBuffer(const SoundBuffer& original)
        : m_flags(original.m_flags), m_format(original.m_format), m_data(original.m_data),
          m_frequency(original.m_frequency), m_volume(original.m_volume), m_pan(original.m_pan)
    {
        Mixer::Add(this);
    }
    ~SoundBuffer() override
    {
        if (!(m_flags & DSBCAPS_PRIMARYBUFFER))
            Mixer::Remove(this);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** object) override
    {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_references; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG left = --m_references;
        if (!left)
            delete this;
        return left;
    }

    HRESULT STDMETHODCALLTYPE GetCaps(void* caps) override
    {
        auto* c = static_cast<DWORD*>(caps); // DSBCAPS: dwSize, dwFlags, dwBufferBytes, ...
        c[1] = m_flags;
        c[2] = static_cast<DWORD>(m_data->size());
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentPosition(LPDWORD play, LPDWORD write) override
    {
        std::lock_guard lock(m_lock);
        if (play)
            *play = m_position;
        if (write)
            *write = static_cast<DWORD>((m_position + m_format.nBlockAlign * 16) % m_data->size());
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFormat(LPWAVEFORMATEX format, DWORD size, LPDWORD written) override
    {
        if (format)
            std::memcpy(format, &m_format, std::min<DWORD>(size, sizeof(m_format)));
        if (written)
            *written = sizeof(m_format);
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE GetVolume(LPLONG volume) override
    {
        *volume = m_volume;
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPan(LPLONG pan) override
    {
        *pan = m_pan;
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFrequency(LPDWORD frequency) override
    {
        *frequency = m_frequency;
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE GetStatus(LPDWORD status) override
    {
        std::lock_guard lock(m_lock);
        *status = m_playing ? (DSBSTATUS_PLAYING | (m_looping ? DSBSTATUS_LOOPING : 0)) : 0;
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE Initialize(IDirectSound8*, LPCDSBUFFERDESC) override { return DS_OK; }
    HRESULT STDMETHODCALLTYPE Lock(DWORD offset, DWORD bytes, LPVOID* first, LPDWORD firstBytes, LPVOID* second,
                                   LPDWORD secondBytes, DWORD flags) override
    {
        DWORD size = static_cast<DWORD>(m_data->size());
        if (flags & DSBLOCK_ENTIREBUFFER) {
            offset = 0;
            bytes = size;
        } else if (flags & DSBLOCK_FROMWRITECURSOR) {
            GetCurrentPosition(nullptr, &offset);
        }
        if (offset >= size || bytes > size)
            return DSERR_INVALIDPARAM;
        DWORD head = std::min(bytes, size - offset);
        *first = m_data->data() + offset;
        *firstBytes = head;
        if (second)
            *second = bytes > head ? m_data->data() : nullptr;
        if (secondBytes)
            *secondBytes = bytes - head;
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE Play(DWORD, DWORD, DWORD flags) override
    {
        std::lock_guard lock(m_lock);
        m_playing = true;
        m_looping = (flags & DSBPLAY_LOOPING) != 0;
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCurrentPosition(DWORD position) override
    {
        std::lock_guard lock(m_lock);
        m_position = position % static_cast<DWORD>(m_data->size());
        m_position -= m_position % std::max<DWORD>(m_format.nBlockAlign, 1);
        m_fraction = 0;
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFormat(LPCWAVEFORMATEX format) override
    {
        std::lock_guard lock(m_lock);
        m_format = *format;
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE SetVolume(LONG volume) override
    {
        m_volume = volume;
        if (m_flags & DSBCAPS_PRIMARYBUFFER)
            Mixer::SetMasterVolume(volume);
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPan(LONG pan) override
    {
        m_pan = pan;
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFrequency(DWORD frequency) override
    {
        std::lock_guard lock(m_lock);
        m_frequency = frequency ? frequency : m_format.nSamplesPerSec;
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE Stop() override
    {
        std::lock_guard lock(m_lock);
        m_playing = false;
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE Unlock(LPVOID, DWORD, LPVOID, DWORD) override { return DS_OK; }
    HRESULT STDMETHODCALLTYPE Restore() override { return DS_OK; }

    SoundBuffer* Duplicate() { return new SoundBuffer(*this); }

private:
    // The mixer's: the buffer's frames from its cursor at its frequency, resampled (linearly) to `rate` and added at
    // its volume and pan; the cursor moved on, wrapping when looping, the buffer stopping at its end otherwise.
    void Mix(float* out, int frames, int rate) override
    {
        std::lock_guard lock(m_lock);
        if (!m_playing)
            return;
        const int channels = std::clamp<int>(m_format.nChannels, 1, 2);
        const int bits = m_format.wBitsPerSample;
        const uint32_t align = std::max<uint32_t>(m_format.nBlockAlign, 1);
        const uint8_t* data = m_data->data();
        const uint32_t count = static_cast<uint32_t>(m_data->size()) / align;
        if (!count || m_format.wFormatTag != WAVE_FORMAT_PCM || (bits != 8 && bits != 16))
            return;
        float left, right;
        Mixer::Gains(m_volume, m_pan, left, right);
        auto sample = [&](uint32_t frame, int channel) -> float {
            const uint8_t* at = data + static_cast<size_t>(frame) * align;
            if (bits == 16) {
                int16_t value;
                std::memcpy(&value, at + channel * 2, 2);
                return value * (1.0f / 32768.0f);
            }
            return (at[channel] - 128) * (1.0f / 128.0f);
        };
        const double step = static_cast<double>(m_frequency) / rate;
        uint32_t frame = m_position / align;
        double fraction = m_fraction;
        for (int i = 0; i < frames; ++i) {
            uint32_t next = frame + 1 < count ? frame + 1 : m_looping ? 0 : frame;
            float f = static_cast<float>(fraction);
            float l = sample(frame, 0) + (sample(next, 0) - sample(frame, 0)) * f;
            float r = channels == 2 ? sample(frame, 1) + (sample(next, 1) - sample(frame, 1)) * f : l;
            out[i * 2] += l * left;
            out[i * 2 + 1] += r * right;
            fraction += step;
            auto whole = static_cast<uint32_t>(fraction);
            fraction -= whole;
            frame += whole;
            if (frame >= count) {
                if (!m_looping) {
                    m_playing = false;
                    m_position = 0;
                    m_fraction = 0;
                    return;
                }
                frame %= count;
            }
        }
        m_position = frame * align;
        m_fraction = fraction;
    }

    ULONG m_references = 1;
    DWORD m_flags;
    WAVEFORMATEX m_format;
    std::shared_ptr<std::vector<uint8_t>> m_data;
    std::mutex m_lock;
    bool m_playing = false, m_looping = false;
    DWORD m_position = 0;
    double m_fraction = 0;
    DWORD m_frequency;
    LONG m_volume = 0, m_pan = 0;
};

class DirectSound final : public IDirectSound8 {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** object) override
    {
        *object = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_references; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG left = --m_references;
        if (!left)
            delete this;
        return left;
    }
    HRESULT STDMETHODCALLTYPE CreateSoundBuffer(LPCDSBUFFERDESC desc, LPDIRECTSOUNDBUFFER* buffer, IUnknown*) override
    {
        *buffer = new SoundBuffer(*desc);
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCaps(LPDSCAPS caps) override
    {
        std::memset(reinterpret_cast<uint8_t*>(caps) + sizeof(DWORD), 0, sizeof(*caps) - sizeof(DWORD));
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER original, LPDIRECTSOUNDBUFFER* duplicate) override
    {
        *duplicate = original ? static_cast<SoundBuffer*>(original)->Duplicate() : nullptr;
        return original ? DS_OK : DSERR_INVALIDPARAM;
    }
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND, DWORD) override { return DS_OK; }
    HRESULT STDMETHODCALLTYPE Compact() override { return DS_OK; }
    HRESULT STDMETHODCALLTYPE GetSpeakerConfig(LPDWORD config) override
    {
        *config = 4; // DSSPEAKER_STEREO
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSpeakerConfig(DWORD) override { return DS_OK; }
    HRESULT STDMETHODCALLTYPE Initialize(const GUID*) override { return DS_OK; }
    HRESULT STDMETHODCALLTYPE VerifyCertification(LPDWORD certified) override
    {
        *certified = 0;
        return DS_OK;
    }

private:
    ULONG m_references = 1;
};

} // namespace

extern "C" HRESULT WINAPI DirectSoundCreate8(const GUID*, LPDIRECTSOUND8* sound, IUnknown*)
{
    *sound = new DirectSound;
    return DS_OK;
}

// One device, the default.
extern "C" HRESULT WINAPI DirectSoundEnumerateA(BOOL(CALLBACK* callback)(GUID*, const char*, const char*, void*),
                                                void* context)
{
    callback(nullptr, "Primary Sound Driver", "", context);
    return DS_OK;
}

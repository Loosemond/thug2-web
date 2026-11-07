#pragma once

// Bink 1 files' sound, as binkw32.dll decoded it: the file's header and frame index, and the DCT audio codec THUG2's
// music and streams use (data\streams: 48 / 44.1 kHz, stereo or mono). Written from the format's description and
// checked against FFmpeg's decoder (binkaudio.c) sample for sample. The RDFT kind isn't decoded (no file uses it).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace BinkAudio {

constexpr uint16_t kFlagDct = 0x1000;
constexpr uint16_t kFlagStereo = 0x2000;

// The header ("BIK" and a version letter): the frames, their rate, the first sound track's format and where each
// frame is (frames + 1 offsets, the keyframe bit cleared).
struct Header {
    uint32_t frames = 0;
    uint32_t width = 0, height = 0;
    uint32_t frameRate = 0, frameRateDiv = 1;
    uint32_t tracks = 0;
    uint32_t sampleRate = 0;
    uint16_t flags = 0;
    std::vector<uint32_t> offsets;

    // The header's size, from its first 0x2C bytes (0 if they aren't a Bink 1 file's).
    static size_t Size(const uint8_t* start, size_t available);
    bool Parse(const uint8_t* data, size_t size);
    int Channels() const { return flags & kFlagStereo ? 2 : 1; }
};

// The first track's packet within a frame's bytes (null if the frame has none).
const uint8_t* TrackPacket(const uint8_t* frame, size_t frameSize, size_t& packetSize);

class Decoder {
public:
    // False if the format isn't one this decodes.
    bool Init(uint32_t sampleRate, int channels, uint16_t flags);
    // A frame's packet (its reported size, then the bits): its blocks' samples, interleaved, appended to `out`.
    void Decode(const uint8_t* packet, size_t size, std::vector<float>& out);
    // Back to the start of a stream (after a seek): no overlap with what came before.
    void Reset() { m_first = true; }
    int Channels() const { return m_channels; }

private:
    bool DecodeBlock(class Bits& bits, std::vector<float>& out);
    void InverseDct(float* data);

    int m_channels = 0;
    int m_frameLength = 0, m_overlap = 0;
    int m_bands = 0;
    int m_bandStarts[26] = {};
    float m_quant[96] = {};
    float m_root = 0;
    bool m_first = true;
    std::vector<float> m_coefficients[2];
    std::vector<float> m_previous[2];
    // The inverse DCT's FFT: its twiddles, the pre-twiddles and the work buffer.
    std::vector<float> m_fftCos, m_fftSin, m_preCos, m_preSin;
    std::vector<float> m_re, m_im;
    std::vector<int> m_bitReverse;
};

} // namespace BinkAudio

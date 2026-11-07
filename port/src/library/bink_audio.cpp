// Bink 1's sound (bink_audio.h).

#include "library/bink_audio.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace BinkAudio {

namespace {

uint32_t U32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }

// The bands' upper edges (Hz): the critical bands, as WMA's.
constexpr uint16_t kCriticalFrequencies[25] = {
    100,  200,  300,  400,  510,  630,  770,  920,  1080, 1270,  1480,  1720,  2000,
    2320, 2700, 3150, 3700, 4400, 5300, 6400, 7700, 9500, 12000, 15500, 24500,
};
// A run's length (in 8s) by its 4-bit code.
constexpr uint8_t kRunLengths[16] = {2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32, 64};

} // namespace

// The bit stream: little-endian, each byte's low bits first.
class Bits {
public:
    Bits(const uint8_t* data, size_t size) : m_data(data), m_bytes(size), m_bits(size * 8) {}

    uint32_t Get(int count)
    {
        if (!count)
            return 0;
        size_t byte = m_position >> 3;
        uint64_t word = 0;
        if (byte + 8 <= m_bytes) {
            std::memcpy(&word, m_data + byte, 8);
        } else {
            for (size_t i = 0; byte + i < m_bytes && i < 8; ++i)
                word |= static_cast<uint64_t>(m_data[byte + i]) << (8 * i);
        }
        m_position += count;
        return static_cast<uint32_t>((word >> (m_position - count & 7)) & ((uint64_t{1} << count) - 1));
    }
    // A float: 5 bits of power, 23 of mantissa, the sign.
    float GetFloat()
    {
        int power = static_cast<int>(Get(5));
        float value = std::ldexp(static_cast<float>(Get(23)), power - 23);
        return Get(1) ? -value : value;
    }
    void Skip(int count) { m_position += count; }
    void Align32() { m_position = (m_position + 31) & ~size_t{31}; }
    long Left() const { return static_cast<long>(m_bits) - static_cast<long>(m_position); }

private:
    const uint8_t* m_data;
    size_t m_bytes, m_bits;
    size_t m_position = 0;
};

// --- The file -------------------------------------------------------------------------------------------------------

size_t Header::Size(const uint8_t* start, size_t available)
{
    if (available < 0x2C || std::memcmp(start, "BIK", 3) != 0)
        return 0;
    uint32_t frames = U32(start + 8), tracks = U32(start + 0x28);
    if (frames > 1000000 || tracks > 256)
        return 0;
    return 0x2C + tracks * 12 + (frames + 1) * 4;
}

bool Header::Parse(const uint8_t* data, size_t size)
{
    size_t needed = Size(data, size);
    if (!needed || needed > size)
        return false;
    frames = U32(data + 8);
    width = U32(data + 0x14);
    height = U32(data + 0x18);
    frameRate = U32(data + 0x1C);
    frameRateDiv = std::max<uint32_t>(U32(data + 0x20), 1);
    tracks = U32(data + 0x28);
    // Each track: its largest decoded size, then all the rates and flags, then all the track ids; then the index.
    const uint8_t* format = data + 0x2C + tracks * 4;
    sampleRate = tracks ? (format[0] | format[1] << 8) : 0;
    flags = tracks ? static_cast<uint16_t>(format[2] | format[3] << 8) : 0;
    const uint8_t* index = data + 0x2C + tracks * 12;
    offsets.resize(frames + 1);
    for (uint32_t i = 0; i <= frames; ++i)
        offsets[i] = U32(index + i * 4) & ~1u;
    return true;
}

const uint8_t* TrackPacket(const uint8_t* frame, size_t frameSize, size_t& packetSize)
{
    if (frameSize < 4)
        return nullptr;
    packetSize = U32(frame);
    if (packetSize < 4 || packetSize > frameSize - 4)
        return nullptr;
    return frame + 4;
}

// --- The decoder ----------------------------------------------------------------------------------------------------

bool Decoder::Init(uint32_t sampleRate, int channels, uint16_t flags)
{
    if (!(flags & kFlagDct) || sampleRate == 0 || channels < 1 || channels > 2)
        return false;
    m_channels = channels;
    int bits = sampleRate < 22050 ? 9 : sampleRate < 44100 ? 10 : 11;
    m_frameLength = 1 << bits;
    m_overlap = m_frameLength / 16;
    int halfRate = static_cast<int>((sampleRate + 1) / 2);
    m_root = static_cast<float>(m_frameLength / (std::sqrt(static_cast<double>(m_frameLength)) * 32768.0));
    for (int i = 0; i < 96; ++i)
        m_quant[i] = std::exp(i * 0.15289164787221953823f) * m_root; // (0.0664 / log10(e))
    for (m_bands = 1; m_bands < 25; ++m_bands)
        if (halfRate <= kCriticalFrequencies[m_bands - 1])
            break;
    m_bandStarts[0] = 2;
    for (int i = 1; i < m_bands; ++i)
        m_bandStarts[i] = (kCriticalFrequencies[i - 1] * m_frameLength / halfRate) & ~1;
    m_bandStarts[m_bands] = m_frameLength;
    for (int ch = 0; ch < 2; ++ch) {
        m_coefficients[ch].assign(m_frameLength, 0.0f);
        m_previous[ch].assign(m_overlap, 0.0f);
    }

    // The inverse DCT as an FFT of the same length: twiddles e^(i 2 pi j / N) and pre-twiddles e^(i pi k / 2N).
    const int n = m_frameLength;
    m_fftCos.resize(n / 2);
    m_fftSin.resize(n / 2);
    for (int j = 0; j < n / 2; ++j) {
        m_fftCos[j] = static_cast<float>(std::cos(2 * M_PI * j / n));
        m_fftSin[j] = static_cast<float>(std::sin(2 * M_PI * j / n));
    }
    m_preCos.resize(n);
    m_preSin.resize(n);
    for (int k = 0; k < n; ++k) {
        m_preCos[k] = static_cast<float>(std::cos(M_PI * k / (2.0 * n)));
        m_preSin[k] = static_cast<float>(std::sin(M_PI * k / (2.0 * n)));
    }
    m_bitReverse.resize(n);
    for (int i = 0; i < n; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            r |= ((i >> b) & 1) << (bits - 1 - b);
        m_bitReverse[i] = r;
    }
    m_re.resize(n);
    m_im.resize(n);
    m_first = true;
    return true;
}

// y[n] = 2/N (X[0] / 2 + sum over k >= 1 of X[k] cos(pi k (2n + 1) / 2N)) (a DCT-III, scaled as Bink's), in place:
// with V[0] = X[0] / 2, V[k] = (X[k] - i X[N - k]) e^(i pi k / 2N) / 2, v = the inverse FFT of V (unscaled), the even
// outputs are v's first half and the odd ones its second half reversed.
void Decoder::InverseDct(float* data)
{
    const int n = m_frameLength;
    const float scale = 1.0f / static_cast<float>(n); // (the 1/2 and the 2/N)
    float* re = m_re.data();
    float* im = m_im.data();
    for (int k = 0; k < n; ++k) {
        float a = data[k] * scale, b = k ? -data[n - k] * scale : 0.0f;
        float c = m_preCos[k], s = m_preSin[k];
        int at = m_bitReverse[k];
        re[at] = a * c - b * s;
        im[at] = a * s + b * c;
    }
    for (int length = 2; length <= n; length <<= 1) {
        int half = length / 2, step = n / length;
        for (int i = 0; i < n; i += length)
            for (int k = 0; k < half; ++k) {
                float wr = m_fftCos[k * step], wi = m_fftSin[k * step];
                float xr = re[i + k + half], xi = im[i + k + half];
                float tr = xr * wr - xi * wi, ti = xr * wi + xi * wr;
                re[i + k + half] = re[i + k] - tr;
                im[i + k + half] = im[i + k] - ti;
                re[i + k] += tr;
                im[i + k] += ti;
            }
    }
    for (int m = 0; m < n / 2; ++m) {
        data[2 * m] = re[m];
        data[2 * m + 1] = re[n - 1 - m];
    }
}

bool Decoder::DecodeBlock(Bits& bits, std::vector<float>& out)
{
    bits.Skip(2);
    for (int ch = 0; ch < m_channels; ++ch) {
        float* coefficients = m_coefficients[ch].data();
        if (bits.Left() < 58)
            return false;
        coefficients[0] = bits.GetFloat() * m_root;
        coefficients[1] = bits.GetFloat() * m_root;
        if (bits.Left() < m_bands * 8)
            return false;
        float quant[25];
        for (int i = 0; i < m_bands; ++i)
            quant[i] = m_quant[std::min<uint32_t>(bits.Get(8), 95)];

        int k = 0;
        float q = quant[0];
        int i = 2;
        while (i < m_frameLength) {
            int end = bits.Get(1) ? i + kRunLengths[bits.Get(4)] * 8 : i + 8;
            end = std::min(end, m_frameLength);
            int width = static_cast<int>(bits.Get(4));
            if (width == 0) {
                std::fill(coefficients + i, coefficients + end, 0.0f);
                i = end;
                while (m_bandStarts[k] < i)
                    q = quant[k++];
            } else {
                while (i < end) {
                    if (m_bandStarts[k] == i)
                        q = quant[k++];
                    uint32_t value = bits.Get(width);
                    coefficients[i] = value ? (bits.Get(1) ? -q : q) * static_cast<float>(value) : 0.0f;
                    ++i;
                }
            }
        }
        coefficients[0] *= 2.0f;
        InverseDct(coefficients);
    }

    // The block's start faded in over the last block's end; its own end kept for the next.
    const int count = m_overlap * m_channels;
    for (int ch = 0; ch < m_channels; ++ch) {
        float* samples = m_coefficients[ch].data();
        if (!m_first) {
            int j = ch;
            for (int i = 0; i < m_overlap; ++i, j += m_channels)
                samples[i] = (m_previous[ch][i] * static_cast<float>(count - j) + samples[i] * static_cast<float>(j)) /
                             static_cast<float>(count);
        }
        std::memcpy(m_previous[ch].data(), samples + m_frameLength - m_overlap, m_overlap * sizeof(float));
    }
    m_first = false;

    const int samples = m_frameLength - m_overlap;
    size_t at = out.size();
    out.resize(at + static_cast<size_t>(samples) * m_channels);
    for (int i = 0; i < samples; ++i)
        for (int ch = 0; ch < m_channels; ++ch)
            out[at + i * m_channels + ch] = m_coefficients[ch][i];
    return true;
}

void Decoder::Decode(const uint8_t* packet, size_t size, std::vector<float>& out)
{
    if (!m_channels || size < 4)
        return;
    Bits bits(packet, size);
    bits.Skip(32); // (the decoded size it reports)
    while (bits.Left() > 0) {
        if (!DecodeBlock(bits, out))
            break;
        bits.Align32();
    }
}

} // namespace BinkAudio

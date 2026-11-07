#pragma once

// The sound's mixer: every playing DirectSound buffer (dsound.cpp) and the other sources - the Bink stand-in's music -
// mixed on a thread of its own into a ring of stereo float frames in the wasm memory, which the page's AudioWorklet
// plays (web/index.html: thug2_audio_ring / thug2_audio_start). Without the page's sound (Node, ?mute, an iPhone
// before its first touch, a hidden tab) it mixes at the clock's pace and throws the sound away, so voices still finish
// and streams still drain as they would with output.

namespace Mixer {

class Source {
public:
    virtual ~Source() = default;
    // Adds `frames` stereo frames (interleaved, -1..1) at `rate` frames a second to `out`. On the mixer's thread.
    virtual void Mix(float* out, int frames, int rate) = 0;
};

// (A source is mixed from Add until Remove returns: Remove waits for a mix in progress.)
void Add(Source* source);
void Remove(Source* source);

// The primary buffer's volume (hundredths of a decibel, as DirectSound's): over everything.
void SetMasterVolume(long volume);

// DirectSound's volume (hundredths of a decibel, 0 down to -10000) and pan (-10000 left .. 10000 right) as the two
// channels' gains.
void Gains(long volume, long pan, float& left, float& right);

// A stream's sound ran out while it played (`frames` at `rate` a second): a gap - for the page's stats.
void NoteDry(int frames, int rate);

} // namespace Mixer

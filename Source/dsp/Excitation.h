#pragma once
#include <array>
#include <cstdint>

namespace fw::dsp {

// Longest excitation we ever need: the lowest playable pitch (MIDI 38,
// 73.4 Hz) at 96 kHz is about 1310 samples.
inline constexpr int kMaxExcite = 2048;

struct ExcitationRequest
{
    double f0          = 82.41;
    double fs          = 44100.0;
    double period      = 535.0;    // samples, fs / f0
    double pickPos01   = 0.15;
    double pickHard01  = 0.5;
    double vel         = 0.8;
    double fretNoise01 = 0.3;
    int    exc         = 0;        // 0 Pick, 1 Finger, 2 Thumb
    int    damp        = 0;        // 0 Open, 1 PalmMute, 2 Dead
    std::uint32_t seed = 1;
};

// One-shot levels that ride alongside the excitation buffer.
struct ExcitationResult
{
    float clickAmp     = 0.f;   // pick click into the bridge signal
    float thumpAmp     = 0.f;   // Dead-note body thump
    float fretNoiseAmp = 0.f;   // fret noise on release / legato
};

// Fixed-capacity scratch. Sized once, never grown.
class ExcitationBuffer
{
public:
    void setLength (int n) noexcept { len = n < 0 ? 0 : (n > kMaxExcite ? kMaxExcite : n); }
    int  length() const noexcept { return len; }

    float*       data()       noexcept { return buf.data(); }
    const float* data() const noexcept { return buf.data(); }

    float& operator[] (int i)       noexcept { return buf[(std::size_t) i]; }
    float  operator[] (int i) const noexcept { return buf[(std::size_t) i]; }

private:
    std::array<float, kMaxExcite> buf {};
    std::array<float, kMaxExcite> scratch {};
    int len = 0;
};

// Builds the noise burst that is injected into the waveguide at a pluck.
// Writes len samples into out and the one-shot levels into info.
void renderExcitation (const ExcitationRequest& r,
                       ExcitationBuffer& out,
                       ExcitationResult& info) noexcept;

} // namespace fw::dsp

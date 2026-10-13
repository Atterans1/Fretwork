#pragma once
#include "Contracts.h"
#include "dsp/Excitation.h"
#include "dsp/LoopDesign.h"
#include "dsp/StringModelParams.h"

#include <array>

namespace fw::dsp {

// Longest loop delay we will ever need: the lowest playable pitch (MIDI 38,
// 73.4 Hz) at 96 kHz is about 1310 samples, plus bend and comb headroom.
inline constexpr int kMaxStringDelay = 8192;

// Longest one-shot transient: the Dead-note thump is 25 ms.
inline constexpr int kMaxBurst = 8192;

// One waveguide string.
//
// Signal flow, once around the loop:
//   read (delay D_live) -> optional harmonic comb
//                        -> 1-pole loop lowpass
//                        -> 4 identical dispersion allpasses
//                        -> release gain Gr
//                        -> add sympathetic drive and write back
//
// The bridge signal is the delay read, taken before the write, so the loop
// latency is exactly D_live.
class GuitarString
{
public:
    void prepare (double sampleRate);
    void setModel (const StringModelParams& p);

    void pluck  (const StringEvent& e, int stringIdx);
    void legato (const StringEvent& e);
    void release (float releaseSec);

    // Returns the bridge signal for this sample.
    // sympatheticIn is already scaled by coupling; the string's own
    // recirculating signal is scaled by (1 - coupling) so the total stays passive.
    float tick (float sympatheticIn, float coupling = 0.f);

    void setBendSemis (float semis);
    void setVibrato   (float cents01);

    // Additive: the engine may drive slide length, defaults to 90 ms.
    void setGlideTime (float seconds) noexcept { glideSec_ = seconds > 0.f ? seconds : 0.09f; }

    void reset();

    // Current period including bend and vibrato; the pickup combs need this.
    float periodSamples() const noexcept;

    // Current fundamental including bend and vibrato.
    float fundamentalHz() const noexcept;

private:
    float readInterp (double pos) const noexcept;
    void  writeAt   (double pos, float v) noexcept;

    void  design (const StringEvent& e);
    void  inject (float scale);
    void  startBurst (std::array<float, kMaxBurst>& buf, int& len, int& pos,
                      int samples, float amp, bool highpass, double centreHz);
    float readBurst (std::array<float, kMaxBurst>& buf, int& len, int& pos) noexcept;

    double fs_ = 44100.0;
    StringModelParams model_ {};

    std::array<float, kMaxStringDelay> line_ {};
    double writePos_ = 0.0;

    LoopDesignResult design_ {};

    // Filter state.
    float lpPrev_ = 0.f;

    // Each allpass stage needs its own history: stage i reads stage (i-1) OUTPUT
    // from one sample ago, which a shared register cannot hold.
    std::array<float, 4> apX_ {};
    std::array<float, 4> apY_ {};

    float releaseGain_    = 1.f;
    float naturalGain_    = 1.f;   // loop gain for the string's own T60
    float lastVel_        = 1.f;
    float releaseTarget_  = 1.f;
    float releaseInc_     = 0.f;

    // Pitch control, in semitones.
    float bendTarget_    = 0.f;
    float vibTargetCents_= 0.f;
    float semiSmoothed_  = 0.f;
    float semiIncCoeff_  = 0.f;

    // Legato / slide glide, in semitones, ramped linearly.
    float glideFrom_   = 0.f;
    float glideTo_     = 0.f;
    float glidePos_    = 0.f;
    float glideStep_   = 0.f;
    float glideSec_    = 0.09f;

    double baseF0_ = 82.41;

    // Group delay of the loop filter chain, which bending must NOT scale.
    double chainDelay_ = 0.0;

    std::uint32_t seed_ = 0x1234567u;
    int stringIdx_ = 0;
    bool prepared_ = false;

    std::array<float, kMaxBurst> clickBuf_ {};
    std::array<float, kMaxBurst> thumpBuf_ {};
    std::array<float, kMaxBurst> fretBuf_  {};
    int clickLen_ = 0, clickPos_ = 0;
    int thumpLen_ = 0, thumpPos_ = 0;
    int fretLen_  = 0, fretPos_  = 0;

    ExcitationBuffer excite_ {};

    // The excitation is added at the loop input as it is consumed, never written
    // ahead into the line (the write pointer would overwrite it).
    int   injectPos_ = 0;
    int   injectLen_ = 0;
    float injectScale_ = 1.f;
};

} // namespace fw::dsp

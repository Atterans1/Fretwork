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
//
// A real string vibrates in two planes at once. The plane square to the top
// (vertical) drives the bridge hard, so it is loud and loses energy fast; the
// plane parallel to the top (horizontal) barely moves the bridge, so it is
// quiet and rings on. The two sit a cent or so apart because the saddle is
// slightly stiffer one way than the other. A second, lighter loop carries the
// horizontal plane: the note fades in two stages (a quick first drop, then a
// long soft tail) and the upper partials beat slowly as the planes drift in
// and out of phase. That is the 'alive' shimmer a single loop cannot make.
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

    // The horizontal plane is on by default. Off gives the single-plane string
    // (what Fretwork was before), for comparison and for CPU-bound hosts.
    void setSecondPlane (bool on) noexcept { secondPlane_ = on; }

    // Current period including bend and vibrato; the pickup combs need this.
    float periodSamples() const noexcept;

    // Current fundamental including bend and vibrato.
    float fundamentalHz() const noexcept;

private:
    float readInterp (double pos) const noexcept;
    void  writeAt   (double pos, float v) noexcept;
    float readInterpH (double pos) const noexcept;

    // Semitones of the slide/legato glide at progress u (0..1).
    float glideShape (float u) const noexcept;

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

    // Legato / slide glide, in semitones. Progress u runs 0..1 at glideDu_ per
    // sample; glideShape maps it to pitch (eased, and fret by fret for slides).
    float glideFrom_   = 0.f;
    float glideTo_     = 0.f;
    float glidePos_    = 0.f;
    float glideU_      = 1.f;
    float glideDu_     = 0.f;
    bool  glideFretted_ = false;
    float glideSec_    = 0.09f;

    // Tension modulation: a hard pluck stretches the string, so the note starts
    // a few cents sharp and settles as the swing dies down. In semitones.
    float attackSemi_  = 0.f;
    float attackCoef_  = 0.f;    // per-sample decay of attackSemi_

    // Horizontal polarisation (see the class comment).
    std::array<float, kMaxStringDelay> lineH_ {};
    float  lpPrevH_     = 0.f;
    std::array<float, 4> apXH_ {};
    std::array<float, 4> apYH_ {};
    double lpCH_        = 0.0;
    double lpGH_        = 0.0;
    double chainDelayH_ = 0.0;
    double detuneH_     = 1.0;    // period scale of the horizontal plane
    float  gainBoostH_  = 1.f;    // its own loop gain over the vertical one
    float  outH_        = 0.f;    // how much of it reaches the bridge
    float  planeTrim_   = 1.f;    // keeps the attack level the same with two planes
    float  injectH_     = 0.f;    // share of the pluck that goes into it
    bool   secondPlane_ = true;

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

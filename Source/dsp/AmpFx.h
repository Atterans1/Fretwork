#pragma once
#include "dsp/Biquad.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <memory>
#include <vector>

namespace fw::dsp {

struct AmpFxParams
{
    int   ampModel  = 0;      // 0 Off, 1 Clean, 2 Crunch, 3 Lead
    float gain01    = 0.4f;
    float bass01    = 0.5f;   // 0.5 = flat
    float mid01     = 0.5f;
    float treble01  = 0.5f;
    float levelDb   = 0.f;
    int   cab       = 2;      // 0 Off, 1 1x12, 2 2x12, 3 4x12 (only with the amp on)

    float chorusMix01  = 0.f; // 0 = chorus off
    float chorusRateHz = 0.8f;

    float delayMix01   = 0.f; // 0 = delay off
    float delayMs      = 380.f;
    float feedback01   = 0.3f;
};

/** Guitar amp, speaker cabinet, chorus and delay, between the instrument and
    the master output stage.

    Amp: the stereo instrument is summed to mono like a real amp input, high-
    passed and driven through a 4x oversampled preamp. Each gain stage thins
    the lows before it clips (the small coupling and cathode caps a real
    preamp uses), so bass notes stay tight instead of turning to mud, and
    smooths the top after it (the Miller capacitance of a triode), so
    distortion is warm rather than fizzy. Clean: one soft symmetric stage;
    Crunch: one asymmetric stage with even harmonics; Lead: two cascaded
    stages with a coupling high-pass between them. Then a DC blocker, the
    Bass/Mid/Treble stack, and the power amp's sag: a loud chord pulls the
    supply down and the amp compresses and blooms back. Cabinet: speaker
    response per size: the cone's low resonance, a low-mid dip on closed
    backs, the presence peak, the notch and peak of cone break-up, and a
    4th-order top roll-off. Chorus: two modulated short delays, LFOs 90 degrees apart. Delay:
    stereo, darkened feedback, smoothed time.

    With the amp Off and both effect mixes at 0 the audio is not touched, so
    every existing sound is bit-identical. Allocation-free after prepare(). */
class AmpFx
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void setParams (const AmpFxParams& p) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;
    void reset() noexcept;

    bool active() const noexcept;
    int  latencySamples() const noexcept;

private:
    void processAmp (float* left, float* right, int n) noexcept;
    void processChorus (float* left, float* right, int n) noexcept;
    void processDelay (float* left, float* right, int n) noexcept;
    void updateFilters() noexcept;

    double fs_ = 48000.0;
    int maxBlock_ = 512;
    AmpFxParams p_ {};
    int lastModel_ = 0, lastCab_ = -1;
    bool chorusWasOn_ = false, delayWasOn_ = false;

    // amp
    std::unique_ptr<juce::dsp::Oversampling<float>> os_;
    std::vector<float> mono_;
    Biquad inHp_, bass_, mid_, treble_;
    // preamp, at the oversampled rate
    double osFs_ = 192000.0;
    Biquad preThin1_, preThin2_, interHp_, postLp1_, postLp2_;
    // power amp sag
    float sagEnv_ = 0.f, sagAtt_ = 0.f, sagRel_ = 0.f;
    float dcX_ = 0.f, dcY_ = 0.f, dcR_ = 0.995f;
    float gainNow_ = 0.f, levelNow_ = 1.f;

    // cabinet
    std::array<Biquad, 8> cab_ {};

    // chorus
    std::vector<float> chL_, chR_;
    int chW_ = 0;
    double lfo_ = 0.0;

    // delay
    std::vector<float> dlL_, dlR_;
    int dlW_ = 0;
    float delaySamplesNow_ = 0.f;
    float fbLpL_ = 0.f, fbLpR_ = 0.f, fbCoef_ = 0.5f;
};

} // namespace fw::dsp

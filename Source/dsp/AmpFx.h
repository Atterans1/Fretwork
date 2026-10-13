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
    passed, driven through a 4x oversampled waveshaper (Clean: soft symmetric;
    Crunch: asymmetric, even harmonics; Lead: two cascaded stages), DC-blocked,
    shaped by a Bass/Mid/Treble stack and trimmed by Level. Cabinet: speaker
    response as high-pass, low resonance, presence peak and a steep top roll-off
    per size. Chorus: two modulated short delays, LFOs 90 degrees apart. Delay:
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
    float dcX_ = 0.f, dcY_ = 0.f, dcR_ = 0.995f;
    float gainNow_ = 0.f, levelNow_ = 1.f;

    // cabinet
    std::array<Biquad, 5> cab_ {};

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

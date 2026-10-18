#pragma once
#include "Contracts.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

namespace fw::dsp {

struct OutputParams
{
    float width01 = 0.6f;
    float toneHz  = 9000.f;
    float drive01 = 0.f;
    float room01  = 0.12f;
    int   roomType = 0;     // 0 Booth, 1 Room, 2 Hall, 3 Plate
    float outDb   = -6.f;
    int   power   = 1;
};

// Metronome blip is 5 ms; at 192 kHz that is 960 samples.
inline constexpr int kMaxClick = 2048;

// Master chain: tone, drive, room, output gain, soft clip, metronome.
class OutputStage
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void setParams (const OutputParams& p);

    // Schedules a blip to begin at sampleOffset of the next rendered block.
    void clickAt (int sampleOffset, bool accent);

    void process (float* left, float* right, int numSamples) noexcept;
    void reset() noexcept;

private:
    void renderClick (bool accent) noexcept;

    double fs_ = 44100.0;
    OutputParams p_ {};

    // juce::dsp::Reverb needs a ProcessContext wrapper that this codebase has
    // no use for; the audio_basics reverb takes the stereo pair directly.
    juce::Reverb reverb_;
    juce::Reverb::Parameters reverbParams_;

    // One-pole tone state.
    float toneState_ = 0.f;
    float toneCoef_  = 1.f;

    std::array<float, kMaxClick> click_ {};
    int  clickLen_    = 0;
    int  clickPos_    = 0;
    int  clickOffset_ = -1;
    bool clickAccent_ = false;
};

} // namespace fw::dsp

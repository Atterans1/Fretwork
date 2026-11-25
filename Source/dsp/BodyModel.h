#pragma once
#include "dsp/Biquad.h"
#include "dsp/ModelTables.h"

#include <array>

namespace fw::dsp {

// The acoustic path: a parallel bank of resonant body modes driven by the
// bridge signal through a piezo-like highpass. No samples, no impulse
// responses: every mode is an analytic biquad.
class BodyModel
{
public:
    void prepare (double sampleRate);
    void setBody (int bodyIndex, float sizeScale, float resScale, float topDamp01);

    // in: summed bridge signal. piezoOut: highpassed bridge. micOut: body modes.
    void process (const float* in, float* piezoOut, float* micOut, int numSamples) noexcept;

    // One sample of the same path, so the engine can run the body inside its
    // per-sample loop and feed the top motion back into the strings.
    void processSample (float in, float& piezoOut, float& micOut) noexcept;

    void reset() noexcept;

private:
    void rebuild (int bodyIndex, float sizeScale, float resScale);

    double fs_ = 44100.0;

    std::array<Biquad, kMaxBodyModes> modes_ {};
    int    count_ = 0;

    Biquad piezoHp_ {};
    Biquad topDamp_ {};

    // Cached parameters, so the bank is rebuilt only when they actually move.
    int   cachedBody_  = -1;
    float cachedSize_  = -1.f;
    float cachedRes_   = -1.f;
    float cachedDamp_  = -1.f;
};

} // namespace fw::dsp

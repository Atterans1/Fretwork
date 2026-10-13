#pragma once
#include "Contracts.h"
#include "dsp/BodyModel.h"
#include "dsp/GuitarString.h"
#include "dsp/AmpFx.h"
#include "dsp/ArticulationNoise.h"
#include "dsp/OutputStage.h"
#include "dsp/PickupModel.h"

#include <array>
#include <vector>

namespace fw::dsp {

struct EngineParams
{
    std::array<StringModelParams, kNumStrings> strings {};

    int   body        = 0;        // BodyKind
    float sympathetic = 30.f;     // %
    float glideTime   = 90.f;     // ms
    float bendAmount  = 2.f;      // semitones
    float bendTime    = 120.f;    // ms
    float vibRate     = 5.5f;     // Hz
    float vibDepth    = 25.f;     // cents

    int   pickupType  = 0;
    int   pickupSel   = 0;
    float pickupHeight = 50.f;    // %
    float pickupTone  = 50.f;     // %
    float pickupOut   = 0.f;      // dB

    float bodySize    = 1.f;
    float bodyRes     = 100.f;    // %
    float topDamp     = 30.f;     // %
    float micBlend    = 70.f;     // %

    float tone        = 2811.f;   // Hz
    float drive       = 0.f;      // %
    float room        = 12.f;     // %
    float width       = 60.f;     // %
    float output      = -6.f;     // dB
    int   power       = 1;

    // Amp, cabinet and effects (AmpFx). Amp Off and both mixes 0 = bypassed.
    int   ampModel      = 0;      // 0 Off, 1 Clean, 2 Crunch, 3 Lead
    float ampGain       = 40.f;   // %
    float ampBass       = 50.f;   // %, 50 flat
    float ampMid        = 50.f;
    float ampTreble     = 50.f;
    float ampLevel      = 0.f;    // dB
    int   cab           = 2;      // 0 Off, 1 1x12, 2 2x12, 3 4x12
    float chorusMix     = 0.f;    // %
    float chorusRate    = 0.8f;   // Hz
    float delayMix      = 0.f;    // %
    float delayTime     = 380.f;  // ms
    float delayFeedback = 30.f;   // %
};

// Exponential smoother stepped once per sample.
struct Smooth
{
    float value  = 0.f;
    float target = 0.f;
    float coef   = 0.f;

    void setCoefficient (double fs, double tauSeconds) noexcept
    {
        coef = (float) (1.0 - std::exp (-1.0 / (fs * (tauSeconds > 0.0 ? tauSeconds : 0.02))));
    }

    void jump (float t) noexcept { target = t; value = t; }

    void step() noexcept { value += (target - value) * coef; }
};

// The physical instrument: six waveguide strings feeding a body model, a
// pickup model and a master output stage.
class GuitarEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void setParams (const EngineParams& p);

    void handle (const StringEvent& e);

    void setPitchBend (float semis);
    void setModWheel  (float value01);

    void clickAt (int sampleOffset, bool accent);

    void render (float* left, float* right, int numSamples);
    void reset();

    // Test taps: the most recently rendered block's per-string bridge signal.
    const float* debugStringOutput (int stringIndex) const noexcept;
    int debugStringOutputSize() const noexcept;

private:
    void refreshModel();
    void updatePan();

    std::array<GuitarString, kNumStrings> strings_ {};

    PickupModel pickup_ {};
    BodyModel   body_ {};
    ArticulationNoise noise_ {};
    AmpFx       amp_ {};
    OutputStage out_ {};

    EngineParams p_ {};

    double fs_ = 44100.0;
    bool   prepared_ = false;

    // Continuous parameters, stepped once per sample.
    Smooth sSympathetic_ {};
    Smooth sWidth_      {};
    Smooth sTone_       {};
    Smooth sDrive_      {};
    Smooth sRoom_       {};
    Smooth sMicBlend_   {};
    Smooth sBodyRes_    {};
    Smooth sTopDamp_    {};
    Smooth sOutput_     {};

    OutputParams outParams_ {};

    std::array<float, kNumStrings> panL_ {};
    std::array<float, kNumStrings> panR_ {};
    std::array<float, kNumStrings> panBase_ {};

    // Per-string bend ramp, for Bend articulation notes.
    std::array<float, kNumStrings> bendNow_    {};
    std::array<float, kNumStrings> bendTarget_ {};
    std::array<float, kNumStrings> bendStep_   {};

    float globalBend_ = 0.f;
    float modWheel_   = 0.f;
    double lfoPhase_  = 0.0;
    // Previous sample's bridge output per string, for sympathetic coupling.
    std::array<float, kNumStrings> prevBridge_ {};
    float prevBridgeSum_ = 0.f;

    float electricLevel_ = 1.f;
    float acousticLevel_ = 1.f;

    // Scratch, sized once in prepare().
    std::vector<std::vector<float>> debug_;
    std::vector<float> bridgeSum_ {};
    std::vector<float> piezo_     {};
    std::vector<float> mic_       {};
    std::vector<float> el_        {};
    std::vector<float> er_        {};

    int debugCount_ = 0;
    int pendingClickOffset_ = 0;
    bool pendingClickAccent_ = false;
    bool hasPendingClick_ = false;
};

} // namespace fw::dsp

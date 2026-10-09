#pragma once
namespace fw::dsp {

// Per-string physical/modal parameters. Orchestrator-owned contract so that the
// string model (Slice A) and the body/pickup engine (Slice B) cannot disagree.
struct StringModelParams
{
    int   material      = 0;      // 0 Steel Light, 1 Steel Medium, 2 Nylon, 3 Flatwound
    float age           = 20.f;   // %
    float stiffness     = 100.f;  // %
    float pickPos       = 15.f;   // % of scale length from the bridge
    float pickHard      = 50.f;   // %
    float sustain       = 100.f;  // %
    float fretNoise     = 30.f;   // %
    float bodyLossScale = 1.f;    // per-body loop-loss multiplier
    float scaleLengthM  = 0.648f; // low E, metres
};

} // namespace fw::dsp

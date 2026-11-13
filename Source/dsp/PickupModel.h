#pragma once
#include "Contracts.h"
#include "dsp/Biquad.h"
#include "dsp/ModelTables.h"

#include <array>

namespace fw::dsp {

// Longest comb tap we ever need. The neck pickup sits at 0.19 of the scale
// length, which at MIDI 38 and 96 kHz is about 250 samples.
inline constexpr int kTapHistory = 1024;

// Bridge and neck humbuckers, two coils each.
inline constexpr int kMaxTaps = 4;

struct PickupParams
{
    int   type     = 0;      // PickupType: Single-Coil, Humbucker, P90
    int   sel      = 0;      // PickupSel: Bridge, Middle, Neck, Bridge+Neck
    float height01 = 0.5f;
    float tone01   = 0.5f;   // guitar tone pot, 0 = rolled off, 1 = full
    float outDb    = 0.f;
};

// Turns each string's bridge signal into a pickup voltage.
//
// Two physical stages, per string:
//  1. Where the pickup sits. A magnetic pickup senses the string's velocity at
//     its position x along the scale. With travelling waves that is the bridge
//     signal minus itself delayed by 2x/L of a period, a comb whose notches
//     fall on the harmonics with a node at the pickup. Humbucker coils are
//     wound and magnetised in opposition, so their string signals add in phase
//     and the coil spacing adds a second comb.
//  2. The coil and its load. A passive pickup is a voltage source behind its
//     DC resistance R and inductance L, loaded by its own winding capacitance
//     plus the cable (C) and by the volume/tone pots and amp input (Rl). That
//     RLC network is a resonant second-order low-pass:
//         H(s) = 1 / (LC s^2 + (RC + L/Rl) s + 1 + R/Rl)
//     Its peak (2-5 kHz) and Q are what make single coils bright and P90s and
//     humbuckers darker. The tone pot lowers Rl, which damps and lowers the
//     peak exactly as on a real guitar.
class PickupModel
{
public:
    void prepare (double sampleRate);
    void setParams (const PickupParams& p);

    float processString (int stringIndex, float bridge, float periodSamples) noexcept;
    void  reset() noexcept;

    // Analog peak frequency and Q of the loaded coil, for tests and the UI.
    double resonanceHz() const noexcept { return resHz_; }
    double resonanceQ()  const noexcept { return resQ_; }

private:
    struct StringState
    {
        std::array<float, kTapHistory> hist {};
        int    write = 0;
        Biquad rlc;
    };

    void rebuild() noexcept;

    double fs_ = 44100.0;

    std::array<StringState, kNumStrings> strings_ {};

    std::array<float, kMaxTaps> tapFrac_ {};
    std::array<float, kMaxTaps> tapGain_ {};
    int numTaps_ = 0;

    double resHz_ = 0.0, resQ_ = 0.0;
    float  gain_ = 1.f;

    PickupParams params_ {};
    bool dirty_ = true;
};

} // namespace fw::dsp

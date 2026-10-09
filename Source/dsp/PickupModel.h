#pragma once
#include "Contracts.h"
#include "dsp/Biquad.h"
#include "dsp/ModelTables.h"

#include <array>

namespace fw::dsp {

// Longest comb tap we ever need. The neck humbucker sits at 0.218 of the scale
// length, which at MIDI 38 and 96 kHz is about 285 samples.
inline constexpr int kTapHistory = 512;

// Maximum simultaneous comb taps: bridge and neck humbuckers, two coils each.
inline constexpr int kMaxTaps = 4;

struct PickupParams
{
    int   type     = 0;      // PickupType
    int   sel      = 0;      // PickupSel
    float height01 = 0.5f;
    float tone01   = 0.5f;
    float outDb    = 0.f;
};

// Turns each string's bridge signal into a pickup voltage.
//
// A pickup is a comb: v[n] = b[n] - b[n - round(x * P)], where x is the
// pickup's fraction of the scale length from the bridge and P the string
// period. Position-symmetric pickups cancel in between strings because of it,
// and it is why this needs no samples.
class PickupModel
{
public:
    void prepare (double sampleRate);
    void setParams (const PickupParams& p);

    float processString (int stringIndex, float bridge, float periodSamples) noexcept;
    void  reset() noexcept;

private:
    struct StringState
    {
        std::array<float, kTapHistory> hist {};
        int    write = 0;
        Biquad tone;
    };

    void rebuildTaps() noexcept;

    double fs_ = 44100.0;

    std::array<StringState, kNumStrings> strings_ {};

    // Scale-length fraction per tap, and its weight.
    std::array<float, kMaxTaps> tapFrac_ {};
    std::array<float, kMaxTaps> tapGain_ {};
    int numTaps_ = 0;

    float gain_ = 1.f;

    PickupParams params_ {};
    bool dirty_ = true;
};

} // namespace fw::dsp

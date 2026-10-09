
#pragma once
#include "Contracts.h"
#include "engine/StrumEngine.h"
#include "engine/Voicing.h"

#include <array>
#include <cstdint>

namespace fw {

// Arpeggiator. Builds a pool from the voiced strings selected by the per-string
// mask, then emits one pluck per step in the configured direction, each closed
// by a gate release.
class ArpEngine
{
public:
    // Step length in quarters for each arpRate choice: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32.
    static constexpr double kStepLenQuarters[6] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };

    enum Order { Up = 0, Down, UpDown, Random, kNumOrders };

    static constexpr int kMaxPendingGates = 16;

    struct PoolEntry
    {
        int   stringIndex = 0;
        float pitch       = 40.f;
        int   fret        = 0;
    };

    struct Params
    {
        int   rate       = 3;     // arpRate choice index
        int   order      = Up;
        float gate01     = 0.8f;  // arpGate normalised to 0..1
        float swing01    = 0.f;   // arpSwing normalised to 0..1
        float strokeVol  = 1.f;
        float humanize01 = 0.f;
    };

    // A gate release lands a fraction of a step after its pluck and may fall in
    // a later block, so it is parked in ppq units until the block that contains
    // it arrives.
    struct PendingGate
    {
        double       endPpq      = 0.0;
        int          stringIndex = 0;
        float        pitch       = 40.f;
        int          fret        = 0;
        int          harmonicN   = 0;
        Articulation art {};
    };

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    }

    // Reseeds the Random order and resets the cursor. Called on mode entry.
    void seed (std::uint32_t s) noexcept
    {
        rng_.seed (s);
        direction_ = 1;
        cursor_ = 0;
        lastStep_ = -1;
        lastPpq_ = 0.0;
        gateCount_ = 0;
    }

    // Rebuilds the pool: voiced strings whose mask bit is on, sorted by pitch
    // ascending, truncated to the lowest arpStrings entries.
    // mask is indexed by string index, 0 = low E.
    void rebuild (const Voicing& v, const bool* mask, int arpStrings);

    int  poolSize() const noexcept { return poolCount_; }
    const PoolEntry& pool (int i) const noexcept { return pool_[(std::size_t) i]; }

    // Emits every step boundary crossed between ppqStart and ppqEnd, both in
    // quarter notes. stringVel is the per-string held velocity indexed by string
    // index, 0 = low E. Returns the number of StrumHits written.
    int advance (double ppqStart,
                 double ppqEnd,
                 double sampleRate,
                 double bpm,
                 const Params& p,
                 const float* open,
                 const float* stringVel,
                 Articulation art,
                 Rng& jitterRng,
                 StrumHit* out,
                 int cap);

private:
    double sampleRate_ = 44100.0;
    Rng    rng_ {};
    std::array<PoolEntry, kNumStrings> pool_ {};
    int    poolCount_ = 0;
    int    cursor_    = 0;
    int    direction_ = 1;
    std::array<PendingGate, kMaxPendingGates> gates_ {};
    int    gateCount_ = 0;
    long long lastStep_ = -1;
    double lastPpq_   = 0.0;
};

} // namespace fw

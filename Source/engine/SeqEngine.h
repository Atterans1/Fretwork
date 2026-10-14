
#pragma once
#include "Contracts.h"
#include "engine/SeqPattern.h"
#include "engine/StrumEngine.h"
#include "engine/Voicing.h"

#include <array>

namespace fw {

// Rows 0..5 are stroke rows; rows 6..11 are guitar strings 1..6, where row 6 is
// the high E (string index 5) and row 11 the low E (string index 0).
class SeqEngine
{
public:
    static constexpr int kNumRows        = 12;
    static constexpr int kNumStrokeRows  = 6;
    static constexpr int kFirstStringRow = 6;
    static constexpr int kMaxPendingReleases = 64;

    struct PendingRelease
    {
        long long step      = 0;
        int       stringIndex = 0;
    };

    void reset() noexcept;

    // Emits everything that happens between ppqStart and ppqEnd, both in
    // quarter notes. A note fires when step % stepsPerBar matches its start;
    // the strings it struck are released start + len steps later.
    //
    // Row membership in the voicing is what "the chord has a note on it" means:
    // assignVoicing() derives the voicing straight from the chord, so a string
    // present in the voicing carries a chord note.
    //
    // Returns the number of StrumHits written.
    int advance (double ppqStart,
                 double ppqEnd,
                 double sampleRate,
                 double bpm,
                 double stepsPerQuarter,
                 int stepsPerBar,
                 const SeqPattern* pattern,
                 const Voicing& voicing,
                 const StrumEngine::StrokeParams& strokeParams,
                 const StrumEngine& strum,
                 const float* open,
                 float releaseSec,
                 Articulation art,
                 Rng& rng,
                 StrumHit* out,
                 int cap,
                 float swing01 = 0.f);

    // Row to string index for rows 6..11, -1 for stroke rows and out of range.
    static int rowToString (int row) noexcept
    {
        return (row >= kFirstStringRow && row < kNumRows) ? (kNumRows - 1 - row) : -1;
    }

    // ks::Stroke index for rows 0..5, -1 otherwise.
    static int rowToStroke (int row) noexcept
    {
        return (row >= 0 && row < kNumStrokeRows) ? row : -1;
    }

    int currentStep() const noexcept { return currentStep_; }

private:
    void pushRelease (long long step, int stringIndex) noexcept;

    std::array<PendingRelease, kMaxPendingReleases> pending_ {};
    int     pendingCount_  = 0;
    long long lastStep_     = -1;
    double   lastPpq_       = 0.0;
    int      currentStep_   = -1;
};

} // namespace fw

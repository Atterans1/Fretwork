
#pragma once
#include "Contracts.h"
#include <array>

namespace fw {

// Result of assigning a sorted chord to guitar strings.
struct Voicing
{
    int   count = 0;
    int   stringIndex[kNumStrings] {};  // string the note is fretted on
    int   fret[kNumStrings]     {};     // integral fret
    float pitch[kNumStrings]    {};     // absolute MIDI pitch of the sounding note
    float fretFloat[kNumStrings] {};   // fret including any fine-tune fraction

    void clear() noexcept { count = 0; }
};

// Assigns an ascending pitch list to strictly increasing string indices.
//
// Preferred path: exhaustive enumeration of the at most C(6,k) strictly
// increasing string index tuples, scoring each legal one and keeping the
// minimum. Ties resolve to the lower string indices because the enumeration
// runs in lexicographic order and only a strictly lower cost replaces the
// incumbent.
//
// Fallback: a greedy ascending pass that gives each note the lowest-fret valid
// unused string. Notes with no valid string are dropped.
//
// Assignment is a pure function of its arguments: no state, no allocation,
// no RNG.
namespace voicing {

// Legality test for one (pitch, string) pair. Writes the fret on success.
bool fits (float pitch, float openPitch, int maxFret, float& fretOut) noexcept;

// Number of strictly increasing k-tuples from kNumStrings string indices.
constexpr int combinationCount (int k) noexcept
{
    return (k < 0 || k > kNumStrings) ? 0
         : (k == 0) ? 1
         : (k == 1) ? kNumStrings
         : (k == 2) ? kNumStrings * (kNumStrings - 1) / 2
         : (k == 3) ? kNumStrings * (kNumStrings - 1) * (kNumStrings - 2) / 6
         : (k == 4) ? kNumStrings * (kNumStrings - 1) * (kNumStrings - 2) * (kNumStrings - 3) / 24
         : (k == 5) ? kNumStrings * (kNumStrings - 1) * (kNumStrings - 2) * (kNumStrings - 3) * (kNumStrings - 4) / 120
                    : 1; // C(6,6)
}

} // namespace voicing

void assignVoicing (const float* pitches,
                    int          n,
                    const float* open,
                    int          maxFret,
                    Voicing&     out) noexcept;

} // namespace fw

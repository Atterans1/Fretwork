#pragma once
#include "Contracts.h"
#include "engine/Voicing.h"

namespace fw {

// A recognised chord: root and bass are pitch classes (0 = C), quality indexes
// chord::qualityCount() templates. root < 0 means "not a chord".
struct ChordName
{
    int root    = -1;
    int quality = -1;
    int bass    = -1;   // lowest held pitch class; differs from root for slash chords

    bool valid() const noexcept { return root >= 0 && quality >= 0; }

    // Packed for a lock-free UI hand-off: -1 when invalid.
    int  pack() const noexcept { return valid() ? (root | (quality << 4) | (bass << 12)) : -1; }
    static ChordName unpack (int v) noexcept
    {
        ChordName c;
        if (v < 0) return c;
        c.root = v & 15; c.quality = (v >> 4) & 255; c.bass = (v >> 12) & 15;
        return c;
    }
};

namespace chord {

int qualityCount() noexcept;

/** Names the chord formed by the held pitches (any octave, any order).
    A template matches when every held pitch class belongs to it and its
    required tones are all held. Among matches the one whose root is the
    lowest held note wins, then the smaller template. No match: invalid. */
ChordName detect (const int* pitches, int n) noexcept;

/** "C", "Am7", "G/B", "F#m7b5" ... written to out (>= 12 chars), never allocates. */
void label (const ChordName& c, char* out, int outSize) noexcept;

/** Builds a playable guitar shape for the chord: chord tones only, the bass
    pitch class lowest, every required tone present, sounding strings
    adjacent, frets within a 4-fret hand position (open strings allowed), at
    most 4 fretting fingers unless the lowest fret is barred. Scored on muted
    strings, neck position, stretch, fingers and - when previous is given -
    how far each string moves from it. Pure, deterministic, allocation-free.
    Returns false (out cleared) when no shape exists within maxFret. */
bool guitarVoicing (const ChordName& c, const float* open, int maxFret,
                    const Voicing* previous, Voicing& out) noexcept;

} // namespace chord
} // namespace fw

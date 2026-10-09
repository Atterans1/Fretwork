
#pragma once
#include "Contracts.h"

#include <array>
#include <cstddef>

namespace fw {

// A single held note, fingered or latched by the pedal or the hold switch.
struct ChordNote
{
    int   pitch    = -1;   // absolute MIDI pitch, transpose and capo already applied
    float velocity = 0.f;
    float fret     = 0.f;  // relative to the open pitch of the string it landed on
    int   string   = -1;   // string it was assigned to in Poly mode, -1 when unassigned
    bool  latched  = false;
};

// Held/latched note store for one chord, sorted ascending by pitch with a
// capacity of 6. When more notes arrive than fit, the lowest note plus the 5
// highest are kept.
class ChordState
{
public:
    static constexpr int kCapacity = 6;

    void clear() noexcept;

    // Adds a note. Returns false when the pitch is below basePitch, the pitch of
    // open string 0, which is the low edge of the transposed playable range.
    bool add (int pitch, float velocity, int basePitch) noexcept;

    bool remove (int pitch) noexcept;
    bool contains (int pitch) const noexcept;

    int  size() const noexcept { return count; }
    const ChordNote& at (int i) const noexcept { return notes[(std::size_t) i]; }

    // String assignment written back by Poly mode.
    void setString (int pitch, int stringIndex, float fret) noexcept;

    // The string a note occupies, -1 when it has none.
    int  stringFor (int pitch) const noexcept;

    // True when any held note occupies this string.
    bool stringIsHeld (int stringIndex) const noexcept;

    // True when this pitch is flagged latched.
    bool isLatched (int pitch) const noexcept;

    // Nearest note below pitch that shares its string, written to pitchOut and
    // returned; -1 when there is none. Drives the Legato pull-off rule.
    int  nextLowerOnSameString (int pitch, int& pitchOut) const noexcept;

    // Flags every note latched (sustain capture) or clears the flags. Notes added
    // while latched are born flagged.
    void setLatched (bool shouldLatch) noexcept;

    // Hold switch. Turning it on flags every held note latched; turning it off
    // drops the latched ones.
    void setHold (bool on) noexcept;
    bool latched() const noexcept { return latch; }

    // Nearest note below pitch that shares its string, -1 when there is none.
    bool hasLowerNoteOnSameString (int pitch) const noexcept;

    // Mean nonzero fret of the held notes, 3 when there is none.
    float handPosition() const noexcept;

    // Removes every note flagged latched. Returns true when anything was removed.
    bool releaseLatched() noexcept;

private:
    std::array<ChordNote, kCapacity> notes {};
    int count = 0;
    bool latch = false;
    bool holdOn = false;
};

} // namespace fw

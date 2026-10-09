
#include "engine/ChordState.h"

#include <cstddef>

namespace fw {

namespace {
    // Shared selection rule for the capacity limit: keep the lowest note plus
    // the 5 highest. Applied repeatedly to an ascending list, dropping index 1
    // each time, it yields exactly that set for any oversized input.
    template <std::size_t N>
    inline void dropIndexOne (std::array<ChordNote, N>& n, int& count) noexcept
    {
        for (int i = 1; i + 1 < count; ++i)
            n[(std::size_t) i] = n[(std::size_t) i + 1];
        --count;
    }
}

void ChordState::clear() noexcept
{
    for (auto& n : notes) n = ChordNote {};
    count = 0;
}

bool ChordState::add (int pitch, float velocity, int basePitch) noexcept
{
    if (pitch < basePitch) return false;   // below the transposed playable range

    // Assemble into an oversized local so the trim rule sees the whole set.
    std::array<ChordNote, kCapacity + 1> work {};
    int n = 0;
    for (int i = 0; i < count; ++i)
        work[(std::size_t) n++] = notes[(std::size_t) i];

    ChordNote fresh;
    fresh.pitch    = pitch;
    fresh.velocity = velocity;
    fresh.latched  = latch;

    // Insertion sort ascending by pitch; equal pitches keep press order.
    int at = 0;
    while (at < n && work[(std::size_t) at].pitch <= fresh.pitch) ++at;
    for (int i = n; i > at; --i)
        work[(std::size_t) i] = work[(std::size_t) i - 1];
    work[(std::size_t) at] = fresh;
    ++n;

    while (n > kCapacity) dropIndexOne (work, n);

    for (int i = 0; i < n; ++i)
        notes[(std::size_t) i] = work[(std::size_t) i];
    count = n;
    return true;
}

bool ChordState::remove (int pitch) noexcept
{
    for (int i = 0; i < count; ++i)
    {
        if (notes[(std::size_t) i].pitch != pitch) continue;
        for (int k = i; k + 1 < count; ++k)
            notes[(std::size_t) k] = notes[(std::size_t) k + 1];
        notes[(std::size_t) (--count)] = ChordNote {};
        return true;
    }
    return false;
}

bool ChordState::contains (int pitch) const noexcept
{
    for (int i = 0; i < count; ++i)
        if (notes[(std::size_t) i].pitch == pitch) return true;
    return false;
}

void ChordState::setString (int pitch, int stringIndex, float fret) noexcept
{
    for (int i = 0; i < count; ++i)
    {
        if (notes[(std::size_t) i].pitch != pitch) continue;
        notes[(std::size_t) i].string = stringIndex;
        notes[(std::size_t) i].fret   = fret;
        return;
    }
}

int ChordState::stringFor (int pitch) const noexcept
{
    for (int i = 0; i < count; ++i)
        if (notes[(std::size_t) i].pitch == pitch) return notes[(std::size_t) i].string;
    return -1;
}

bool ChordState::stringIsHeld (int stringIndex) const noexcept
{
    for (int i = 0; i < count; ++i)
        if (notes[(std::size_t) i].string == stringIndex) return true;
    return false;
}

bool ChordState::isLatched (int pitch) const noexcept
{
    for (int i = 0; i < count; ++i)
        if (notes[(std::size_t) i].pitch == pitch) return notes[(std::size_t) i].latched;
    return false;
}

bool ChordState::hasLowerNoteOnSameString (int pitch) const noexcept
{
    int lower = -1;
    return nextLowerOnSameString (pitch, lower) >= 0;
}

int ChordState::nextLowerOnSameString (int pitch, int& pitchOut) const noexcept
{
    pitchOut = -1;

    const int myString = stringFor (pitch);
    if (myString < 0) return -1;

    // The list is ascending, so the last match below is the nearest lower note.
    for (int i = 0; i < count; ++i)
    {
        const ChordNote& o = notes[(std::size_t) i];
        if (o.string == myString && o.pitch < pitch)
            pitchOut = o.pitch;
    }
    return pitchOut;
}

void ChordState::setLatched (bool shouldLatch) noexcept
{
    if (latch == shouldLatch) return;
    latch = shouldLatch;
    for (int i = 0; i < count; ++i)
        notes[(std::size_t) i].latched = shouldLatch;
}

void ChordState::setHold (bool on) noexcept
{
    if (holdOn == on) return;
    holdOn = on;

    if (on)
    {
        for (int i = 0; i < count; ++i)
            notes[(std::size_t) i].latched = true;
    }
    else
    {
        releaseLatched();
    }
}

float ChordState::handPosition() const noexcept
{
    float sum = 0.f;
    int   n   = 0;
    for (int i = 0; i < count; ++i)
    {
        const float f = notes[(std::size_t) i].fret;
        if (f > 0.f) { sum += f; ++n; }
    }
    if (n == 0) return 3.f;
    return sum / (float) n;
}

bool ChordState::releaseLatched() noexcept
{
    int w = 0;
    bool any = false;
    for (int i = 0; i < count; ++i)
    {
        if (notes[(std::size_t) i].latched) { any = true; continue; }
        notes[(std::size_t) w++] = notes[(std::size_t) i];
    }
    count = w;
    return any;
}

} // namespace fw

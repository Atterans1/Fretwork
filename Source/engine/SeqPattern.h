#pragma once
#include <array>
#include <cstdint>

namespace fw {

// One sequencer cell. Row 0..5 = stroke rows (StrumDown, StrumUp, MutedDown,
// MutedUp, PalmDown, PalmUp). Row 6..11 = guitar strings 1..6, where row 6 is the
// high E (string index 5) and row 11 is the low E (string index 0).
struct SeqNote
{
    std::uint8_t row   = 0;
    std::uint8_t start = 0;
    std::uint8_t len   = 1;
    std::uint8_t vel   = 100;
};

struct SeqPattern
{
    std::array<SeqNote, 256> notes {};
    int count = 0;

    int findIndex (int row, int start) const noexcept
    {
        for (int i = 0; i < count; ++i)
            if (notes[(size_t) i].row == row && notes[(size_t) i].start == start)
                return i;
        return -1;
    }
};

constexpr int kNumSeqSlots = 8;

} // namespace fw

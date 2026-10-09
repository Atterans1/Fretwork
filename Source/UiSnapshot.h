#pragma once
#include <atomic>
#include <cstdint>
#include "Contracts.h"

// Live playing state published by the audio thread for the editor to poll.
// Orchestrator-owned contract: Slice C produces fw::UiState, the processor maps it
// into this snapshot, Slice D only reads it.
namespace fw {
struct UiSnapshot
{
    // Per-string sounding fret, -1 when the string is idle.
    std::atomic<int> fret[kNumStrings];

    // Sequencer playhead step, -1 when the sequencer is not running.
    std::atomic<int> seqStep { -1 };

    // Latched articulation values, index order: exc, damp, trans, harm, tremolo, mode.
    std::atomic<int> ks[kNumStrings];

    // Bitmask of latched keyswitch notes (bit 0 == MIDI note 12), for keyboard highlighting.
    std::atomic<std::uint32_t> ksNotes { 0u };

    // Bit i set => Slice C changed latched articulation group i; the editor writes
    // the matching choice parameter on the message thread.
    std::atomic<std::uint32_t> ksDirtyMask { 0u };

    UiSnapshot()
    {
        for (int i = 0; i < kNumStrings; ++i)
        {
            fret[i].store (-1);
            ks[i].store (0);
        }
    }
};
}

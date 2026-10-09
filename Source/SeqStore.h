#pragma once
#include <array>
#include <atomic>
#include <juce_core/juce_core.h>
#include "engine/SeqPattern.h"

namespace fw {

// Thread-safe holder for the 8 user-drawn sequencer slots.
//
// Design: the message thread owns the slot data and mutates it under a spin lock,
// bumping a version counter on every change. The audio thread calls refresh() once
// per block; refresh() only ever succeeds through ScopedTryLock so it never blocks.
// Because refresh() and every audio-thread read happen on the same thread inside one
// block, the audio cache is never written concurrently with a read.
class SeqStore
{
public:
    // ---------------------------------------------------------- message thread
    void clearSlot (int slot);
    void copySlot (int from, int to);
    void copyToClipboard (int from);
    void pasteFromClipboard (int into);

    void addNote (int slot, const SeqNote& note);
    void removeNote (int slot, int row, int start);
    void setNote (int slot, int row, int start, const SeqNote& note);

    // Message thread only.
    const SeqPattern& getSlot (int slot) const;
    bool hasNote (int slot, int row, int start) const;
    int slotCount() const noexcept { return kNumSeqSlots; }

    // ------------------------------------------------------------- audio thread
    void refresh();
    const SeqPattern& snapshot (int slot) const;

    // ------------------------------------------------------------------- state
    juce::String serialiseSlot (int slot) const;
    void deserialiseSlot (int slot, const juce::String& text);

private:
    void bumpVersion() noexcept { version.store (version.load() + 1); }

    mutable juce::SpinLock lock;
    std::array<SeqPattern, kNumSeqSlots> slots {};
    SeqPattern clipboard {};
    std::array<SeqPattern, kNumSeqSlots> audioCache {};
    std::atomic<int> version { 0 };
    int cachedVersion = -1;
};

} // namespace fw

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "SeqStore.h"

namespace fw
{

/** Steps per quarter note for the Quantize choice (1/8, 1/16, 1/32). */
int seqStepsPerQuarter (int quantizeIndex) noexcept;

/** Steps in one bar for the Quantize and Signature choices. */
int seqStepsPerBar (int quantizeIndex, int signatureIndex) noexcept;

struct SeqImportResult
{
    bool ok = false;        // false: nothing usable, the slot was left untouched
    int  imported = 0;      // strokes written into the slot
    int  ignored = 0;       // notes that are not strum keys
    int  beyondBar = 0;     // strum keys after the first bar (a slot holds one bar)
};

/** Imports a strum pattern from a MIDI file into one sequencer slot.

    Uses the strum keyswitches (notes 28-33: down, up, muted down, muted up,
    palm down, palm up), which map one-to-one onto the sequencer's six stroke
    rows. Notes are quantised to the slot's grid, keep their velocity and
    length, and only the first bar is taken. The slot is replaced only if at
    least one stroke was found. */
SeqImportResult importStrumPattern (const juce::MidiFile& file, SeqStore& store, int slot,
                                    int stepsPerQuarter, int stepsPerBar);

} // namespace fw

#include "SeqImport.h"

#include <cmath>

namespace fw
{

int seqStepsPerQuarter (int quantizeIndex) noexcept
{
    return 2 << juce::jlimit (0, 2, quantizeIndex);   // 1/8, 1/16, 1/32
}

int seqStepsPerBar (int quantizeIndex, int signatureIndex) noexcept
{
    static constexpr int kSignatures[9][2] = { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 12, 8 }, { 8, 4 }, { 9, 8 } };
    const auto& sig = kSignatures[juce::jlimit (0, 8, signatureIndex)];
    return juce::jmax (1, (int) std::lround ((double) sig[0] * 4.0 / (double) sig[1]
                                             * (double) seqStepsPerQuarter (quantizeIndex)));
}

SeqImportResult importStrumPattern (const juce::MidiFile& file, SeqStore& store, int slot,
                                    int stepsPerQuarter, int stepsPerBar)
{
    SeqImportResult r;
    const int ticksPerQuarter = file.getTimeFormat();
    if (ticksPerQuarter <= 0 || stepsPerQuarter <= 0 || stepsPerBar <= 0)
        return r;   // SMPTE time or no grid: nothing to map onto steps

    constexpr int kFirstStrumKey = 28, kNumStrokeRows = 6;
    std::array<std::array<int, 128>, kNumStrokeRows> vel {};   // per row and step, 0 = empty
    std::array<std::array<int, 128>, kNumStrokeRows> len {};
    const int cols = juce::jmin (stepsPerBar, 128);

    for (int t = 0; t < file.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*file.getTrack (t));
        seq.updateMatchedPairs();

        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            const auto& m = seq.getEventPointer (i)->message;
            if (! m.isNoteOn())
                continue;

            const int row = m.getNoteNumber() - kFirstStrumKey;
            if (row < 0 || row >= kNumStrokeRows)
            {
                ++r.ignored;
                continue;
            }

            const double startQ = m.getTimeStamp() / (double) ticksPerQuarter;
            const int step = (int) std::lround (startQ * (double) stepsPerQuarter);
            if (step >= cols)
            {
                ++r.beyondBar;
                continue;
            }

            const double offTicks = seq.getTimeOfMatchingKeyUp (i);
            const double durQ = offTicks > m.getTimeStamp() ? (offTicks - m.getTimeStamp()) / (double) ticksPerQuarter : 0.0;
            const int steps = juce::jlimit (1, cols - step, (int) std::lround (durQ * (double) stepsPerQuarter));

            // Two strokes landing on the same step after quantising: keep the louder.
            const int v = juce::jlimit (1, 127, (int) m.getVelocity());
            if (v > vel[(size_t) row][(size_t) step])
            {
                vel[(size_t) row][(size_t) step] = v;
                len[(size_t) row][(size_t) step] = steps;
            }
        }
    }

    for (int row = 0; row < kNumStrokeRows; ++row)
        for (int step = 0; step < cols; ++step)
            if (vel[(size_t) row][(size_t) step] > 0)
                ++r.imported;

    if (r.imported == 0)
        return r;

    store.clearSlot (slot);
    for (int row = 0; row < kNumStrokeRows; ++row)
        for (int step = 0; step < cols; ++step)
            if (const int v = vel[(size_t) row][(size_t) step]; v > 0)
                store.addNote (slot, SeqNote { (std::uint8_t) row, (std::uint8_t) step,
                                               (std::uint8_t) len[(size_t) row][(size_t) step], (std::uint8_t) v });

    r.ok = true;
    return r;
}

} // namespace fw

#include "SeqStore.h"

namespace fw
{
namespace
{
    // juce::String has no split method in JUCE 8, so split on the two delimiters this
    // file needs using indexOfChar/substring.
    void splitOn (const juce::String& text, juce::juce_wchar delimiter, juce::StringArray& dest)
    {
        dest.clear();

        int start = 0;

        for (;;)
        {
            const int pos = text.indexOfChar (start, delimiter);

            if (pos < 0)
            {
                dest.add (text.substring (start).trim());
                break;
            }

            dest.add (text.substring (start, pos).trim());
            start = pos + 1;
        }
    }
}

// Factory patterns, 16th grid, rows 0 Down 1 Up 2 Muted Down 3 Muted Up
// 4 Palm Down 5 Palm Up (row,start,len,velocity).
namespace
{
    const char* const kFactoryPatterns[] = {
        // 1 Pop: down, down-up, up-down-up
        "0,0,4,105;0,4,2,95;1,6,2,75;1,10,2,80;0,12,2,100;1,14,2,75",
        // 2 Folk: accented down on 1 and 3, light ups between
        "0,0,6,110;1,6,2,70;0,8,2,100;1,10,2,70;1,14,2,70",
        // 3 Rock: palm-muted 8ths, accented on the beat
        "4,0,2,115;4,2,2,90;4,4,2,115;4,6,2,90;4,8,2,115;4,10,2,90;4,12,2,115;4,14,2,90",
        // 4 Funk: muted 16ths, open accents on 2 and 4
        "2,0,1,75;3,1,1,60;2,2,1,75;3,3,1,60;0,4,1,110;3,5,1,60;2,6,1,75;3,7,1,60;2,8,1,75;3,9,1,60;2,10,1,75;3,11,1,60;0,12,1,110;3,13,1,60;2,14,1,75;3,15,1,60",
    };
}

SeqStore::SeqStore()
{
    for (int s = 0; s < (int) std::size (kFactoryPatterns); ++s)
        deserialiseSlot (s, kFactoryPatterns[s]);
}

void SeqStore::clearSlot (int slot)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    slots[(size_t) juce::jlimit (0, kNumSeqSlots - 1, slot)] = {};
    bumpVersion();
}

void SeqStore::copySlot (int from, int to)
{
    const int a = juce::jlimit (0, kNumSeqSlots - 1, from);
    const int b = juce::jlimit (0, kNumSeqSlots - 1, to);

    const juce::SpinLock::ScopedLockType sl (lock);
    slots[(size_t) b] = slots[(size_t) a];
    bumpVersion();
}

void SeqStore::copyToClipboard (int from)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    clipboard = slots[(size_t) juce::jlimit (0, kNumSeqSlots - 1, from)];
}

void SeqStore::pasteFromClipboard (int into)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    slots[(size_t) juce::jlimit (0, kNumSeqSlots - 1, into)] = clipboard;
    bumpVersion();
}

void SeqStore::addNote (int slot, const SeqNote& note)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    auto& p = slots[(size_t) juce::jlimit (0, kNumSeqSlots - 1, slot)];

    if (p.count < (int) p.notes.size())
        p.notes[(size_t) p.count++] = note;

    bumpVersion();
}

void SeqStore::removeNote (int slot, int row, int start)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    auto& p = slots[(size_t) juce::jlimit (0, kNumSeqSlots - 1, slot)];

    const int idx = p.findIndex (row, start);
    if (idx < 0)
        return;

    for (int i = idx; i < p.count - 1; ++i)
        p.notes[(size_t) i] = p.notes[(size_t) (i + 1)];

    --p.count;
    bumpVersion();
}

void SeqStore::setNote (int slot, int row, int start, const SeqNote& note)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    auto& p = slots[(size_t) juce::jlimit (0, kNumSeqSlots - 1, slot)];

    const int idx = p.findIndex (row, start);
    if (idx >= 0)
        p.notes[(size_t) idx] = note;
    else if (p.count < (int) p.notes.size())
        p.notes[(size_t) p.count++] = note;

    bumpVersion();
}

const SeqPattern& SeqStore::getSlot (int slot) const
{
    return slots[(size_t) juce::jlimit (0, kNumSeqSlots - 1, slot)];
}

bool SeqStore::hasNote (int slot, int row, int start) const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return slots[(size_t) juce::jlimit (0, kNumSeqSlots - 1, slot)].findIndex (row, start) >= 0;
}

void SeqStore::refresh()
{
    const int v = version.load();

    if (v == cachedVersion)
        return;

    juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked())
        return;   // a UI write is in flight; keep using the previous snapshot

    audioCache = slots;
    cachedVersion = v;
}

const SeqPattern& SeqStore::snapshot (int slot) const
{
    return audioCache[(size_t) juce::jlimit (0, kNumSeqSlots - 1, slot)];
}

juce::String SeqStore::serialiseSlot (int slot) const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    const auto& p = slots[(size_t) juce::jlimit (0, kNumSeqSlots - 1, slot)];

    juce::StringArray items;

    for (int i = 0; i < p.count; ++i)
    {
        const auto& n = p.notes[(size_t) i];
        items.add (juce::String (n.row) + "," + juce::String (n.start) + ","
                   + juce::String (n.len) + "," + juce::String (n.vel));
    }

    return items.joinIntoString (";");
}

void SeqStore::deserialiseSlot (int slot, const juce::String& text)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    auto& p = slots[(size_t) juce::jlimit (0, kNumSeqSlots - 1, slot)];
    p = {};

    if (text.isNotEmpty())
    {
        juce::StringArray items;
        splitOn (text, ';', items);

        for (const auto& item : items)
        {
            juce::StringArray fields;
            splitOn (item, ',', fields);

            if (fields.size() != 4)
                continue;

            SeqNote n;
            n.row   = (std::uint8_t) fields[0].getIntValue();
            n.start = (std::uint8_t) fields[1].getIntValue();
            n.len   = (std::uint8_t) juce::jmax (1, fields[2].getIntValue());
            n.vel   = (std::uint8_t) juce::jlimit (1, 127, fields[3].getIntValue());

            if (p.count < (int) p.notes.size())
                p.notes[(size_t) p.count++] = n;
        }
    }

    bumpVersion();
}

} // namespace fw

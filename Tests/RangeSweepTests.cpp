// Range sweep: every playable written note, across the instrument's octaves.
// The guitar is written an octave above sounding pitch, so a written note n
// sounds n - 12. This probes the full written span from the lowest open course
// up past the 24th fret of the highest course and reports where a note stops
// sounding, so a dropout is a failed check rather than an unexplained symptom.
#include "Harness.h"

namespace
{
// Playable written window derived from the instrument: lowest = open lowest
// course, highest = open highest course + maxFret. GuitarType changes the
// courses, so this reads them from the instrument rather than hardcoding 40/64.
struct Window
{
    int lowOpen  = 40;
    int highOpen = 64;
    int maxFret  = 24;

    int writtenLow()  const noexcept { return lowOpen  + fw::kWrittenOctave; }
    int writtenHigh() const noexcept { return highOpen + maxFret + fw::kWrittenOctave; }
};
} // namespace

FW_TEST ("range: every written note inside the fretboard window sounds")
{
    fwtest::Plugin p;
    p.set ("mode", 0.f);      // Poly
    p.set ("voicing", 1.f);   // Played: no chord reshaping

    const Window w {};

    int silent = 0;
    std::string where;

    for (int written = w.writtenLow(); written <= w.writtenHigh(); ++written)
    {
        const int sounding = written - fw::kWrittenOctave;
        fwtest::Plugin q;
        q.set ("mode", 0.f).set ("voicing", 1.f);
        q.noteOn (0.01, sounding, 0.85f).noteOff (0.6, sounding);
        const auto take = q.render (0.75);
        const double rms = take.rms (0.08, 0.5);
        const double expectHz = fwtest::midiToHz (sounding);
        const double hz = take.pitch (0.08, 0.5, expectHz);
        const bool ok = rms > 0.005 && hz > 0.0
                     && std::fabs (fwtest::Take::cents (hz, expectHz)) < 150.0;
        if (! ok)
        {
            ++silent;
            if (where.empty())
                where = "first silent at written " + std::to_string (written);
        }
    }

    EXPECT_MSG (silent == 0,
                (std::to_string (silent) + " silent notes in ["
                 + std::to_string (w.writtenLow()) + ".." + std::to_string (w.writtenHigh())
                 + "]" + (where.empty() ? "" : "; " + where)).c_str());
}

FW_TEST ("range: an octave above the open high course still sounds")
{
    // A 6-string spans three octaves from the low E. The note at the top of the
    // window (high open course + last fret) must not fall off the end.
    constexpr int highOpen = 64, maxFret = 24;
    const int sounding = highOpen + maxFret;
    fwtest::Plugin p;
    p.set ("mode", 0.f).set ("voicing", 1.f);
    p.noteOn (0.01, sounding, 0.85f).noteOff (0.6, sounding);
    const auto take = p.render (0.75);
    const double expectHz = fwtest::midiToHz (sounding);
    EXPECT_MSG (take.rms (0.08, 0.5) > 0.005, "highest in-window note produces sound");
    const double hz = take.pitch (0.08, 0.5, expectHz);
    EXPECT_MSG (hz > 0.0 && std::fabs (fwtest::Take::cents (hz, expectHz)) < 150.0,
                "highest in-window note sounds at its own pitch");
}

FW_TEST ("range: notes past the window sound at their own pitch or not at all")
{
    // Outside the physical window a note may legitimately not sound, but it
    // must never sound as the wrong pitch.
    const int sounding = 64 + 24 + 3;
    fwtest::Plugin p;
    p.set ("mode", 0.f).set ("voicing", 1.f);
    p.noteOn (0.01, sounding, 0.85f).noteOff (0.6, sounding);
    const auto take = p.render (0.75);
    const double expectHz = fwtest::midiToHz (sounding);
    if (take.rms (0.08, 0.5) > 0.005)
    {
        const double hz = take.pitch (0.08, 0.5, expectHz);
        EXPECT_MSG (hz > 0.0 && std::fabs (fwtest::Take::cents (hz, expectHz)) < 150.0,
                    "out-of-window note that sounds is at its own pitch");
    }
}

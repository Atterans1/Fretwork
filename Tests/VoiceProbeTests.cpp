// Voice probe: chord shaping in Auto and Guitar modes must place every held
// note on a playable course. Chord shaping only runs outside Poly
// (PerformanceEngine refreshVoicing skips it there), so the chord is played in
// Strum mode and the frets the audio thread published are read back.
//
// The invariant that matters for the player: every course the shape frets must
// sound a pitch class the player is holding. A shape that invents a note an
// octave down, or frets a course to a pitch nobody played, is the "voicing is
// wrong" symptom, so it is asserted directly rather than via RMS.
#include "Harness.h"

namespace
{
void playChord (fwtest::Plugin& p, float voicingMode)
{
    p.set ("mode", 1.f)            // Strum: chord shaping runs
     .set ("voicing", voicingMode);
    for (int n : { 52, 55, 60, 64 }) p.noteOn (0.01, n, 0.8f);
    p.render (0.5);
}

std::string fretsOf (fwtest::Plugin& p)
{
    std::string s;
    for (int i = 0; i < fw::kNumStrings; ++i)
    {
        const int f = p.processor ().getUi ().fret[i].load ();
        if (i != 0) s += " ";
        s += std::to_string (i) + "=" + std::to_string (f);
    }
    return s;
}

int frettedCount (fwtest::Plugin& p)
{
    int n = 0;
    for (int i = 0; i < fw::kNumStrings; ++i)
        if (p.processor ().getUi ().fret[i].load () >= 0) ++n;
    return n;
}

// Pitch classes the C major triad the player held spells.
unsigned heldClasses()
{
    unsigned m = 0;
    for (int n : { 52, 55, 60, 64 }) m |= 1u << (n % 12);
    return m;
}

// Every fretted course must sound one of the held pitch classes, read from the
// published frets against the standard-tuning open courses.
bool everyCourseIsAHeldNote (fwtest::Plugin& p, std::string& wrong)
{
    static const float open[fw::kNumStrings] = { 40.f, 45.f, 50.f, 55.f, 59.f, 64.f };
    const unsigned held = heldClasses();
    wrong.clear ();
    for (int i = 0; i < fw::kNumStrings; ++i)
    {
        const int f = p.processor ().getUi ().fret[i].load ();
        if (f < 0) continue;
        const int sounding = (int) std::lround (open[i]) + f;
        if ((held & (1u << (sounding % 12))) == 0)
        {
            wrong += " s" + std::to_string (i) + "=" + std::to_string (sounding);
        }
    }
    return wrong.empty ();
}
} // namespace

FW_TEST ("voice probe: Auto shapes C major onto held pitch classes only")
{
    fwtest::Plugin p;
    playChord (p, 0.f);
    const std::string f = fretsOf (p);
    std::string wrong;
    const bool clean = everyCourseIsAHeldNote (p, wrong);
    std::printf ("    Auto frets: %s (n=%d)%s\n", f.c_str (), frettedCount (p), clean ? "" : ("  NOT HELD:" + wrong).c_str ());

    EXPECT_MSG (frettedCount (p) >= 3, "Auto frets at least the triad's three courses");
    EXPECT_MSG (clean, "every Auto-fretted course sounds a pitch the player held");
    EXPECT_MSG (p.processor ().getUi ().chordName.load () >= 0, "Auto names the shaped chord");
}

FW_TEST ("voice probe: Guitar mode shapes C major onto held pitch classes only")
{
    fwtest::Plugin p;
    playChord (p, 2.f);
    const std::string f = fretsOf (p);
    std::string wrong;
    const bool clean = everyCourseIsAHeldNote (p, wrong);
    std::printf ("    Guitar frets: %s (n=%d)%s\n", f.c_str (), frettedCount (p), clean ? "" : ("  NOT HELD:" + wrong).c_str ());

    EXPECT_MSG (frettedCount (p) >= 3, "Guitar frets at least the triad's three courses");
    EXPECT_MSG (clean, "every Guitar-fretted course sounds a pitch the player held");
    EXPECT_MSG (p.processor ().getUi ().chordName.load () >= 0, "Guitar names the shaped chord");
}

FW_TEST ("voice probe: Auto and Guitar produce the same C major shape")
{
    fwtest::Plugin a;
    playChord (a, 0.f);
    fwtest::Plugin g;
    playChord (g, 2.f);

    const std::string fa = fretsOf (a), fg = fretsOf (g);
    std::printf ("    Auto %s vs Guitar %s\n", fa.c_str (), fg.c_str ());
    EXPECT_MSG (fa == fg, "Auto and Guitar agree on the frets for a plain triad");
}

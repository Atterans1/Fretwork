// Chord detection, guitar voicing and the Auto voicing switch.
#include "Harness.h"
#include "engine/ChordDetect.h"

#include <string>

namespace
{
const float kStd[fw::kNumStrings] = { 40.f, 45.f, 50.f, 55.f, 59.f, 64.f };

std::string name (std::initializer_list<int> pitches)
{
    int p[8]; int n = 0;
    for (int x : pitches) p[n++] = x;
    char buf[24];
    fw::chord::label (fw::chord::detect (p, n), buf, (int) sizeof (buf));
    return buf;
}

// Shape as text, low E first: "x32010".
std::string shapeText (const fw::Voicing& v)
{
    std::string s (fw::kNumStrings, 'x');
    for (int i = 0; i < v.count; ++i)
        s[(size_t) v.stringIndex[i]] = v.fret[i] < 10 ? (char) ('0' + v.fret[i]) : (char) ('a' + v.fret[i] - 10);
    return s;
}

std::string shapeOf (std::initializer_list<int> pitches, const fw::Voicing* prev = nullptr)
{
    int p[8]; int n = 0;
    for (int x : pitches) p[n++] = x;
    fw::Voicing v;
    if (! fw::chord::guitarVoicing (fw::chord::detect (p, n), kStd, 20, prev, v)) return "none";
    return shapeText (v);
}

std::string uiShape (fwtest::Plugin& p)
{
    std::string s (fw::kNumStrings, 'x');
    for (int i = 0; i < fw::kNumStrings; ++i)
    {
        const int f = p.processor().getUi().fret[i].load();
        if (f >= 0) s[(size_t) i] = f < 10 ? (char) ('0' + f) : (char) ('a' + f - 10);
    }
    return s;
}

std::string uiChord (fwtest::Plugin& p)
{
    char buf[24];
    fw::chord::label (fw::ChordName::unpack (p.processor().getUi().chordName.load()), buf, (int) sizeof (buf));
    return buf;
}
} // namespace

FW_TEST ("chords: detection names triads, sevenths, slash chords and rejects non-chords")
{
    EXPECT (name ({ 60, 64, 67 }) == "C");
    EXPECT (name ({ 57, 60, 64 }) == "Am");
    EXPECT (name ({ 55, 59, 62, 65 }) == "G7");
    EXPECT (name ({ 57, 60, 64, 67 }) == "Am7");
    EXPECT_MSG (name ({ 60, 64, 67, 69 }) == "C6", "same notes as Am7 with C in the bass name C6");
    EXPECT (name ({ 52, 60, 67 }) == "C/E");
    EXPECT (name ({ 59, 62, 65 }) == "Bdim");
    EXPECT (name ({ 55, 60, 62 }) == "Gsus4");
    EXPECT (name ({ 48, 55 }) == "C5");
    EXPECT (name ({ 59, 62, 65, 69 }) == "Bm7b5");
    EXPECT_MSG (name ({ 60 }).empty(), "a single note is not a chord");
    EXPECT_MSG (name ({ 60, 61, 62 }).empty(), "a chromatic cluster is not a chord");
}

FW_TEST ("chords: guitar voicing finds the standard open and barre shapes")
{
    EXPECT_MSG (shapeOf ({ 60, 64, 67 }) == "x32010", ("C -> " + shapeOf ({ 60, 64, 67 })).c_str());
    EXPECT_MSG (shapeOf ({ 55, 59, 62 }) == "320003", ("G -> " + shapeOf ({ 55, 59, 62 })).c_str());
    EXPECT_MSG (shapeOf ({ 62, 66, 69 }) == "xx0232", ("D -> " + shapeOf ({ 62, 66, 69 })).c_str());
    EXPECT_MSG (shapeOf ({ 57, 60, 64 }) == "x02210", ("Am -> " + shapeOf ({ 57, 60, 64 })).c_str());
    EXPECT_MSG (shapeOf ({ 52, 56, 59 }) == "022100", ("E -> " + shapeOf ({ 52, 56, 59 })).c_str());
    EXPECT_MSG (shapeOf ({ 62, 65, 69 }) == "xx0231", ("Dm -> " + shapeOf ({ 62, 65, 69 })).c_str());
    EXPECT_MSG (shapeOf ({ 53, 57, 60 }) == "133211", ("F -> " + shapeOf ({ 53, 57, 60 })).c_str());
    EXPECT_MSG (shapeOf ({ 52, 60, 67 })[0] != '3', "C/E keeps E as the lowest note");
}

FW_TEST ("chords: the previous shape pulls the next one up the neck (voice leading)")
{
    fw::Voicing barreAm;
    {
        int p[] = { 57, 60, 64 };
        fw::Voicing open;
        fw::chord::guitarVoicing (fw::chord::detect (p, 3), kStd, 20, nullptr, open);
        // Fifth-position A minor barre as the previous shape.
        const int frets[] = { 5, 7, 7, 5, 5, 5 };
        for (int s = 0; s < 6; ++s) { barreAm.stringIndex[s] = s; barreAm.fret[s] = frets[s]; barreAm.pitch[s] = kStd[s] + (float) frets[s]; }
        barreAm.count = 6;
    }
    const auto alone = shapeOf ({ 62, 65, 69 });
    const auto after = shapeOf ({ 62, 65, 69 }, &barreAm);
    EXPECT_MSG (alone == "xx0231", alone.c_str());
    EXPECT_MSG (after != alone && after.find ('0') == std::string::npos, ("Dm after a 5th-position barre -> " + after).c_str());
}

FW_TEST ("chords: Auto voicing follows what is played")
{
    fwtest::Plugin p;
    p.set ("mode", 1.f);     // Strum
    p.set ("voicing", 0.f);  // Auto

    p.chord (0.01, { 60, 64, 67 });
    p.render (0.1);
    EXPECT_MSG (uiChord (p) == "C" && uiShape (p) == "x32010", ("C held -> " + uiChord (p) + " " + uiShape (p)).c_str());

    p.noteOff (0.11, 60).noteOff (0.11, 64).noteOff (0.11, 67).noteOn (0.12, 64);
    p.render (0.1);
    EXPECT_MSG (uiChord (p).empty(), "a single note is played as held, no chord name");

    p.noteOn (0.21, 71);   // E + B: an interval, still as held in Auto
    p.render (0.1);
    EXPECT_MSG (uiChord (p).empty(), "an interval stays as held in Auto");

    p.noteOn (0.31, 68);   // E G# B: a chord again
    p.render (0.1);
    EXPECT_MSG (uiChord (p) == "E", ("E major held -> " + uiChord (p) + " " + uiShape (p)).c_str());
}

FW_TEST ("chords: Played never re-voices, Guitar shapes intervals too, Poly always as held")
{
    {
        fwtest::Plugin p;
        p.set ("mode", 1.f).set ("voicing", 1.f);   // Strum, Played
        p.chord (0.01, { 60, 64, 67 });
        p.render (0.1);
        EXPECT_MSG (uiChord (p).empty(), "Played keeps the held notes");
    }
    {
        fwtest::Plugin p;
        p.set ("mode", 1.f).set ("voicing", 2.f);   // Strum, Guitar
        p.chord (0.01, { 48, 55 });
        p.render (0.1);
        EXPECT_MSG (uiChord (p) == "C5", ("Guitar shapes a power chord -> " + uiChord (p)).c_str());
    }
    {
        fwtest::Plugin p;
        p.set ("mode", 0.f).set ("voicing", 2.f);   // Poly, Guitar
        p.chord (0.01, { 60, 64, 67 });
        p.render (0.1);
        EXPECT_MSG (uiChord (p).empty(), "Poly plays each key on its own string");
    }
}

FW_TEST ("chords: Arp over a guitar shape is not silent")
{
    fwtest::Plugin p;
    p.set ("mode", 2.f).set ("voicing", 0.f);    // Arp, Auto
    p.chord (0.01, { 60, 64, 67 });
    const auto t = p.render (1.5);
    EXPECT_MSG (uiChord (p) == "C", uiChord (p).c_str());
    EXPECT_MSG (t.peak (0.2, 1.5) > 0.01, "the arp plays the shape's strings, including notes nobody holds");
}

// 7-string guitar: the seventh course (low B1) sounds, and six-string
// behaviour is byte-for-byte unchanged when the type is SixString.
#include "Harness.h"

FW_TEST ("seven-string: the low B1 course sounds at its own pitch")
{
    fwtest::Plugin p;
    p.set ("mode", 0.f).set ("voicing", 1.f).set ("guitarType", 4.f);
    // Sounding 35 = B1, open seventh course of a 7-string in standard tuning.
    p.noteOn (0.01, 35, 0.85f);
    const auto take = p.render (0.75);
    const double expectHz = fwtest::midiToHz (35);
    EXPECT_MSG (take.rms (0.08, 0.5) > 0.005, "the seventh course produces sound");
    const double hz = take.pitch (0.08, 0.5, expectHz);
    EXPECT_MSG (hz > 0.0 && std::fabs (fwtest::Take::cents (hz, expectHz)) < 150.0,
                "the low B sounds at 61.7 Hz, not an octave off");
}

FW_TEST ("seven-string: the low E still sounds through course 1")
{
    fwtest::Plugin p;
    p.set ("mode", 0.f).set ("voicing", 1.f).set ("guitarType", 4.f);
    p.noteOn (0.01, 40, 0.85f);   // E2, the second course of a 7-string
    const auto take = p.render (0.75);
    const double expectHz = fwtest::midiToHz (40);
    const double hz = take.pitch (0.08, 0.5, expectHz);
    EXPECT_MSG (take.rms (0.08, 0.5) > 0.005, "the sixth course produces sound");
    EXPECT_MSG (hz > 0.0 && std::fabs (fwtest::Take::cents (hz, expectHz)) < 150.0,
                "E2 stays at 82.4 Hz on a 7-string");
}

FW_TEST ("seven-string: state round-trips through the APVTS")
{
    fwtest::Plugin p;
    p.set ("guitarType", 4.f);
    const float back = p.get ("guitarType");
    EXPECT_MSG (back == 4.0f, "guitarType 4 (7-String) round-trips");
}

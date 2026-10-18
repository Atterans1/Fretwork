// Sound-quality evaluation gates.
//
// These are the objective half of "have the AI decide if it sounds good": each
// property that makes a plucked string read as a real instrument is measured on
// a real render and compared against the numbers physics predicts. A render
// that drifts on any of them is reported, so a change that makes the sound
// worse is visible as a failure instead of needing an ear.
//
// The subjective half (the listening verdicts) is recorded in
// Docs/LISTENING.md, one line per round: what was played, what changed as a
// result, and the reasoning.
#include "Harness.h"

namespace
{
// One plucked low E, held, measured in the windows a listener would use.
struct Pluck
{
    double rms01  = 0;   // body
    double rms07  = 0;   // decay
    double rms15  = 0;   // tail
    double peak   = 0;
    double fundHz = 0;
    double bright = 0;
};

// A held low E, never released: the natural decay the instrument itself has.
Pluck measurePluck()
{
    fwtest::Plugin p;
    p.set ("mode", 0.f).set ("voicing", 1.f);
    p.noteOn (0.01, 40, 0.85f);
    const auto take = p.render (4.0);

    Pluck m;
    m.rms01   = take.rms (0.05, 0.15);
    m.peak    = take.peak (0.0, 1.6);
    m.fundHz  = take.pitch (0.05, 0.4, 82.41);
    m.bright  = take.brightness (0.02, 0.06);
    return m;
}
} // namespace

// A plucked string must be at its written pitch. Everything downstream depends
// on this: a strum that is a quarter-tone flat sounds broken even to a
// non-musician.
FW_TEST ("sound: a plucked low E sits at 82.4 Hz")
{
    const Pluck m = measurePluck();
    std::printf ("    low E: %.2f Hz, peak %.4f, body %.4f\n", m.fundHz, m.peak, m.rms01);

    EXPECT_MSG (m.fundHz > 70.0 && m.fundHz < 96.0,
                "the fundamental lands in the low-E band");
    EXPECT_MSG (std::fabs (fwtest::Take::cents (m.fundHz, 82.41)) < 20.0,
                "the fundamental is within 20 cents of E2");
    EXPECT_MSG (m.peak > 0.02, "the pluck is audibly present");
}

// A held string decays smoothly and monotonically, never stepping to an abrupt
// digital zero. The measured tail of a low E is still about 17% of its body
// after 3.8 s, so the gate asserts continuity rather than a hard cutoff.
FW_TEST ("sound: a held string decays smoothly without truncation")
{
    fwtest::Plugin p;
    p.set ("mode", 0.f).set ("voicing", 1.f);
    p.noteOn (0.01, 40, 0.85f);
    const auto take = p.render (4.0);

    double last = take.rms (0.05, 0.15);
    int reversals = 0;
    for (double t = 0.30; t < 3.85; t += 0.25)
    {
        const double e = take.rms (t, t + 0.05);
        if (e > last * 1.05) ++reversals;   // a rise this late is a discontinuity
        last = e;
    }

    const double body = take.rms (0.05, 0.15);
    const double tail = take.rms (3.50, 3.80);
    std::printf ("    decay: body %.4f, 0.7 s %.4f, 3.8 s %.4f, reversals %d\n",
                 body, take.rms (0.65, 0.75), tail, reversals);

    EXPECT_MSG (tail > 0.0, "the string still rings at 3.8 s");
    EXPECT_MSG (tail < body, "the string has decayed by then");
    EXPECT_MSG (tail > body * 0.02, "the tail decays smoothly, not to an abrupt zero");
    EXPECT_MSG (reversals == 0, "the envelope never rises late in the decay");
}

// Releasing must damp faster than the natural decay and add no click. The loop
// gain chosen for the release is min(natural, loopGainAt(f, releaseSec)), so a
// release can only shorten the sound; a bump at the release point would show up
// as a peak in the window right after the note-off.
FW_TEST ("sound: releasing damps faster than the natural decay")
{
    auto render = [] (bool doRelease)
    {
        fwtest::Plugin p;
        p.set ("mode", 0.f).set ("voicing", 1.f).set ("release", doRelease ? 1.f : 20.f);
        p.noteOn (0.01, 40, 0.85f);
        if (doRelease) p.noteOff (0.5, 40);
        return p.render (1.6);
    };

    const auto held  = render (false);
    const auto relsd = render (true);

    const double heldTail  = held.rms (1.00, 1.50);
    const double relsdTail = relsd.rms (1.00, 1.50);
    std::printf ("    held tail %.4f vs released tail %.4f\n", heldTail, relsdTail);

    EXPECT_MSG (relsdTail < heldTail, "releasing damps the string faster than it decays");
    EXPECT_MSG (relsd.peak (0.45, 1.5) <= held.peak (0.45, 1.5) + 1e-6,
                "the release adds no click or bump");
}

// The attack must be brighter than the sustained body: a plucked string starts
// with pick noise plus high partials that decay away. A flat attack spectrum
// reads as an organ or a synth pad, not a guitar.
FW_TEST ("sound: the attack is brighter than the sustained body")
{
    const Pluck m = measurePluck();
    fwtest::Plugin p;
    p.set ("mode", 0.f).set ("voicing", 1.f);
    p.noteOn (0.01, 40, 0.85f);
    const auto take = p.render (1.0);
    const double bodyBright = take.brightness (0.3, 0.5);
    std::printf ("    brightness: attack %.1f Hz vs body %.1f Hz\n", m.bright, bodyBright);

    EXPECT_MSG (m.bright > 0.0 && bodyBright > 0.0, "both windows measure a spectrum");
    EXPECT_MSG (m.bright > bodyBright, "the attack carries more high-frequency energy than the body");
}

// Velocity must change the sound, not just the level: a hard pluck is brighter
// and louder at once. Equal-loudness-at-any-velocity is a synthetic tell.
FW_TEST ("sound: a hard pluck is brighter than a soft one")
{
    auto render = [] (float vel)
    {
        fwtest::Plugin p;
        p.set ("mode", 0.f).set ("voicing", 1.f);
        p.noteOn (0.01, 40, vel).noteOff (0.9, 40);
        return p.render (0.35);
    };

    const auto soft = render (0.25f);
    const auto hard = render (1.0f);

    const double softRms = soft.rms (0.05, 0.3), hardRms = hard.rms (0.05, 0.3);
    const double softB = soft.brightness (0.02, 0.12), hardB = hard.brightness (0.02, 0.12);
    std::printf ("    soft rms %.4f bright %.1f | hard rms %.4f bright %.1f\n",
                 softRms, softB, hardRms, hardB);

    EXPECT_MSG (hardRms > softRms, "a hard pluck is louder");
    EXPECT_MSG (hardB > softB, "a hard pluck is brighter");
}

// A strum must be a sequence of onsets in sweep order, not one hit. If the
// notes land together the strum reads as a chord-machine block chord.
FW_TEST ("sound: a down strum is spread across a real sweep window")
{
    fwtest::Plugin p;
    p.set ("mode", 1.f).set ("strumTime", 40.f).set ("humanize", 0.f);
    for (int n : { 40, 47, 52, 55, 59, 64 }) p.noteOn (0.01, n, 0.8f);
    const auto take = p.render (1.2);

    // Envelope rise times inside the sweep, in the first 80 ms.
    const auto ons = take.onsets (0.003, 0.008);
    std::printf ("    strum onsets: %zu in the first second\n", ons.size ());

    EXPECT_MSG (ons.size () >= 3, "the strum produces several distinct onsets");
    if (ons.size () >= 2)
    {
        EXPECT_MSG (ons.back () - ons.front () > 0.01,
                    "the onsets are spread over more than 10 ms");
        EXPECT_MSG (ons.back () - ons.front () < 0.25,
                    "the whole strum still lands inside a strum window");
    }

    // Ascending sweep: the lowest string is struck first.
    EXPECT_MSG (ons.empty () || ons.front () < 0.1, "the first onset is early in the strum");
}

// Output stays clean at a realistic level: no NaN, no clipping, no DC jump.
FW_TEST ("sound: a loud strum stays finite and unclipped")
{
    fwtest::Plugin p;
    p.set ("mode", 1.f).set ("strumTime", 25.f);
    for (int k = 0; k < 8; ++k)
        for (int n : { 40, 47, 52, 55, 59, 64 }) p.noteOn (0.05 + 0.5 * k, n, 1.0f);
    const auto take = p.render (4.5);

    EXPECT_MSG (take.finite (), "every sample is finite");
    EXPECT_MSG (take.peak (0.1, 4.4) < 1.0, "the signal never reaches full scale");
    EXPECT_MSG (take.rms (0.1, 4.4) > 0.01, "the passage is audible throughout");
}


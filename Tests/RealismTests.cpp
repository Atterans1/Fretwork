// Realism: the behaviours that make a plucked string sound like a guitar and
// not a synth voice. Each test names what a real guitar does and checks the
// model does it, at the string (deterministic) or through the whole plugin.
#include "Harness.h"

#include "dsp/GuitarString.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kFs = 48000.0;
constexpr double kPi = 3.14159265358979323846;

fw::StringEvent pluckAt (float pitch, float vel)
{
    fw::StringEvent e {};
    e.string = 0;
    e.pitch = pitch;
    e.velocity = vel;
    e.type = fw::EventType::Pluck;
    return e;
}

// Level in dB of the strongest component within +-1 % of hz, over [t0, t1).
// Goertzel at 11 frequencies across the band, so a slightly stretched
// (inharmonic) partial is still found.
double partialDb (const std::vector<float>& x, double t0, double t1, double hz)
{
    const size_t a = (size_t) (t0 * kFs), b = std::min (x.size(), (size_t) (t1 * kFs));
    double best = 0.0;
    for (int k = -5; k <= 5; ++k)
    {
        const double f = hz * (1.0 + 0.002 * k);
        const double w = 2.0 * kPi * f / kFs, c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (size_t i = a; i < b; ++i)
        {
            // Hann window, so the side lobes of the fundamental stay out.
            const double win = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) (i - a) / (double) (b - a));
            const double s0 = x[i] * win + c * s1 - s2;
            s2 = s1; s1 = s0;
        }
        const double p = s1 * s1 + s2 * s2 - c * s1 * s2;
        best = std::max (best, p);
    }
    return 10.0 * std::log10 (best + 1e-30);
}

std::vector<float> renderString (fw::dsp::GuitarString& s, double secs)
{
    std::vector<float> y ((size_t) (secs * kFs));
    for (auto& v : y) v = s.tick (0.f, 0.f);
    return y;
}

} // namespace

FW_TEST ("realism: an open string fades in two stages, quick then long and soft")
{
    // A string swings in two planes. The one square to the top drives the
    // bridge and dies first; the one parallel to it rings on quietly. Against
    // a one-plane string of the same overall sustain, the note falls a little
    // faster in its first seconds and then hangs on in a long soft tail,
    // while the attack is as loud as before.
    // The wound strings ring so long in both planes that their second stage
    // only shows well past six seconds, so the tail is checked on the two
    // plain treble strings; every string keeps its attack level.
    for (const auto& np : { std::pair { 40, 3 }, std::pair { 55, 1 }, std::pair { 59, 1 }, std::pair { 64, 1 } })
    {
        const int note = np.first, partial = np.second;   // plain ints: older Apple clang can't capture bindings
        const auto take = [note] (bool twoPlanes)
        {
            fw::dsp::GuitarString s;
            s.prepare (kFs);
            s.setModel (fw::dsp::StringModelParams {});
            s.setSecondPlane (twoPlanes);
            s.pluck (pluckAt ((float) note, 0.9f), note == 40 ? 0 : note == 55 ? 3 : note == 59 ? 4 : 5);
            return renderString (s, 6.0);
        };
        const auto one = take (false), two = take (true);
        const double hz = partial * 440.0 * std::pow (2.0, (note - 69.0) / 12.0);

        const auto rmsDb = [] (const std::vector<float>& y, double t0, double t1)
        {
            double e = 0.0;
            for (size_t i = (size_t) (t0 * kFs); i < (size_t) (t1 * kFs); ++i) e += (double) y[i] * y[i];
            return 10.0 * std::log10 (e / ((t1 - t0) * kFs) + 1e-30);
        };
        const auto gain = [&] (double t0, double t1) { return partialDb (two, t0, t1, hz) - partialDb (one, t0, t1, hz); };
        const double attack = rmsDb (two, 0.05, 0.3) - rmsDb (one, 0.05, 0.3);
        const double first  = gain (1.0, 1.8);
        const double tail   = gain (5.0, 5.8);
        std::printf ("    note %d against one plane: attack %+.1f dB, partial %d at 1 s %+.1f dB, at 5 s %+.1f dB\n",
                     note, attack, partial, first, tail);
        EXPECT_MSG (std::fabs (attack) < 0.5, "the attack keeps its level");
        if (note >= 59)
            EXPECT_MSG (tail - first > 3.0, "the decay slows into a long soft tail");
    }
}

FW_TEST ("realism: a palm-muted string dies in one stage")
{
    // With the palm on the strings both planes are damped; no long tail.
    fw::dsp::GuitarString s;
    s.prepare (kFs);
    s.setModel (fw::dsp::StringModelParams {});
    auto e = pluckAt (40.f, 0.9f);
    e.art.damp = fw::Damping::PalmMute;
    s.pluck (e, 0);
    const auto y = renderString (s, 2.0);
    const double at05 = partialDb (y, 0.05, 0.25, 82.41 * 2);
    const double at15 = partialDb (y, 1.3, 1.5, 82.41 * 2);
    std::printf ("    palm-muted 2nd partial: %.1f dB down after 1.25 s\n", at05 - at15);
    EXPECT_MSG (at05 - at15 > 30.0, "no long horizontal tail rings under a palm mute");
}

FW_TEST ("realism: a hard pick starts sharp and settles; a soft one starts in tune")
{
    // Tension modulation: a big swing stretches the string, so a dug-in note
    // begins a few cents sharp and falls back as it decays.
    const auto drift = [] (float vel)
    {
        fwtest::Plugin p;
        p.set ("room", 0.f).set ("mode", 0.f).set ("voicing", 1.f).set ("sympathetic", 0.f);
        p.noteOn (0.01, 40, vel);
        const auto t = p.render (1.3);
        const double hz = 82.41;
        // Both windows end before 0.6 s, when automatic vibrato would start.
        const double start = fwtest::Take::cents (t.pitch (0.06, 0.12, hz), hz);
        const double held  = fwtest::Take::cents (t.pitch (0.4, 0.55, hz), hz);
        std::printf ("    velocity %.2f: starts %+.2f c, settles %+.2f c\n", vel, start, held);
        EXPECT_MSG (std::fabs (held) < 2.0, "settles on pitch");
        return start - held;
    };
    // The pitch reading just after the attack carries a few cents of its own
    // bias at any velocity, so the hard pick is judged against the soft one.
    const double hard = drift (1.0f), soft = drift (0.4f);
    std::printf ("    hard pick starts %+.2f c sharper than a soft one\n", hard - soft);
    EXPECT_MSG (hard - soft > 2.5 && hard - soft < 12.0, "a full-force pick starts audibly but only slightly sharp");
}

FW_TEST ("realism: a finger slide steps through the frets and lands exactly")
{
    // On a fretted neck a sliding finger sits near each fret it passes and
    // moves quickly between them; a synth portamento glides at one speed.
    fw::dsp::GuitarString s;
    s.prepare (kFs);
    s.setModel (fw::dsp::StringModelParams {});
    s.setGlideTime (0.5f);
    s.pluck (pluckAt (45.f, 0.3f), 0);
    for (int i = 0; i < (int) (0.2 * kFs); ++i) s.tick (0.f, 0.f);

    fw::StringEvent l = pluckAt (50.f, 0.8f);
    l.type = fw::EventType::Legato;
    l.pitchFrom = 45.f;
    s.legato (l);

    const double base = 440.0 * std::pow (2.0, (45.0 - 69.0) / 12.0);
    std::vector<double> semis;
    for (int i = 0; i < (int) (0.6 * kFs); ++i)
    {
        s.tick (0.f, 0.f);
        semis.push_back (12.0 * std::log2 ((double) s.fundamentalHz() / base));
    }

    const size_t glideN = (size_t) (0.5 * kFs);
    int nearFret = 0; bool monotone = true;
    for (size_t i = 0; i < glideN; ++i)
    {
        const double frac = semis[i] - std::floor (semis[i]);
        nearFret += (frac < 0.2 || frac > 0.8);
        if (i > 0 && semis[i] < semis[i - 1] - 1e-4) monotone = false;
    }
    const double share = (double) nearFret / (double) glideN;
    const double firstTenth = semis[glideN / 10] - semis[0];
    const double landed = semis.back();
    std::printf ("    slide: %.0f %% of the time within 0.2 st of a fret (smooth glide 40 %%), "
                 "first tenth moves %.2f st, lands %+.3f st\n", 100.0 * share, firstTenth, landed);
    EXPECT_MSG (monotone, "the slide never turns back");
    EXPECT_MSG (share > 0.55, "the pitch dwells near the frets it passes");
    EXPECT_MSG (firstTenth < 0.25, "the finger eases off the first fret");
    EXPECT_MSG (std::fabs (landed - 5.0) < 0.02, "it lands on the target fret");
}

FW_TEST ("realism: a hammer-on still snaps straight to the new fret")
{
    fw::dsp::GuitarString s;
    s.prepare (kFs);
    s.setModel (fw::dsp::StringModelParams {});
    s.pluck (pluckAt (45.f, 0.3f), 0);
    for (int i = 0; i < (int) (0.2 * kFs); ++i) s.tick (0.f, 0.f);
    fw::StringEvent l = pluckAt (47.f, 0.6f);
    l.type = fw::EventType::Legato;
    s.legato (l);
    for (int i = 0; i < (int) (0.004 * kFs); ++i) s.tick (0.f, 0.f);
    const double semi = 12.0 * std::log2 ((double) s.fundamentalHz() / (440.0 * std::pow (2.0, (45.0 - 69.0) / 12.0)));
    EXPECT_MSG (std::fabs (semi - 2.0) < 0.02, "on the new fret within 4 ms");
}

FW_TEST ("realism: steel-string vibrato bends up from the note; classical swings both ways")
{
    const auto range = [] (int body, double& lo, double& hi)
    {
        fwtest::Plugin p;
        p.set ("room", 0.f).set ("body", (float) body).set ("vibDepth", 100.f).set ("mode", 0.f).set ("voicing", 1.f);
        p.cc (0.0, 1, 127);
        p.noteOn (0.01, 64, 0.5f);
        const auto t = p.render (1.6);
        const double hz = fwtest::midiToHz (64);
        lo = 1e9; hi = -1e9;
        for (double a = 0.3; a < 1.5; a += 0.02)
        {
            const double c = fwtest::Take::cents (t.pitch (a, a + 0.03, hz), hz);
            lo = std::min (lo, c); hi = std::max (hi, c);
        }
    };
    double lo = 0, hi = 0;
    range (0, lo, hi);
    std::printf ("    dreadnought vibrato: %+.0f to %+.0f c\n", lo, hi);
    EXPECT_MSG (lo > -12.0, "a bend vibrato never drops well under the fretted note");
    EXPECT_MSG (hi > 60.0, "it rises well above it");
    range (1, lo, hi);
    std::printf ("    classical vibrato: %+.0f to %+.0f c\n", lo, hi);
    EXPECT_MSG (lo < -40.0 && hi > 40.0, "a classical roll swings both ways");
}

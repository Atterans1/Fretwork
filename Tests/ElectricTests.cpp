// Electric guitar: what a pickup, an amp and a speaker cabinet do to a string,
// checked against how the real parts behave.
#include "Harness.h"

#include "dsp/Biquad.h"
#include "dsp/GuitarEngine.h"
#include "dsp/PickupModel.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

// Level in dB of the strongest component within +-1 % of hz, Hann-windowed.
double partialDb (const std::vector<float>& x, double fs, double t0, double t1, double hz)
{
    const size_t a = (size_t) (t0 * fs), b = std::min (x.size(), (size_t) (t1 * fs));
    double best = 0.0;
    for (int k = -5; k <= 5; ++k)
    {
        const double f = hz * (1.0 + 0.002 * k);
        const double c = 2.0 * std::cos (2.0 * kPi * f / fs);
        double s1 = 0.0, s2 = 0.0;
        for (size_t i = a; i < b; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) (i - a) / (double) (b - a));
            const double s0 = x[i] * win + c * s1 - s2;
            s2 = s1; s1 = s0;
        }
        best = std::max (best, s1 * s1 + s2 * s2 - c * s1 * s2);
    }
    return 10.0 * std::log10 (best + 1e-30);
}

// Share of energy below (or above) a corner, 4th-order Butterworth split.
double bandShare (const fwtest::Take& t, double corner, bool above, double t0, double t1)
{
    fw::dsp::Biquad f1, f2;
    if (above) { f1.setHighPass (t.fs, corner, 0.5412); f2.setHighPass (t.fs, corner, 1.3066); }
    else       { f1.setLowPass  (t.fs, corner, 0.5412); f2.setLowPass  (t.fs, corner, 1.3066); }
    double band = 0, tot = 0;
    for (size_t i = 0; i < (size_t) (t1 * t.fs) && i < t.left.size(); ++i)
    {
        const float x = 0.5f * (t.left[i] + t.right[i]);
        const float y = f2.process (f1.process (x));
        if (i < (size_t) (t0 * t.fs)) continue;
        band += (double) y * y; tot += (double) x * x;
    }
    return tot > 0 ? band / tot : 0;
}

std::vector<float> mono (const fwtest::Take& t)
{
    std::vector<float> y (t.left.size());
    for (size_t i = 0; i < y.size(); ++i) y[i] = 0.5f * (t.left[i] + t.right[i]);
    return y;
}

fwtest::Take electricNote (int pickupSel, int note)
{
    fwtest::Plugin p;
    p.set ("body", 2.f).set ("tone", 16000.f).set ("room", 0.f).set ("sympathetic", 0.f)
     .set ("pickupSel", (float) pickupSel).set ("pickupTone", 100.f).set ("voicing", 1.f);
    p.noteOn (0.01, note, 0.8f);
    return p.render (1.0);
}

// A lead amp at 60 % gain on an E2 power chord.
fwtest::Take powerChord (int model, int cab)
{
    fwtest::Plugin p;
    p.set ("body", 2.f).set ("tone", 16000.f).set ("room", 0.f)
     .set ("ampModel", (float) model).set ("cab", (float) cab).set ("ampGain", 60.f);
    p.chord (0.01, { 40, 47, 52 }, 0.9f, 0.006);
    return p.render (1.2);
}

} // namespace

FW_TEST ("electric: the neck pickup sits where its 4th harmonic cancels")
{
    // A pickup hears the string's motion at its own spot, so harmonics with a
    // node there vanish. The neck pickup sits near a quarter of the scale, so
    // the 4th harmonic all but disappears there; the bridge pickup keeps it.
    // This is much of why a neck pickup sounds round and a bridge one twangs.
    const double f0 = 440.0 * std::pow (2.0, (52 - 69) / 12.0);
    const auto bridge = mono (electricNote (0, 52)), neck = mono (electricNote (2, 52));
    const auto rel4 = [f0] (const std::vector<float>& y)
    { return partialDb (y, 48000.0, 0.1, 0.6, 4 * f0) - partialDb (y, 48000.0, 0.1, 0.6, f0); };
    const double b = rel4 (bridge), n = rel4 (neck);
    std::printf ("    4th harmonic against the fundamental: bridge %+.1f dB, neck %+.1f dB\n", b, n);
    EXPECT_MSG (b - n > 12.0, "the neck pickup's 4th harmonic is far below the bridge pickup's");
}

FW_TEST ("electric: bridge, middle and neck go from bright to warm")
{
    double prev = 1e9;
    for (int sel : { 0, 1, 2 })
    {
        const double hi = bandShare (electricNote (sel, 52), 1500.0, true, 0.05, 0.9);
        std::printf ("    pickup %d: %.2f %% of the energy above 1.5 kHz\n", sel, 100 * hi);
        EXPECT_MSG (hi < prev, "each pickup towards the neck is warmer");
        prev = hi;
    }
}

FW_TEST ("electric: a seven-string's top string reaches the pickup")
{
    // The seventh string is index 6. The pickup used to keep combs for six
    // strings only, so on an electric body the 7-string's high E was silent.
    const auto render = [] (int stringIdx)
    {
        fw::dsp::GuitarEngine eng;
        eng.prepare (48000.0, 480);
        fw::dsp::EngineParams p {};
        p.body = 2;
        p.activeStringCount = 7;
        p.room = 0.f;
        p.sympathetic = 0.f;
        eng.setParams (p);
        fw::StringEvent e {};
        e.string = stringIdx;
        e.pitch = 64.f;
        e.velocity = 0.8f;
        e.type = fw::EventType::Pluck;
        eng.handle (e);
        std::vector<float> l (480), r (480);
        double sum = 0.0;
        for (int b = 0; b < 50; ++b)
        {
            eng.render (l.data(), r.data(), 480);
            for (float v : l) sum += (double) v * v;
        }
        return std::sqrt (sum / (50.0 * 480.0));
    };
    const double sixth = render (5), seventh = render (6);
    std::printf ("    high E rms: on string index 5 %.5f, on index 6 %.5f\n", sixth, seventh);
    EXPECT_MSG (seventh > 0.5 * sixth, "the seventh string is heard through the pickup");
}

FW_TEST ("electric: a 12-string's paired string has its own pickup comb")
{
    // Main and course strings used to share one history, so each wrote over
    // the other's and both combs were wrong. Two channels driven together
    // must now match two separate pickups.
    fw::dsp::PickupModel both, onlyMain, onlyCourse;
    for (auto* m : { &both, &onlyMain, &onlyCourse })
    {
        m->prepare (48000.0);
        fw::dsp::PickupParams pp {};
        pp.sel = 2;
        m->setParams (pp);
    }
    double err = 0.0, ref = 0.0;
    for (int i = 0; i < 4800; ++i)
    {
        const float a = (float) std::sin (2.0 * kPi * 110.0 * i / 48000.0) * 0.3f;
        const float c = (float) std::sin (2.0 * kPi * 220.0 * i / 48000.0) * 0.3f;
        const float x = both.processString (0, a, 436.f) + both.processString (fw::kMaxStrings, c, 218.f);
        const float y = onlyMain.processString (0, a, 436.f) + onlyCourse.processString (fw::kMaxStrings, c, 218.f);
        err += (double) (x - y) * (x - y);
        ref += (double) y * y;
    }
    std::printf ("    difference against separate pickups: %.2e\n", std::sqrt (err / ref));
    EXPECT_MSG (ref > 1.0, "both channels are heard");
    EXPECT_MSG (err < 1e-12 * ref + 1e-20, "the two strings do not disturb each other's comb");
}

FW_TEST ("amp: high gain stays tight in the lows and smooth on top")
{
    // A real high-gain preamp thins the lows before each stage clips, so a
    // power chord does not turn to rumble, and its triodes roll off the top,
    // so the distortion is not fizzy. The old single waveshaper put 2.7 % of
    // an E2 power chord below 70 Hz and 0.41 % above 8 kHz (no cabinet).
    const auto dry = powerChord (0, 0), lead = powerChord (3, 0);
    const double rumble = bandShare (lead, 70.0, false, 0.05, 1.0), fizz = bandShare (lead, 8000.0, true, 0.05, 1.0);
    const double thump  = bandShare (powerChord (3, 3), 160.0, false, 0.05, 1.0);
    std::printf ("    lead, no cab: below 70 Hz %.2f %% (dry %.2f %%), above 8 kHz %.3f %%; with 4x12 below 160 Hz %.1f %%\n",
                 100 * rumble, 100 * bandShare (dry, 70.0, false, 0.05, 1.0), 100 * fizz, 100 * thump);
    EXPECT_MSG (rumble < 0.015, "little rumble under a distorted power chord");
    EXPECT_MSG (fizz < 0.0025, "little fizz above 8 kHz");
    EXPECT_MSG (thump > 0.15, "the low end still thumps through a 4x12");
}

FW_TEST ("amp: a crunch amp sags on a hard chord and recovers")
{
    // Power-amp sag: the attack of a hard chord is held back a little more
    // than its ringing tail, so the chord blooms. Compare the amp's gain on the
    // first 60 ms with its gain half a second later, against the dry signal.
    const auto take = [] (int model)
    {
        fwtest::Plugin p;
        p.set ("body", 2.f).set ("tone", 16000.f).set ("room", 0.f).set ("ampModel", (float) model).set ("cab", 0.f).set ("ampGain", 20.f);
        p.chord (0.01, { 40, 47, 52, 56, 59, 64 }, 1.0f, 0.004);
        return p.render (1.2);
    };
    const auto dry = take (0), amp = take (2);
    const double early = amp.rms (0.02, 0.08) / dry.rms (0.02, 0.08);
    const double late  = amp.rms (0.70, 0.90) / dry.rms (0.70, 0.90);
    std::printf ("    crunch gain against dry: first 60 ms %.3f, at 0.7 s %.3f\n", early, late);
    EXPECT_MSG (late > early * 1.05, "the ringing chord comes back up after the attack");
}

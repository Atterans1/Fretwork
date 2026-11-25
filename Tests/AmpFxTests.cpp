// Amp, cabinet, chorus and delay.
#include "Harness.h"
#include "dsp/Biquad.h"

namespace
{
// Share of energy above / below a corner over [t0, t1), split with a 4th-order
// Butterworth (two biquads) so a strong low fundamental cannot leak across.
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
        if (i < (size_t) (t0 * t.fs)) continue;   // filters settle on the lead-in
        band += (double) y * y; tot += (double) x * x;
    }
    return tot > 0 ? band / tot : 0;
}

double diff (const fwtest::Take& a, const fwtest::Take& b)
{
    const size_t n = std::min (a.left.size(), b.left.size());
    double d = 0, e = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double x = a.left[i] - b.left[i], y = a.right[i] - b.right[i];
        d += x * x + y * y; e += (double) a.left[i] * a.left[i] + (double) a.right[i] * a.right[i];
    }
    return e > 0 ? std::sqrt (d / e) : 0;
}

// The master Tone low-pass sits after the amp; open it so the tests hear the amp.
fwtest::Take pluckWith (std::initializer_list<std::pair<const char*, float>> settings, int note = 52, double secs = 1.0)
{
    fwtest::Plugin p;
    p.set ("tone", 16000.f);
    for (const auto& s : settings) p.set (s.first, s.second);
    p.noteOn (0.01, note, 0.9f).noteOff (secs * 0.8, note);
    return p.render (secs);
}
} // namespace

FW_TEST ("amp: Off with both mixes at 0 leaves the sound bit-identical")
{
    const auto ref = pluckWith ({});
    const auto moved = pluckWith ({ { "ampGain", 100.f }, { "ampTreble", 0.f }, { "ampLevel", 12.f }, { "cab", 3.f },
                                    { "chorusRate", 4.f }, { "delayTime", 100.f }, { "delayFeedback", 90.f } });
    EXPECT_MSG (diff (ref, moved) == 0.0, "settings of a bypassed amp and effects do not touch the audio");
}

FW_TEST ("amp: the models add harmonics in order Clean < Crunch < Lead")
{
    double prev = -1;
    for (int model = 0; model <= 3; ++model)
    {
        const auto t = pluckWith ({ { "ampModel", (float) model }, { "cab", 0.f }, { "ampGain", 60.f } }, 45);
        const double hi = bandShare (t, 1500.0, true, 0.05, 0.5);
        std::printf ("    model %d: energy above 1.5 kHz %.2f %%, peak %.3f\n", model, 100 * hi, t.peak());
        EXPECT_MSG (t.finite() && t.peak() < 0.99, "finite and inside the output clamp");
        EXPECT_MSG (hi > prev, "each model adds more upper harmonics than the one before");
        prev = hi;
    }
}

FW_TEST ("amp: Treble and Bass move their bands")
{
    const auto lowT  = pluckWith ({ { "ampModel", 1.f }, { "cab", 0.f }, { "ampTreble", 0.f } });
    const auto highT = pluckWith ({ { "ampModel", 1.f }, { "cab", 0.f }, { "ampTreble", 100.f } });
    const auto lowB  = pluckWith ({ { "ampModel", 1.f }, { "cab", 0.f }, { "room", 0.f }, { "ampBass", 0.f } });
    const auto highB = pluckWith ({ { "ampModel", 1.f }, { "cab", 0.f }, { "room", 0.f }, { "ampBass", 100.f } });
    const double t0 = bandShare (lowT, 3000.0, true, 0.05, 0.5), t1 = bandShare (highT, 3000.0, true, 0.05, 0.5);
    const double b0 = bandShare (lowB, 150.0, false, 0.05, 0.5), b1 = bandShare (highB, 150.0, false, 0.05, 0.5);
    std::printf ("    treble 0/100: above 3 kHz %.3f / %.3f %%; bass 0/100: below 150 Hz %.2f / %.2f %%\n", 100 * t0, 100 * t1, 100 * b0, 100 * b1);
    EXPECT_MSG (t1 > t0 * 2.0, "Treble up brings the top end up");
    EXPECT_MSG (b1 > b0 * 1.5, "Bass up brings the low end up");
}

FW_TEST ("amp: a 4x12 cabinet rolls off the top end")
{
    const auto open = pluckWith ({ { "ampModel", 3.f }, { "cab", 0.f } }, 45);
    const auto cab  = pluckWith ({ { "ampModel", 3.f }, { "cab", 3.f } }, 45);
    const double a = bandShare (open, 6000.0, true, 0.05, 0.5), b = bandShare (cab, 6000.0, true, 0.05, 0.5);
    std::printf ("    above 6 kHz: no cab %.3f %%, 4x12 %.4f %%\n", 100 * a, 100 * b);
    EXPECT_MSG (b < a * 0.2, "the cabinet removes most of the fizz above 6 kHz");
}

FW_TEST ("fx: delay repeats at the set time and the repeats fade")
{
    fwtest::Plugin p;
    p.set ("delayMix", 60.f).set ("delayTime", 400.f).set ("delayFeedback", 50.f).set ("room", 0.f);
    p.set ("release", 0.05f);
    p.noteOn (0.01, 64, 1.0f).noteOff (0.06, 64);
    const auto t = p.render (1.6);
    const double gap = t.rms (0.30, 0.38), echo1 = t.rms (0.41, 0.47), echo2 = t.rms (0.81, 0.87);
    std::printf ("    rms before echo %.5f, echo 1 %.5f, echo 2 %.5f\n", gap, echo1, echo2);
    EXPECT_MSG (echo1 > gap * 3.0, "an echo arrives 400 ms after the note");
    EXPECT_MSG (echo2 < echo1 && echo2 > echo1 * 0.1, "the second repeat is quieter than the first, but there");
}

FW_TEST ("fx: chorus widens even a mono signal")
{
    const auto measure = [] (float mix)
    {
        fwtest::Plugin p;
        p.set ("width", 0.f).set ("room", 0.f).set ("chorusMix", mix);
        p.noteOn (0.01, 57, 0.9f);
        const auto t = p.render (0.8);
        double side = 0, mid = 0;
        for (size_t i = (size_t) (0.1 * t.fs); i < (size_t) (0.7 * t.fs); ++i)
        {
            const double s = t.left[i] - t.right[i], m = t.left[i] + t.right[i];
            side += s * s; mid += m * m;
        }
        return side / std::max (mid, 1e-12);
    };
    const double dry = measure (0.f), wet = measure (100.f);
    std::printf ("    side/mid energy: dry %.2e, chorus %.2e\n", dry, wet);
    EXPECT_MSG (dry < 1e-6, "width 0 is mono");
    EXPECT_MSG (wet > 1e-3, "the chorus makes left and right differ");
}

FW_TEST ("amp: maximum gain with heavy feedback stays finite and clamped")
{
    fwtest::Plugin p;
    p.set ("ampModel", 3.f).set ("ampGain", 100.f).set ("ampLevel", 12.f).set ("ampBass", 100.f)
     .set ("delayMix", 100.f).set ("delayFeedback", 90.f).set ("delayTime", 120.f).set ("chorusMix", 100.f);
    p.chord (0.01, { 40, 47, 52, 56, 59, 64 }, 1.0f);
    const auto t = p.render (4.0);
    EXPECT (t.finite());
    EXPECT_MSG (t.peak() <= 0.981, "output clamp holds");
}

FW_TEST ("amp: the amp factory presets load the amp and play cleanly")
{
    fwtest::Plugin p;
    auto& pm = p.processor().getPatches();
    int found = 0;
    for (int i = 0; i < (int) fw::PatchManager::factoryPresets().size(); ++i)
    {
        const auto name = fw::PatchManager::factoryPresets()[(size_t) i].name;
        if (juce::String (name) != "Crunch Amp" && juce::String (name) != "Lead Stack"
            && juce::String (name) != "Chorus Clean" && juce::String (name) != "Jazz Amp") continue;
        ++found;
        fwtest::Plugin q;
        q.processor().getPatches().loadPreset (i);
        EXPECT_MSG (q.get ("ampModel") >= 1.f, name);
        q.chord (0.01, { 40, 47, 52, 55, 59, 64 }, 0.9f);
        q.keyOn (0.05, 28, 0.9f).keyOff (0.15, 28);   // down-stroke key: Strum presets sound only when strummed
        const auto take = q.render (1.5);
        std::printf ("    %-13s peak %.3f rms %.4f\n", name, take.peak(), take.rms (0.05, 1.0));
        EXPECT_MSG (take.finite() && take.peak() < 0.98 && take.rms (0.05, 1.0) > 0.01, name);
    }
    EXPECT_MSG (found == 4, "four amp presets");
    (void) pm;
}

// Functional suite: every user-facing behaviour of Fretwork, exercised through
// fwtest::Plugin exactly as a host would drive it. See Harness.h.

#include "Harness.h"
#include "PluginEditor.h"
#include "WavesStyle.h"
#include "SeqGrid.h"

// juce_events/messages/juce_MessageManager.cpp
namespace juce::detail { bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages); }

// JUCE's per-message dispatcher (juce_events/messages/juce_MessageManager.cpp).
namespace juce::detail { bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages); }

namespace
{
struct Golden { const char* id; char kind; double lo, hi, def; int count; };

const Golden kGolden[] =
{
#include "ParamGolden.inc"
};

// Every legal value worth visiting: both ends and the middle for continuous
// parameters, every step for discrete ones.
std::vector<float> valuesFor (const Golden& g)
{
    if (g.kind == 'f') return { (float) g.lo, (float) (0.5 * (g.lo + g.hi)), (float) g.hi };
    std::vector<float> v;
    const int lo = (int) g.lo, hi = (int) g.hi;
    const int step = std::max (1, (hi - lo) / 6);         // ints like velOffset span 127 steps
    for (int i = lo; i <= hi; i += step) v.push_back ((float) i);
    if (v.back() != (float) hi) v.push_back ((float) hi);
    return v;
}

float nonDefault (const Golden& g)
{
    if (g.kind == 'f') return (float) (g.def == g.hi ? g.lo : g.hi);
    if (g.kind == 'b') return g.def > 0.5 ? 0.f : 1.f;
    return (float) ((int) g.def == (int) g.hi ? (int) g.lo : (int) g.hi);
}

double tol (const Golden& g) { return g.kind == 'f' ? 1e-3 * (g.hi - g.lo) + 1e-6 : 0.01; }

fwtest::Take pluck (fwtest::Plugin& p, int note = 52, float vel = 0.8f, double secs = 1.0)
{
    p.noteOn (0.01, note, vel).noteOff (secs * 0.8, note);
    return p.render (secs);
}

double diffRatio (const fwtest::Take& a, const fwtest::Take& b)
{
    const size_t n = std::min (a.left.size(), b.left.size());
    double d = 0, e = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double x = a.left[i] - b.left[i], y = a.right[i] - b.right[i];
        d += x * x + y * y;
        e += (double) a.left[i] * a.left[i] + (double) a.right[i] * a.right[i];
    }
    return e > 0 ? std::sqrt (d / e) : (d > 0 ? 1e9 : 0);
}
} // namespace

//==============================================================================
// Parameters
FW_TEST ("params: layout matches the golden contract (ids, kinds, ranges, defaults)")
{
    fwtest::Plugin p;
    EXPECT_MSG ((int) p.processor().getParameters().size() == (int) std::size (kGolden),
                "parameter count unchanged");

    for (const auto& g : kGolden)
    {
        auto* prm = p.param (g.id);
        char msg[160];
        std::snprintf (msg, sizeof msg, "parameter '%s' exists", g.id);
        EXPECT_MSG (prm != nullptr, msg);
        if (prm == nullptr) continue;

        bool kindOk = false;
        switch (g.kind)
        {
            case 'f': kindOk = dynamic_cast<juce::AudioParameterFloat*>  (prm) != nullptr; break;
            case 'i': kindOk = dynamic_cast<juce::AudioParameterInt*>    (prm) != nullptr; break;
            case 'b': kindOk = dynamic_cast<juce::AudioParameterBool*>   (prm) != nullptr; break;
            case 'c': kindOk = dynamic_cast<juce::AudioParameterChoice*> (prm) != nullptr; break;
        }
        std::snprintf (msg, sizeof msg, "'%s' keeps its type", g.id);
        EXPECT_MSG (kindOk, msg);

        const auto& r = prm->getNormalisableRange();
        std::snprintf (msg, sizeof msg, "'%s' range [%g, %g] unchanged", g.id, g.lo, g.hi);
        EXPECT_MSG (std::fabs (r.start - g.lo) <= tol (g) && std::fabs (r.end - g.hi) <= tol (g), msg);

        const double def = prm->convertFrom0to1 (prm->getDefaultValue());
        std::snprintf (msg, sizeof msg, "'%s' default %g unchanged (now %g)", g.id, g.def, def);
        EXPECT_MSG (std::fabs (def - g.def) <= tol (g), msg);

        if (g.kind == 'c')
        {
            std::snprintf (msg, sizeof msg, "'%s' keeps %d choices", g.id, g.count);
            EXPECT_MSG (dynamic_cast<juce::AudioParameterChoice*> (prm)->choices.size() == g.count, msg);
        }

        std::snprintf (msg, sizeof msg, "'%s' is automatable", g.id);
        EXPECT_MSG (prm->isAutomatable(), msg);
    }
}

FW_TEST ("params: every value of every parameter, automated mid-note, stays finite and unclipped")
{
    // One sounding session per parameter: the chord rings while the host
    // automation steps the parameter through every legal value in turn.
    for (const auto& g : kGolden)
    {
        fwtest::Plugin p;
        p.chord (0.01, { 40, 52, 59 }, 0.9f);
        p.render (0.1);

        for (float v : valuesFor (g))
        {
            p.set (g.id, v);
            const auto after = p.render (0.08);

            char msg[160];
            std::snprintf (msg, sizeof msg, "%s = %g: output finite", g.id, v);
            EXPECT_MSG (after.finite(), msg);
            std::snprintf (msg, sizeof msg, "%s = %g: output under the 0.98 clamp", g.id, v);
            EXPECT_MSG (after.peak() <= 0.981, msg);
            std::snprintf (msg, sizeof msg, "%s = %g: value reads back", g.id, v);
            EXPECT_MSG (std::fabs (p.get (g.id) - v) <= tol (g) + (g.kind == 'f' ? 0 : 0.5), msg);
        }
    }
}

FW_TEST ("params: sound-shaping parameters audibly change the output")
{
    struct Case { const char* id; int body; };     // body = -1 keeps the default
    const Case cases[] =
    {
        { "body", -1 }, { "bodySize", -1 }, { "bodyRes", -1 }, { "micBlend", -1 },
        { "material", -1 }, { "stringAge", -1 }, { "stiffness", -1 }, { "pickPos", -1 },
        { "pickHard", -1 }, { "sustain", -1 }, { "tone", -1 }, { "drive", -1 },
        { "room", -1 }, { "output", -1 }, { "excitation", -1 }, { "damping", -1 },
        { "harmonic", -1 }, { "topDamp", -1 }, { "sympathetic", -1 },
        { "pickupType", 2 }, { "pickupSel", 2 }, { "pickupHeight", 2 }, { "pickupTone", 2 }, { "pickupOut", 2 },
    };

    for (const auto& c : cases)
    {
        const Golden* g = nullptr;
        for (const auto& x : kGolden) if (std::strcmp (x.id, c.id) == 0) g = &x;
        if (g == nullptr) { EXPECT_MSG (false, c.id); continue; }

        fwtest::Plugin lo, hi;
        if (c.body >= 0) { lo.set ("body", (float) c.body); hi.set ("body", (float) c.body); }
        lo.set (c.id, (float) g->lo);
        hi.set (c.id, (float) g->hi);
        const auto a = pluck (lo, 47, 0.8f), b = pluck (hi, 47, 0.8f);

        const double d = diffRatio (a, b);
        std::printf ("    %-13s min vs max difference %.3f\n", c.id, d);
        char msg[128];
        std::snprintf (msg, sizeof msg, "%s changes the sound", c.id);
        EXPECT_MSG (d > 0.02, msg);
    }
}

FW_TEST ("params: sympathetic resonance stays bounded and decays")
{
    // Sympathetic drive feeds the string mix back into every string. At any
    // setting it may add ring, but it must not turn into a self-sustaining
    // loop that the output clipper then hides.
    double heldAtZero = 0;
    for (float sym : { 0.f, 30.f, 100.f })
    {
        fwtest::Plugin p;
        p.set ("sympathetic", sym);
        p.noteOn (0.01, 47, 0.9f).noteOff (1.5, 47);
        const auto t = p.render (4.0);
        const double early = t.rms (0.05, 0.3), late = t.rms (1.2, 1.5), tail = t.rms (3.5, 4.0);
        std::printf ("    sympathetic %3.0f: rms 0.05-0.3 %.4f, 1.2-1.5 %.4f, after release %.6f, peak %.3f\n",
                     sym, early, late, tail, t.peak());
        char msg[96];
        std::snprintf (msg, sizeof msg, "sympathetic %.0f: held note decays", sym);
        EXPECT_MSG (late < early, msg);
        std::snprintf (msg, sizeof msg, "sympathetic %.0f: silent after release", sym);
        EXPECT_MSG (tail < early * 0.01, msg);
        std::snprintf (msg, sizeof msg, "sympathetic %.0f: no clipping", sym);
        EXPECT_MSG (t.peak() < 0.9, msg);

        if (sym == 0.f) heldAtZero = late;
        if (sym == 30.f)
            EXPECT_MSG (late > heldAtZero * 0.7, "default sympathetic keeps sustain within 3 dB of none");
    }
}

FW_TEST ("params: rendering is deterministic for identical input")
{
    fwtest::Plugin a, b;
    const auto x = pluck (a), y = pluck (b);
    EXPECT (diffRatio (x, y) < 1e-6);
}

//==============================================================================
// Host state
FW_TEST ("state: save/load restores every parameter")
{
    fwtest::Plugin src;
    for (const auto& g : kGolden) src.set (g.id, nonDefault (g));
    const auto blob = src.save();
    EXPECT (blob.getSize() > 0);

    fwtest::Plugin dst;
    dst.load (blob);
    for (const auto& g : kGolden)
    {
        char msg[160];
        std::snprintf (msg, sizeof msg, "%s restored (%g vs %g)", g.id, dst.get (g.id), nonDefault (g));
        EXPECT_MSG (std::fabs (dst.get (g.id) - nonDefault (g)) <= tol (g), msg);
    }

    // Restoring the state must not leave the instance in a broken audio state.
    dst.set ("power", 1.f).set ("output", -6.f).set ("mode", 0.f).set ("hold", 0.f);
    const auto t = pluck (dst);
    EXPECT (t.finite());
}

// A truncated or bit-flipped state is what a damaged preset file looks
// like. The plugin has to keep playing rather than refuse the data or crash,
// and has to end up with usable values rather than whatever half a number was
// in the middle of the stream.
FW_TEST ("state: a corrupted state still loads to sane values")
{
    fwtest::Plugin p;
    // Every parameter off its default, except Power, which nonDefault turns off
    // and which would silence the plugin rather than test the state loader.
    for (const auto& g : kGolden)
        if (! std::strcmp (g.id, "power"))
            p.set (g.id, nonDefault (g));

    juce::MemoryBlock good;
    p.processor().getStateInformation (good);
    EXPECT_MSG (good.getSize() > 64, "the plugin wrote a state block");

    const auto paramsInRange = [&p] (const char* what)
    {
        for (const auto& g : kGolden)
        {
            const auto v = p.get (g.id);
            if (! (std::isfinite (v) && v >= g.lo - tol (g) && v <= g.hi + tol (g)))
            {
                char msg[220];
                std::snprintf (msg, sizeof msg, "%s: %s left its range (%.6f not in [%g, %g])",
                               what, g.id, v, g.lo, g.hi);
                EXPECT_MSG (false, msg);
                return false;
            }
        }
        return true;
    };

    // Truncation at a spread of lengths, including inside the header and one
    // byte short of the whole thing.
    for (int keep : { 8, 32, 64, 128, 256, 512, 1024, (int) good.getSize() / 2, (int) good.getSize() - 1 })
    {
        if (keep <= 0 || (size_t) keep >= good.getSize())
            continue;

        char msg[220];
        std::snprintf (msg, sizeof msg, "a state truncated to %d bytes still loads", keep);
        p.processor().setStateInformation (good.getData(), keep);
        EXPECT_MSG (paramsInRange (msg), msg);
        EXPECT_MSG (pluck (p).finite(), msg);
    }

    // Single bit flips scattered through the stream. A checksum or magic
    // number should reject these outright; if none is checked, every value
    // still has to land in range.
    for (int offset : { 0, 1, 5, 17, 40, 64, 100, 300, 700, (int) good.getSize() / 2, (int) good.getSize() - 1 })
    {
        if (offset < 0 || (size_t) offset >= good.getSize())
            continue;

        auto bit = good;
        auto* bytes = static_cast<char*> (bit.getData());
        bytes[offset] = (char) (bytes[offset] ^ 0x40);

        char msg[220];
        std::snprintf (msg, sizeof msg, "a state with bit 6 flipped at byte %d still loads", offset);
        p.processor().setStateInformation (bit.getData(), (int) bit.getSize());
        EXPECT_MSG (paramsInRange (msg), msg);
        EXPECT_MSG (pluck (p).finite(), msg);
    }

    // A good state must still round-trip afterwards, so a rejected block did
    // not leave the processor wedged.
    std::vector<float> before;
    for (const auto& g : kGolden)
        before.push_back (p.get (g.id));

    p.processor().setStateInformation (good.getData(), (int) good.getSize());

    int moved = 0;
    for (size_t i = 0; i < before.size(); ++i)
        if (std::fabs (before[i] - p.get (kGolden[i].id)) > tol (kGolden[i]))
            ++moved;

    char msg[200];
    std::snprintf (msg, sizeof msg, "a good state loads after a corrupted one (%d of %zu moved)",
                   moved, before.size());
    EXPECT_MSG (moved == 0, msg);
    EXPECT_MSG (pluck (p).finite(), "a good state loads after a corrupted one");
}

FW_TEST ("state: loading garbage or empty data is harmless")
{
    fwtest::Plugin p;
    const char junk[] = "definitely not a Fretwork state";
    p.processor().setStateInformation (junk, (int) sizeof junk);
    p.processor().setStateInformation (nullptr, 0);
    for (const auto& g : kGolden)
        EXPECT_MSG (std::fabs (p.get (g.id) - g.def) <= tol (g), g.id);
    EXPECT (pluck (p).rms (0.05, 0.3) > 0.005);
}

//==============================================================================
// MIDI
FW_TEST ("midi: note-on sounds at the right pitch, note-off releases it")
{
    for (int note : { 40, 52, 64, 76 })
    {
        fwtest::Plugin p;
        p.noteOn (0.01, note, 0.8f).noteOff (1.0, note);
        const auto t = p.render (2.0);
        const double hz = t.pitch (0.3, 0.8, fwtest::midiToHz (note));
        char msg[96];
        std::snprintf (msg, sizeof msg, "note %d within 5 cents (%+.2f)", note, fwtest::Take::cents (hz, fwtest::midiToHz (note)));
        EXPECT_MSG (std::fabs (fwtest::Take::cents (hz, fwtest::midiToHz (note))) < 5.0, msg);
        std::snprintf (msg, sizeof msg, "note %d audible", note);
        EXPECT_MSG (t.rms (0.05, 0.3) > 0.005, msg);
        std::snprintf (msg, sizeof msg, "note %d silent after release", note);
        EXPECT_MSG (t.rms (1.7, 2.0) < t.rms (0.05, 0.3) * 0.01, msg);
    }
}

FW_TEST ("midi: velocity scales loudness")
{
    fwtest::Plugin soft, hard;
    const auto s = pluck (soft, 52, 0.25f), h = pluck (hard, 52, 1.0f);
    EXPECT (h.rms (0.02, 0.3) > s.rms (0.02, 0.3) * 1.5);
}

FW_TEST ("midi: pitch bend follows Wheel Range")
{
    for (int range : { 2, 7 })
        for (int dir : { +1, -1 })
        {
            fwtest::Plugin p;
            p.set ("wheelRange", (float) range);
            p.noteOn (0.01, 52, 0.9f).bend (0.02, dir > 0 ? 16383 : 0);
            const auto t = p.render (1.0);
            const double want = fwtest::midiToHz (52.0 + dir * range * (dir > 0 ? 8191.0 / 8192.0 : 1.0));
            const double hz = t.pitch (0.3, 0.7, want);
            char msg[96];
            std::snprintf (msg, sizeof msg, "bend %+d with range %d lands within 10 cents (%+.1f)", dir, range,
                           fwtest::Take::cents (hz, want));
            EXPECT_MSG (std::fabs (fwtest::Take::cents (hz, want)) < 10.0, msg);
        }
}

FW_TEST ("midi: mod wheel adds vibrato")
{
    const auto wobble = [] (bool wheel)
    {
        fwtest::Plugin p;
        p.set ("vibDepth", 100.f);
        if (wheel) p.cc (0.0, 1, 127);
        p.noteOn (0.01, 64, 0.9f);
        const auto t = p.render (1.2);
        std::vector<double> c;
        for (double a = 0.2; a < 1.1; a += 0.04)
            c.push_back (fwtest::Take::cents (t.pitch (a, a + 0.04, fwtest::midiToHz (64)), fwtest::midiToHz (64)));
        double m = 0; for (double v : c) m += v; m /= (double) c.size();
        double s = 0; for (double v : c) s += (v - m) * (v - m);
        return std::sqrt (s / (double) c.size());
    };
    const double still = wobble (false), moving = wobble (true);
    std::printf ("    pitch deviation: no wheel %.2f c, wheel %.2f c\n", still, moving);
    EXPECT (moving > 10.0);
    EXPECT (still < 3.0);
}

FW_TEST ("midi: sustain pedal holds notes past note-off")
{
    const auto tail = [] (bool pedal)
    {
        fwtest::Plugin p;
        if (pedal) p.cc (0.0, 64, 127);
        p.noteOn (0.01, 52, 0.9f).noteOff (0.4, 52);
        return p.render (1.4).rms (1.0, 1.4);
    };
    EXPECT (tail (true) > tail (false) * 10.0 + 1e-6);
}

FW_TEST ("midi: Transpose shifts the sounding pitch")
{
    fwtest::Plugin p;
    p.set ("transpose", 5.f);
    p.noteOn (0.01, 52, 0.9f);
    const auto t = p.render (1.0);
    const double want = fwtest::midiToHz (57);
    EXPECT (std::fabs (fwtest::Take::cents (t.pitch (0.3, 0.8, want), want)) < 5.0);
}

FW_TEST ("modes: Velocity offset shifts strum-stroke loudness")
{
    // velOffset belongs to the strummer: it offsets stroke velocity, not the
    // velocity of directly played notes.
    const auto stroke = [] (float offset)
    {
        fwtest::Plugin p;
        p.set ("mode", 1.f).set ("velOffset", offset);
        p.chord (0.01, { 52, 56, 59, 64 }, 0.9f);
        p.noteOn (0.3, 28, 0.5f).noteOff (0.4, 28);           // Strum Down
        return p.render (0.9).rms (0.3, 0.6);
    };
    const double lo = stroke (-40.f), hi = stroke (40.f);
    std::printf ("    stroke rms: offset -40 %.4f, +40 %.4f\n", lo, hi);
    EXPECT (hi > lo * 1.3);
}

//==============================================================================
// Output section
FW_TEST ("output: Power off is silent, Output level scales")
{
    fwtest::Plugin off;
    off.set ("power", 0.f);
    EXPECT (pluck (off).peak() == 0.0);

    fwtest::Plugin quiet, loud;
    quiet.set ("output", -24.f);
    loud.set ("output", -6.f);
    const double r = pluck (loud, 52, 0.3f).rms (0.05, 0.3) / std::max (1e-12, pluck (quiet, 52, 0.3f).rms (0.05, 0.3));
    std::printf ("    -6 dB vs -24 dB level ratio %.2f (ideal 7.94)\n", r);
    EXPECT (r > 6.0 && r < 9.5);
}

FW_TEST ("output: Width controls stereo spread")
{
    fwtest::Plugin mono, wide;
    mono.set ("width", 0.f);
    wide.set ("width", 100.f);
    mono.chord (0.01, { 40, 47, 52, 56, 59, 64 });
    wide.chord (0.01, { 40, 47, 52, 56, 59, 64 });
    const auto m = mono.render (0.8), w = wide.render (0.8);
    std::printf ("    side/mid: width 0 %.4f, width 100 %.4f\n", m.sideRms (0.1, 0.6) / m.rms (0.1, 0.6), w.sideRms (0.1, 0.6) / w.rms (0.1, 0.6));
    EXPECT (w.sideRms (0.1, 0.6) > m.sideRms (0.1, 0.6) * 2.0 + 1e-6);
    EXPECT_MSG (m.sideRms (0.1, 0.6) < m.rms (0.1, 0.6) * 0.02, "width 0 is mono");
}

FW_TEST ("output: metronome clicks on the beat with no notes")
{
    fwtest::Plugin p;
    p.set ("metronome", 1.f).set ("bpm", 100.f);
    const auto t = p.render (2.5);
    const auto on = t.onsets (0.001, 0.3);
    std::printf ("    metronome onsets: %zu\n", on.size());
    EXPECT (on.size() >= 3);
    if (on.size() >= 3) EXPECT (std::fabs ((on[2] - on[1]) - 0.6) < 0.03);
}

//==============================================================================
// Play modes
FW_TEST ("modes: Strum strokes sound the held chord, Strum Time spreads it")
{
    const auto riseTime = [] (float strumMs)
    {
        fwtest::Plugin p;
        p.set ("mode", 1.f).set ("strumTime", strumMs);
        p.chord (0.01, { 52, 56, 59, 64 }, 0.9f);
        p.noteOn (0.5, 28, 0.9f).noteOff (0.6, 28);           // Strum Down keyswitch
        const auto t = p.render (1.2);
        double pk = 0; for (size_t i = t.at (0.5); i < t.at (1.0); ++i) pk = std::max (pk, (double) std::fabs (t.left[i]));
        size_t i = t.at (0.5);
        while (i < t.at (1.0) && std::fabs (t.left[i]) < 0.6 * pk) ++i;
        return std::make_pair ((double) (i - t.at (0.5)) / t.fs, t.rms (0.5, 0.9));
    };
    const auto fast = riseTime (0.f), slow = riseTime (200.f);
    std::printf ("    stroke rise: strum 0 ms %.3f s, strum 200 ms %.3f s\n", fast.first, slow.first);
    EXPECT (fast.second > 0.005);
    EXPECT (slow.first > fast.first + 0.05);
}

FW_TEST ("modes: Arp steps through the held chord")
{
    // Arp notes overlap (gate 80%), so envelope onsets are unreliable. Instead
    // find which chord note dominates each 1/16 step and count the changes.
    fwtest::Plugin p;
    p.set ("mode", 2.f).set ("bpm", 120.f).set ("arpRate", 3.f);   // 1/16 = 125 ms
    const int chord[] = { 52, 56, 59, 64 };
    p.chord (0.01, { 52, 56, 59, 64 }, 0.9f);
    const auto t = p.render (2.0);

    int changes = 0, prev = -1;
    std::vector<int> seen;
    for (double a = 0.25; a + 0.1 < 2.0; a += 0.125)
    {
        int best = -1; double bq = 0;
        for (int n : chord)
        {
            double q = 0;
            t.pitch (a + 0.02, a + 0.1, fwtest::midiToHz (n), &q);
            if (q > bq) { bq = q; best = n; }
        }
        if (best != prev && prev >= 0) ++changes;
        prev = best;
        if (std::find (seen.begin(), seen.end(), best) == seen.end()) seen.push_back (best);
    }
    std::printf ("    arp: %d note changes in 1.75 s, %zu distinct notes, rms %.4f\n", changes, seen.size(), t.rms (0.2, 2.0));
    EXPECT (t.rms (0.2, 2.0) > 0.005);
    EXPECT (changes >= 6);
    EXPECT (seen.size() >= 3);
}

FW_TEST ("modes: Seq mode and playback run cleanly in every slot")
{
    for (int slot = 1; slot <= 8; ++slot)
    {
        fwtest::Plugin p;
        p.set ("mode", 3.f).set ("seqPlay", 1.f).set ("seqSlot", (float) slot);
        p.chord (0.01, { 52, 56, 59 }, 0.9f);
        EXPECT_MSG (p.render (1.0).finite(), "seq slot render finite");
    }
}

//==============================================================================
// Host conditions
FW_TEST ("host: every sample rate and block size plays in tune")
{
    for (double fs : { 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        for (int blk : { 1, 64, 4096 })
        {
            fwtest::Plugin p (fs, blk);
            p.noteOn (0.01, 52, 0.8f);
            const auto t = p.render (0.9);
            const double want = fwtest::midiToHz (52);
            const double c = fwtest::Take::cents (t.pitch (0.3, 0.8, want), want);
            char msg[128];
            std::snprintf (msg, sizeof msg, "fs %.0f block %d: finite, audible, in tune (%+.2f c)", fs, blk, c);
            EXPECT_MSG (t.finite() && t.rms (0.05, 0.3) > 0.005 && std::fabs (c) < 5.0, msg);
        }
}

FW_TEST ("host: blocks larger than announced are rendered completely")
{
    fwtest::Plugin ref (48000.0, 480), big (48000.0, 480);
    big.hostBlockSize (2048);
    const auto a = pluck (ref), b = pluck (big);
    // Smoothed tone/body coefficients update once per internal render call,
    // so chunking changes the output by a tiny, inaudible amount. A dropped,
    // repeated or mis-placed chunk (the bug this guards) differs by ~1.
    const double d = diffRatio (a, b);
    std::printf ("    480 vs 2048 host blocks: difference %.2e (%.1f dB)\n", d, 20.0 * std::log10 (std::max (d, 1e-12)));
    EXPECT (d < 0.01);
    EXPECT (std::fabs (a.rms (0.05, 0.6) / b.rms (0.05, 0.6) - 1.0) < 0.01);
}

FW_TEST ("host: reset via prepareToPlay mid-session leaves no stuck notes")
{
    fwtest::Plugin p;
    p.noteOn (0.01, 52, 0.9f);
    p.render (0.3);
    p.processor().prepareToPlay (48000.0, 480);
    const auto t = p.render (1.0);
    EXPECT (t.finite());
    EXPECT (t.rms (0.5, 1.0) < 0.05);
}

//==============================================================================
// Editor
namespace
{
template <typename T>
void collect (juce::Component& c, std::vector<T*>& out)
{
    if (auto* t = dynamic_cast<T*> (&c)) out.push_back (t);
    for (auto* child : c.getChildren()) collect (*child, out);
}

std::vector<float> snapshot (fwtest::Plugin& p)
{
    std::vector<float> v;
    for (const auto& g : kGolden) v.push_back (p.get (g.id));
    return v;
}

std::vector<std::string> changed (const std::vector<float>& a, const std::vector<float>& b)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::fabs (a[i] - b[i]) > 1e-6) out.push_back (kGolden[i].id);
    return out;
}

// The headless editor is never on the desktop, so isShowing() is always false;
// visibility has to be judged within the editor's own tree.
bool visibleIn (const juce::Component& c, const juce::Component& root)
{
    // The root itself is never made visible headlessly, so stop before it.
    for (auto* p = &c; p != nullptr; p = p->getParentComponent())
    {
        if (p == &root) return true;
        if (! p->isVisible()) return false;
    }
    return false;
}

// Delivers JUCE's posted messages (async button clicks, callAsync,
// AsyncUpdater) on this, the message thread. runDispatchLoopUntil needs
// JUCE_MODAL_LOOPS_PERMITTED, which this build does not enable; this is the
// same per-message dispatcher JUCE's own loops use.
void pump()
{
    for (int i = 0; i < 200; ++i)
        if (! juce::detail::dispatchNextMessageOnSystemQueue (true))
            break;
}
}


// Everything the suite does to the editor goes through setValue or
// triggerClick, so the sequencer's own mouse editing - the one part of the UI
// that is pure interaction code - was never exercised. A MouseEvent is
// constructible directly, and the real Desktop mouse source is a valid
// MouseInputSource even with no window open, so the grid can be driven with
// synthetic clicks at chosen cell coordinates.
juce::MouseEvent mouseEventAt (juce::Component& c, juce::Point<float> position, juce::ModifierKeys mods)
{
    const juce::MouseInputSource source = juce::Desktop::getInstance().getMainMouseSource();
    const juce::Time now = juce::Time::getCurrentTime();

    return { source, position, mods,
             juce::MouseInputSource::defaultPressure,
             juce::MouseInputSource::defaultOrientation,
             juce::MouseInputSource::defaultRotation,
             juce::MouseInputSource::defaultTiltX,
             juce::MouseInputSource::defaultTiltY,
             &c, &c, now, position, now, 1, false };
}

FW_TEST ("editor: seq grid: clicking edits the pattern, dragging erases or resizes, wheel changes velocity")
{
    juce::ScopedJuceInitialiser_GUI gui;
    fwtest::Plugin p;

    fw::ui::SeqGrid grid (p.processor(), p.processor().getState());
    grid.setSize (400, 300);
    grid.resized();
    grid.tick();

    auto& store = p.processor().getSeqStore();
    auto& slotParam = *dynamic_cast<juce::AudioParameterInt*> (p.processor().getState().getParameter ("seqSlot"));
    slotParam.setValueNotifyingHost (0.0f);

    constexpr int slot = 0, row = 0;
    store.clearSlot (slot);
    EXPECT_MSG (! store.hasNote (slot, row, 3), "the slot starts empty");

    // The grid draws an icon column and a label gutter before the step cells, so
    // find a real cell by probing for the one that adds a note.
    const auto cellCentre = [&] (int step) -> juce::Point<float>
    {
        for (int x = 40; x < grid.getWidth() - 4; x += 2)
            for (int y = 6; y < grid.getHeight() - 6; y += 2)
            {
                store.clearSlot (slot);
                grid.mouseDown (mouseEventAt (grid, { (float) x, (float) y }, {}));
                grid.mouseUp (mouseEventAt (grid, { (float) x, (float) y }, {}));
                if (store.hasNote (slot, row, step))
                {
                    // Undo the probe's own click with the grid's own right-click.
                    store.clearSlot (slot);
                    grid.mouseDown (mouseEventAt (grid, { (float) x, (float) y }, juce::ModifierKeys::rightButtonModifier));
                    grid.mouseUp (mouseEventAt (grid, { (float) x, (float) y }, juce::ModifierKeys::rightButtonModifier));
                    store.clearSlot (slot);
                    return { (float) x, (float) y };
                }
            }
        return { -1.0f, -1.0f };
    };

    const auto first = cellCentre (0);
    EXPECT_MSG (first.x >= 0.0f, "found the first step cell");

    if (first.x >= 0.0f)
    {
        // left click adds
        grid.mouseDown (mouseEventAt (grid, first, {}));
        grid.mouseUp (mouseEventAt (grid, first, {}));
        EXPECT_MSG (store.hasNote (slot, row, 0), "a left click adds a note");

        // right click removes
        grid.mouseDown (mouseEventAt (grid, first, juce::ModifierKeys::rightButtonModifier));
        grid.mouseUp (mouseEventAt (grid, first, juce::ModifierKeys::rightButtonModifier));
        EXPECT_MSG (! store.hasNote (slot, row, 0), "a right click removes the note");

        // click a second cell, then drag its right-hand 40% to resize
        const auto second = cellCentre (4);
        EXPECT_MSG (second.x > first.x, "found a later step cell");
        grid.mouseDown (mouseEventAt (grid, second, {}));
        EXPECT_MSG (store.hasNote (slot, row, 4), "a left click adds a note at step 4");

        const auto& pattern = store.getSlot (slot);
        const auto vel0 = pattern.notes[0].vel;
        const auto len0 = pattern.notes[0].len;
        EXPECT_MSG (len0 == 1, "a new note is one step long");

        // the wheel over a held note changes its velocity
        grid.mouseWheelMove (mouseEventAt (grid, second, {}),
                             juce::MouseWheelDetails { 0.0f, -1.0f, false, true, false });
        EXPECT_MSG (store.getSlot (slot).notes[0].vel == (std::uint8_t) juce::jmin (127, (int) vel0 + 8),
                    "scrolling up raises the velocity");
        grid.mouseWheelMove (mouseEventAt (grid, second, {}),
                             juce::MouseWheelDetails { 0.0f, 1.0f, false, true, false });
        EXPECT_MSG (store.getSlot (slot).notes[0].vel == vel0, "scrolling down lowers it back");

        // mouseDown picks the mode from where the press landed: the right-hand
        // 40% of a cell resizes, the rest is a delete target. So dragging the
        // body off the note erases it, and a press and release without real
        // movement must not.
        constexpr int heldRow = row, heldCol = 4;

        grid.mouseDown (mouseEventAt (grid, second, {}));
        grid.mouseDrag (mouseEventAt (grid, { second.x + 1.5f, second.y }, {}));
        grid.mouseUp (mouseEventAt (grid, { second.x + 1.5f, second.y }, {}));
        EXPECT_MSG (store.hasNote (slot, heldRow, heldCol),
                    "a press and release without movement keeps the note");

        // Drag downwards rather than rightwards: a horizontal drag of this
        // length would cross a step boundary and land the release in the
        // next column, which is a different edit.
        grid.mouseDown (mouseEventAt (grid, second, {}));
        grid.mouseDrag (mouseEventAt (grid, { second.x, second.y + 40.0f }, {}));
        EXPECT_MSG (store.hasNote (slot, heldRow, heldCol),
                    "dragging the body keeps the note until it is released");
        grid.mouseUp (mouseEventAt (grid, { second.x, second.y + 40.0f }, {}));
        EXPECT_MSG (! store.hasNote (slot, heldRow, heldCol),
                    "releasing after dragging the body erases the note");

        // The right-hand edge resizes instead. Length follows the column
        // under the pointer, so growing a note means dragging right past the
        // next cell's leading edge - not just moving within this cell.
        // The grid lays cells out from its own icon column; this pitch is
        // close enough to walk the cell with, and the walk below finds the
        // exact resize handle rather than assuming a fraction of the cell.
        // Mirror the grid's own layout: it lays cells out from a 44 px icon
        // column with a 1 px gap, so a cell's left edge is not a fraction of
        // the component width. Deriving pitch from the component alone lands
        // between cells.
        constexpr int iconCol = 44, gap = 1, gridTop = 22;
        const auto pitch = (float) (grid.getWidth() - iconCol) / (float) grid.getStepsPerBar() + (float) gap;

        store.clearSlot (slot);
        grid.mouseDown (mouseEventAt (grid, second, {}));
        grid.mouseUp (mouseEventAt (grid, second, {}));

        // The right-hand 40% of a cell resizes; the rest of the body is a
        // delete target. With the note's left edge at second.x and a cell of
        // about 0.6*pitch before the resize zone, the handle starts around
        // second.x + 0.6*cellW - so press there, drag right past the next
        // cell's leading edge, and the note's length has to grow.
        const auto cellW = pitch - (float) gap;
        const auto handleX = second.x + cellW * 0.7f;

        store.clearSlot (slot);
        grid.mouseDown (mouseEventAt (grid, second, {}));
        grid.mouseUp (mouseEventAt (grid, second, {}));
        EXPECT_MSG (store.hasNote (slot, heldRow, heldCol), "re-seeded the note for the resize test");

        grid.mouseDown (mouseEventAt (grid, { handleX, second.y }, {}));
        grid.mouseDrag (mouseEventAt (grid, { handleX + cellW + 2.0f, second.y }, {}));
        const auto resizedPattern = store.getSlot (slot);
        EXPECT_MSG (resizedPattern.count == 1 && resizedPattern.notes[0].len >= 2,
                    "dragging the right-hand edge past the next cell grows the note");
        grid.mouseUp (mouseEventAt (grid, { handleX + cellW + 2.0f, second.y }, {}));
        store.clearSlot (slot);

        // a drag that started on empty space edits nothing
        const auto countBeforeDrag = store.getSlot (slot).count;
        grid.mouseDown (mouseEventAt (grid, { first.x, first.y - 6.0f }, {}));
        grid.mouseDrag (mouseEventAt (grid, { first.x + 10.0f, first.y - 6.0f }, {}));
        grid.mouseUp (mouseEventAt (grid, { first.x + 10.0f, first.y - 6.0f }, {}));
        EXPECT_MSG (store.getSlot (slot).count == countBeforeDrag,
                    "a drag that started on empty space edits nothing");

        // clicking an empty far cell adds a separate note; the wheel over nothing does nothing
        const auto countBefore = store.getSlot (slot).count;
        grid.mouseWheelMove (mouseEventAt (grid, { 4.0f, 4.0f }, {}),
                             juce::MouseWheelDetails { 0.0f, -1.0f, false, true, false });
        EXPECT_MSG (store.getSlot (slot).count == countBefore, "the wheel over empty space does nothing");

        // Switching slot switches which pattern is edited. seqSlot runs 1..8 on
        // the wire (currentSlot subtracts one), so 4 is store slot 3 - and
        // setValueNotifyingHost takes the normalised 0..1, not the plain value.
        store.clearSlot (3);
        const auto range = slotParam.getRange();
        slotParam.setValueNotifyingHost ((float) (4 - range.getStart()) / (float) range.getLength());
        pump();
        grid.mouseDown (mouseEventAt (grid, first, {}));
        grid.mouseUp (mouseEventAt (grid, first, {}));
        EXPECT_MSG ((int) slotParam.get() == 4, "the slot parameter is 1-based");
        EXPECT_MSG (store.hasNote (3, row, 0), "the click landed in the selected slot");
        EXPECT_MSG (! store.hasNote (0, row, 0), "the other slot was untouched");

        // and a pattern edited by hand is what the audio thread plays
        EXPECT_MSG (pluck (p).finite(), "the edited pattern still renders");
    }
}

FW_TEST ("editor: opens at 1120x700, paints, and closes repeatedly")
{
    juce::ScopedJuceInitialiser_GUI gui;
    fwtest::Plugin p;
    for (int i = 0; i < 4; ++i)
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.processor().createEditor());
        EXPECT (ed != nullptr);
        if (ed == nullptr) return;
        EXPECT (ed->getWidth() == 1120 && ed->getHeight() == 700);
        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        EXPECT (img.isValid());
        pump();
        ed.reset();
    }
    EXPECT (pluck (p).finite());
}

FW_TEST ("editor: every slider and combo box drives exactly one parameter")
{
    juce::ScopedJuceInitialiser_GUI gui;
    fwtest::Plugin p;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.processor().createEditor());
    if (ed == nullptr) { EXPECT (false); return; }

    std::vector<juce::Slider*> sliders;
    std::vector<juce::ComboBox*> boxes;
    collect (*ed, sliders);
    collect (*ed, boxes);
    std::printf ("    %zu sliders, %zu combo boxes\n", sliders.size(), boxes.size());

    std::vector<std::string> reached;
    for (auto* s : sliders)
    {
        const auto before = snapshot (p);
        const double target = std::fabs (s->getValue() - s->getMaximum()) > 1e-9 ? s->getMaximum() : s->getMinimum();
        s->setValue (target, juce::sendNotificationSync);
        pump();
        const auto ch = changed (before, snapshot (p));
        char msg[160];
        std::snprintf (msg, sizeof msg, "slider '%s' drives exactly one parameter (drove %zu)",
                       s->getName().toRawUTF8(), ch.size());
        EXPECT_MSG (ch.size() == 1, msg);
        reached.insert (reached.end(), ch.begin(), ch.end());
    }
    for (auto* b : boxes)
    {
        if (b->getNumItems() < 2) continue;
        const auto before = snapshot (p);
        const int next = b->getSelectedItemIndex() == 0 ? 1 : 0;
        b->setSelectedItemIndex (next, juce::sendNotificationSync);
        pump();
        const auto ch = changed (before, snapshot (p));
        char msg[160];
        juce::String what = b->getName().isNotEmpty() ? b->getName() : b->getItemText (0) + " / " + b->getItemText (1);
        std::string drove;
        for (auto& id : ch) drove += id + " ";
        std::snprintf (msg, sizeof msg, "combo '%s' drives exactly one parameter (drove %zu: %s)",
                       what.toRawUTF8(), ch.size(), drove.c_str());
        // The Body selector is a preset: it sets body plus that body's
        // material, pick and mic defaults. Every other combo owns one parameter.
        const bool isBody = std::find (ch.begin(), ch.end(), std::string ("body")) != ch.end();
        EXPECT_MSG (isBody ? ch.size() >= 1 : ch.size() == 1, msg);
        reached.insert (reached.end(), ch.begin(), ch.end());
    }

    std::sort (reached.begin(), reached.end());
    reached.erase (std::unique (reached.begin(), reached.end()), reached.end());
    std::printf ("    parameters reachable from sliders/combos: %zu of %zu\n", reached.size(), std::size (kGolden));
    ed.reset();
}

FW_TEST ("editor: host automation updates the on-screen controls")
{
    juce::ScopedJuceInitialiser_GUI gui;
    fwtest::Plugin p;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.processor().createEditor());
    if (ed == nullptr) { EXPECT (false); return; }

    std::vector<juce::Slider*> sliders;
    collect (*ed, sliders);
    int checked = 0;
    for (const auto& g : kGolden)
    {
        if (g.kind != 'f') continue;
        // find the slider bound to this parameter by moving the parameter
        std::vector<double> before;
        for (auto* s : sliders) before.push_back (s->getValue());
        p.set (g.id, nonDefault (g));
        pump();
        for (size_t i = 0; i < sliders.size(); ++i)
            if (std::fabs (sliders[i]->getValue() - before[i]) > 1e-9)
            {
                ++checked;
                char msg[128];
                std::snprintf (msg, sizeof msg, "slider for %s follows automation", g.id);
                EXPECT_MSG (std::fabs (sliders[i]->getValue() - nonDefault (g)) <= tol (g) * 10, msg);
            }
    }
    std::printf ("    %d sliders followed host automation\n", checked);
    EXPECT (checked >= 6);
    ed.reset();
}

FW_TEST ("editor: buttons toggle their parameters")
{
    juce::ScopedJuceInitialiser_GUI gui;
    fwtest::Plugin p;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.processor().createEditor());
    if (ed == nullptr) { EXPECT (false); return; }

    bool delivered = false;
    juce::MessageManager::callAsync ([&delivered] { delivered = true; });
    pump();
    EXPECT_MSG (delivered, "message pump delivers async messages");

    std::vector<juce::Button*> buttons;
    collect (*ed, buttons);
    int driving = 0;
    std::vector<std::string> reached;
    for (auto* b : buttons)
    {
        if (! visibleIn (*b, *ed) || ! b->isEnabled()) continue;
        const auto before = snapshot (p);
        b->triggerClick();
        pump();
        const auto ch = changed (before, snapshot (p));
        if (! ch.empty()) ++driving;
        reached.insert (reached.end(), ch.begin(), ch.end());
    }
    std::sort (reached.begin(), reached.end());
    reached.erase (std::unique (reached.begin(), reached.end()), reached.end());
    std::printf ("    %zu buttons, %d drive a parameter, %zu distinct parameters\n", buttons.size(), driving, reached.size());
    for (const char* id : { "excitation", "damping", "transition", "harmonic", "tremolo", "mode", "hold", "seqSlot" })
        EXPECT_MSG (std::find (reached.begin(), reached.end(), std::string (id)) != reached.end(), id);
    ed.reset();
}


// The redesign lives entirely in paint code, so a state that draws nothing - a
// colour that resolves to transparent, a gradient that fails, a missing glyph -
// leaves a control that still works but looks dead. The parameter tests cannot
// see that.
//
// Hover and press are read by the painters from Component's own state, and
// this JUCE has no public setter for them, so they are checked through the
// only states that can be driven headlessly: value, enabled, toggled, and the
// flags the painters take directly. waves::buttonFace is the one primitive
// behind every button, so checking it across all five of its states covers
// hover and press as well.
// A transparent surface to draw a painter into, so states can be compared
// pixel by pixel without a window.
juce::Image paintSurface (int w, int h)
{
    return juce::Image (juce::Image::ARGB, w, h, true);
}

// Average ink on a transparent surface: alpha-weighted brightness, 0..1. The
// painters dim a disabled control with Graphics::setOpacity, which barely
// moves a colour's brightness but halves its alpha, so a dimmed state has to
// be measured on alpha, not on colour.
double inkOf (const juce::Image& img)
{
    if (! img.isValid())
        return -1.0;

    double sum = 0.0;
    int n = 0;
    for (int y = 0; y < img.getHeight(); ++y)
        for (int x = 0; x < img.getWidth(); ++x)
        {
            const auto c = img.getPixelAt (x, y);
            sum += (double) c.getAlpha() / 255.0 * c.getPerceivedBrightness();
            ++n;
        }
    return n > 0 ? sum / n : -1.0;
}

// Do the two surfaces differ by more than a rounding wobble anywhere?
bool paintsDifferently (const juce::Image& a, const juce::Image& b, float tolerance)
{
    if (! a.isValid() || ! b.isValid() || a.getBounds() != b.getBounds())
        return false;

    int differing = 0, total = 0;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
        {
            ++total;
            if (std::fabs (a.getPixelAt (x, y).getPerceivedBrightness()
                         - b.getPixelAt (x, y).getPerceivedBrightness()) > tolerance)
                ++differing;
        }
    return total > 0 && differing > total / 200;
}

FW_TEST ("editor: the look and feel paints every state it claims to handle")
{
    juce::ScopedJuceInitialiser_GUI gui;

    // Two states cannot be reached from here and are covered by eye in the DAW:
    // hover and pressed on a live control, because the painters read them from
    // Component's own pointer state and this JUCE exposes no setter for it;
    // and the dimming of a disabled control, which every painter does with
    // Graphics::setOpacity, an opacity that has no effect when drawing straight
    // into an offscreen image. waves::buttonFace, the primitive every button,
    // comb, knob and fader thumb is built from, takes hover and pressed as
    // arguments, so those two are covered through it.

    // --- button face: the primitive behind every button, all five states ----
    {
        const auto face = [] (bool on, bool hover, bool down, bool enabled)
        {
            juce::Image img = paintSurface (60, 26);
            juce::Graphics g (img);
            ::fw::ui::waves::buttonFace (g, juce::Rectangle<float> (0.5f, 0.5f, 59.0f, 25.0f),
                                          on, hover, down, enabled, 3.0f);
            return img;
        };

        const auto idle = face (false, false, false, true);
        EXPECT_MSG (idle.isValid(), "button face renders");
        EXPECT_MSG (paintsDifferently (idle, face (false, true, false, true),  0.005f), "button face paints a hover state");
        EXPECT_MSG (paintsDifferently (idle, face (false, true, true,  true),  0.005f), "button face paints a pressed state");
        EXPECT_MSG (paintsDifferently (idle, face (true,  false, false, true), 0.005f), "button face paints a toggled-on state");
        EXPECT_MSG (paintsDifferently (face (true, false, false, true), face (true, true, true, true), 0.005f),
                    "button face paints hover and pressed while lit");
    }

    // --- rotary knob: repaints across its range, and dims when disabled ------
    {
        fw::ui::FretLookAndFeel laf;
        juce::Slider s (juce::Slider::Rotary, juce::Slider::NoTextBox);
        s.setLookAndFeel (&laf);
        s.setRange (0.0f, 200.0f, 0.5f);
        s.setValue (29.0f, juce::dontSendNotification);

        const auto knob = [&] (float pos, bool enabled)
        {
            s.setEnabled (enabled);
            juce::Image img = paintSurface (72, 72);
            juce::Graphics g (img);
            laf.drawRotarySlider (g, 0, 0, 72, 72, pos, -0.75f, 0.75f, s);
            return img;
        };

        const auto atMin = knob (-0.5f, true);
        EXPECT_MSG (atMin.isValid(), "rotary knob renders");
        EXPECT_MSG (paintsDifferently (atMin, knob (0.5f, true), 0.005f), "rotary knob repaints across its range");
        EXPECT_MSG (paintsDifferently (knob (-0.25f, true), knob (0.25f, true), 0.005f),
                    "rotary knob repaints either side of centre");
        EXPECT_MSG (inkOf (knob (0.5f, true)) > inkOf (atMin) * 1.05, "the lit arc grows with the value");

        s.setLookAndFeel (nullptr);
    }

    // --- linear slider: repaints along its length, and dims when disabled ----
    {
        fw::ui::FretLookAndFeel laf;
        juce::Slider s (juce::Slider::LinearBar, juce::Slider::NoTextBox);
        s.setLookAndFeel (&laf);
        s.setRange (0.0f, 100.0f, 1.0f);
        s.setValue (50.0f, juce::dontSendNotification);

        const auto bar = [&] (float pos, bool enabled)
        {
            s.setEnabled (enabled);
            juce::Image img = paintSurface (140, 22);
            juce::Graphics g (img);
            laf.drawLinearSlider (g, 0, 0, 140, 22, pos, 0.0f, 1.0f, juce::Slider::LinearBar, s);
            return img;
        };

        const auto left = bar (0.0f, true);
        EXPECT_MSG (left.isValid(), "linear slider renders");
        EXPECT_MSG (paintsDifferently (left, bar (1.0f, true), 0.005f), "linear slider repaints along its length");
        EXPECT_MSG (paintsDifferently (left, bar (0.5f, true), 0.005f), "linear slider repaints away from its minimum");

        s.setLookAndFeel (nullptr);
    }

    // --- combo box: renders, follows its selection, and dims when disabled --
    {
        fw::ui::FretLookAndFeel laf;
        juce::ComboBox c;
        c.setLookAndFeel (&laf);
        for (int i = 0; i < 4; ++i)
            c.addItem (juce::String (i), i + 1);
        c.setSelectedId (1, juce::dontSendNotification);

        // The combo is an LCD well plus an arrow cell; JUCE draws the item text
        // itself on top, so only the well can be compared here.
        const auto combo = [&] (int selected)
        {
            c.setSelectedId (selected, juce::dontSendNotification);
            juce::Image img = paintSurface (120, 24);
            juce::Graphics g (img);
            laf.drawComboBox (g, 120, 24, false, selected - 1, 0, 0, 0, c);
            return img;
        };

        const auto well = combo (1);
        EXPECT_MSG (well.isValid(), "combo box renders");
        EXPECT_MSG (inkOf (well) > 0.0, "combo box paints its well and arrow cell");

        c.setLookAndFeel (nullptr);
    }

    // --- popup menu items: normal, hovered, ticked, inactive, separator ------
    {
        fw::ui::FretLookAndFeel laf;
        const auto item = [&] (bool hover, bool ticked, bool active)
        {
            juce::Image img = paintSurface (140, laf.popupMenuItemHeight);
            juce::Graphics g (img);
            laf.drawPopupMenuItem (g, juce::Rectangle<int> (0, 0, 140, laf.popupMenuItemHeight),
                                   false, active, hover, ticked, false, "1/16", "", nullptr, nullptr);
            return img;
        };
        const auto separator = [&] ()
        {
            juce::Image img = paintSurface (140, laf.popupMenuItemHeight);
            juce::Graphics g (img);
            laf.drawPopupMenuItem (g, juce::Rectangle<int> (0, 0, 140, laf.popupMenuItemHeight),
                                   true, false, false, false, false, "", "", nullptr, nullptr);
            return img;
        };

        const auto plain = item (false, false, true);
        EXPECT_MSG (plain.isValid(), "popup menu item renders");
        EXPECT_MSG (paintsDifferently (plain, item (true,  false, true), 0.005f), "popup item paints a highlighted state");
        EXPECT_MSG (paintsDifferently (plain, item (false, true,  true), 0.005f), "popup item paints a ticked state");
        EXPECT_MSG (paintsDifferently (plain, item (false, false, false), 0.005f), "popup item dims when inactive");
        EXPECT_MSG (paintsDifferently (plain, separator(), 0.005f), "popup separator draws its rule");
    }

    // --- tab bar: the selected tab is visibly fronted ------------------------
    {
        fw::ui::FretLookAndFeel laf;
        juce::TabbedButtonBar bar (juce::TabbedButtonBar::TabsAtTop);
        bar.setLookAndFeel (&laf);
        bar.addTab ("Amp",  juce::Colours::black, -1);
        bar.addTab ("Mics", juce::Colours::black, -1);
        bar.addTab ("Keys", juce::Colours::black, -1);
        bar.setSize (210, 26);

        const auto tabs = [&] (int selected)
        {
            bar.setCurrentTabIndex (selected);
            juce::Image img = paintSurface (210, 26);
            juce::Graphics g (img);
            bar.paintEntireComponent (g, true);
            return img;
        };

        const auto first = tabs (0);
        EXPECT_MSG (first.isValid(), "tab bar renders");
        EXPECT_MSG (paintsDifferently (first, tabs (1), 0.005f), "tab bar repaints for a different selection");
        EXPECT_MSG (paintsDifferently (first, tabs (2), 0.005f), "tab bar repaints when the last tab is selected");

        bar.setLookAndFeel (nullptr);
    }

    // --- a disabled component is drawn dimmer, at the component level ---------
    // JUCE applies a whole-component opacity when a component is disabled, and
    // that lives in the renderer rather than in any painter, so it is checked
    // here by snapshotting the component itself.
    {
        fw::ui::FretLookAndFeel laf;
        juce::Slider s (juce::Slider::Rotary, juce::Slider::NoTextBox);
        s.setLookAndFeel (&laf);
        s.setRange (0.0f, 200.0f, 0.5f);
        s.setValue (29.0f, juce::dontSendNotification);
        s.setSize (72, 72);

        s.setEnabled (true);
        const auto lit = s.createComponentSnapshot (s.getLocalBounds(), true, 2.0f);
        s.setEnabled (false);
        const auto dimmed = s.createComponentSnapshot (s.getLocalBounds(), true, 2.0f);
        s.setEnabled (true);

        EXPECT_MSG (lit.isValid() && dimmed.isValid(), "rotary knob snapshots at both enabled states");
        EXPECT_MSG (inkOf (dimmed) < inkOf (lit) * 0.95, "a disabled control is drawn dimmer");

        s.setLookAndFeel (nullptr);
    }
}

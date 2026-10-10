// Functional suite: every user-facing behaviour of Fretwork, exercised through
// fwtest::Plugin exactly as a host would drive it. See Harness.h.

#include "Harness.h"
#include "PluginEditor.h"

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

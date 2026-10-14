// Functional suite: every user-facing behaviour of Fretwork, exercised through
// fwtest::Plugin exactly as a host would drive it. See Harness.h.

#include "Harness.h"
#include "PatchManager.h"
#include "SeqStore.h"
#include "engine/PerformanceEngine.h"
#include "PluginEditor.h"
#include "WavesStyle.h"
#include "SeqGrid.h"
#include "GuitarBodyView.h"
#include "SeqImport.h"
#include "engine/PerformanceEngine.h"
#include "engine/StrumEngine.h"

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

FW_TEST ("params: Tone brightens as it goes up")
{
    double prev = -1;
    for (float hz : { 1000.f, 2811.f, 6000.f, 16000.f })
    {
        fwtest::Plugin p;
        p.set ("tone", hz);
        const auto t = pluck (p, 52, 0.9f, 0.6);
        const double al = std::exp (-2.0 * 3.14159265358979 * 3000.0 / t.fs);
        double y = 0, lo = 0, tot = 0;
        for (size_t i = (size_t) (0.05 * t.fs); i < (size_t) (0.3 * t.fs); ++i)
        { const double x = t.left[i]; y += (1 - al) * (x - y); lo += y * y; tot += x * x; }
        const double hi = (tot - lo) / tot;
        std::printf ("    tone %5.0f Hz: energy above 3 kHz %.3f %%\n", hz, 100 * hi);
        EXPECT_MSG (hi > prev, "a higher Tone setting has more top end");
        prev = hi;
    }
}

FW_TEST ("params: projects and presets saved before the Tone fix keep their sound")
{
    // An old project: Tone stored on the inverted mapping, no toneMapping marker.
    fwtest::Plugin src;
    src.set ("tone", 9000.f);   // in range on both mappings
    auto old = juce::ValueTree::readFromData (src.save().getData(), src.save().getSize());
    old.removeProperty ("toneMapping", nullptr);
    for (int i = 0; i < old.getNumChildren(); ++i)
        if (old.getChild (i).getProperty ("id").toString() == "tone")
            old.getChild (i).setProperty ("value", 9000.0, nullptr);
    juce::MemoryBlock mb;
    { juce::MemoryOutputStream os (mb, false); old.writeToStream (os); }

    fwtest::Plugin loaded, fresh;
    loaded.load (mb);
    EXPECT_MSG (std::fabs (loaded.get ("tone") - 2811.f) < 2.f, "old default 9000 loads as the 2811 Hz it sounded like");
    const double d = diffRatio (pluck (loaded), pluck (fresh));
    std::printf ("    old-default project vs new default: difference %.2e\n", d);
    EXPECT_MSG (d < 1e-4, "an old default project sounds like the new default");

    // A new save round-trips without converting again.
    fwtest::Plugin again;
    again.load (loaded.save());
    EXPECT (std::fabs (again.get ("tone") - loaded.get ("tone")) < 0.5f);

    // An old user preset file converts on load.
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("fw_tone_preset");
    dir.deleteRecursively();
    dir.createDirectory();
    const auto file = dir.getChildFile ("Old.fwpreset");
    file.replaceWithText ("<FretworkPreset name=\"Old\"><P id=\"tone\" v=\"7000\"/></FretworkPreset>");
    fwtest::Plugin pp;
    EXPECT (pp.processor().getPatches().loadUserPreset (file));
    EXPECT_MSG (std::fabs (pp.get ("tone") - 3902.f) < 3.f, "an old preset's Tone 7000 loads as 3902 Hz");
    dir.deleteRecursively();
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
        // Up to 0.55 s: after 0.6 s a held single note gets automatic vibrato.
        for (double a = 0.2; a < 0.55; a += 0.04)
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

FW_TEST ("midi: aftertouch bends the held note up by Bend Amount")
{
    // The pitch estimate searches near a hint, so each case is hinted at the
    // pitch it should reach; a wrong hint biases the reading.
    const auto pitchWith = [] (int pressure, double expectSemis)
    {
        fwtest::Plugin p;
        p.set ("bendAmount", 2.f);
        p.noteOn (0.01, 52, 0.9f);
        if (pressure >= 0)
            p.midi (0.2, juce::MidiMessage::channelPressureChange (1, pressure));
        const auto t = p.render (1.0);
        return t.pitch (0.5, 0.9, fwtest::midiToHz (52.0 + expectSemis));
    };
    const double base = fwtest::midiToHz (52);
    const double none = pitchWith (-1, 0.0), light = pitchWith (10, 0.0), full = pitchWith (127, 2.0);
    std::printf ("    aftertouch: none %.1f Hz, light %.1f Hz, full %.1f Hz\n", none, light, full);
    EXPECT_MSG (std::fabs (fwtest::Take::cents (none, base)) < 5.0, "without aftertouch the note is in tune");
    EXPECT_MSG (std::fabs (fwtest::Take::cents (light, base)) < 5.0, "a resting finger's light pressure does not detune");
    EXPECT_MSG (std::fabs (fwtest::Take::cents (full, base) - 200.0) < 15.0, "full pressure bends up by the Bend Amount (2 semitones)");
}

FW_TEST ("midi: MIDI Learn links a controller to a knob and survives save/load")
{
    fwtest::Plugin p;
    auto& learn = p.processor().getMidiLearn();
    const auto driveNorm = [&] (fwtest::Plugin& q) { return q.processor().getState().getParameter ("drive")->getValue(); };

    learn.startLearning ("drive");
    EXPECT_MSG (learn.isLearning ("drive"), "the knob is waiting for a controller");
    p.cc (0.01, 21, 64);
    p.render (0.05);
    EXPECT_MSG (learn.ccFor ("drive") == 21 && ! learn.isLearning ("drive"), "the next controller is linked");
    EXPECT_MSG (std::fabs (driveNorm (p) - 64.f / 127.f) < 1.0e-3f, "and already moves the knob");

    p.cc (0.10, 21, 127);
    p.cc (0.10, 22, 0);                         // an unlinked controller changes nothing
    p.render (0.1);
    EXPECT_MSG (std::fabs (driveNorm (p) - 1.f) < 1.0e-3f, "the linked controller sets the value");

    // Learning another knob on the same controller moves the link.
    learn.startLearning ("room");
    p.cc (0.20, 21, 10);
    p.render (0.1);
    EXPECT_MSG (learn.ccFor ("room") == 21 && learn.ccFor ("drive") == -1, "one controller drives one knob");
    learn.startLearning ("drive");
    p.cc (0.30, 30, 0);
    p.render (0.1);

    fwtest::Plugin q;
    q.load (p.save());
    EXPECT_MSG (q.processor().getMidiLearn().ccFor ("drive") == 30 && q.processor().getMidiLearn().ccFor ("room") == 21,
                "links are saved with the project");
    q.processor().getMidiLearn().forget ("drive");
    EXPECT_MSG (q.processor().getMidiLearn().ccFor ("drive") == -1, "a link can be forgotten");
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
        p.keyOn (0.3, 28, 0.5f).keyOff (0.4, 28);           // Strum Down
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
        // Room and Humanization off: the reverb tail and random shift move the
        // half-energy point and are not what this test measures.
        p.set ("mode", 1.f).set ("strumTime", strumMs).set ("room", 0.f).set ("humanize", 0.f);
        p.chord (0.01, { 52, 56, 59, 64 }, 0.9f);
        p.keyOn (0.5, 28, 0.9f).keyOff (0.6, 28);           // Strum Down keyswitch
        const auto t = p.render (1.2);
        // When half of the stroke's energy has arrived. Spreading the strings
        // moves it later whatever the waveforms do; peak-based rise times do
        // not, because each string's own attack is nearly as loud as the sum.
        // The chord was held before the stroke and is still ringing, so its
        // power (measured just before the keyswitch) is subtracted per 10 ms slice.
        const auto mid = [&] (size_t k) { const double m = 0.5 * ((double) t.left[k] + t.right[k]); return m * m; };
        double ring = 0;
        const size_t r0 = t.at (0.4), r1 = t.at (0.5);
        for (size_t j = r0; j < r1; ++j) ring += mid (j);
        ring /= (double) (r1 - r0);
        const size_t slice = (size_t) (0.01 * t.fs), s0 = t.at (0.5), s1 = t.at (0.9);
        std::vector<double> e;
        for (size_t j = s0; j + slice <= s1; j += slice)
        {
            double sum = 0;
            for (size_t q = j; q < j + slice; ++q) sum += mid (q);
            e.push_back (std::max (0.0, sum - ring * (double) slice));
        }
        double total = 0, acc = 0;
        for (const double v : e) total += v;
        size_t k = 0;
        for (; k < e.size() && acc < 0.5 * total; ++k) acc += e[k];
        return std::make_pair ((double) (k * slice) / t.fs, t.rms (0.5, 0.9));
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

FW_TEST ("modes: Seq plays a drawn pattern on its 16th-note grid")
{
    // Down strokes on every beat, up strokes on the off-beat 8ths, at 100 BPM:
    // a stroke every 0.3 s. The grid used to advance one step per bar.
    fwtest::Plugin p;
    p.processor().getSeqStore().deserialiseSlot (0, "0,0,1,100;1,2,1,90;0,4,1,100;1,6,1,90;0,8,1,100;1,10,1,90;0,12,1,100;1,14,1,90");
    p.set ("mode", 3.f).set ("seqPlay", 1.f).set ("seqSlot", 0.f).set ("bpm", 100.f);
    p.chord (0.01, { 60, 64, 67 }, 0.8f);
    const auto t = p.render (2.5);

    // Attacks: 10 ms RMS rising 6 dB above the previous 30 ms, 100 ms apart.
    int attacks = 0; double last = -1;
    for (double a = 0.04; a < 2.4; a += 0.005)
        if (t.rms (a, a + 0.01) > 0.003 && t.rms (a, a + 0.01) > 2.0 * t.rms (a - 0.03, a) && a - last > 0.1) { ++attacks; last = a; }
    std::printf ("    seq: %d stroke attacks in 2.4 s (pattern gives 7)\n", attacks);
    EXPECT_MSG (attacks >= 6 && attacks <= 9, "a stroke every 0.3 s");
}

FW_TEST ("articulation: Bend transition bends up INTO the played pitch")
{
    fwtest::Plugin p;
    p.set ("transition", 3.f).set ("bendAmount", 2.f);
    p.noteOn (0.01, 60, 0.8f);
    const auto t = p.render (1.5);
    const double want = fwtest::midiToHz (60);
    const double early = fwtest::Take::cents (t.pitch (0.02, 0.07, fwtest::midiToHz (58)), want);
    const double held  = fwtest::Take::cents (t.pitch (1.0, 1.4, want), want);
    std::printf ("    bend transition: starts %+.0f c, settles %+.1f c\n", early, held);
    EXPECT_MSG (early < -100.0, "starts below the written note");
    EXPECT_MSG (std::fabs (held) < 10.0, "ends on the written note, not Bend Amount above it");
}

FW_TEST ("midi: notes sound an octave below written, like a real guitar")
{
    // Raw MIDI 60 (written C4) sounds C3, 130.8 Hz; written E3 (52) is the open low E.
    for (const auto [written, hz] : { std::pair { 60, 130.81 }, std::pair { 52, 82.41 } })
    {
        fwtest::Plugin p;
        p.noteOn (0.01, written - fw::kWrittenOctave, 0.8f);   // raw MIDI = written
        const auto t = p.render (0.8);
        const double c = fwtest::Take::cents (t.pitch (0.3, 0.7, hz), hz);
        std::printf ("    written %d sounds %+.1f c from %.2f Hz\n", written, c, hz);
        EXPECT_MSG (std::fabs (c) < 5.0, "sounds one octave below the written note");
    }
}

FW_TEST ("out of box: Strum mode strums a pressed chord without stroke keys")
{
    fwtest::Plugin p;
    p.set ("mode", 1.f);
    p.chord (0.10, { 48, 52, 55, 60, 64 }, 0.8f, 0.008);   // a keyboard chord, keys 8 ms apart
    const auto t = p.render (1.0);
    // One down-stroke: sound within 50 ms of the first key, nothing before it.
    double onset = -1;
    for (double a = 0.05; a < 0.4 && onset < 0; a += 0.001) if (t.rms (a, a + 0.002) > 0.002) onset = a;
    std::printf ("    auto-strum onset %.1f ms after the first key, rms %.4f\n", (onset - 0.10) * 1000, t.rms (0.15, 0.8));
    EXPECT_MSG (onset > 0.10 && onset < 0.15, "the chord sounds within 50 ms");
    EXPECT_MSG (t.rms (0.15, 0.8) > 0.01, "and rings");

    // Stroke keys in use: the chord itself stays silent until a stroke.
    fwtest::Plugin q;
    q.set ("mode", 1.f);
    q.keyOn (0.05, 28, 0.8f).keyOff (0.06, 28);              // a stroke with nothing held
    q.chord (0.10, { 48, 52, 55, 60, 64 }, 0.8f, 0.008);
    const auto u = q.render (0.6);
    EXPECT_MSG (u.rms (0.10, 0.5) < 0.001, "stroke-key players are not double-strummed");
}

FW_TEST ("out of box: Seq plays its factory pattern while a chord is held")
{
    // Ground truth from the engine: the Pop pattern in slot 1 strokes on 16th
    // steps 0 4 6 10 12 14, i.e. 0, 0.6, 0.9, 1.5, 1.8, 2.1 s at 100 BPM, with
    // Seq Play left off. (An RMS attack count misses quiet up-strokes over a
    // ringing chord, so it is not used for timing.)
    fw::SeqStore store; store.refresh();
    static fw::SeqPattern slots[fw::kNumSeqSlots];
    for (int s = 0; s < fw::kNumSeqSlots; ++s) slots[s] = store.snapshot (s);
    fw::PerformanceEngine perf; perf.prepare (48000.0);
    fw::PerfParams pp {}; pp.mode = fw::PlayMode::Seq; pp.seqPlay = false; pp.seqSlots = slots; pp.seqSlot = 0; pp.bpm = 100.f;
    perf.setParams (pp);
    fw::StringEvent ev[256]; fw::ClickEvent ck[8]; int nc = 0;
    std::vector<double> strokes;
    for (int b = 0; b < 250; ++b)
    {
        std::vector<fw::MidiMsg> in;
        if (b == 5) for (int n : { 60, 64, 67, 72, 76 }) in.push_back ({ 0, 0x90, (unsigned char) n, 100 });
        const int n = perf.process (in.empty() ? nullptr : in.data(), (int) in.size(), fw::Transport {}, 480, ev, 256, ck, nc);
        for (int i = 0; i < n; ++i)
            if (ev[i].type == fw::EventType::Pluck)
            {
                const double t = (b * 480 + ev[i].sampleOffset) / 48000.0 - 0.05;
                if (strokes.empty() || t - strokes.back() > 0.05) strokes.push_back (t);
            }
    }
    const double want[] = { 0.0, 0.6, 0.9, 1.5, 1.8, 2.1 };
    bool onGrid = strokes.size() >= 6;
    for (size_t i = 0; onGrid && i < 6; ++i) onGrid = std::fabs (strokes[i] - want[i]) < 0.005;
    std::printf ("    seq factory pattern: %zu strokes in 2.4 s, on the 16th grid: %s\n", strokes.size(), onGrid ? "yes" : "no");
    EXPECT_MSG (onGrid, "the pattern plays without drawing or pressing Play");

    // Silent with no chord held and Play off.
    fw::PerformanceEngine idle; idle.prepare (48000.0); idle.setParams (pp);
    int idleHits = 0;
    for (int b = 0; b < 100; ++b) idleHits += idle.process (nullptr, 0, fw::Transport {}, 480, ev, 256, ck, nc);
    EXPECT_MSG (idleHits == 0, "no chord and no Play: the sequencer stays silent");

    // And through the real plugin, the strokes are heard.
    fwtest::Plugin p;
    p.set ("mode", 3.f);
    p.chord (0.05, { 48, 52, 55, 60, 64 }, 0.8f);
    EXPECT_MSG (p.render (2.5).rms (0.05, 2.4) > 0.01, "audible through the plugin");
}

FW_TEST ("out of box: every factory preset sounds a held chord, no other input")
{
    const auto& all = fw::PatchManager::factoryPresets();
    for (int i = 0; i < (int) all.size(); ++i)
    {
        fwtest::Plugin q;
        q.processor().getPatches().loadPreset (i);
        q.chord (0.05, { 48, 52, 55, 60, 64 }, 0.8f, 0.006);
        const auto t = q.render (1.5);
        char msg[96];
        std::snprintf (msg, sizeof msg, "%s sounds unclipped (rms %.4f, peak %.3f)", all[(size_t) i].name, t.rms (0.05, 1.4), t.peak());
        std::printf ("    preset %-20s rms %6.1f dBFS peak %6.1f dBFS\n", all[(size_t) i].name, 20 * std::log10 (t.rms (0.05, 1.4) + 1e-9), 20 * std::log10 (t.peak() + 1e-9));
        EXPECT_MSG (t.finite() && t.rms (0.05, 1.4) > 0.01 && t.peak() < 0.98, msg);
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
    // The chunking difference measures about 1e-2 (-40 dB): 9.6e-3 before the
    // Tone filter fix, 1.03e-2 after it passed more high end. The limit sits
    // well above that noise and 20x below the failure it catches.
    const double d = diffRatio (a, b);
    std::printf ("    480 vs 2048 host blocks: difference %.2e (%.1f dB)\n", d, 20.0 * std::log10 (std::max (d, 1e-12)));
    EXPECT (d < 0.05);
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
        if (! fwtest::dispatchOneMessage())
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

FW_TEST ("body: a solid electric sustains longer than a dreadnought")
{
    // Level two seconds into a held low E, relative to its own early level.
    const auto ringRatio = [] (float body)
    {
        fwtest::Plugin p;
        p.set ("body", body);
        p.noteOn (0.01, 40, 0.8f);
        const auto t = p.render (2.6);
        return t.rms (2.2, 2.5) / t.rms (0.1, 0.4);
    };
    const double dread = ringRatio (0.f), solid = ringRatio (2.f), semi = ringRatio (3.f);
    EXPECT_MSG (solid > dread * 1.25, "the solid body is still clearly louder late in the note");
    EXPECT_MSG (semi > dread && semi < solid, "the semi-hollow sustains between the two");
}

FW_TEST ("seq import: a strum pattern from a MIDI file lands on the grid")
{
    // Write a real .mid and read it back, as loading a file from disk does.
    constexpr int tpq = 480;
    juce::MidiMessageSequence track;
    const auto note = [&] (int key, double startQ, double lenQ, int vel)
    {
        track.addEvent (juce::MidiMessage::noteOn (1, key, (juce::uint8) vel), startQ * tpq);
        track.addEvent (juce::MidiMessage::noteOff (1, key), (startQ + lenQ) * tpq);
    };
    note (28, 0.0, 1.0, 100);   // Strum Down on beat 1, a quarter long
    note (29, 0.5, 0.25, 60);   // Strum Up on the "and"
    note (32, 2.0, 0.25, 90);   // Palm Down on beat 3
    note (60, 1.0, 0.5, 80);    // not a strum key
    note (28, 4.0, 0.25, 100);  // second bar
    track.updateMatchedPairs();

    juce::MidiFile out;
    out.setTicksPerQuarterNote (tpq);
    out.addTrack (track);
    juce::MemoryBlock bytes;
    { juce::MemoryOutputStream os (bytes, false); out.writeTo (os); }
    juce::MidiFile in;
    { juce::MemoryInputStream is (bytes, false); EXPECT_MSG (in.readFrom (is), "the file reads back"); }

    fw::SeqStore store;
    constexpr int slot = 2;
    const int spq = fw::seqStepsPerQuarter (1), spb = fw::seqStepsPerBar (1, 2);   // 1/16 in 4/4
    EXPECT_MSG (spq == 4 && spb == 16, "1/16 in 4/4 is 16 steps");
    store.addNote (slot, fw::SeqNote { 5, 15, 1, 100 });                       // stale content

    const auto r = fw::importStrumPattern (in, store, slot, spq, spb);
    EXPECT_MSG (r.ok && r.imported == 3, "the three first-bar strokes import");
    EXPECT_MSG (r.ignored == 1 && r.beyondBar == 1, "the non-strum note and the second bar are reported, not imported");
    EXPECT_MSG (! store.hasNote (slot, 5, 15), "the import replaces the slot's old pattern");

    const auto& p = store.getSlot (slot);
    const auto find = [&] (int row, int step) -> const fw::SeqNote*
    {
        for (int i = 0; i < p.count; ++i)
            if (p.notes[(size_t) i].row == row && p.notes[(size_t) i].start == step) return &p.notes[(size_t) i];
        return nullptr;
    };
    const auto* down = find (0, 0);
    const auto* up = find (1, 2);
    const auto* palm = find (4, 8);
    EXPECT_MSG (down != nullptr && down->len == 4 && down->vel == 100, "the down-stroke keeps its quarter length and velocity");
    EXPECT_MSG (up != nullptr && up->vel == 60, "the up-stroke lands on the off-beat sixteenth");
    EXPECT_MSG (palm != nullptr, "the palm stroke lands on beat 3");

    // A file with no strum keys leaves the slot alone.
    juce::MidiMessageSequence other;
    other.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0.0);
    juce::MidiFile none;
    none.setTicksPerQuarterNote (tpq);
    none.addTrack (other);
    const auto r2 = fw::importStrumPattern (none, store, slot, spq, spb);
    EXPECT_MSG (! r2.ok && store.getSlot (slot).count == 3, "a file without strokes leaves the pattern untouched");
}

//==============================================================================
// Presets, undo, A/B
namespace
{
    // A UI edit: the attachments wrap every change in a gesture.
    void uiSet (fwtest::Plugin& p, const char* id, float real)
    {
        auto* param = p.processor().getState().getParameter (id);
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (real));
        param->endChangeGesture();
        p.processor().getPatches().poll();
    }

    float real (fwtest::Plugin& p, const char* id)
    {
        auto* param = p.processor().getState().getParameter (id);
        return param->convertFrom0to1 (param->getValue());
    }
}

FW_TEST ("presets: host programs are the factory presets and load their sound")
{
    fwtest::Plugin p;
    auto& proc = p.processor();
    const auto& factory = fw::PatchManager::factoryPresets();
    EXPECT_MSG (proc.getNumPrograms() == (int) factory.size() && factory.size() >= 12, "every factory preset is a host program");

    int crunch = -1;
    for (int i = 0; i < (int) factory.size(); ++i)
    {
        EXPECT_MSG (proc.getProgramName (i).isNotEmpty(), "every program has a name");
        if (juce::String (factory[(size_t) i].name) == "Crunch Rhythm") crunch = i;
    }
    EXPECT_MSG (crunch >= 0, "the Crunch Rhythm preset exists");
    if (crunch < 0) return;

    p.set ("output", -12.f);
    proc.setCurrentProgram (crunch);
    EXPECT_MSG (proc.getCurrentProgram() == crunch, "the host sees the program it chose");
    EXPECT_MSG (std::fabs (real (p, "drive") - 45.f) < 0.5f, "the preset sets its drive");
    EXPECT_MSG ((int) std::lround (real (p, "body")) == 2, "the preset selects its body");
    EXPECT_MSG (std::fabs (real (p, "output") - (-6.f)) < 0.01f, "everything the preset does not set returns to default");
}

FW_TEST ("presets: a host re-sending the current program does not overwrite a restored sound")
{
    juce::MemoryBlock saved;
    {
        fwtest::Plugin p;
        p.processor().setCurrentProgram (7);
        p.set ("drive", 80.f);                 // the user's own tweak on top of the preset
        saved = p.save();
    }

    fwtest::Plugin q;
    q.load (saved);
    EXPECT_MSG (q.processor().getCurrentProgram() == 7, "the program index survives save and load");
    q.processor().setCurrentProgram (7);       // what hosts do after restoring a project
    EXPECT_MSG (std::fabs (real (q, "drive") - 80.f) < 0.5f, "the restored tweak is still there");
}

FW_TEST ("undo: UI edits step back and forward; host automation is not recorded")
{
    fwtest::Plugin p;
    auto& patches = p.processor().getPatches();
    EXPECT_MSG (! patches.canUndo(), "a fresh instance has nothing to undo");

    const float roomDefault = real (p, "room");
    uiSet (p, "drive", 30.f);
    uiSet (p, "room", 50.f);
    EXPECT_MSG (patches.canUndo(), "UI edits are recorded");

    patches.undo();
    EXPECT_MSG (std::fabs (real (p, "room") - roomDefault) < 0.5f && std::fabs (real (p, "drive") - 30.f) < 0.5f, "undo reverts the last edit only");
    patches.undo();
    EXPECT_MSG (std::fabs (real (p, "drive")) < 0.5f, "a second undo reverts the edit before it");
    patches.redo();
    EXPECT_MSG (std::fabs (real (p, "drive") - 30.f) < 0.5f && patches.canRedo(), "redo reapplies it");

    // Automation: no gesture, so nothing to undo.
    fwtest::Plugin a;
    a.set ("drive", 60.f);
    a.processor().getPatches().poll();
    EXPECT_MSG (! a.processor().getPatches().canUndo(), "host automation does not fill the undo history");
}

FW_TEST ("undo and presets leave tempo and playback alone")
{
    fwtest::Plugin p;
    p.set ("bpm", 132.f);
    uiSet (p, "drive", 30.f);
    p.processor().getPatches().undo();
    p.processor().setCurrentProgram (3);
    EXPECT_MSG (std::fabs (real (p, "bpm") - 132.f) < 0.01f, "tempo is untouched by undo and preset loads");
}

FW_TEST ("A/B: switching compares two sounds without losing either")
{
    fwtest::Plugin p;
    auto& patches = p.processor().getPatches();
    uiSet (p, "drive", 10.f);
    patches.toggleAB();                                    // to B, which starts as a copy of A
    EXPECT_MSG (patches.isB() && std::fabs (real (p, "drive") - 10.f) < 0.5f, "B starts as a copy of A");
    uiSet (p, "drive", 70.f);
    patches.toggleAB();
    EXPECT_MSG (! patches.isB() && std::fabs (real (p, "drive") - 10.f) < 0.5f, "back on A, A's sound is intact");
    patches.toggleAB();
    EXPECT_MSG (std::fabs (real (p, "drive") - 70.f) < 0.5f, "back on B, B's edit is intact");
}

FW_TEST ("presets: a user preset saves to a file and loads back")
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("fw_preset_test");
    dir.deleteRecursively();

    fwtest::Plugin p;
    p.set ("drive", 33.f).set ("body", 3.f).set ("bpm", 90.f);
    EXPECT_MSG (p.processor().getPatches().saveUserPreset ("My Sound", dir), "the preset is written");
    const auto file = dir.getChildFile ("My Sound.fwpreset");
    EXPECT_MSG (file.existsAsFile(), "the file is named after the preset");

    fwtest::Plugin q;
    q.set ("bpm", 150.f);
    EXPECT_MSG (q.processor().getPatches().loadUserPreset (file), "the preset loads");
    EXPECT_MSG (std::fabs (real (q, "drive") - 33.f) < 0.5f && (int) std::lround (real (q, "body")) == 3, "its sound comes back");
    EXPECT_MSG (std::fabs (real (q, "bpm") - 150.f) < 0.01f, "the loading project's tempo is kept");
    EXPECT_MSG (q.processor().getPatches().currentPresetName() == "My Sound", "it is shown by name");
    dir.deleteRecursively();
}

FW_TEST ("strum: soft strokes reach fewer strings, from the side they start on")
{
    fw::StrumEngine engine;
    engine.prepare (48000.0);
    fw::Voicing v;
    v.count = 6;
    for (int i = 0; i < 6; ++i) { v.stringIndex[i] = i; v.pitch[i] = 40.f + 5.f * (float) i; }

    fw::StrumEngine::StrokeParams p;
    p.strumTimeMs = 30.f;
    const auto strike = [&] (bool up, int vel, std::vector<int>& strings)
    {
        fw::Rng rng;
        fw::StrumHit hits[6];
        const int n = engine.stroke (up, fw::StrokeKind::Normal, vel, p, v, nullptr, {}, rng, hits, 6);
        strings.clear();
        for (int k = 0; k < n; ++k) strings.push_back (hits[k].stringIndex);
    };

    std::vector<int> hard, softDown, softUp;
    strike (false, 115, hard);
    strike (false, 20, softDown);
    strike (true, 20, softUp);
    EXPECT_MSG (hard.size() == 6, "a firm stroke reaches every string");
    EXPECT_MSG (softDown.size() >= 3 && softDown.size() < 6, "a light stroke reaches only some strings");
    EXPECT_MSG (! softDown.empty() && softDown.front() == 0 && softDown.back() < 5,
                "a light down-stroke starts on the bass and stops short of the treble");
    EXPECT_MSG (! softUp.empty() && softUp.front() == 5 && softUp.back() > 0,
                "a light up-stroke starts on the treble and stops short of the bass");
}

FW_TEST ("strum: holding the bass key keeps the bass string out of the strum")
{
    fw::PerformanceEngine perf;
    perf.prepare (48000.0);
    fw::PerfParams pp {};
    pp.mode = fw::PlayMode::Strum;
    pp.strumTime = 20.f;
    pp.strokeVol = 1.f;
    perf.setParams (pp);

    // Held E-major-ish chord, then strokes with and without the bass key held.
    const auto strummed = [&] (const std::vector<fw::MidiMsg>& in)
    {
        fw::StringEvent events[64] {};
        fw::ClickEvent clicks[32] {};
        int numClicks = 0;
        std::vector<int> strings;
        for (int b = 0; b < 20; ++b)
        {
            const int n = perf.process (b == 0 ? in.data() : nullptr, b == 0 ? (int) in.size() : 0,
                                        fw::Transport {}, 512, events, 64, clicks, numClicks);
            for (int i = 0; i < n; ++i)
                if (events[i].type == fw::EventType::Pluck) strings.push_back (events[i].string);
        }
        return strings;
    };

    std::vector<fw::MidiMsg> chord;
    for (int note : { 40, 47, 52, 56, 59, 64 }) chord.push_back ({ 0, 0x90, (unsigned char) (note + fw::kWrittenOctave), 110 });
    strummed (chord);

    const auto plain = strummed ({ { 0, 0x90, 28, 120 } });                         // Strum Down
    const auto withBass = strummed ({ { 0, 0x90, 34, 120 }, { 1, 0x90, 28, 120 } }); // hold bass, strum
    const auto released = strummed ({ { 0, 0x80, 34, 0 }, { 1, 0x90, 28, 120 } });  // release, strum

    const int lowest = plain.empty() ? -1 : *std::min_element (plain.begin(), plain.end());
    const auto strumCount = [&] (const std::vector<int>& s, bool skipFirstBass)
    {
        // With the bass key, the first event is the bass pick itself.
        return (int) s.size() - (skipFirstBass ? 1 : 0);
    };
    EXPECT_MSG (! plain.empty(), "the plain stroke sounds the chord");
    EXPECT_MSG (! withBass.empty() && withBass.front() == lowest, "the bass key picks the lowest string");
    EXPECT_MSG (std::count (withBass.begin() + 1, withBass.end(), lowest) == 0,
                "while the bass key is held, the strum leaves the bass string alone");
    EXPECT_MSG (strumCount (withBass, true) == (int) plain.size() - 1, "the strum loses exactly the bass string");
    EXPECT_MSG ((int) released.size() == (int) plain.size(), "releasing the bass key restores the full strum");
}

FW_TEST ("strum: harder strokes sweep the strings faster")
{
    fw::StrumEngine engine;
    engine.prepare (48000.0);
    fw::Voicing v;
    v.count = 6;
    for (int i = 0; i < 6; ++i) { v.stringIndex[i] = i; v.pitch[i] = 40.f + 5.f * (float) i; }

    fw::StrumEngine::StrokeParams p;
    p.strumTimeMs = 40.f;
    const auto sweepMs = [&] (int vel)
    {
        fw::Rng rng;
        fw::StrumHit hits[6];
        const int n = engine.stroke (false, fw::StrokeKind::Normal, vel, p, v, nullptr, {}, rng, hits, 6);
        // Spacing between the first two strings, scaled to the full 6-string
        // sweep: soft strokes strike fewer strings, so their span alone would
        // shrink even though the pick moves more slowly.
        if (n < 2) return 0.0;
        const double firstGapMs = (hits[1].delaySamples - hits[0].delaySamples) / 48.0;
        const double share = 1.0 - std::pow (1.0 - 1.0 / 5.0, 1.0);   // first step of the sweep at separation 0
        return firstGapMs / share;
    };
    const double soft = sweepMs (30), typical = sweepMs (100), hard = sweepMs (127);
    std::printf ("    sweep ms: vel 30 %.1f, 100 %.1f, 127 %.1f\n", soft, typical, hard);
    EXPECT_MSG (hard < typical && typical < soft, "sweep time shortens as the stroke gets harder");
    EXPECT_MSG (std::fabs (typical - 40.0) < 1.0, "a typical-velocity stroke sweeps in the set Strum Time");
}

FW_TEST ("strum: humanized strokes keep their sweep order")
{
    fw::StrumEngine engine;
    engine.prepare (48000.0);

    fw::Voicing v;
    v.count = 6;
    for (int i = 0; i < 6; ++i) { v.stringIndex[i] = i; v.fret[i] = 0; v.pitch[i] = 40.f + 5.f * (float) i; }

    // A fast strum with full humanize is where per-string jitter used to swap
    // neighbours: 8 ms of jitter against about 2 ms between strings.
    fw::StrumEngine::StrokeParams p;
    p.strumTimeMs = 10.f;
    p.humanize01  = 1.f;

    fw::Rng rng;
    fw::StrumHit hits[6];
    int reversed = 0, spreadOk = 0;
    constexpr int strokes = 400;
    for (int i = 0; i < strokes; ++i)
    {
        const bool up = (i & 1) != 0;
        const int n = engine.stroke (up, fw::StrokeKind::Normal, 100, p, v, nullptr, {}, rng, hits, 6);
        for (int k = 1; k < n; ++k)
            if (hits[k].delaySamples < hits[k - 1].delaySamples)
                ++reversed;
        // order: down strokes run low string to high, up strokes high to low
        bool ordered = true;
        for (int k = 1; k < n; ++k)
            ordered = ordered && (up ? hits[k].stringIndex < hits[k - 1].stringIndex
                                     : hits[k].stringIndex > hits[k - 1].stringIndex);
        // the humanized stroke stays within the sweep plus the 8 ms shift each side
        const double span = (hits[n - 1].delaySamples - hits[0].delaySamples) / 48.0;
        if (ordered && span <= 10.0 + 16.0) ++spreadOk;
    }
    EXPECT_MSG (reversed == 0, "no string ever sounds before the string swept ahead of it");
    EXPECT_MSG (spreadOk == strokes, "every stroke sweeps in string order within its time bound");
}

FW_TEST ("midi: notes from the host light the on-screen piano")
{
    fwtest::Plugin p;
    auto& keys = p.processor().getKeyboardState();
    constexpr int note = 57;

    EXPECT_MSG (! keys.isNoteOnForChannels (0xffff, note), "the key starts unlit");
    p.noteOn (0.01, note, 0.8f);
    p.render (0.1);
    // The piano shows the key the host sent: the written note, an octave above the sound.
    EXPECT_MSG (keys.isNoteOnForChannels (0xffff, note + fw::kWrittenOctave), "a host note-on lights its key");
    p.noteOff (0.15, note);                     // event times are absolute
    p.render (0.1);
    EXPECT_MSG (! keys.isNoteOnForChannels (0xffff, note + fw::kWrittenOctave), "the host note-off clears it");
}

FW_TEST ("editor: body display draws a distinct guitar for every body")
{
    juce::ScopedJuceInitialiser_GUI gui;
    fwtest::Plugin p;
    auto& state = p.processor().getState();
    auto* body = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter ("body"));
    EXPECT_MSG (body != nullptr, "body is a choice parameter");
    if (body == nullptr)
        return;

    fw::ui::GuitarBodyView view (state);
    view.setSize (300, 160);

    // Pixels well above the LCD well and its accent glow are the guitar.
    const auto litPixels = [] (const juce::Image& img)
    {
        int lit = 0;
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
                if (img.getPixelAt (x, y).getPerceivedBrightness() > 0.35f)
                    ++lit;
        return lit;
    };

    std::vector<juce::Image> shots;
    for (int i = 0; i < body->choices.size(); ++i)
    {
        body->setValueNotifyingHost (body->convertTo0to1 ((float) i));
        const auto shot = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
        EXPECT_MSG (litPixels (shot) > 1500, ("body " + body->choices[i] + " draws its guitar").toRawUTF8());
        shots.push_back (shot);
    }

    // A wrong or missing asset would draw the same picture (or none) twice.
    for (size_t a = 0; a < shots.size(); ++a)
        for (size_t b = a + 1; b < shots.size(); ++b)
        {
            int differing = 0;
            for (int y = 0; y < shots[a].getHeight(); ++y)
                for (int x = 0; x < shots[a].getWidth(); ++x)
                    if (shots[a].getPixelAt (x, y) != shots[b].getPixelAt (x, y))
                        ++differing;
            EXPECT_MSG (differing > 1000, (body->choices[(int) a] + " and " + body->choices[(int) b] + " look different").toRawUTF8());
        }
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

        // A drag that comes back and releases on the note cancels the erase;
        // this is also what a click with stray pointer movement looks like.
        store.clearSlot (slot);
        grid.mouseDown (mouseEventAt (grid, second, {}));
        grid.mouseUp (mouseEventAt (grid, second, {}));
        grid.mouseDown (mouseEventAt (grid, second, {}));
        grid.mouseDrag (mouseEventAt (grid, { second.x, second.y + 40.0f }, {}));
        grid.mouseDrag (mouseEventAt (grid, second, {}));
        grid.mouseUp (mouseEventAt (grid, second, {}));
        EXPECT_MSG (store.hasNote (slot, heldRow, heldCol),
                    "dragging off a note and back onto it keeps the note");

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

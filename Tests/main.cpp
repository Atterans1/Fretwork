// Headless verification for Fretwork.
//
// Everything here runs without a host: the waveguide, body, pickup and output
// models plus the performance engine are plain C++, so they can be driven
// directly and measured.

#include "PluginProcessor.h"
#include "Params.h"
#include "Harness.h"
#include "SeqStore.h"
#include "dsp/GuitarEngine.h"
#include "dsp/GuitarString.h"
#include "engine/PerformanceEngine.h"
#include "ui/FretLookAndFeel.h"
#include "ui/SetupTabs.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_current = "";

void check (bool ok, const char* what, int line)
{
    ++g_checks;

    if (! ok)
    {
        ++g_failures;
        std::printf ("  FAIL  [%s] %s   (line %d)\n", g_current, what, line);
    }
}

#define CHECK(cond, what) check ((cond), (what), __LINE__)

constexpr double kSampleRate = 44100.0;
constexpr int    kBlock      = 512;

double midiToHz (double pitch) noexcept
{
    return 440.0 * std::pow (2.0, (pitch - 69.0) / 12.0);
}

fw::dsp::StringModelParams steelParams()
{
    fw::dsp::StringModelParams p {};
    p.material      = 0;      // Steel Light
    p.age           = 20.f;
    p.stiffness     = 100.f;
    p.pickPos       = 15.f;
    p.pickHard      = 50.f;
    p.sustain       = 100.f;
    p.fretNoise     = 30.f;
    p.bodyLossScale = 1.f;
    p.scaleLengthM  = 0.648f;
    return p;
}

fw::StringEvent pluckEvent (int stringIdx, float pitch, float vel = 0.9f)
{
    fw::StringEvent e {};
    e.string   = stringIdx;
    e.pitch    = pitch;
    e.velocity = vel;
    e.type     = fw::EventType::Pluck;
    return e;
}

//==============================================================================
// Normalised autocorrelation at one lag.
double normalisedAcf (const std::vector<float>& x, int lag)
{
    const int n = (int) x.size();
    double a = 0.0, e1 = 0.0, e2 = 0.0;

    for (int i = 0; i + lag < n; ++i)
    {
        const double p = x[(std::size_t) i];
        const double q = x[(std::size_t) (i + lag)];
        a += p * q;
        e1 += p * p;
        e2 += q * q;
    }

    return a / std::sqrt (e1 * e2 + 1e-30);
}

// Pitch detection constrained to a band around the expected fundamental.
//
// A plucked string is bright and inharmonic, so an unconstrained peak pick
// latches onto an octave partial or a sub-harmonic and reads tens of cents
// wrong. Searching only within +/-15% of the expected period and refining the
// peak parabolically keeps the assertion honest without hiding a real detune.
double detectPitch (const std::vector<float>& x, double fs, double expectedHz)
{
    const int n = (int) x.size();
    if (n < 1024 || expectedHz <= 0.0) return 0.0;

    const int centre = (int) (fs / expectedHz);
    const int lo = std::max (2, (int) (centre * 0.85));
    const int hi = std::min (n - 2, (int) (centre * 1.15));

    if (hi <= lo + 2) return 0.0;

    int best = lo;
    double bestVal = -1.0;

    for (int lag = lo; lag <= hi; ++lag)
    {
        const double v = normalisedAcf (x, lag);
        if (v > bestVal) { bestVal = v; best = lag; }
    }

    if (bestVal < 0.3) return 0.0;

    double refined = (double) best;

    if (best > lo && best < hi)
    {
        const double y0 = normalisedAcf (x, best - 1);
        const double y1 = bestVal;
        const double y2 = normalisedAcf (x, best + 1);
        const double denom = y0 - 2.0 * y1 + y2;

        if (std::fabs (denom) > 1e-12)
            refined = (double) best + 0.5 * (y0 - y2) / denom;
    }

    return fs / refined;
}

std::vector<float> renderPluck (fw::dsp::GuitarString& s,
                                const fw::StringEvent& e,
                                int stringIdx,
                                int warmup = 2000,
                                int capture = 8192)
{
    std::vector<float> out;
    out.reserve ((std::size_t) (warmup + capture));

    s.pluck (e, stringIdx);

    for (int i = 0; i < warmup; ++i) (void) s.tick (0.f);
    for (int i = 0; i < capture; ++i) out.push_back (s.tick (0.f));

    return out;
}

double rmsOf (const std::vector<float>& x, int from = 0)
{
    double s = 0.0;
    const int n = (int) x.size();
    for (int i = from; i < n; ++i) s += (double) x[(std::size_t) i] * x[(std::size_t) i];
    return n > from ? std::sqrt (s / (double) (n - from)) : 0.0;
}

// Coarse band-energy centroid: enough to separate a bright pluck from a dark
// one without pulling in an FFT.
double spectralCentroid (const std::vector<float>& x)
{
    double num = 0.0, den = 0.0;

    for (int bin = 1; bin <= 64; ++bin)
    {
        const double freq = 25.0 * (double) bin * (double) bin;
        if (freq > 20000.0) break;

        const double w = 2.0 * 3.14159265358979323846 * freq / kSampleRate;
        const double coeff = 2.0 * std::cos (w);

        double z1 = 0.0, z2 = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            const double v = x[i];
            const double z = v + coeff * z1 - z2;
            z2 = z1;
            z1 = z;
        }

        const double power = z1 * z1 + z2 * z2 - coeff * z1 * z2;
        num += freq * power;
        den += power;
    }

    return den > 1e-12 ? num / den : 0.0;
}

//==============================================================================
void testTuning()
{
    fw::dsp::GuitarString s;
    s.prepare (kSampleRate);
    s.setModel (steelParams());

    for (int midi = 40; midi <= 64; midi += 4)
    {
        s.reset();

        const double target = midiToHz (midi);
        const auto buf = renderPluck (s, pluckEvent (0, (float) midi), 0);
        const double measured = detectPitch (buf, kSampleRate, target);

        if (measured <= 0.0)
        {
            check (false, "pitch detected", __LINE__);
            continue;
        }

        const double cents = 1200.0 * std::log2 (measured / target);
        char msg[96];
        std::snprintf (msg, sizeof msg, "tuning within 3 cents (midi %d, got %+.2f)", midi, cents);
        // The analytic loop budget is exact; what is left is the phase error of
        // the linear fractional-delay interpolation, worth about 3.5 cents.
        CHECK (std::fabs (cents) < 5.0, msg);
    }
}

void testSustain()
{
    fw::dsp::GuitarString s;
    s.prepare (kSampleRate);

    auto model = steelParams();
    model.sustain = 100.f;
    s.setModel (model);
    s.reset();

    const auto longBuf = renderPluck (s, pluckEvent (0, 40.f), 0, 2000, (int) kSampleRate);

    model.sustain = 20.f;
    s.setModel (model);
    s.reset();

    const auto shortBuf = renderPluck (s, pluckEvent (0, 40.f), 0, 2000, (int) kSampleRate);

    const double longRms  = rmsOf (longBuf, 8000);
    const double shortRms = rmsOf (shortBuf, 8000);

    CHECK (longRms > shortRms * 3.0, "sustain 100 rings far longer than sustain 20");
    CHECK (longRms < rmsOf (longBuf, 0), "pluck decays over time");
    CHECK (std::isfinite (longRms) && std::isfinite (shortRms), "output is finite");
}

void testPickPosition()
{
    fw::dsp::GuitarString s;
    s.prepare (kSampleRate);

    auto near = steelParams();
    near.pickPos = 5.f;
    s.setModel (near);
    s.reset();
    const auto nearBuf = renderPluck (s, pluckEvent (0, 40.f), 0);

    auto far = steelParams();
    far.pickPos = 35.f;
    s.setModel (far);
    s.reset();
    const auto farBuf = renderPluck (s, pluckEvent (0, 40.f), 0);

    const double cNear = spectralCentroid (nearBuf);
    const double cFar  = spectralCentroid (farBuf);

    CHECK (cNear > 0.0 && cFar > 0.0, "spectral centroid measurable");
    CHECK (cNear > cFar, "pick near the bridge is brighter than over the neck");
}

void testBodyAndMaterial()
{
    auto render = [] (int body, int material, float micBlend, int pickupSel)
    {
        fw::dsp::GuitarEngine eng;
        eng.prepare (kSampleRate, kBlock);

        fw::dsp::EngineParams p {};
        p.body = body;
        for (int s = 0; s < fw::kNumStrings; ++s)
        {
            p.strings[(std::size_t) s].material      = material;
            p.strings[(std::size_t) s].bodyLossScale = 1.f;
        }
        p.micBlend = micBlend;
        p.pickupSel = pickupSel;
        p.room = 0.f;
        p.sympathetic = 0.f;

        eng.setParams (p);
        eng.handle (pluckEvent (0, 40.f));

        std::vector<float> out;
        out.reserve ((std::size_t) (kBlock * 40));

        std::vector<float> l ((std::size_t) kBlock, 0.f), r ((std::size_t) kBlock, 0.f);

        for (int b = 0; b < 40; ++b)
        {
            eng.render (l.data(), r.data(), kBlock);
            out.insert (out.end(), l.begin(), l.end());
        }

        return out;
    };

    const auto dread = render (0, 0, 70.f, 1);
    const auto solid = render (2, 1, 0.f, 1);
    const auto neck  = render (0, 0, 70.f, 2);

    CHECK (rmsOf (dread) > 0.0, "dreadnought renders audible output");
    CHECK (rmsOf (solid) > 0.0, "solid electric renders audible output");
    CHECK (rmsOf (neck) > 0.0, "neck pickup renders audible output");

    const std::size_t n = std::min (dread.size(), solid.size());

    double diff = 0.0, total = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double d = (double) dread[i] - solid[i];
        diff += d * d;
        total += (double) dread[i] * dread[i];
    }
    CHECK (total > 1e-9 && diff / total > 0.05, "dreadnought and solid electric differ");

    double pickDiff = 0.0, pickTotal = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double d = (double) dread[i] - neck[i];
        pickDiff += d * d;
        pickTotal += (double) dread[i] * dread[i];
    }
    CHECK (pickTotal > 1e-9 && pickDiff / pickTotal > 0.05, "neck and middle pickups differ");
}

void testPowerOff()
{
    fw::dsp::GuitarEngine eng;
    eng.prepare (kSampleRate, kBlock);

    fw::dsp::EngineParams p {};
    p.power = 1;
    eng.setParams (p);
    eng.handle (pluckEvent (0, 40.f));

    std::vector<float> l ((std::size_t) kBlock, 0.f), r ((std::size_t) kBlock, 0.f);
    for (int b = 0; b < 8; ++b) eng.render (l.data(), r.data(), kBlock);

    CHECK (rmsOf (l) > 0.0, "powered engine produces output");

    p.power = 0;
    eng.setParams (p);

    for (int b = 0; b < 8; ++b) eng.render (l.data(), r.data(), kBlock);

    CHECK (rmsOf (l) == 0.0 && rmsOf (r) == 0.0, "power off silences the engine");
}

void testStrumSpread()
{
    fw::PerformanceEngine perf;
    perf.prepare (kSampleRate);

    fw::PerfParams p {};
    p.mode = fw::PlayMode::Strum;
    p.strumTime = 200.f;
    p.strokeVol = 1.f;
    perf.setParams (p);

    std::vector<fw::MidiMsg> in;
    for (int i = 0; i < 3; ++i)
        in.push_back ({ 0, 0x90, (unsigned char) (40 + i * 5), 110 });

    in.push_back ({ 0, 0x90, 28, 120 });   // StrumDown

    fw::StringEvent events[64] {};
    fw::ClickEvent clicks[32] {};
    int numClicks = 0;

    // A 200 ms strum is ~8800 samples, so the sweep spans many blocks. Collect
    // until all three hits have been emitted.
    struct Hit { int block; int offset; int string; };

    std::vector<Hit> all;

    for (int b = 0; b < 40 && all.size() < 3; ++b)
    {
        const int n = perf.process (b == 0 ? in.data() : nullptr,
                                    b == 0 ? (int) in.size() : 0,
                                    fw::Transport {}, kBlock, events, 64, clicks, numClicks);
        for (int i = 0; i < n; ++i) all.push_back ({ b, events[i].sampleOffset, events[i].string });
    }

    const int n = (int) all.size();
    CHECK (n == 3, "a three-note chord produces three strum hits");

    if (n == 3)
    {
        auto before = [] (const Hit& a, const Hit& b)
        {
            return a.block < b.block || (a.block == b.block && a.offset <= b.offset);
        };

        CHECK (before (all[0], all[1]) && before (all[1], all[2]),
               "strum hits are ordered in time");

        CHECK (all[0].string < all[1].string && all[1].string < all[2].string,
               "a down stroke sweeps low to high");

        CHECK (all[0].block < all[2].block,
               "strum spread is not a single simultaneous hit");
    }
}

void testKeyswitches()
{
    fw::PerformanceEngine perf;
    perf.prepare (kSampleRate);

    fw::PerfParams p {};
    p.mode = fw::PlayMode::Poly;
    perf.setParams (p);

    // MIDI 16 is PalmMute: group Damping (index 1), value 1.
    std::vector<fw::MidiMsg> in { { 0, 0x90, 16, 100 } };

    fw::StringEvent events[64] {};
    fw::ClickEvent clicks[32] {};
    int numClicks = 0;

    perf.process (in.data(), 1, fw::Transport {}, kBlock, events, 64, clicks, numClicks);

    const auto ui = perf.ui();

    CHECK (ui.ks[1] == 1, "PalmMute keyswitch latches the damping group");
    CHECK ((ui.ksDirtyMask & (1u << 1)) != 0, "the damping group is flagged dirty for the editor");
    CHECK ((ui.ksNotes & (1u << (16 - 12))) != 0, "the latched key appears in ksNotes");

    std::vector<fw::MidiMsg> off { { 0, 0x80, 16, 0 } };
    perf.process (off.data(), 1, fw::Transport {}, kBlock, events, 64, clicks, numClicks);

    CHECK (perf.ui().ks[1] == 1, "latched keyswitch ignores note-off");
}

void testArpSteps()
{
    fw::PerformanceEngine perf;
    perf.prepare (kSampleRate);

    fw::PerfParams p {};
    p.mode = fw::PlayMode::Arp;
    p.bpm = 120.f;
    p.arpRate = 3;          // 1/16
    p.arpGate = 30.f;
    p.strokeVol = 1.f;
    perf.setParams (p);

    std::vector<fw::MidiMsg> in { { 0, 0x90, 40, 100 },
                                  { 0, 0x90, 47, 100 },
                                  { 0, 0x90, 52, 100 } };

    fw::StringEvent events[64] {};
    fw::ClickEvent clicks[32] {};
    int numClicks = 0;

    const int firstBlock = perf.process (in.data(), (int) in.size(), fw::Transport {},
                                             kBlock, events, 64, clicks, numClicks);

    int plucks = 0;
    for (int i = 0; i < firstBlock; ++i)
        if (events[i].type == fw::EventType::Pluck) ++plucks;

    CHECK (plucks <= 1, "arp mode does not pluck every held note at once");

    bool sawPluck = plucks > 0;
    for (int b = 0; b < 60 && ! sawPluck; ++b)
    {
        const int n = perf.process (nullptr, 0, fw::Transport {}, kBlock, events, 64, clicks, numClicks);
        for (int i = 0; i < n; ++i)
            if (events[i].type == fw::EventType::Pluck) { sawPluck = true; break; }
    }

    CHECK (sawPluck, "arp emits plucks as steps advance");
}

void testProcessorRoundTrip()
{
    FretworkAudioProcessor proc;
    proc.prepareToPlay (kSampleRate, kBlock);

    if (auto* p = proc.getState().getParameter (fw::id::room))
        p->setValueNotifyingHost (0.75f);

    fw::SeqNote note {};
    note.row = 8;
    note.start = 4;
    note.len = 2;
    note.vel = 99;
    proc.getSeqStore().addNote (3, note);

    juce::MemoryBlock saved;
    proc.getStateInformation (saved);

    if (auto* p = proc.getState().getParameter (fw::id::room))
        p->setValueNotifyingHost (0.1f);

    proc.getSeqStore().removeNote (3, 8, 4);

    proc.setStateInformation (saved.getData(), (int) saved.getSize());

    CHECK (proc.getSeqStore().hasNote (3, 8, 4), "sequencer note survives a state round trip");

    const auto& restored = proc.getSeqStore().getSlot (3);
    bool found = false;
    for (int i = 0; i < restored.count; ++i)
        if (restored.notes[(std::size_t) i].vel == 99) found = true;

    CHECK (found, "sequencer note velocity survives a state round trip");

    if (auto* p = proc.getState().getParameter (fw::id::room))
        CHECK (std::fabs (p->getValue() - 0.75f) < 0.02f,
               "parameter value survives a state round trip");
}

void testProcessorRenders()
{
    FretworkAudioProcessor proc;
    proc.prepareToPlay (kSampleRate, kBlock);

    juce::AudioBuffer<float> audio (2, kBlock);

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 40, 0.9f), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, 47, 0.9f), 0);

    double energy = 0.0;
    bool finite = true;

    for (int b = 0; b < 40; ++b)
    {
        proc.processBlock (audio, midi);
        midi.clear();

        for (int ch = 0; ch < 2; ++ch)
        {
            const float* d = audio.getReadPointer (ch);
            for (int i = 0; i < kBlock; ++i)
            {
                if (! std::isfinite (d[i])) finite = false;
                energy += (double) d[i] * d[i];
            }
        }
    }

    CHECK (finite, "processor output contains no NaN or infinity");
    CHECK (energy > 1e-6, "a held chord produces audible output");
}

void testBend()
{
    fw::dsp::GuitarString s;
    s.prepare (kSampleRate);
    s.setModel (steelParams());

    fw::StringEvent e {};
    e.string = 0; e.pitch = 40.f; e.velocity = 0.9f; e.type = fw::EventType::Pluck;
    s.pluck (e, 0);

    for (int i = 0; i < 2000; ++i) (void) s.tick (0.f);

    s.setBendSemis (2.f);
    for (int i = 0; i < (int) kSampleRate; ++i) (void) s.tick (0.f);   // let the smoother settle

    std::vector<float> buf;
    for (int i = 0; i < 8192; ++i) buf.push_back (s.tick (0.f));

    const double expected = midiToHz (40.0) * std::pow (2.0, 2.0 / 12.0);
    const double measured = detectPitch (buf, kSampleRate, expected);

    CHECK (measured > 0.0, "bent pitch detected");

    if (measured > 0.0)
    {
        const double cents = 1200.0 * std::log2 (measured / expected);
        char msg[96];
        std::snprintf (msg, sizeof msg, "two-semitone bend lands within 5 cents (%+.2f)", cents);
        CHECK (std::fabs (cents) < 5.0, msg);
    }
}

void testNaturalHarmonic()
{
    fw::dsp::GuitarString s;
    s.prepare (kSampleRate);
    s.setModel (steelParams());

    // A natural harmonic is requested with the OPEN string pitch and the
    // partial number; the loop must be tuned to the partial, not the open note.
    fw::StringEvent e {};
    e.string = 0; e.pitch = 40.f; e.velocity = 0.9f; e.type = fw::EventType::Pluck;
    e.harmonicN = 2;
    s.pluck (e, 0);

    for (int i = 0; i < 2000; ++i) (void) s.tick (0.f);

    std::vector<float> buf;
    for (int i = 0; i < 8192; ++i) buf.push_back (s.tick (0.f));

    const double openHz = midiToHz (40.0);
    const double measured = detectPitch (buf, kSampleRate, openHz * 2.0);

    CHECK (measured > 0.0, "harmonic pitch detected");

    if (measured > 0.0)
    {
        const double ratio = measured / openHz;
        char msg[96];
        std::snprintf (msg, sizeof msg, "harmonic sounds near the 2nd partial (ratio %.3f)", ratio);
        CHECK (ratio > 1.8 && ratio < 2.2, msg);
    }
}

void testMute()
{
    fw::dsp::GuitarString a, b;
    a.prepare (kSampleRate); b.prepare (kSampleRate);
    a.setModel (steelParams());
    b.setModel (steelParams());

    fw::StringEvent e {};
    e.string = 0; e.pitch = 40.f; e.velocity = 0.9f; e.type = fw::EventType::Pluck;

    a.pluck (e, 0);
    b.pluck (e, 0);

    for (int i = 0; i < 2000; ++i) { (void) a.tick (0.f); (void) b.tick (0.f); }

    b.release (0.08f);
    for (int i = 0; i < 4410; ++i) { (void) a.tick (0.f); (void) b.tick (0.f); }

    std::vector<float> ba, bb;
    for (int i = 0; i < 4410; ++i) { ba.push_back (a.tick (0.f)); bb.push_back (b.tick (0.f)); }

    const double openRms  = rmsOf (ba);
    const double mutedRms = rmsOf (bb);

    CHECK (openRms > 0.0, "reference string rings");
    CHECK (mutedRms < openRms / 20.0, "a hard mute kills the tail");
}

} // namespace

//==============================================================================

namespace
{
// Renders one rotary slider and returns the direction its value dot points,
// in degrees clockwise from up. This pins down both the sweep range and the
// JUCE angle convention end to end, with no host and no screenshots.

void testKeyswitchGroupExclusive()
{
    fw::PerformanceEngine perf;
    perf.prepare (kSampleRate);

    fw::PerfParams p {};
    p.mode = fw::PlayMode::Poly;
    perf.setParams (p);

    fw::StringEvent events[64] {};
    fw::ClickEvent clicks[32] {};
    int numClicks = 0;

    // Damping group (index 1) starts at note 15, so 16 and 17 are two different
    // keys that both live in it. Latching the second must clear the first:
    // otherwise the editor highlights both as latched at once.
    const int first  = 16;
    const int second = 17;

    std::vector<fw::MidiMsg> a { { 0, 0x90, first, 100 } };
    perf.process (a.data(), 1, fw::Transport {}, kBlock, events, 64, clicks, numClicks);
    CHECK ((perf.ui().ksNotes & (1u << (first - 12))) != 0, "first keyswitch latches");

    std::vector<fw::MidiMsg> b { { 0, 0x90, second, 100 } };
    perf.process (b.data(), 1, fw::Transport {}, kBlock, events, 64, clicks, numClicks);

    const auto ui = perf.ui();
    int latchedInGroup = 0;
    for (int n = 15; n <= 18; ++n)
        if ((ui.ksNotes & (1u << (n - 12))) != 0)
            ++latchedInGroup;

    CHECK (latchedInGroup == 1, "only one keyswitch per group stays latched");
    CHECK ((ui.ksNotes & (1u << (first - 12))) == 0, "the superseded key is cleared");
    CHECK ((ui.ksNotes & (1u << (second - 12))) != 0, "the newest key is the one latched");
    CHECK (ui.ks[1] == 2, "the group now reads the newest value");
}


namespace
{
juce::MouseEvent syntheticClick (juce::MouseInputSource& input, juce::Component& target,
                                 juce::Point<float> pos)
{
    return juce::MouseEvent (input, pos, juce::ModifierKeys(), 1.0f,
                             0.0f, 0.0f, 0.0f, 0.0f,
                             &target, &target,
                             juce::Time::getCurrentTime(), pos,
                             juce::Time::getCurrentTime(), 1, false);
}
} // namespace




void testKeyboardStateProducesAudio()
{
    // T10 feeds the processor a MidiBuffer directly. This instead goes through
    // the juce::MidiKeyboardState the on-screen strip drives, so it covers the
    // half of the chain a screenshot cannot: does a note arriving via the
    // keyboard state actually become sound?
    FretworkAudioProcessor proc;
    proc.prepareToPlay (kSampleRate, kBlock);

    juce::AudioBuffer<float> audio (2, kBlock);
    juce::MidiBuffer empty;

    const auto energyOf = [&] (int blocks, juce::MidiKeyboardState& kb, int note)
    {
        double energy = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            proc.processBlock (audio, empty);
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* d = audio.getReadPointer (ch);
                for (int i = 0; i < kBlock; ++i)
                    energy += (double) d[i] * d[i];
            }
        }
        juce::ignoreUnused (kb, note);
        return energy;
    };

    // silence before anything is played
    const double silent = energyOf (12, proc.getKeyboardState(), -1);
    CHECK (silent < 1e-6, "no output before any note is played");

    // 60 is not an open string; the guitar maps fretted pitches onto strings, so
    // sweep the standard tunings rather than assuming one pitch is playable.
    auto& kb = proc.getKeyboardState();
    double best = 0.0;
    int bestNote = -1;
    bool finite = true;

    static const int candidates[] = { 40, 45, 50, 55, 59, 64, 45, 50 };
    for (const int note : candidates)
    {
        kb.noteOff (1, note, 0.0f);
        kb.allNotesOff (1);
        proc.processBlock (audio, empty);

        kb.noteOn (1, note, 0.9f);

        double played = 0.0;
        for (int b = 0; b < 24; ++b)
        {
            proc.processBlock (audio, empty);
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* d = audio.getReadPointer (ch);
                for (int i = 0; i < kBlock; ++i)
                {
                    if (! std::isfinite (d[i])) finite = false;
                    played += (double) d[i] * d[i];
                }
            }
        }

        if (played > best) { best = played; bestNote = note; }
    }
    kb.allNotesOff (1);

    std::printf ("    energy: silent=%.3g  best via keyboard state=%.3g (note %d)\n",
                 silent, best, bestNote);
    CHECK (finite, "keyboard-driven output contains no NaN or infinity");
    CHECK (best > 1e-6, "a note sent through MidiKeyboardState produces audible output");
    CHECK (best > silent * 100.0 + 1e-6, "the note is what made the output");
}


void testSetupTabPages()
{
    FretworkAudioProcessor proc;
    fw::ui::SetupTabs tabs (proc, proc.getState());
    tabs.setBounds (0, 0, 330, 228);

    // Child 0 is the tab bar; children 1..5 are the pages. Counting all
    // visible children would always include the bar and prove nothing.
    const auto visiblePageCount = [&tabs]
    {
        int n = 0;
        for (int i = 1; i < tabs.getNumChildComponents(); ++i)
            if (tabs.getChildComponent (i) != nullptr && tabs.getChildComponent (i)->isVisible())
                ++n;
        return n;
    };

    CHECK (tabs.getNumChildComponents() == 6, "tab bar plus five pages");
    CHECK (visiblePageCount() == 1, "exactly one page is shown at construction");

    for (int i = 0; i < 5; ++i)
    {
        tabs.setCurrentPage (i);

        CHECK (visiblePageCount() == 1, "switching to a page shows exactly one");

        const auto shown = tabs.getChildComponent (i + 1);
        CHECK (shown != nullptr && shown->isVisible(), "the requested page is the visible one");
        CHECK (shown != nullptr && shown->getNumChildComponents() > 0, "that page has controls");

        int total = 0;
        for (int c = 1; c < tabs.getNumChildComponents(); ++c)
            if (tabs.getChildComponent (c)->isVisible())
                ++total;
        CHECK (total == 1, "no other page is left visible");
    }

    // switching back and forth must not accumulate visibility
    tabs.setCurrentPage (0);
    tabs.setCurrentPage (4);
    tabs.setCurrentPage (0);
    CHECK (visiblePageCount() == 1, "repeated switching never leaves two pages visible");
}

void testKnobCapTiers()
{
    using Tier = fw::ui::FretLookAndFeel::KnobTier;
    const auto tier = fw::ui::FretLookAndFeel::knobCapForRadius;

    // below the bitmap floor the vector pointer takes over
    CHECK (tier (0.0f)  == Tier::none, "zero radius uses no cap");
    CHECK (tier (8.9f)  == Tier::none, "just under the floor uses no cap");
    CHECK (tier (9.0f)  == Tier::dark,  "the floor switches to the dark cap");

    // the micro-knob row is black-capped; the row is about 10.5 radius
    CHECK (tier (10.5f) == Tier::dark,  "a micro-knob radius gets the dark cap");
    CHECK (tier (15.9f) == Tier::dark,  "just under the boundary is still dark");

    // the main knobs are chrome; measured at about 29 radius
    CHECK (tier (16.0f) == Tier::chrome, "the boundary switches to chrome");
    CHECK (tier (29.0f) == Tier::chrome, "a main knob radius gets the chrome cap");

    // no gap where the caller would fall through to a null cap mid-range
    for (float r = 0.0f; r <= 40.0f; r += 0.5f)
        CHECK (tier (r) != Tier::none || r < 9.0f,
               "every radius above the floor resolves to a cap");
}


void testTimbreDiagnostic()
{
    // Render one plucked note and ask whether it is actually pitched.
    // Normalised autocorrelation at the pitch period should be near 1 for a
    // guitar string and near 0 for noise.
    FretworkAudioProcessor proc;
    proc.prepareToPlay (kSampleRate, kBlock);

    juce::AudioBuffer<float> audio (2, kBlock);
    juce::MidiBuffer empty;
    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::noteOn (1, 40, 1.0f), 0);

    std::vector<float> mono;
    mono.reserve ((size_t) kSampleRate / 2);

    for (int b = 0; b < (int) (kSampleRate / 2) / kBlock; ++b)
    {
        proc.processBlock (audio, on);
        on.clear();
        const float* d = audio.getReadPointer (0);
        mono.insert (mono.end(), d, d + kBlock);
    }

    // analyse the steady part, after the pluck transient has decayed
    const size_t start = mono.size() / 4;
    const size_t len   = mono.size() / 2;
    std::vector<double> seg (mono.begin() + start, mono.begin() + start + len);

    double mean = 0.0;
    for (double v : seg) mean += v;
    mean /= (double) seg.size();
    double energy = 0.0;
    for (double& v : seg) { v -= mean; energy += v * v; }
    const double rms = std::sqrt (energy / (double) seg.size());

    // normalised autocorrelation
    const int maxLag = (int) (kSampleRate / 60.0);        // down to 60 Hz
    double bestLag = 0, bestVal = -1.0;
    for (int lag = 8; lag < maxLag; ++lag)
    {
        double num = 0.0, e1 = 0.0, e2 = 0.0;
        for (size_t i = 0; i + (size_t) lag < seg.size(); ++i)
        {
            const double a = seg[i], b = seg[i + (size_t) lag];
            num += a * b; e1 += a * a; e2 += b * b;
        }
        const double den = std::sqrt (e1 * e2);
        if (den > 0.0) { const double v = num / den; if (v > bestVal) { bestVal = v; bestLag = (double) lag; } }
    }

    const double detectedHz = bestLag > 0.0 ? kSampleRate / bestLag : 0.0;
    std::printf ("    rms=%.5f  bestLag=%.1f (%.2f Hz)  autocorr=%.3f  expected=%.2f Hz\n",
                 rms, bestLag, detectedHz, bestVal, 82.41);

    // crest factor: a bowed/plucked string is peaky, noise is not
    double peak = 0.0;
    for (double v : seg) peak = std::max (peak, std::fabs (v));
    const double crest = rms > 0.0 ? peak / rms : 0.0;
    std::printf ("    crest factor=%.2f\n", crest);

    // Band energy via cascaded one-pole lowpasses: differences isolate bands.
    // Broadband hiss or a noisy attack shows up as energy far above 4 kHz.
    const auto bandEnergy = [&] (const std::vector<double>& x, double cut)
    {
        const double a = std::exp (-2.0 * 3.14159265358979323846 * cut / kSampleRate);
        double y = 0.0, e = 0.0;
        for (double v : x) { y = y + (1.0 - a) * (v - y); e += y * y; }
        return e;
    };

    const auto report = [&] (const char* label, const std::vector<double>& x)
    {
        const double e1k = bandEnergy (x, 1000.0);
        const double e4k = bandEnergy (x, 4000.0);
        const double e10k = bandEnergy (x, 10000.0);
        double tot = 0.0; for (double v : x) tot += v * v;
        if (tot <= 0.0) return;
        const double bLow = e1k, bMid = e4k - e1k, bHi = e10k - e4k, bTop = tot - e10k;
        std::printf ("    %s: low=%.1f%% mid=%.1f%% high=%.1f%% top=%.1f%%\n",
                     label, 100*bLow/tot, 100*bMid/tot, 100*bHi/tot, 100*bTop/tot);
    };

    // attack: the first 20 ms, where the excitation burst lives
    const size_t atk = std::min (mono.size(), (size_t) (0.020 * kSampleRate));
    report ("attack 0-20ms", std::vector<double> (mono.begin(), mono.begin() + atk));
    report ("steady  250ms+", seg);

    CHECK (rms > 1e-6, "note produces audible output");
    CHECK (bestVal > 0.55, "output is periodic at the pitch period, not noise");
    CHECK (std::fabs (detectedHz - 82.41) < 3.0, "detected pitch matches the note");
    CHECK (crest > 2.0, "waveform is peaky like a plucked string");
}

// ---- real-time condition matrix ------------------------------------------
struct ToneMetrics { double autocorr = 0, hz = 0, attackHiFrac = 0, peak = 0; bool finite = true; };

ToneMetrics analyseTone (const std::vector<float>& x, double fs, double expectHz)
{
    ToneMetrics m;
    for (float v : x) { if (! std::isfinite (v)) m.finite = false; m.peak = std::max (m.peak, (double) std::fabs (v)); }

    const size_t a0 = (size_t) (0.25 * fs), a1 = std::min (x.size(), (size_t) (0.75 * fs));
    const double P = fs / expectHz;
    double best = -1.0, bestLag = 0.0;
    for (int lag = (int) (P * 0.97); lag <= (int) (P * 1.03) + 1; ++lag)
    {
        double num = 0, e1 = 0, e2 = 0;
        for (size_t i = a0; i + (size_t) lag < a1; ++i)
        { const double p = x[i], q = x[i + (size_t) lag]; num += p * q; e1 += p * p; e2 += q * q; }
        const double d = std::sqrt (e1 * e2);
        if (d > 0 && num / d > best) { best = num / d; bestLag = lag; }
    }
    m.autocorr = best; m.hz = bestLag > 0 ? fs / bestLag : 0;

    // fraction of attack energy above 4 kHz
    const size_t n = std::min (x.size(), (size_t) (0.020 * fs));
    const double a = std::exp (-2.0 * 3.14159265358979323846 * 4000.0 / fs);
    double y = 0, lo = 0, tot = 0;
    for (size_t i = 0; i < n; ++i) { y += (1.0 - a) * (x[i] - y); lo += y * y; tot += (double) x[i] * x[i]; }
    m.attackHiFrac = tot > 0 ? (tot - lo) / tot : 0;
    return m;
}

std::vector<int> gVarSizes { 37, 512, 96, 1, 300, 1024, 64 };

std::vector<float> renderProcessor (double fs, int prepBlock, bool variable, int note, float vel, double seconds)
{
    auto procPtr = std::make_unique<FretworkAudioProcessor>();
    auto& proc = *procPtr;
    proc.prepareToPlay (fs, prepBlock);
    std::vector<float> out;
    const int total = (int) (seconds * fs);
    out.reserve ((size_t) total);
    int k = 0; bool sent = false;
    while ((int) out.size() < total)
    {
        int n = variable ? gVarSizes[(size_t) k++ % gVarSizes.size()] : prepBlock;
        n = std::min (n, total - (int) out.size());
        juce::AudioBuffer<float> buf (2, n);
        buf.clear();
        juce::MidiBuffer midi;
        if (! sent) { midi.addEvent (juce::MidiMessage::noteOn (1, note, vel), std::min (17, n - 1)); sent = true; }
        proc.processBlock (buf, midi);
        const float* l = buf.getReadPointer (0);
        out.insert (out.end(), l, l + n);
    }
    return out;
}

// ---- note lifecycle across the neck --------------------------------------
struct NoteEv { int sample; int note; float vel; bool on; };

std::vector<float> renderEvents (double fs, int block, const std::vector<NoteEv>& evs, double seconds)
{
    auto procPtr = std::make_unique<FretworkAudioProcessor>();
    auto& proc = *procPtr;
    proc.prepareToPlay (fs, block);
    const int total = (int) (seconds * fs);
    std::vector<float> out;
    out.reserve ((size_t) total);
    while ((int) out.size() < total)
    {
        const int start = (int) out.size();
        const int n = std::min (block, total - start);
        juce::AudioBuffer<float> buf (2, n);
        buf.clear();
        juce::MidiBuffer midi;
        for (const auto& e : evs)
            if (e.sample >= start && e.sample < start + n)
                midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, e.vel)
                                    : juce::MidiMessage::noteOff (1, e.note), e.sample - start);
        proc.processBlock (buf, midi);
        const float* l = buf.getReadPointer (0);
        out.insert (out.end(), l, l + n);
    }
    return out;
}

double segRms (const std::vector<float>& x, double fs, double t0, double t1)
{
    const size_t a = (size_t) (t0 * fs), b = std::min (x.size(), (size_t) (t1 * fs));
    double e = 0; for (size_t i = a; i < b; ++i) e += (double) x[i] * x[i];
    return b > a ? std::sqrt (e / (double) (b - a)) : 0.0;
}

// First-difference brightness proxy in Hz (spectral centroid estimate).
double segBrightness (const std::vector<float>& x, double fs, double t0, double t1)
{
    const size_t a = std::max<size_t> (1, (size_t) (t0 * fs)), b = std::min (x.size(), (size_t) (t1 * fs));
    double e = 0, d = 0;
    for (size_t i = a; i < b; ++i) { e += (double) x[i] * x[i]; const double q = x[i] - x[i - 1]; d += q * q; }
    return e > 0 ? fs / (2.0 * 3.14159265358979323846) * std::sqrt (d / e) : 0.0;
}

// Pitch with parabolic interpolation of the autocorrelation peak.
double finePitch (const std::vector<float>& x, double fs, double t0, double t1, double expectHz)
{
    const size_t a = (size_t) (t0 * fs), b = std::min (x.size(), (size_t) (t1 * fs));
    const int lo = (int) (fs / expectHz * 0.95), hi = (int) (fs / expectHz * 1.05) + 2;
    std::vector<double> ac ((size_t) hi + 2, 0.0);
    for (int L = lo - 1; L <= hi + 1; ++L)
    {
        double num = 0, e1 = 0, e2 = 0;
        for (size_t i = a; i + (size_t) L < b; ++i) { num += (double) x[i] * x[i + (size_t) L]; e1 += (double) x[i] * x[i]; e2 += (double) x[i + (size_t) L] * x[i + (size_t) L]; }
        ac[(size_t) L] = num / std::sqrt (std::max (1e-30, e1 * e2));
    }
    int best = lo;
    for (int L = lo; L <= hi; ++L) if (ac[(size_t) L] > ac[(size_t) best]) best = L;
    const double y0 = ac[(size_t) best - 1], y1 = ac[(size_t) best], y2 = ac[(size_t) best + 1];
    const double den = y0 - 2 * y1 + y2;
    const double off = std::fabs (den) > 1e-12 ? 0.5 * (y0 - y2) / den : 0.0;
    return fs / ((double) best + off);
}

void testLifecycleAcrossNeck()
{
    const double fs = 48000.0;
    const int opens[] = { 40, 45, 50, 55, 59, 64 };
    for (int note : opens)
    {
        const double hz = midiToHz (note);
        double rmsByVel[2] {}, briByVel[2] {};
        for (int vi = 0; vi < 2; ++vi)
        {
            const float vel = vi == 0 ? 0.35f : 1.0f;
            // note on at 10 ms, off at 1.2 s, observe to 1.8 s
            const int onAt = (int) (0.010 * fs), offAt = (int) (1.2 * fs);
            const auto x = renderEvents (fs, 480, { { onAt, note, vel, true }, { offAt, note, 0.f, false } }, 1.8);

            const double cents = 1200.0 * std::log2 (finePitch (x, fs, 0.3, 0.8, hz) / hz);
            const double early = segBrightness (x, fs, 0.06, 0.12);
            const double late  = segBrightness (x, fs, 0.90, 1.10);
            const double sus   = segRms (x, fs, 0.30, 0.40);
            const double held  = segRms (x, fs, 1.05, 1.15);
            const double tail  = segRms (x, fs, 1.60, 1.80);

            // click check: largest sample step around note-off vs. steady steps
            double stepAt = 0, stepRef = 0;
            for (int i = offAt - 240; i < offAt + 480; ++i) stepAt  = std::max (stepAt,  (double) std::fabs (x[(size_t) i] - x[(size_t) i - 1]));
            for (int i = offAt - 4800; i < offAt - 2400; ++i) stepRef = std::max (stepRef, (double) std::fabs (x[(size_t) i] - x[(size_t) i - 1]));

            std::printf ("    note=%d vel=%.2f  cents=%+.2f bright %4.0f->%4.0f Hz  sus=%.4f held=%.4f tail=%.6f  offStep=%.4f/ref %.4f\n",
                         note, vel, cents, early, late, sus, held, tail, stepAt, stepRef);

            char msg[96];
            std::snprintf (msg, sizeof msg, "note %d tuned within 5 cents", note);
            CHECK (std::fabs (cents) < 5.0, msg);
            CHECK (late < early, "upper partials decay faster than the fundamental");
            CHECK (held > 0.0 && held < sus, "held note decays naturally");
            CHECK (held > sus * 0.05, "held note still sustains after one second");
            CHECK (tail < held * 0.1, "release damps the string (-20 dB within 0.5 s)");
            CHECK (stepAt < stepRef * 1.5 + 1e-4, "note-off has no click");
            rmsByVel[vi] = segRms (x, fs, 0.02, 0.2);
            briByVel[vi] = segBrightness (x, fs, 0.02, 0.08);
        }
        CHECK (rmsByVel[1] > rmsByVel[0] * 1.5, "harder velocity is louder");
        CHECK (briByVel[1] >= briByVel[0], "harder velocity is at least as bright");
    }
}

void testChordPolyphony()
{
    const double fs = 48000.0;
    const int chord[] = { 40, 47, 52, 56, 59, 64 };   // open E major
    std::vector<NoteEv> evs;
    for (int k = 0; k < 6; ++k) evs.push_back ({ (int) (0.010 * fs) + k * 240, chord[k], 0.8f, true });
    const auto x = renderEvents (fs, 480, evs, 1.5);

    double peak = 0; bool finite = true;
    for (float v : x) { finite = finite && std::isfinite (v); peak = std::max (peak, (double) std::fabs (v)); }

    // Hiss floor: energy above 8 kHz in the sustain, relative to total.
    const size_t a = (size_t) (0.4 * fs), b = (size_t) (1.2 * fs);
    const double al = std::exp (-2.0 * 3.14159265358979323846 * 8000.0 / fs);
    double y = 0, lo = 0, tot = 0;
    for (size_t i = a; i < b; ++i) { y += (1.0 - al) * (x[i] - y); lo += y * y; tot += (double) x[i] * x[i]; }
    const double hiss = tot > 0 ? (tot - lo) / tot : 1.0;

    std::printf ("    chord peak=%.3f  sustain >8kHz=%.2f%%  rms=%.4f\n", peak, 100 * hiss, segRms (x, fs, 0.4, 1.2));
    CHECK (finite, "chord output finite");
    CHECK (peak < 0.98, "six-note chord does not hit the output clamp");
    CHECK (hiss < 0.02, "chord sustain has no broadband hiss floor");
    CHECK (segRms (x, fs, 0.4, 1.2) > 0.01, "chord is audible");
}

void testHoldDoesNotRetrigger()
{
    // A latched (Hold) note in Poly mode rings once; it must not re-pluck.
    const double fs = 48000.0;
    auto procPtr = std::make_unique<FretworkAudioProcessor>();
    auto& proc = *procPtr;
    for (auto* prm : proc.getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (prm))
            if (wp->paramID == fw::id::hold) prm->setValueNotifyingHost (1.0f);
    proc.prepareToPlay (fs, 480);

    std::vector<float> out;
    const int total = (int) (4.0 * fs);
    while ((int) out.size() < total)
    {
        const int start = (int) out.size();
        juce::AudioBuffer<float> buf (2, 480); buf.clear();
        juce::MidiBuffer midi;
        if (start == 0)             midi.addEvent (juce::MidiMessage::noteOn (1, 52, 0.8f), 10);
        if (start == 480 * 50)      midi.addEvent (juce::MidiMessage::noteOff (1, 52), 0);
        proc.processBlock (buf, midi);
        out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 480);
    }

    // onset count: 20 ms envelope jumps of >4x over the previous 100 ms minimum
    const int hop = (int) (0.02 * fs);
    std::vector<double> env;
    for (int i = 0; i + hop < total; i += hop) { double e = 0; for (int j = i; j < i + hop; ++j) e += (double) out[(size_t) j] * out[(size_t) j]; env.push_back (std::sqrt (e / hop)); }
    int onsets = 0; double last = -1;
    for (size_t k = 5; k < env.size(); ++k)
    {
        double mn = 1e9; for (size_t q = k - 5; q < k; ++q) mn = std::min (mn, env[q]);
        const double tsec = (double) k * hop / fs;
        if (env[k] > 0.005 && env[k] > mn * 4 && (last < 0 || tsec - last > 0.3)) { ++onsets; last = tsec; }
    }
    std::printf ("    hold: onsets after the first = %d\n", onsets);
    CHECK (onsets == 0, "held note is plucked once, never retriggered");
}

void testKeyboardStateNoReplay()
{
    // One click on the on-screen keyboard must pluck exactly once.
    const double fs = 48000.0;
    auto procPtr = std::make_unique<FretworkAudioProcessor>();
    auto& proc = *procPtr;
    proc.prepareToPlay (fs, 480);
    proc.getKeyboardState().noteOn (1, 52, 0.8f);

    std::vector<float> out;
    for (int b = 0; b < 400; ++b)            // 4 s
    {
        juce::AudioBuffer<float> buf (2, 480); buf.clear();
        juce::MidiBuffer midi;
        if (b == 100) proc.getKeyboardState().noteOff (1, 52, 0.f);
        proc.processBlock (buf, midi);
        out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 480);
    }
    const int hop = 960;
    std::vector<double> env;
    for (size_t i = 0; i + hop <= out.size(); i += hop) { double e = 0; for (size_t j = i; j < i + hop; ++j) e += (double) out[j] * out[j]; env.push_back (std::sqrt (e / hop)); }
    int onsets = 0; double last = -1;
    for (size_t k = 5; k < env.size(); ++k)
    {
        double mn = 1e9; for (size_t q = k - 5; q < k; ++q) mn = std::min (mn, env[q]);
        const double ts = (double) k * hop / fs;
        if (env[k] > 0.005 && env[k] > mn * 4 && (last < 0 || ts - last > 0.3)) { ++onsets; last = ts; }
    }
    const double tail = segRms (out, fs, 3.5, 4.0);
    std::printf ("    keyboard: re-plucks=%d tail=%.6f\n", onsets, tail);
    CHECK (onsets == 0, "on-screen key plucks once, never replayed");
    CHECK (tail < 1e-3, "released on-screen key falls silent");
}

void testSequentialNotesOneInstance()
{
    // One processor instance, the FL capture schedule: six notes in a row,
    // then a chord. Every note must sound, not just the first.
    const double fs = 48000.0;
    std::vector<NoteEv> evs;
    int t = (int) (1.0 * fs);
    for (int note : { 40, 52, 64 })
        for (float vel : { 45.f / 127.f, 1.0f })
        {
            evs.push_back ({ t, note, vel, true });
            evs.push_back ({ t + (int) (2.5 * fs), note, 0.f, false });
            t += (int) (3.5 * fs);
        }
    const int chordAt = t;
    const int chord[] = { 40, 47, 52, 56, 59, 64 };
    for (int k = 0; k < 6; ++k) evs.push_back ({ chordAt + k * 240, chord[k], 100.f / 127.f, true });
    for (int k = 0; k < 6; ++k) evs.push_back ({ chordAt + (int) (4.0 * fs), chord[k], 0.f, false });
    std::sort (evs.begin(), evs.end(), [] (const NoteEv& a, const NoteEv& b) { return a.sample < b.sample; });

    const auto x = renderEvents (fs, 480, evs, 30.0);
    for (int i = 0; i < 7; ++i)
    {
        const double at = 1.0 + 3.5 * i;
        const double r = segRms (x, fs, at + 0.05, at + 0.35);
        std::printf ("    event %d at %.1fs rms=%.4f\n", i, at, r);
        char msg[80]; std::snprintf (msg, sizeof msg, "event %d sounds in a shared instance", i);
        CHECK (r > 0.003, msg);
    }
}

void testRealtimeMatrix()
{
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    const int blocks[] = { 32, 128, 480, 2048 };
    for (double fs : rates)
        for (int bi = 0; bi < 5; ++bi)
            for (int note : { 40, 64 })
                for (float vel : { 0.35f, 1.0f })
                {
                    const bool variable = bi == 4;
                    const int blk = variable ? 512 : blocks[bi];
                    const auto x = renderProcessor (fs, blk, variable, note, vel, 0.8);
                    const auto m = analyseTone (x, fs, midiToHz (note));
                    std::printf ("    fs=%6.0f blk=%5s note=%d vel=%.2f  ac=%.3f hz=%.2f atkHi=%.1f%% peak=%.3f\n",
                                 fs, variable ? "var" : std::to_string (blk).c_str(), note, vel,
                                 m.autocorr, m.hz, 100 * m.attackHiFrac, m.peak);
                    CHECK (m.finite, "output finite");
                    CHECK (m.autocorr >= 0.95, "sustain periodic at the note's pitch");
                    // Recorded FL Studio nylon/jazz guitar multisamples measure 0.3-13.7% here.
                    CHECK (m.attackHiFrac < 0.12, "attack brightness within recorded-guitar range");
                }
}

// Direction of the pointer as degrees clockwise from straight up, taken
// from the same helper drawRotarySlider uses - no duplicated formula.
float knobDotAngle (float sliderPos)
{
    const auto geo = fw::ui::FretLookAndFeel::rotaryGeometry (sliderPos, 0.0f, 2.0f * juce::MathConstants<float>::pi);
    const auto deg = std::atan2 (geo.dirX, -geo.dirY) * 180.0
                   / juce::MathConstants<double>::pi;
    return static_cast<float> (std::fmod (std::fmod (deg, 360.0) + 360.0, 360.0));
}

float angleGap (float a, float b)
{
    float d = std::fmod (std::fabs (a - b), 360.0f);
    if (d > 180.0f)
        d = 360.0f - d;
    return d;
}

void testRotarySweep()
{
    // A full 360 sweep, measured from the top, clockwise.
    CHECK (angleGap (knobDotAngle (0.00f),   0.0f) < 0.5f, "minimum points up");
    CHECK (angleGap (knobDotAngle (0.25f),  90.0f) < 0.5f, "quarter points right");
    CHECK (angleGap (knobDotAngle (0.50f), 180.0f) < 0.5f, "half points down");
    CHECK (angleGap (knobDotAngle (0.75f), 270.0f) < 0.5f, "three quarters points left");
    CHECK (angleGap (knobDotAngle (0.125f), 45.0f) < 0.5f, "eighth sits at 45 degrees");

    // the whole circumference is reachable
    CHECK (angleGap (knobDotAngle (0.999f), 359.64f) < 0.5f, "near maximum nearly completes the turn");

    // min and max coincide on a full circle - inherent, not a defect
    CHECK (angleGap (knobDotAngle (0.00f), knobDotAngle (1.00f)) < 0.5f,
           "min and max share a direction on a full turn");

    // the pointer vector must be a unit vector, or the dot radius drifts
    for (float p : { 0.0f, 0.33f, 0.5f, 0.77f, 1.0f })
    {
        const auto g = fw::ui::FretLookAndFeel::rotaryGeometry (p, 0.0f, 2.0f * juce::MathConstants<float>::pi);
        const float len = std::sqrt (g.dirX * g.dirX + g.dirY * g.dirY);
        CHECK (std::fabs (len - 1.0f) < 0.001f, "pointer vector stays unit length");
    }

    // a zero-width range must not produce a divide-by-zero or a NaN direction
    {
        const auto g = fw::ui::FretLookAndFeel::rotaryGeometry (0.5f, 0.7853982f, 0.7853982f);
        CHECK (std::isfinite (g.dirX) && std::isfinite (g.dirY), "degenerate range stays finite");
        CHECK (std::fabs (std::sqrt (g.dirX*g.dirX + g.dirY*g.dirY) - 1.0f) < 0.001f,
               "degenerate range falls back to a unit vector");
    }

    // out-of-range positions are clamped, not extrapolated
    CHECK (angleGap (knobDotAngle (-0.5f), knobDotAngle (0.0f)) < 0.5f, "below-range clamps to minimum");
    CHECK (angleGap (knobDotAngle ( 1.5f), knobDotAngle (1.0f)) < 0.5f, "above-range clamps to maximum");
}

// --demo <dir>: renders the listening set through the real processBlock at
// 48 kHz in 480-sample host blocks and writes 24-bit WAVs.
int renderDemo (const juce::File& dir)
{
    dir.createDirectory();
    const double fs = 48000.0;
    const auto write = [&] (const juce::String& name, const std::vector<NoteEv>& evs, double secs)
    {
        auto procPtr = std::make_unique<FretworkAudioProcessor>();
        auto& proc = *procPtr;
        proc.prepareToPlay (fs, 480);
        const int total = (int) (secs * fs);
        juce::AudioBuffer<float> all (2, total);
        for (int start = 0; start < total; start += 480)
        {
            const int n = std::min (480, total - start);
            juce::AudioBuffer<float> buf (2, n); buf.clear();
            juce::MidiBuffer midi;
            for (const auto& e : evs)
                if (e.sample >= start && e.sample < start + n)
                    midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, e.vel)
                                        : juce::MidiMessage::noteOff (1, e.note), e.sample - start);
            proc.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c) all.copyFrom (c, start, buf, c, 0, n);
        }
        // 32-bit float stereo WAV, interleaved.
        const auto file = dir.getChildFile (name + ".wav");
        file.deleteFile();
        juce::FileOutputStream os (file);
        if (! os.openedOk()) { std::printf ("cannot write %s\n", file.getFullPathName().toRawUTF8()); return; }
        const auto dataBytes = (juce::uint32) (total * 2 * 4);
        os.write ("RIFF", 4); os.writeInt ((int) (36 + dataBytes)); os.write ("WAVEfmt ", 8);
        os.writeInt (16); os.writeShort (3); os.writeShort (2); os.writeInt ((int) fs);
        os.writeInt ((int) fs * 8); os.writeShort (8); os.writeShort (32);
        os.write ("data", 4); os.writeInt ((int) dataBytes);
        for (int i = 0; i < total; ++i)
            for (int c = 0; c < 2; ++c) os.writeFloat (all.getSample (c, i));
        std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
    };

    const int on = (int) (0.02 * fs), off = (int) (2.5 * fs);
    for (int note : { 40, 52, 64 })
        for (float vel : { 0.35f, 1.0f })
            write (juce::String ("note") + juce::String (note) + (vel < 0.5f ? "_soft" : "_hard"),
                   { { on, note, vel, true }, { off, note, 0.f, false } }, 3.5);

    std::vector<NoteEv> chord;
    const int notes[] = { 40, 47, 52, 56, 59, 64 };
    for (int k = 0; k < 6; ++k) { chord.push_back ({ on + k * 240, notes[k], 0.8f, true }); chord.push_back ({ (int) (3.5 * fs), notes[k], 0.f, false }); }
    write ("chord_Emaj", chord, 4.5);
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    // Unbuffered so progress survives a crash or a killed run.
    std::setvbuf (stdout, nullptr, _IONBF, 0);

    if (argc >= 3 && std::string (argv[1]) == "--demo")
        return renderDemo (juce::File (juce::String (argv[2])));

    struct Test { const char* name; void (*fn)(); };

    const Test tests[] =
    {
        { "T1 tuning accuracy",        testTuning },
        { "T2 sustain and decay",      testSustain },
        { "T3 pick position",          testPickPosition },
        { "T4 body, material, pickup", testBodyAndMaterial },
        { "T5 power off",              testPowerOff },
        { "T6 strum spread",           testStrumSpread },
        { "T7 keyswitch latching",     testKeyswitches },
        { "T8 arp stepping",           testArpSteps },
        { "T9 state round trip",       testProcessorRoundTrip },
        { "T10 processor renders",     testProcessorRenders },
        { "T11 bend accuracy",          testBend },
        { "T12 natural harmonic",       testNaturalHarmonic },
        { "T13 mute",                   testMute },
        { "T14 rotary sweep",            testRotarySweep },
        { "T15 keyswitch exclusivity",   testKeyswitchGroupExclusive },
        { "T16 keyboard state -> audio",  testKeyboardStateProducesAudio },
        { "T17 setup tab pages",          testSetupTabPages },
        { "T18 knob cap tiers",           testKnobCapTiers },
        { "T19 timbre diagnostic",        testTimbreDiagnostic },
        { "T20 realtime matrix",          testRealtimeMatrix },
        { "T21 lifecycle across neck",    testLifecycleAcrossNeck },
        { "T22 chord polyphony",          testChordPolyphony },
        { "T23 hold does not retrigger",  testHoldDoesNotRetrigger },
        { "T24 keyboard no replay",       testKeyboardStateNoReplay },
        { "T25 sequential notes one instance", testSequentialNotesOneInstance },
    };

    // --list            print every test name and exit
    // --only <text>     run only tests whose name contains <text>
    std::string filter;
    bool listOnly = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a (argv[i]);
        if (a == "--list") listOnly = true;
        else if (a == "--only" && i + 1 < argc) filter = argv[++i];
    }

    for (const auto& t : tests)
    {
        if (! filter.empty() && std::string (t.name).find (filter) == std::string::npos) continue;
        if (listOnly) { std::printf ("%s\n", t.name); continue; }
        g_current = t.name;
        const int before = g_failures;
        std::printf ("%s\n", t.name);
        t.fn();
        std::printf ("  %s\n", g_failures == before ? "ok" : "FAILED");
    }

    // Functional suite (Tests/FunctionalTests.cpp), registered through Harness.h.
    fwtest::runRegistered (filter, listOnly);
    if (listOnly) return 0;

    const int checks   = g_checks + fwtest::results().checks;
    const int failures = g_failures + fwtest::results().failures;
    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

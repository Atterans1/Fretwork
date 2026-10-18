// SoundJudge: renders the plugin like a host, extracts the acoustic features
// a listener judges by, and writes them as JSON for a model to score.
//
// The judge is the second half of Docs/EVALUATION.md: the gates in
// Tests/SoundEvalTests.cpp assert the numbers, this tool supplies the numbers
// plus enough context for a model to say whether the result sounds like a
// guitar, and what to change if it does not.
//
// Usage: SoundJudge <outdir>
// Writes <outdir>/features-<case>.json, one per case.
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
struct Note { double t; int note; float vel; bool on; };

struct Features
{
    // Envelope
    double body01 = 0, mid05 = 0, tail30 = 0, peak = 0;
    // Decay shape: slope of log-energy vs time, dB per second.
    double decayDbPerSec = 0;
    // Onsets: count and total span.
    int    onsets = 0;
    double onsetSpan = 0;
    // Spectrum: first-difference brightness early (attack) and late (body).
    double brightAttack = 0, brightBody = 0;
    // Pitch stability: standard deviation of the detected fundamental, in cents.
    double pitchCentsDev = 0;
    // Roughness: fraction of frames whose autocorrelation peak is weak.
    double weakPeriodic = 0;
};

juce::AudioBuffer<float> render (const std::vector<Note>& evs, double secs,
                                 const std::vector<std::pair<const char*, float>>& params)
{
    auto proc = std::make_unique<FretworkAudioProcessor>();
    proc->prepareToPlay (48000.0, 480);
    for (const auto& kv : params)
        if (auto* p = proc->getState ().getParameter (kv.first))
            p->setValueNotifyingHost (p->convertTo0to1 (kv.second));

    const int total = (int) (secs * 48000.0);
    juce::AudioBuffer<float> all (2, total);
    for (int start = 0; start < total; start += 480)
    {
        const int n = std::min (480, total - start);
        juce::AudioBuffer<float> buf (2, n);
        buf.clear ();
        juce::MidiBuffer midi;
        for (const auto& e : evs)
            if (e.t * 48000.0 >= start && e.t * 48000.0 < start + n)
                midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note + fw::kWrittenOctave, e.vel)
                                    : juce::MidiMessage::noteOff (1, e.note + fw::kWrittenOctave),
                               (int) (e.t * 48000.0) - start);
        proc->processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) all.copyFrom (c, start, buf, c, 0, n);
    }
    return all;
}

double rms (const juce::AudioBuffer<float>& b, double t0, double t1)
{
    const int a = std::max (0, (int) (t0 * 48000.0)), e = std::min (b.getNumSamples (), (int) (t1 * 48000.0));
    if (e <= a) return 0.0;
    double s = 0.0;
    for (int i = a; i < e; ++i) s += 0.5 * ((double) b.getSample (0, i) * b.getSample (0, i)
                                          + (double) b.getSample (1, i) * b.getSample (1, i));
    return std::sqrt (s / (double) (e - a));
}

Features extract (const juce::AudioBuffer<float>& b)
{
    Features f;
    f.body01 = rms (b, 0.10, 0.60);
    f.mid05  = rms (b, 1.00, 1.60);
    f.tail30 = rms (b, 2.80, 3.40);

    double pk = 0.0;
    for (int i = 0; i < b.getNumSamples (); ++i)
        pk = std::max (pk, (double) std::fabs (b.getSample (0, i)));
    f.peak = pk;

    // Log-energy slope from the body to the tail.
    if (f.body01 > 1e-9 && f.tail30 > 1e-9)
    {
        const double dbBody = 20.0 * std::log10 (f.body01);
        const double dbTail = 20.0 * std::log10 (f.tail30);
        f.decayDbPerSec = (dbTail - dbBody) / 2.4;   // body centre to tail centre
    }

    // Onsets over the whole render.
    {
        const int hop = (int) (0.01 * 48000.0);
        std::vector<double> env;
        for (int i = 0; i + hop <= b.getNumSamples (); i += hop)
        {
            double e = 0.0;
            for (int j = i; j < i + hop; ++j) e += 0.5 * ((double) b.getSample (0, j) * b.getSample (0, j)
                                                       + (double) b.getSample (1, j) * b.getSample (1, j));
            env.push_back (std::sqrt (e / hop));
        }
        double prev = 1e9;
        for (size_t k = 1; k < env.size (); ++k)
        {
            double mn = 1e9;
            for (size_t q = (k >= 10 ? k - 10 : 0); q < k; ++q) mn = std::min (mn, env[q]);
            if (env[k] > 0.003 && env[k] > mn * 4.0
                && (f.onsets == 0 || (double) k * hop / 48000.0 - prev > 0.08))
            {
                const double t = (double) k * hop / 48000.0;
                if (f.onsets == 0) f.onsetSpan = 0.0;
                else f.onsetSpan = t - prev;
                ++f.onsets;
                prev = t;
            }
        }
    }

    const auto bright = [&] (double t0, double t1)
    {
        const int a = std::max (1, (int) (t0 * 48000.0)), e = std::min (b.getNumSamples (), (int) (t1 * 48000.0));
        double en = 0.0, d = 0.0;
        for (int i = a; i < e; ++i)
        {
            en += (double) b.getSample (0, i) * b.getSample (0, i);
            const double q = (double) b.getSample (0, i) - b.getSample (0, i - 1);
            d += q * q;
        }
        return en > 0 ? 48000.0 / (2.0 * 3.14159265358979323846) * std::sqrt (d / en) : 0.0;
    };
    f.brightAttack = bright (0.02, 0.10);
    f.brightBody   = bright (0.60, 1.20);

    // Pitch stability: period estimate per 100 ms frame, in cents.
    {
        const int win = 2400;
        std::vector<double> cents;
        for (int i = (int) (0.2 * 48000.0); i + win < (int) (2.4 * 48000.0); i += (int) (0.1 * 48000.0))
        {
            double best = -1.0;
            int    bestL = 0;
            for (int L = 40; L <= 1000; ++L)
            {
                double num = 0.0, e1 = 0.0, e2 = 0.0;
                for (int k = 0; k < win - L; ++k)
                { const double x = b.getSample (0, i + k), y = b.getSample (0, i + k + L); num += x * y; e1 += x * x; e2 += y * y; }
                const double c = e1 * e2 > 0 ? num / std::sqrt (e1 * e2) : 0.0;
                if (c > best) { best = c; bestL = L; }
            }
            if (bestL > 0 && best > 0.3)
                cents.push_back (1200.0 * std::log2 (48000.0 / bestL / 82.41));
        }
        if (cents.size () > 2)
        {
            double m = 0.0;
            for (double c : cents) m += c;
            m /= (double) cents.size ();
            double v = 0.0;
            for (double c : cents) v += (c - m) * (c - m);
            f.pitchCentsDev = std::sqrt (v / (double) cents.size ());
        }
    }
    return f;
}

void writeJson (const juce::File& out, const char* name, const Features& f)
{
    juce::String s;
    s << "{\n";
    s << "  \"case\": \"" << name << "\",\n";
    s << "  \"envelope\": {\n";
    s << "    \"body_0_6s_rms\": " << juce::String (f.body01, 5) << ",\n";
    s << "    \"mid_1_1_6s_rms\": " << juce::String (f.mid05, 5) << ",\n";
    s << "    \"tail_2_8_3_4s_rms\": " << juce::String (f.tail30, 5) << ",\n";
    s << "    \"peak\": " << juce::String (f.peak, 5) << ",\n";
    s << "    \"decay_db_per_sec\": " << juce::String (f.decayDbPerSec, 2) << "\n  },\n";
    s << "  \"rhythm\": {\n";
    s << "    \"onset_count\": " << f.onsets << ",\n";
    s << "    \"last_onset_gap_sec\": " << juce::String (f.onsetSpan, 4) << "\n  },\n";
    s << "  \"timbre\": {\n";
    s << "    \"brightness_attack_hz\": " << juce::String (f.brightAttack, 1) << ",\n";
    s << "    \"brightness_body_hz\": " << juce::String (f.brightBody, 1) << "\n  },\n";
    s << "  \"intonation\": {\n";
    s << "    \"pitch_dev_cents\": " << juce::String (f.pitchCentsDev, 2) << "\n  }\n";
    s << "}\n";
    out.deleteFile ();
    out.create ();
    juce::FileOutputStream os (out);
    os.writeText (s, false, false, nullptr);
}
} // namespace

int main (int argc, char** argv)
{
    const juce::String dir = argc > 1 ? juce::String (argv[1]) : juce::String (".");
    juce::File d (dir);
    d.createDirectory ();

    // Each case: a MIDI passage plus the parameter state that defines it.
    struct Case { const char* name; std::vector<Note> evs; double secs;
                  std::vector<std::pair<const char*, float>> params; };

    std::vector<Case> cases;
    cases.push_back ({ "standard_soft_low_E", { { 0.02, 40, 0.40f, true }, { 3.4, 40, 0.f, false } }, 3.6, {} });
    cases.push_back ({ "standard_hard_low_E", { { 0.02, 40, 126.f / 127.f, true }, { 3.4, 40, 0.f, false } }, 3.6, {} });
    cases.push_back ({ "pure_nickel_hard_low_E",
                       { { 0.02, 40, 126.f / 127.f, true }, { 3.4, 40, 0.f, false } }, 3.6,
                       { { "stringSet", 1.f }, { "material", 4.f } } });
    cases.push_back ({ "twelve_string_low_E",
                       { { 0.02, 40, 126.f / 127.f, true }, { 3.4, 40, 0.f, false } }, 3.6,
                       { { "guitarType", 1.f } } });

    for (const auto& c : cases)
    {
        const auto buf = render (c.evs, c.secs, c.params);
        const Features f = extract (buf);
        writeJson (d.getChildFile (juce::String ("features-") + c.name + ".json"), c.name, f);
        std::printf ("%s: body %.4f tail %.4f decay %.1f dB/s onsets %d attack %.0f Hz body %.0f Hz pitch dev %.1f c\n",
                     c.name, f.body01, f.tail30, f.decayDbPerSec, f.onsets, f.brightAttack, f.brightBody, f.pitchCentsDev);
    }
    return 0;
}

#pragma once
// Fretwork test harness.
//
// fwtest::Plugin  - one real FretworkAudioProcessor driven exactly like a host:
//                   parameters in real units, MIDI at exact sample times,
//                   audio rendered through processBlock in host-sized blocks.
// fwtest::Take    - the rendered audio plus the measurements tests assert on.
// FW_TEST/EXPECT  - self-registering tests; main.cpp runs them after T1..Tn.
//
// Usage:
//     FW_TEST ("midi: note sounds")
//     {
//         fwtest::Plugin p;                      // 48 kHz, 480-sample blocks
//         p.noteOn (0.01, 52, 0.8f).noteOff (1.0, 52);
//         auto take = p.render (1.5);
//         EXPECT (take.rms (0.05, 0.3) > 0.01);
//     }

#include "PluginProcessor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fwtest
{

//==============================================================================
// Registry and assertions
struct TestCase { std::string name; std::function<void()> fn; };

inline std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }

struct Results { int checks = 0; int failures = 0; std::string current; };
inline Results& results() { static Results r; return r; }

inline void expect (bool ok, const char* what, const char* file, int line)
{
    auto& r = results();
    ++r.checks;
    if (ok) return;
    ++r.failures;
    const char* base = std::strrchr (file, '\\') != nullptr ? std::strrchr (file, '\\') + 1 : file;
    std::printf ("  FAIL  [%s] %s   (%s:%d)\n", r.current.c_str(), what, base, line);
}

struct Registrar { Registrar (const char* n, std::function<void()> f) { registry().push_back ({ n, std::move (f) }); } };

#define FW_CAT2(a, b) a##b
#define FW_CAT(a, b) FW_CAT2(a, b)
#define FW_TEST(name) \
    static void FW_CAT(fwTest_, __LINE__)(); \
    static ::fwtest::Registrar FW_CAT(fwReg_, __LINE__) (name, &FW_CAT(fwTest_, __LINE__)); \
    static void FW_CAT(fwTest_, __LINE__)()

#define EXPECT(cond)          ::fwtest::expect ((cond), #cond, __FILE__, __LINE__)
#define EXPECT_MSG(cond, msg) ::fwtest::expect ((cond), (msg), __FILE__, __LINE__)

/** Runs every registered test whose name contains filter (empty = all).
    Returns the number of failures added. */
inline int runRegistered (const std::string& filter, bool listOnly)
{
    auto& r = results();
    const int before = r.failures;
    for (auto& t : registry())
    {
        if (! filter.empty() && t.name.find (filter) == std::string::npos) continue;
        if (listOnly) { std::printf ("%s\n", t.name.c_str()); continue; }
        r.current = t.name;
        const int f0 = r.failures;
        std::printf ("%s\n", t.name.c_str());
        const auto t0 = std::chrono::steady_clock::now();
        t.fn();
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  %s (%.1f s)\n", r.failures == f0 ? "ok" : "FAILED", secs);
    }
    return r.failures - before;
}

//==============================================================================
inline double midiToHz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

/** Rendered stereo audio plus measurements. Times are in seconds. */
struct Take
{
    double fs = 48000.0;
    std::vector<float> left, right;

    size_t at (double t) const { return std::min (left.size(), (size_t) std::max (0.0, t * fs)); }
    double seconds() const { return (double) left.size() / fs; }

    bool finite() const
    {
        for (size_t i = 0; i < left.size(); ++i)
            if (! std::isfinite (left[i]) || ! std::isfinite (right[i])) return false;
        return true;
    }

    double peak (double t0 = 0, double t1 = 1e9) const
    {
        double p = 0;
        for (size_t i = at (t0); i < at (t1); ++i) p = std::max ({ p, (double) std::fabs (left[i]), (double) std::fabs (right[i]) });
        return p;
    }

    double rms (double t0 = 0, double t1 = 1e9) const
    {
        const size_t a = at (t0), b = at (t1);
        if (b <= a) return 0;
        double e = 0;
        for (size_t i = a; i < b; ++i) e += 0.5 * ((double) left[i] * left[i] + (double) right[i] * right[i]);
        return std::sqrt (e / (double) (b - a));
    }

    /** RMS of (L - R): zero for a mono signal. */
    double sideRms (double t0 = 0, double t1 = 1e9) const
    {
        const size_t a = at (t0), b = at (t1);
        if (b <= a) return 0;
        double e = 0;
        for (size_t i = a; i < b; ++i) { const double d = (double) left[i] - right[i]; e += d * d; }
        return std::sqrt (e / (double) (b - a));
    }

    /** Fundamental by normalised autocorrelation with parabolic refinement,
        searched within +-6% of expectHz. Returns 0 when nothing periodic. */
    double pitch (double t0, double t1, double expectHz, double* quality = nullptr) const
    {
        const size_t a = at (t0), b = at (t1);
        const int lo = std::max (2, (int) (fs / expectHz * 0.94)), hi = (int) (fs / expectHz * 1.06) + 2;
        if (b <= a + (size_t) hi * 2) return 0;
        std::vector<double> ac ((size_t) hi + 2, -1.0);
        for (int L = lo - 1; L <= hi + 1; ++L)
        {
            double num = 0, e1 = 0, e2 = 0;
            for (size_t i = a; i + (size_t) L < b; ++i)
            { const double p = left[i], q = left[i + (size_t) L]; num += p * q; e1 += p * p; e2 += q * q; }
            ac[(size_t) L] = e1 * e2 > 0 ? num / std::sqrt (e1 * e2) : -1.0;
        }
        int best = lo;
        for (int L = lo; L <= hi; ++L) if (ac[(size_t) L] > ac[(size_t) best]) best = L;
        if (quality != nullptr) *quality = ac[(size_t) best];
        const double y0 = ac[(size_t) best - 1], y1 = ac[(size_t) best], y2 = ac[(size_t) best + 1];
        const double den = y0 - 2 * y1 + y2;
        const double off = std::fabs (den) > 1e-12 ? 0.5 * (y0 - y2) / den : 0.0;
        return fs / ((double) best + off);
    }

    static double cents (double hz, double refHz) { return hz > 0 && refHz > 0 ? 1200.0 * std::log2 (hz / refHz) : 1e9; }

    /** First-difference brightness estimate in Hz (tracks spectral centroid). */
    double brightness (double t0, double t1) const
    {
        const size_t a = std::max<size_t> (1, at (t0)), b = at (t1);
        double e = 0, d = 0;
        for (size_t i = a; i < b; ++i) { e += (double) left[i] * left[i]; const double q = left[i] - left[i - 1]; d += q * q; }
        return e > 0 ? fs / (2.0 * 3.14159265358979323846) * std::sqrt (d / e) : 0;
    }

    /** Onset times: 20 ms envelope rises of more than 4x over the previous
        100 ms minimum, above an absolute floor, at least minGap apart. */
    std::vector<double> onsets (double floor = 0.003, double minGap = 0.08) const
    {
        const size_t hop = (size_t) (0.01 * fs);
        std::vector<double> env;
        for (size_t i = 0; i + hop <= left.size(); i += hop)
        {
            double e = 0;
            for (size_t j = i; j < i + hop; ++j) e += 0.5 * ((double) left[j] * left[j] + (double) right[j] * right[j]);
            env.push_back (std::sqrt (e / (double) hop));
        }
        std::vector<double> out;
        for (size_t k = 1; k < env.size(); ++k)
        {
            double mn = 1e9;
            for (size_t q = (k >= 10 ? k - 10 : 0); q < k; ++q) mn = std::min (mn, env[q]);
            const double t = (double) k * (double) hop / fs;
            if (env[k] > floor && env[k] > mn * 4.0 && (out.empty() || t - out.back() > minGap)) out.push_back (t);
        }
        return out;
    }
};

//==============================================================================
/** One plugin instance driven like a host. */
class Plugin
{
public:
    explicit Plugin (double sampleRate = 48000.0, int blockSize = 480)
        : fs (sampleRate), block (blockSize), hostBlock (blockSize), proc (std::make_unique<FretworkAudioProcessor>())
    {
        proc->prepareToPlay (fs, block);
    }

    /** Hosts may deliver blocks larger or smaller than they announced in
        prepareToPlay; this sets what render() actually hands to processBlock. */
    Plugin& hostBlockSize (int n) { hostBlock = std::max (1, n); return *this; }

    FretworkAudioProcessor& processor() { return *proc; }

    // ---- parameters, in the parameter's own units (ms, %, dB, index, 0/1)
    juce::RangedAudioParameter* param (const char* id) { return proc->getState().getParameter (id); }

    Plugin& set (const char* id, float realValue)
    {
        if (auto* p = param (id)) p->setValueNotifyingHost (p->convertTo0to1 (realValue));
        return *this;
    }

    float get (const char* id)
    {
        auto* p = param (id);
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : std::nanf ("");
    }

    // ---- MIDI, scheduled at absolute times from the start of the next render
    Plugin& noteOn  (double t, int note, float vel = 0.8f) { return add (t, juce::MidiMessage::noteOn (1, note, vel)); }
    Plugin& noteOff (double t, int note)                   { return add (t, juce::MidiMessage::noteOff (1, note)); }
    Plugin& cc      (double t, int number, int value)      { return add (t, juce::MidiMessage::controllerEvent (1, number, value)); }
    Plugin& bend    (double t, int value14)                { return add (t, juce::MidiMessage::pitchWheel (1, value14)); }
    Plugin& chord   (double t, std::initializer_list<int> notes, float vel = 0.8f, double spreadSec = 0.0)
    {
        int k = 0;
        for (int n : notes) noteOn (t + spreadSec * k++, n, vel);
        return *this;
    }

    /** Renders seconds of audio, delivering scheduled MIDI sample-accurately.
        Time keeps running across calls; events are relative to the session start. */
    Take render (double seconds)
    {
        Take take;
        take.fs = fs;
        const int64_t total = (int64_t) (seconds * fs);
        take.left.reserve ((size_t) total);
        take.right.reserve ((size_t) total);

        juce::AudioBuffer<float> buf (2, hostBlock);
        for (int64_t done = 0; done < total; )
        {
            const int n = (int) std::min<int64_t> (hostBlock, total - done);
            buf.setSize (2, n, false, false, true);
            buf.clear();
            juce::MidiBuffer midi;
            for (const auto& e : events)
                if (e.sample >= clock && e.sample < clock + n)
                    midi.addEvent (e.msg, (int) (e.sample - clock));
            proc->processBlock (buf, midi);
            take.left.insert  (take.left.end(),  buf.getReadPointer (0), buf.getReadPointer (0) + n);
            take.right.insert (take.right.end(), buf.getReadPointer (1), buf.getReadPointer (1) + n);
            clock += n;
            done  += n;
        }
        return take;
    }

    // ---- host state
    juce::MemoryBlock save()                        { juce::MemoryBlock m; proc->getStateInformation (m); return m; }
    void load (const juce::MemoryBlock& m)          { proc->setStateInformation (m.getData(), (int) m.getSize()); }

    double sampleRate() const { return fs; }

private:
    struct Ev { int64_t sample; juce::MidiMessage msg; };

    Plugin& add (double t, const juce::MidiMessage& m)
    {
        events.push_back ({ (int64_t) std::llround (t * fs), m });
        return *this;
    }

    double fs;
    int block;
    int hostBlock;
    std::unique_ptr<FretworkAudioProcessor> proc;
    std::vector<Ev> events;
    int64_t clock = 0;
};

} // namespace fwtest

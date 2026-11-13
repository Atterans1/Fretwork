// GlideRender: renders legato slides and picked references through the real
// plugin processor, writes 16-bit stereo WAVs, and prints the measured pitch
// track through each glide and the attack energy at each onset.
//
// Usage: GlideRender <outdir>
#include "PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr double kFs    = 48000.0;
constexpr int    kBlock = 480;

struct Note { double t; int note; float vel; bool on; };   // note = sounding MIDI
struct Case { const char* name; std::vector<Note> notes; double secs; double onset; };

std::vector<Case> cases()
{
    // Slides start at velocity 120 (kSlideVel is 110); below that a semitone is a hammer-on with no glide.
    return {
        { "semi_up_E3_F3",   { { 0.05, 52, 90, true }, { 1.00, 53, 120, true }, { 1.10, 52, 0, false } }, 2.4, 1.00 },
        { "semi_down_F3_E3", { { 0.05, 53, 90, true }, { 1.00, 52, 120, true }, { 1.10, 53, 0, false } }, 2.4, 1.00 },
        { "tone_up_E3_Fs3",  { { 0.05, 52, 90, true }, { 1.00, 54, 120, true }, { 1.10, 52, 0, false } }, 2.4, 1.00 },
        { "tone_down_Fs3_E3",{ { 0.05, 54, 90, true }, { 1.00, 52, 120, true }, { 1.10, 54, 0, false } }, 2.4, 1.00 },
        { "picked_F3",       { { 0.05, 53, 120, true }, { 1.10, 53, 0, false } }, 1.6, 0.05 },
        { "picked_E3",       { { 0.05, 52, 120, true }, { 1.10, 52, 0, false } }, 1.6, 0.05 },
    };
}

juce::AudioBuffer<float> render (const std::vector<Note>& evs, double secs)
{
    auto proc = std::make_unique<FretworkAudioProcessor>();
    proc->prepareToPlay (kFs, kBlock);

    const int total = (int) (secs * kFs);
    juce::AudioBuffer<float> all (2, total);
    for (int start = 0; start < total; start += kBlock)
    {
        const int n = std::min (kBlock, total - start);
        juce::AudioBuffer<float> buf (2, n);
        buf.clear ();
        juce::MidiBuffer midi;
        for (const auto& e : evs)
            if (e.t * kFs >= start && e.t * kFs < start + n)
                midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note + fw::kWrittenOctave, e.vel)
                                    : juce::MidiMessage::noteOff (1, e.note + fw::kWrittenOctave),
                               (int) (e.t * kFs) - start);
        proc->processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) all.copyFrom (c, start, buf, c, 0, n);
    }
    return all;
}

// Mean-square energy in dB over [t0, t1); mono mix of both channels.
double db (const juce::AudioBuffer<float>& b, double t0, double t1)
{
    const int a = std::max (0, (int) (t0 * kFs)), e = std::min (b.getNumSamples (), (int) (t1 * kFs));
    double s = 0.0;
    for (int i = a; i < e; ++i) s += 0.5 * ((double) b.getSample (0, i) * b.getSample (0, i)
                                          + (double) b.getSample (1, i) * b.getSample (1, i));
    return e > a ? 10.0 * std::log10 (s / (double) (e - a) + 1e-20) : -200.0;
}

// Autocorrelation pitch per frame: 20 ms window centred on the frame time, lag
// limited to 155-200 Hz (E3 to F#3) so transients cannot lock to an octave, sub-sample lag by parabolic fit.
struct Frame { double t, hz, conf; };
std::vector<Frame> track (const juce::AudioBuffer<float>& b, double t0, double t1, double step)
{
    const int win = 960, lagMin = 240, lagMax = 310;
    std::vector<Frame> out;
    for (double t = t0; t < t1; t += step)
    {
        const int s = (int) (t * kFs) - win / 2;
        if (s < 0) continue;
        if (s + win + lagMax >= b.getNumSamples ()) break;
        std::vector<double> c (lagMax + 2, 0.0);
        const float* x = b.getReadPointer (0);
        double e1 = 0.0;
        for (int k = 0; k < win; ++k) e1 += (double) x[s + k] * x[s + k];
        for (int L = lagMin - 1; L <= lagMax + 1; ++L)
        {
            double num = 0.0, e2 = 0.0;
            for (int k = 0; k < win; ++k) { const double y = x[s + k + L]; num += x[s + k] * y; e2 += y * y; }
            c[L] = e1 * e2 > 0 ? num / std::sqrt (e1 * e2) : 0.0;
        }
        int bestL = 0; double best = -1.0;
        for (int L = lagMin; L <= lagMax; ++L) if (c[L] > best) { best = c[L]; bestL = L; }
        if (best < 0.5) continue;
        const double y0 = c[bestL - 1], y1 = c[bestL], y2 = c[bestL + 1];
        const double den = y0 - 2.0 * y1 + y2;
        const double Lf = bestL + (std::fabs (den) > 1e-12 ? 0.5 * (y0 - y2) / den : 0.0);
        out.push_back ({ t, kFs / Lf, best });
    }
    return out;
}

double midiHz (int m) { return 440.0 * std::pow (2.0, (m - 69) / 12.0); }
double cents (double a, double b) { return 1200.0 * std::log2 (a / b); }

bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
{
    f.deleteFile ();
    auto* os = new juce::FileOutputStream (f);   // the writer takes ownership of the stream
    if (os->failedToOpen ()) { delete os; return false; }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os, kFs, 2, 16, {}, 0));
    if (! w) return false;
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples ());
}
} // namespace

int main (int argc, char** argv)
{
    const juce::File dir = juce::File (argc > 1 ? juce::String (argv[1]) : juce::String ("."));
    dir.createDirectory ();

    for (const auto& c : cases ())
    {
        const auto b = render (c.notes, c.secs);
        if (! writeWav (dir.getChildFile (juce::String (c.name) + ".wav"), b))
        { std::printf ("FAIL write %s\n", c.name); return 1; }

        std::printf ("== %s  (wav written)\n", c.name);
        const double pre  = db (b, c.onset - 0.10, c.onset);
        const double a50  = db (b, c.onset, c.onset + 0.05);
        const double a150 = db (b, c.onset + 0.05, c.onset + 0.15);
        std::printf ("attack dB: pre100ms=%.1f  first50ms=%.1f  50to150ms=%.1f  rise50=%.1f\n",
                     pre, a50, a150, a50 - pre);

        // Glide cases: pitch from the first held note to the target note.
        const bool glide = std::string (c.name).rfind ("semi", 0) == 0 || std::string (c.name).rfind ("tone", 0) == 0;
        if (! glide) continue;
        const int fromM = (int) c.notes[0].note, toM = (int) c.notes[1].note;
        const double fromHz = midiHz (fromM), toHz = midiHz (toM), total = cents (toHz, fromHz);
        const auto tr = track (b, c.onset - 0.01, c.onset + 0.12, 0.0025);
        std::printf ("pitch: from %.2f Hz to %.2f Hz (%.0f cents), %zu frames\n", fromHz, toHz, total, tr.size ());
        double t50 = -1, t90 = -1;
        for (const auto& f : tr)
        {
            const double prog = cents (f.hz, fromHz) / total;
            if (t50 < 0 && prog >= 0.5) t50 = f.t - c.onset;
            if (t90 < 0 && prog >= 0.9) t90 = f.t - c.onset;
            std::printf ("  t=%6.1f ms  %8.2f Hz  progress=%5.1f%%  conf=%.2f\n",
                         (f.t - c.onset) * 1000.0, f.hz, 100.0 * prog, f.conf);
        }
        std::printf ("glide 50%% at %.1f ms, 90%% at %.1f ms\n", t50 * 1000.0, t90 * 1000.0);
    }
    return 0;
}

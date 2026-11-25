// IsoRender: each fret rendered alone in its own fresh GuitarEngine (no carry-over, no onset grid).
// Usage: IsoRender <outdir> <label> <string 0..5> <velocity01> [room%] [body] [micBlend%] [stiffness%] [age%]
// Analysis signal is the mid mix 0.5*(L+R). Envelope is a 2 ms RMS. All thresholds are relative to the
// note's own envelope peak, so the result does not depend on any scheduling grid.
// Writes <label>.wav (stereo, 24-bit, 25 notes x 1.5 s concatenated), <label>_env.csv (1 ms steps, first 400 ms, dBFS),
// and prints one CSV row per fret to stdout.
#include "Contracts.h"
#include "dsp/GuitarEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace
{
constexpr double kFs    = 48000.0;
constexpr int    kBlock = 480;
constexpr int    kFrets = 25;
constexpr int    kTotal = 72000;  // 1.5 s per isolated note
constexpr int    kWin   = 96;     // 2 ms RMS window
constexpr float  kOpenPitch[fw::kNumStrings] = { 40.f, 45.f, 50.f, 55.f, 59.f, 64.f };

double ms (int s) { return s < 0 ? -1.0 : s * 1000.0 / kFs; }
double db (double x) { return 20.0 * std::log10 (x + 1e-12); }
} // namespace

int main (int argc, char** argv)
{
    if (argc < 5) { std::fprintf (stderr, "usage: IsoRender <outdir> <label> <string> <velocity01> [room%%] [body] [micBlend%%] [stiffness%%] [age%%]\n"); return 2; }
    const juce::File dir (argv[1]);
    dir.createDirectory ();
    const juce::String label (argv[2]);
    const int   string    = std::atoi (argv[3]);
    const float vel       = (float) std::atof (argv[4]);
    const float room      = argc > 5 ? (float) std::atof (argv[5]) : 12.f;
    const int   body      = argc > 6 ? std::atoi (argv[6]) : 0;
    const float micBlend  = argc > 7 ? (float) std::atof (argv[7]) : 70.f;
    const float stiffness = argc > 8 ? (float) std::atof (argv[8]) : 100.f;
    const float age       = argc > 9 ? (float) std::atof (argv[9]) : 20.f;
    if (string < 0 || string >= fw::kNumStrings) { std::fprintf (stderr, "bad string\n"); return 2; }

    juce::AudioBuffer<float> all (2, kFrets * kTotal);
    all.clear ();
    std::vector<std::vector<double>> envs (kFrets);

    std::printf ("fret,pitch,onset1_ms,onset5_ms,onset10_ms,onset50_ms,firstabs1_ms,peak_ms,peak_dBFS,tail_dBFS,tail_rel_dB,"
                 "f0_Hz,onset10_periods,spk_ms,swell_gain_dB,class\n");
    for (int f = 0; f < kFrets; ++f)
    {
        // GuitarEngine holds several MB of delay lines inline: keep it off the stack.
        auto eng = std::make_unique<fw::dsp::GuitarEngine> ();
        eng->prepare (kFs, kBlock);
        fw::dsp::EngineParams ep {};
        ep.room = room;
        ep.body = body;
        ep.micBlend = micBlend;
        for (int k = 0; k < fw::kNumStrings; ++k) { ep.strings[(std::size_t) k].stiffness = stiffness; ep.strings[(std::size_t) k].age = age; }
        eng->setParams (ep);

        std::vector<double> mid (kTotal, 0.0);
        std::vector<float> L (kBlock), R (kBlock);
        for (int start = 0; start < kTotal; start += kBlock)
        {
            const int n = std::min (kBlock, kTotal - start);
            if (start == 0)
            {
                fw::StringEvent e;
                e.sampleOffset = 0;
                e.string   = string;
                e.type     = fw::EventType::Pluck;
                e.pitch    = kOpenPitch[string] + (float) f;
                e.velocity = vel;
                eng->handle (e);
            }
            eng->render (L.data (), R.data (), n);
            for (int i = 0; i < n; ++i)
            {
                const std::size_t k = (std::size_t) i;
                mid[(std::size_t) (start + i)] = 0.5 * ((double) L[k] + (double) R[k]);
                all.setSample (0, f * kTotal + start + i, L[k]);
                all.setSample (1, f * kTotal + start + i, R[k]);
            }
        }

        // 2 ms RMS envelope of the mid signal (trailing window)
        std::vector<double>& env = envs[(std::size_t) f];
        env.assign ((std::size_t) kTotal, 0.0);
        double acc = 0.0;
        for (int i = 0; i < kTotal; ++i)
        {
            const double x = mid[(std::size_t) i];
            acc += x * x;
            if (i >= kWin) { const double y = mid[(std::size_t) (i - kWin)]; acc -= y * y; }
            env[(std::size_t) i] = std::sqrt (std::max (acc, 0.0) / kWin);
        }
        int pkIdx = 0;
        for (int i = 1; i < kTotal; ++i) if (env[(std::size_t) i] > env[(std::size_t) pkIdx]) pkIdx = i;
        const double pk = env[(std::size_t) pkIdx];

        auto firstAbove = [&] (double frac) { for (int i = 0; i < kTotal; ++i) if (env[(std::size_t) i] >= frac * pk) return i; return -1; };
        const int o1 = firstAbove (0.01), o5 = firstAbove (0.05), o10 = firstAbove (0.10), o50 = firstAbove (0.50);

        double absPk = 0.0;
        for (double x : mid) absPk = std::max (absPk, std::fabs (x));
        int abs1 = -1;
        for (int i = 0; i < kTotal; ++i) if (std::fabs (mid[(std::size_t) i]) >= 0.01 * absPk) { abs1 = i; break; }

        const int t0 = (int) (0.7 * kFs), t1 = (int) (0.9 * kFs);
        double tt = 0.0;
        for (int i = t0; i < t1; ++i) tt += mid[(std::size_t) i] * mid[(std::size_t) i];
        tt = std::sqrt (tt / (t1 - t0));

        // Classification envelope: RMS over two periods of the note's fundamental, so the within-cycle ripple of a
        // low string cannot look like separate swells. (The 2 ms envelope above is only used for onset/peak timing.)
        const double f0 = 440.0 * std::pow (2.0, ((double) kOpenPitch[string] + f - 69.0) / 12.0);
        const int win2 = std::max (kWin, (int) std::lround (2.0 * kFs / f0));
        std::vector<double> senv ((std::size_t) kTotal, 0.0);
        acc = 0.0;
        for (int i = 0; i < kTotal; ++i)
        {
            const double x = mid[(std::size_t) i];
            acc += x * x;
            if (i >= win2) { const double y = mid[(std::size_t) (i - win2)]; acc -= y * y; }
            senv[(std::size_t) i] = std::sqrt (std::max (acc, 0.0) / win2);
        }
        int spk = 0;
        for (int i = 1; i < kTotal; ++i) if (senv[(std::size_t) i] > senv[(std::size_t) spk]) spk = i;
        const double spkv = senv[(std::size_t) spk];
        int so10 = -1;
        for (int i = 0; i < kTotal; ++i) if (senv[(std::size_t) i] >= 0.1 * spkv) { so10 = i; break; }
        // Attack level: highest smoothed envelope in the first 25 ms after the 1 % onset. Swell gain: how far the
        // note's later peak sits above that attack level.
        const int so1 = [&] { for (int i = 0; i < kTotal; ++i) if (senv[(std::size_t) i] >= 0.01 * spkv) return i; return 0; } ();
        double attack = 0.0;
        for (int i = so1; i < std::min (kTotal, so1 + (int) (0.025 * kFs)); ++i) attack = std::max (attack, senv[(std::size_t) i]);
        const double swellGain = db (spkv) - db (attack);
        const double onsetPeriods = ms (o10) * f0 / 1000.0;
        const char* cls = "normal";
        if (spkv <= 1e-6)                                                   cls = "silent";
        else if (spk > so1 + (int) (0.025 * kFs) && swellGain >= 3.0)       cls = "swell";
        else if (onsetPeriods > 2.5)                                        cls = "late_onset";
        else if (spk > so10 + (int) (0.030 * kFs))                          cls = "slow_rise";

        std::printf ("%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.1f,%.1f,%.1f,%.1f,%.2f,%.2f,%.1f,%s\n", f, (int) (kOpenPitch[string] + f),
                     ms (o1), ms (o5), ms (o10), ms (o50), ms (abs1), ms (pkIdx), db (absPk), db (tt), db (tt) - db (pk),
                     f0, onsetPeriods, ms (spk), swellGain, cls);
    }

    juce::String env = "t_ms";
    for (int f = 0; f < kFrets; ++f) env += ",f" + juce::String (f);
    env += "\n";
    for (int t = 0; t < 400; ++t)
    {
        env += juce::String (t);
        for (int f = 0; f < kFrets; ++f) env += "," + juce::String (db (envs[(std::size_t) f][(std::size_t) (t * 48)]), 1);
        env += "\n";
    }
    dir.getChildFile (label + "_env.csv").replaceWithText (env);

    const juce::File wavFile = dir.getChildFile (label + ".wav");
    wavFile.deleteFile ();
    std::unique_ptr<juce::FileOutputStream> fos (wavFile.createOutputStream ().release ());
    if (fos == nullptr) return 1;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (fos.get (), kFs, 2, 24, {}, 0));
    if (w == nullptr) return 1;
    fos.release ();
    if (! w->writeFromAudioSampleBuffer (all, 0, kFrets * kTotal)) return 1;
    w.reset ();
    std::fprintf (stderr, "%s: string %d velocity %.3f room %.1f body %d mic %.1f stiff %.1f age %.1f\n",
                  label.toRawUTF8 (), string, (double) vel, (double) room, body, (double) micBlend, (double) stiffness, (double) age);
    return 0;
}

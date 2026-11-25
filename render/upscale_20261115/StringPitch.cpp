// StringPitch: replicates Tests/main.cpp T1 (testTuning) exactly, standalone GuitarString at 44.1 kHz.
// Prints the test's autocorrelation pitch (cents) and an independent zero-padded FFT pitch on the same capture.
#include "dsp/GuitarString.h"
#include "dsp/StringModelParams.h"
#include "Contracts.h"
#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <string>

namespace {
double kSampleRate = 44100.0;
double midiToHz (double pitch) noexcept { return 440.0 * std::pow (2.0, (pitch - 69.0) / 12.0); }
fw::dsp::StringModelParams steelParams()
{
    fw::dsp::StringModelParams p {};
    p.material = 0; p.age = 20.f; p.stiffness = 100.f; p.pickPos = 15.f; p.pickHard = 50.f;
    p.sustain = 100.f; p.fretNoise = 30.f; p.bodyLossScale = 1.f; p.scaleLengthM = 0.648f;
    return p;
}
fw::StringEvent pluckEvent (int stringIdx, float pitch, float vel = 0.9f)
{
    fw::StringEvent e {}; e.string = stringIdx; e.pitch = pitch; e.velocity = vel; e.type = fw::EventType::Pluck; return e;
}
double normalisedAcf (const std::vector<float>& x, int lag)
{
    const int n = (int) x.size(); double a = 0.0, e1 = 0.0, e2 = 0.0;
    for (int i = 0; i + lag < n; ++i) { const double p = x[(std::size_t) i], q = x[(std::size_t) (i + lag)]; a += p * q; e1 += p * p; e2 += q * q; }
    return a / std::sqrt (e1 * e2 + 1e-30);
}
double detectPitch (const std::vector<float>& x, double fs, double expectedHz)
{
    const int n = (int) x.size();
    const int centre = (int) (fs / expectedHz);
    const int lo = std::max (2, (int) (centre * 0.85)), hi = std::min (n - 2, (int) (centre * 1.15));
    int best = lo; double bestVal = -1.0;
    for (int lag = lo; lag <= hi; ++lag) { const double v = normalisedAcf (x, lag); if (v > bestVal) { bestVal = v; best = lag; } }
    if (bestVal < 0.3) return 0.0;
    double refined = (double) best;
    if (best > lo && best < hi)
    {
        const double y0 = normalisedAcf (x, best - 1), y1 = bestVal, y2 = normalisedAcf (x, best + 1);
        const double denom = y0 - 2.0 * y1 + y2;
        if (std::fabs (denom) > 1e-12) refined = (double) best + 0.5 * (y0 - y2) / denom;
    }
    return fs / refined;
}
void fft (std::vector<double>& re, std::vector<double>& im)
{
    const std::size_t n = re.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) { std::size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); } }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.14159265358979323846 / (double) len, wr = std::cos (ang), wi = std::sin (ang);
        for (std::size_t i = 0; i < n; i += len)
        {
            double cr = 1.0, ci = 0.0;
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const std::size_t a = i + k, c = a + len / 2;
                const double tr = re[c] * cr - im[c] * ci, ti = re[c] * ci + im[c] * cr;
                re[c] = re[a] - tr; im[c] = im[a] - ti; re[a] += tr; im[a] += ti;
                const double nc = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nc;
            }
        }
    }
}
// Direct DFT (Hann window) on a fine frequency grid around the target. Independent of the autocorrelation.
double dftPitch (const std::vector<float>& x, double fs, double expectedHz)
{
    const std::size_t N = x.size();
    std::vector<double> w (N);
    for (std::size_t i = 0; i < N; ++i) w[i] = x[i] * (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (double) i / (double) (N - 1)));
    auto power = [&] (double hz)
    {
        double re = 0.0, im = 0.0;
        const double wstep = 2.0 * 3.14159265358979323846 * hz / fs;
        for (std::size_t i = 0; i < N; ++i) { re += w[i] * std::cos (wstep * (double) i); im -= w[i] * std::sin (wstep * (double) i); }
        return re * re + im * im;
    };
    double bestHz = expectedHz, bestP = -1.0;
    const double step = 0.05;
    for (double hz = expectedHz * 0.97; hz <= expectedHz * 1.03; hz += step) { const double p = power (hz); if (p > bestP) { bestP = p; bestHz = hz; } }
    const double pm = power (bestHz - step), pc = bestP, pp = power (bestHz + step);
    const double d = 0.5 * (pm - pp) / (pm - 2.0 * pc + pp);
    return bestHz + d * step;
}
}

#ifndef SP_FIRST
#define SP_FIRST 84
#endif
#ifndef SP_LAST
#define SP_LAST 85
#endif
int main (int argc, char** argv)
{
    if (argc > 1) kSampleRate = std::atof (argv[1]);
    std::printf ("fs=%.0f  midi  target_Hz  acf_cents\n", kSampleRate);
    fw::dsp::GuitarString s;
    s.prepare (kSampleRate);
    s.setModel (steelParams());
    for (int midi = SP_FIRST; midi <= SP_LAST; midi += 4)
    {
        s.reset();
        const double target = midiToHz (midi);
        const fw::StringEvent e = pluckEvent (0, (float) midi);
        s.pluck (e, 0);
        for (int i = 0; i < 2000; ++i) (void) s.tick (0.f);
        std::vector<float> buf; buf.reserve (8192);
        for (int i = 0; i < 8192; ++i) buf.push_back (s.tick (0.f));
        const double acf = detectPitch (buf, kSampleRate, target);
        { const char* dumpDir = std::getenv ("SP_DUMP"); if (dumpDir) { std::FILE* df = std::fopen ((std::string (dumpDir) + "/cap_" + std::to_string (midi) + ".txt").c_str (), "w"); if (df) { for (float v : buf) std::fprintf (df, "%.9g\n", (double) v); std::fclose (df); } } }
        const double fftHz = dftPitch (buf, kSampleRate, target);
        std::printf ("%4d  %9.3f  %+9.2f  %+9.2f\n", midi, target, 1200.0 * std::log2 (acf / target), 1200.0 * std::log2 (fftHz / target));
    }
    return 0;
}

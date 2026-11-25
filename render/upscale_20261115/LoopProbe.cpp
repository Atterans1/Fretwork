// LoopProbe v8: engine-consistent root-scan optimum vs the chooser (Newton) result, low E and high E, all frets.
#include "dsp/LoopDesign.h"
#include <cmath>
#include <cstdio>
using namespace fw::dsp;
static double chainAt (double f, double fs, double c, double a) { return onePolePhaseDelay (f, fs, c) + 4.0 * allpassPhaseDelay (f, fs, a); }
static double partialEngine (int n, double f0, double fs, double c, double a)
{
    const double P = fs / f0, ref = chainAt (f0, fs, c, a);
    auto g = [&] (double f) { return f * (P + chainAt (f, fs, c, a) - ref) / fs - (double) n; };
    double best = -1.0, bestDist = 1e300;
    const double lo = 0.8 * n * f0, hi = 1.25 * n * f0, step = f0 * 1e-4;
    double pf = lo, pg = g (lo);
    for (double f = lo + step; f <= hi; f += step)
    {
        const double gf = g (f);
        if ((gf > 0.0) != (pg > 0.0)) { const double r = pf + (f - pf) * (pg / (pg - gf)); if (std::fabs (r - n * f0) < bestDist) { bestDist = std::fabs (r - n * f0); best = r; } }
        pf = f; pg = gf;
    }
    return best;
}
static double costEngine (double f0, double fs, double c, double B, double a)
{
    double cost = 0.0;
    for (int n = 1; n <= 10; ++n)
    {
        const double target = (double) n * f0 * std::sqrt (1.0 + B * (double) (n * n));
        const double fn = partialEngine (n, f0, fs, c, a);
        cost += (fn < 0.0) ? 1e12 : (fn - target) * (fn - target);
    }
    return cost;
}
int main ()
{
    const double fs = 48000.0;
    const int openMidi[6] = { 40, 45, 50, 55, 59, 64 };
    const int strings[2] = { 0, 5 };
    for (int si = 0; si < 2; ++si)
    {
        const int s = strings[si];
        for (int fret = 0; fret <= 24; fret += 3)
        {
            const int pitch = openMidi[s] + fret;
            LoopDesignParams p {};
            p.fs = fs; p.string = s; p.material = 0;
            p.f0 = 440.0 * std::pow (2.0, (pitch - 69) / 12.0);
            p.sustain01 = 1.0; p.age01 = 0.2; p.bodyLossScale = 1.0; p.stiffness01 = 1.0; p.pickPos01 = 0.15;
            LoopDesignResult r {};
            designLoop (p, 0, r);
            double bestA = 0.0, bestC = 1e300;
            for (int k = 0; k <= 95; ++k) { const double a = -0.01 * k; const double cc = costEngine (p.f0, fs, r.lpC, r.inharmB, a); if (cc < bestC) { bestC = cc; bestA = a; } }
            const double cChooser = costEngine (p.f0, fs, r.lpC, r.inharmB, r.dispA);
            std::printf ("string %d fret %2d chooserA=%+.2f scanBestA=%+.2f costChooser=%.4g costScanBest=%.4g\n", s, fret, r.dispA, bestA, cChooser, bestC);
        }
    }
    return 0;
}

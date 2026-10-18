#include "dsp/LoopDesign.h"

#include "dsp/ModelTables.h"

#include <cmath>

namespace fw::dsp {

namespace {
    constexpr double kPi = 3.14159265358979323846;

    // 6th partial, clamped below Nyquist.
    inline double partial6 (double f0, double fs) noexcept
    {
        const double f = 6.0 * f0;
        const double maxF = 0.45 * fs;
        return f > maxF ? maxF : f;
    }

    inline double clampd (double v, double lo, double hi) noexcept
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    inline int clampi (int v, int lo, int hi) noexcept
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }
}

//==============================================================================
double baseT60 (int stringIndex) noexcept
{
    // Measured against Pianoteq's guitar (Docs/PIANOTEQ_STUDY.md): at the old table a held A3 fell 15 dB/s, 2-3x faster.
    static constexpr double t[6] = { 19.6, 17.5, 15.4, 13.3, 11.2, 9.8 };
    return t[clampi (stringIndex, 0, 5)];
}

double materialScale (int material) noexcept
{
    static constexpr double legacy[4] = { 1.0, 1.1, 0.9, 0.9 };   // nylon 0.6 -> 0.9: it rang 3x shorter than steel
    if (material < 0) return legacy[0];
    if (material < 4) return legacy[clampi (material, 0, 3)];
    const int n = (int) (sizeof (kMaterialSets) / sizeof (kMaterialSets[0]));
    return kMaterialSets[clampi (material, 0, n - 1)].t60Scale;
}

double materialInharm (int material) noexcept
{
    static constexpr double legacy[4] = { 1.0, 1.4, 0.25, 1.6 };
    if (material < 0) return legacy[0];
    if (material < 4) return legacy[clampi (material, 0, 3)];
    const int n = (int) (sizeof (kMaterialSets) / sizeof (kMaterialSets[0]));
    return kMaterialSets[clampi (material, 0, n - 1)].inharm;
}

double harmonicRatio (int material) noexcept
{
    static constexpr double legacy[4] = { 0.12, 0.12, 0.10, 0.05 };
    if (material < 0) return legacy[0];
    if (material < 4) return legacy[clampi (material, 0, 3)];
    const int n = (int) (sizeof (kMaterialSets) / sizeof (kMaterialSets[0]));
    return kMaterialSets[clampi (material, 0, n - 1)].harmRatio;
}

//==============================================================================
double loopGainAt (double f, double t60) noexcept
{
    if (! (f > 0.0) || ! (t60 > 0.0)) return 0.0;
    return std::pow (10.0, -3.0 / (f * t60));
}

double computeT60Fund (const LoopDesignParams& p, int damping) noexcept
{
    if (damping == 2) return 0.07;   // Dead

    const double age = clampd (p.age01, 0.0, 1.0);
    double t = baseT60 (p.string)
             * clampd (p.sustain01, 0.0, 2.0)
             * materialScale (p.material)
             * (1.0 + (0.35 - 1.0) * age)
             * p.bodyLossScale;

    if (damping == 1) t *= 0.10;     // PalmMute (0.22 before the base T60 table grew 2.2x in total)
    return t > 0.001 ? t : 0.001;
}

double computeT60Harm (const LoopDesignParams& p, int damping) noexcept
{
    if (damping == 2) return 0.03;

    const double age = clampd (p.age01, 0.0, 1.0);
    const double ratio = harmonicRatio (p.material) * (1.2 + (0.4 - 1.2) * age);

    const double t = computeT60Fund (p, damping) * ratio;
    return t > 0.0005 ? t : 0.0005;
}

//==============================================================================
double onePoleMagNoGain (double c, double w) noexcept
{
    const double s = 1.0 + c * c - 2.0 * c * std::cos (w);
    return s > 0.0 ? std::sqrt (s) : 1.0;
}

double onePoleMag (double c, double g, double w) noexcept
{
    return g * (1.0 - c) / onePoleMagNoGain (c, w);
}

double onePolePhaseDelay (double f, double fs, double c) noexcept
{
    if (! (f > 0.0) || ! (fs > 0.0)) return 0.0;

    // w is radians per sample, so arg / w is already a sample count.
    const double w = 2.0 * kPi * f / fs;
    // arg(1 - c e^-jw) = atan2(c sin w, 1 - c cos w)
    const double arg = std::atan2 (c * std::sin (w), 1.0 - c * std::cos (w));
    return arg / w;
}

double allpassPhaseDelay (double f, double fs, double a) noexcept
{
    if (! (f > 0.0) || ! (fs > 0.0)) return 0.0;

    const double w = 2.0 * kPi * f / fs;
    const double argNum = std::atan2 (-std::sin (w), a + std::cos (w));
    const double argDen = std::atan2 (-a * std::sin (w), 1.0 + a * std::cos (w));
    return -(argNum - argDen) / w;
}

//==============================================================================
void solveLoopOnePole (double f0, double fs, double t60Fund, double t60Harm,
                       double& cOut, double& gOut) noexcept
{
    const double g0 = loopGainAt (f0, t60Fund);
    const double f6 = partial6 (f0, fs);
    const double g6 = loopGainAt (f6, t60Harm);

    // |H(w0)|/|H(w6)| == g0/g6. With the gain cancelled this is
    // |1-c e^-jw6| / |1 - c e^-jw0|, which rises monotonically in c.
    const double target = g6 > 1e-12 ? g0 / g6 : 1.0;

    double lo = 0.0, hi = 0.95, c = 0.0;

    if (target <= 1.0)
    {
        c = 0.0;
    }
    else
    {
        const double w0 = 2.0 * kPi * f0 / fs;
        const double w6 = 2.0 * kPi * f6 / fs;

        for (int i = 0; i < 60; ++i)
        {
            c = 0.5 * (lo + hi);
            const double ratio = onePoleMagNoGain (c, w6) / onePoleMagNoGain (c, w0);
            if (ratio < target) lo = c; else hi = c;
        }
    }

    const double w0 = 2.0 * kPi * f0 / fs;
    double g = g0 * onePoleMagNoGain (c, w0) / (1.0 - c);
    if (! (g > 0.0) || g > 0.9999) g = 0.9999;

    cOut = c;
    gOut = g;
}

//==============================================================================
double inharmonicityB (const LoopDesignParams& p) noexcept
{
    static constexpr double base[6] = { 2.0e-5, 2.5e-5, 3.0e-5, 4.0e-5, 2.0e-5, 1.4e-5 };

    const int s = (int) clampd (double (p.string), 0.0, 5.0);
    const double age = clampd (p.age01, 0.0, 1.0);
    (void) age;

    return base[s] * materialInharm (p.material) * clampd (p.stiffness01, 0.0, 4.0);
}

double totalLoopDelay (double f, double fs, double fracPart, double c, double a) noexcept
{
    return fracPart + onePolePhaseDelay (f, fs, c) + 4.0 * allpassPhaseDelay (f, fs, a);
}

//==============================================================================
double chooseDispersionA (double f0, double fs, double fracPart, double c, double B) noexcept
{
    if (B < 1e-7) return 0.0;

    double bestA = 0.0;
    double bestCost = -1.0;

    for (int k = 0; k <= 95; ++k)
    {
        const double a = -0.01 * (double) k;   // 0, -0.01, ... -0.95

        double cost = 0.0;

        for (int n = 1; n <= 10; ++n)
        {
            const double target = (double) n * f0 * std::sqrt (1.0 + B * (double) (n * n));

            // Newton on  f * Dtot(f) / fs - n = 0, started at the ideal partial.
            double f = (double) n * f0;

            for (int it = 0; it < 4; ++it)
            {
                const double d = totalLoopDelay (f, fs, fracPart, c, a);
                const double g = f * d / fs - (double) n;

                const double fd = f * 1e-3;
                const double d2 = totalLoopDelay (f + fd, fs, fracPart, c, a);
                const double deriv = ((f + fd) * d2 / fs - (double) n - g) / fd;

                if (! (deriv > 1e-9)) break;
                f -= g / deriv;

                if (! (f > 1.0))  { f = 1.0; break; }
                if (f > 0.49 * fs) { f = 0.49 * fs; break; }
            }

            const double diff = f - target;
            cost += diff * diff;
        }

        if (bestCost < 0.0 || cost < bestCost)
        {
            bestCost = cost;
            bestA = a;
        }
    }

    return bestA;
}

//==============================================================================
void designLoop (const LoopDesignParams& p, int damping, LoopDesignResult& out) noexcept
{
    const double fs = p.fs > 0.0 ? p.fs : 44100.0;
    const double f0 = p.f0 > 0.0 ? p.f0 : 82.41;

    out.f0      = f0;
    out.period  = fs / f0;
    out.t60Fund = computeT60Fund (p, damping);
    out.t60Harm = computeT60Harm (p, damping);
    out.inharmB = inharmonicityB (p);

    solveLoopOnePole (f0, fs, out.t60Fund, out.t60Harm, out.lpC, out.lpG);

    const double loopPeriod = out.period;

    const double loopF0 = fs / loopPeriod;
    const double tauLp = onePolePhaseDelay (loopF0, fs, out.lpC);

    // The loop period is dLive plus the filter chain's own group delay, and
    // dLive already contains that group delay, so the fractional delay has to
    // pay for the chain twice for the mode to land on a whole cycle.
    double frac = loopPeriod - tauLp;

    for (int iter = 0; iter < 2; ++iter)
    {
        out.dispA = chooseDispersionA (loopF0, fs, frac, out.lpC, out.inharmB);

        frac = loopPeriod
             - 2.0 * (onePolePhaseDelay (loopF0, fs, out.lpC)
                    + 4.0 * allpassPhaseDelay (loopF0, fs, out.dispA));
    }

    out.fracPart = frac;
    out.loopPeriod = loopPeriod;
    out.delay    = totalLoopDelay (f0, fs, out.fracPart, out.lpC, out.dispA);

    // No interpolator compensation is needed: the delay line is read with a
    // four-point Catmull-Rom interpolation, whose phase error is small enough
    // that budgeting for it would over-shorten the loop.

    out.loopGain = onePoleMag (out.lpC, out.lpG, 2.0 * kPi * f0 / fs);

    if (out.fracPart < 1.0) out.fracPart = 1.0;
    if (out.delay < 4.0)    out.delay = 4.0;
}

} // namespace fw::dsp

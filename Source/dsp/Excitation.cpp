#include "dsp/Excitation.h"
#include "dsp/Rng.h"

#include <cmath>

namespace fw::dsp {

namespace {
    inline double clampd (double v, double lo, double hi) noexcept
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }
}

void renderExcitation (const ExcitationRequest& r,
                       ExcitationBuffer& out,
                       ExcitationResult& info) noexcept
{
    info = ExcitationResult {};

    const double fs = r.fs > 0.0 ? r.fs : 44100.0;
    const double f0 = r.f0 > 0.0 ? r.f0 : 82.41;
    const double vel = clampd (r.vel, 0.0, 1.0);
    const double pickHard = clampd (r.pickHard01, 0.0, 1.0);

    int n = (int) std::ceil (r.period);
    if (n < 2) n = 2;
    if (n > kMaxExcite) n = kMaxExcite;

    // ---- initial string shape ----------------------------------------------
    // A pick pulls the string into a triangle whose apex sits at the pick
    // point. Its harmonics fall at 12 dB/octave and it already carries the
    // pick-position notches, which is what makes a pluck sound like a pluck.
    // A pure noise burst (classic Karplus-Strong) has a flat spectrum and is
    // what made the attack read as hiss. A little noise stays for texture.
    Rng rng (r.seed);
    float* buf = out.data();

    {
        int apex = (int) std::lround (clampd (r.pickPos01, 0.02, 0.5) * (double) n);
        if (apex < 1) apex = 1;
        if (apex > n - 1) apex = n - 1;

        for (int i = 0; i < n; ++i)
        {
            const double shape = i <= apex ? (double) i / (double) apex
                                           : (double) (n - i) / (double) (n - apex);
            buf[i] = (float) (shape + 0.06 * rng.bipolar());
        }
    }

    // ---- excitation brightness --------------------------------------------
    // Hardness sets how much high frequency survives; velocity opens the
    // filter as the attack transient needs it.
    double fc = f0 * (6.0 + 34.0 * pickHard) * (0.12 + 0.88 * vel * vel);

    if (r.exc == 1) fc *= 0.35;        // Finger: darker
    else if (r.exc == 2) fc *= 0.5;    // Thumb

    if (r.damp == 1) fc *= 0.6;        // PalmMute

    fc = clampd (fc, 20.0, 0.45 * fs);

    // Two-pole lowpass (12 dB/octave), in place. The pick's width rounds the
    // triangle's corner; a single pole let too much of the edge through.
    {
        const double a = std::exp (-2.0 * 3.14159265358979323846 * fc / fs);
        double z1 = 0.0, z2 = 0.0;

        for (int i = 0; i < n; ++i)
        {
            z1 = z1 * a + (double) buf[i] * (1.0 - a);
            z2 = z2 * a + z1 * (1.0 - a);
            buf[i] = (float) z2;
        }
    }

    // Fingerstyle needs a soft attack so it reads as a fingertip, not a pick.
    if (r.exc == 1)
    {
        const int attack = (int) (0.006 * fs);
        for (int i = 0; i < n && i < attack; ++i)
        {
            const double x = (double) i / (double) (attack > 1 ? attack - 1 : 1);
            buf[i] = (float) ((double) buf[i] * 0.5 * (1.0 - std::cos (3.14159265358979323846 * x)));
        }
    }

    // Thumb hits are simply louder.
    if (r.exc == 2)
        for (int i = 0; i < n; ++i) buf[i] = (float) (buf[i] * 1.3);

    // ---- DC removal and normalise ----------------------------------------
    double mean = 0.0;
    for (int i = 0; i < n; ++i) mean += buf[i];
    mean /= (double) n;

    double sumSq = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double v = (double) buf[i] - mean;
        buf[i] = (float) v;
        sumSq += v * v;
    }

    const double rms = std::sqrt (sumSq / (double) n);
    const double target = 0.35 * std::pow (vel, 1.5);

    if (rms > 1e-12 && target > 0.0)
    {
        const double g = target / rms;
        for (int i = 0; i < n; ++i) buf[i] = (float) (buf[i] * g);
    }
    else
    {
        for (int i = 0; i < n; ++i) buf[i] = 0.0f;
    }

    out.setLength (n);

    // ---- one-shot transients ---------------------------------------------
    info.clickAmp     = (float) (0.014 * pickHard * vel);   // was 0.03: the transient was a separate click
    info.thumpAmp     = r.damp == 2 ? (float) (0.25 * vel) : 0.0f;
    info.fretNoiseAmp = (float) (0.02 * clampd (r.fretNoise01, 0.0, 1.0));
}

} // namespace fw::dsp

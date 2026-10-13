#pragma once
// Articulation verification phrases and measurements for ModeMatrix.
// One 6 s phrase per articulation, played from plain MIDI (Poly mode, plugin
// defaults), so every articulation must come from the auto-detection rules.
// Pass criteria are fixed here, before any run.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_graphics/juce_graphics.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace artic
{
struct AEv { double t; int kind; int a; int b; };   // kind 0 off, 1 on (a note, b vel), 3 pitch bend (a 14-bit)

struct Result { bool pass = false; juce::String evidence; };

struct Phrase
{
    juce::String name;
    std::vector<AEv> events;
    std::function<Result (const std::vector<float>&, double)> measure;
};

constexpr double kLen = 6.0;

inline double hz (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }
inline double db (double v) { return 10.0 * std::log10 (v + 1e-24); }

// -------------------------------------------------------------- analysis kit
inline double energy (const std::vector<float>& x, double fs, double t0, double t1)
{
    double s = 0; size_t n = 0;
    for (size_t i = (size_t) std::max (0.0, t0 * fs); i < (size_t) (t1 * fs) && i < x.size(); ++i) { s += (double) x[i] * x[i]; ++n; }
    return n ? s / (double) n : 0.0;
}

/** Magnitude spectrum (Hann, 4096) of the window starting at t. */
inline std::vector<double> spectrum (const std::vector<float>& x, double fs, double t, int order = 12)
{
    const int N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> b ((size_t) (2 * N), 0.f);
    const long a = (long) (t * fs);
    for (int i = 0; i < N; ++i)
        if (a + i >= 0 && a + i < (long) x.size())
            b[(size_t) i] = x[(size_t) (a + i)] * (0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) N));
    fft.performFrequencyOnlyForwardTransform (b.data());
    return std::vector<double> (b.begin(), b.begin() + N / 2);
}

/** Band energy between f0 and f1 Hz over [t0, t1), from 1024-sample frames. */
inline double bandEnergy (const std::vector<float>& x, double fs, double t0, double t1, double f0, double f1)
{
    double s = 0; int frames = 0;
    const int order = 10, N = 1 << order;
    for (double t = t0; t + N / fs <= t1 + 1e-9; t += 0.25 * N / fs)
    {
        const auto m = spectrum (x, fs, t, order);
        for (int k = (int) (f0 * N / fs); k <= (int) (f1 * N / fs) && k < N / 2; ++k) s += m[(size_t) k] * m[(size_t) k];
        ++frames;
    }
    if (frames == 0)
    {
        const auto m = spectrum (x, fs, t0, order);
        for (int k = (int) (f0 * N / fs); k <= (int) (f1 * N / fs) && k < N / 2; ++k) s += m[(size_t) k] * m[(size_t) k];
        frames = 1;
    }
    return s / frames;
}

/** Spectral flatness (geometric / arithmetic mean of power) in f0..f1 over [t0, t1). Noise ~ 0.5+, tones ~ 0.01. */
inline double flatness (const std::vector<float>& x, double fs, double t0, double t1, double f0, double f1)
{
    const int order = 10, N = 1 << order;
    std::vector<double> acc ((size_t) (N / 2), 0.0);
    int frames = 0;
    for (double t = t0; t + N / fs <= t1 + 1e-9 || frames == 0; t += 0.25 * N / fs)
    {
        const auto m = spectrum (x, fs, t, order);
        for (size_t k = 0; k < acc.size(); ++k) acc[k] += m[k] * m[k];
        ++frames;
        if (t + N / fs > t1) break;
    }
    double lg = 0, ar = 0; int n = 0;
    for (int k = (int) (f0 * N / fs); k <= (int) (f1 * N / fs) && k < N / 2; ++k) { const double p = acc[(size_t) k] + 1e-20; lg += std::log (p); ar += p; ++n; }
    return n ? std::exp (lg / n) / (ar / n) : 0.0;
}

/** YIN pitch (fractional MIDI) over [t, t + 0.046); 0 if unvoiced. */
inline double pitchAt (const std::vector<float>& x, double fs, double t, double loHz = 70, double hiHz = 1200)
{
    const int W = (int) (0.046 * fs), tauMax = (int) (fs / loHz), tauMin = (int) (fs / hiHz);
    const long a = (long) (t * fs);
    if (a < 0 || a + W + tauMax >= (long) x.size()) return 0;
    std::vector<double> d ((size_t) tauMax + 2, 0.0), cm ((size_t) tauMax + 2, 1.0);
    for (int tau = 1; tau <= tauMax + 1; ++tau)
    {
        double s = 0;
        for (int i = 0; i < W; ++i) { const double e = x[(size_t) (a + i)] - x[(size_t) (a + i + tau)]; s += e * e; }
        d[(size_t) tau] = s;
    }
    double run = 0;
    for (int tau = 1; tau <= tauMax + 1; ++tau) { run += d[(size_t) tau]; cm[(size_t) tau] = d[(size_t) tau] * tau / (run + 1e-20); }
    int best = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
        if (cm[(size_t) tau] < 0.2) { while (tau + 1 < tauMax && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau; best = tau; break; }
    if (best < 0) return 0;
    const double l = cm[(size_t) best - 1], c = cm[(size_t) best], r = cm[(size_t) best + 1];
    const double tau = best + 0.5 * (l - r) / (l - 2 * c + r + 1e-20);
    return 69.0 + 12.0 * std::log2 (fs / tau / 440.0);
}

/** Arrival of a pitch: the first time the band around its fundamental reaches
    -6 dB of that band's own peak within [t0, t1]. Overtones of lower strings and
    the pick scrape stay far below a string's own fundamental, so they do not count. */
inline double onsetOf (const std::vector<float>& x, double fs, double midi, double t0, double t1)
{
    juce::dsp::IIR::Filter<float> bp;
    // Constant 20 Hz bandwidth: a fixed Q makes low strings respond tens of ms late.
    bp.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (fs, hz (midi), hz (midi) / 20.0);
    const double a = std::exp (-1.0 / (0.002 * fs));
    std::vector<double> env; double e = 0, peak = 0;
    for (size_t i = (size_t) ((t0 - 0.05) * fs); i < (size_t) (t1 * fs) && i < x.size(); ++i)
    {
        const double y = bp.processSample (x[i]);
        e = a * e + (1 - a) * y * y;
        env.push_back (e);
        if ((double) i / fs >= t0) peak = std::max (peak, e);
    }
    const size_t start = (size_t) (0.05 * fs);
    for (size_t k = start; k < env.size(); ++k)
        if (env[k] >= peak * 0.25) return t0 + (double) (k - start) / fs;
    return -1;
}

// ------------------------------------------------------------- the phrases
inline std::vector<AEv> on (double t, int n, int v) { return { { t, 1, n, v } }; }

inline std::vector<Phrase> phrases()
{
    std::vector<Phrase> P;
    const auto add = [] (std::vector<AEv>& e, std::initializer_list<AEv> l) { e.insert (e.end(), l); };
    // E2 A2 D#3 G#3 C#4 F#4, one per string: no note is one of the first five
    // partials of another, so each string's arrival is measurable on its own.
    const int chord[] = { 40, 45, 51, 56, 61, 66 };

    // 1-2. Strums: six notes 10 ms apart (down = rising), then a block chord.
    for (int dir : { 1, -1 })
    {
        Phrase p;
        p.name = dir > 0 ? "Strum down" : "Strum up";
        for (int k = 0; k < 6; ++k) add (p.events, { { 1.0 + 0.010 * k, 1, chord[dir > 0 ? k : 5 - k], 100 } });
        for (int k = 0; k < 6; ++k) add (p.events, { { 2.8, 0, chord[k], 0 } });
        for (int k = 0; k < 6; ++k) add (p.events, { { 3.5, 1, chord[k], 100 } });
        for (int k = 0; k < 6; ++k) add (p.events, { { 5.3, 0, chord[k], 0 } });
        p.measure = [dir, chord] (const std::vector<float>& x, double fs)
        {
            double t[6]; bool order = true;
            for (int k = 0; k < 6; ++k) t[k] = onsetOf (x, fs, chord[k], 0.95, 1.3);
            // Sweep direction: mean arrival of the three high strings minus the three low ones.
            // (Per-string order is not used: each string rings up at its own rate, which
            // can swap neighbours that start 10 ms apart.)
            for (int k = 0; k < 6; ++k) order = order && t[k] > 0;
            const double sweep = ((t[3] + t[4] + t[5]) - (t[0] + t[1] + t[2])) / 3.0 * 1000.0;
            order = order && (dir > 0 ? sweep >= 10 : sweep <= -10);
            const double nStrum = flatness (x, fs, 1.0, 1.09, 1500, 6000), nSus = flatness (x, fs, 1.6, 2.2, 1500, 6000);
            const double nBlock = flatness (x, fs, 3.5, 3.56, 1500, 6000), nBSus = flatness (x, fs, 4.1, 4.7, 1500, 6000);
            Result r;
            r.pass = order && nStrum > 2 * nSus && nBlock > 1.5 * nBSus;
            r.evidence << "string onsets " << (dir > 0 ? "low->high" : "high->low") << (order ? " confirmed" : " NOT confirmed") << ", high-minus-low arrival " << juce::String (sweep, 1) << " ms"
                       << " (arrivals ms after 1.0 s, low->high string: " << juce::String ((t[0] - 1.0) * 1000, 1) << " " << juce::String ((t[1] - 1.0) * 1000, 1) << " "
                       << juce::String ((t[2] - 1.0) * 1000, 1) << " " << juce::String ((t[3] - 1.0) * 1000, 1) << " " << juce::String ((t[4] - 1.0) * 1000, 1) << " "
                       << juce::String ((t[5] - 1.0) * 1000, 1) << "); stroke-noise flatness 1.5-6 kHz "
                       << juce::String (nStrum, 3) << " vs sustain " << juce::String (nSus, 3) << "; block chord "
                       << juce::String (nBlock, 3) << " vs " << juce::String (nBSus, 3);
            return r;
        };
        P.push_back (p);
    }

    // 3. Palm mute: velocity 35 vs open velocity 80 on E3.
    {
        Phrase p; p.name = "Palm mute";
        add (p.events, { { 0.5, 1, 52, 80 }, { 2.5, 0, 52, 0 }, { 3.0, 1, 52, 35 }, { 5.0, 0, 52, 0 } });
        p.measure = [] (const std::vector<float>& x, double fs)
        {
            const double openDecay = db (energy (x, fs, 0.85, 0.95)) - db (energy (x, fs, 0.52, 0.6));
            const double palmDecay = db (energy (x, fs, 3.35, 3.45)) - db (energy (x, fs, 3.02, 3.1));
            Result r; r.pass = palmDecay < openDecay - 10;
            r.evidence << "level 0.35 s after the attack: open " << juce::String (openDecay, 1) << " dB, palm-muted (vel 35) "
                       << juce::String (palmDecay, 1) << " dB";
            return r;
        };
        P.push_back (p);
    }

    // 4. Mute (dead note): velocity 10.
    {
        Phrase p; p.name = "Mute (dead note)";
        add (p.events, { { 0.5, 1, 52, 80 }, { 2.5, 0, 52, 0 }, { 3.0, 1, 52, 10 }, { 5.0, 0, 52, 0 } });
        p.measure = [] (const std::vector<float>& x, double fs)
        {
            const double open = db (energy (x, fs, 0.70, 0.80)) - db (energy (x, fs, 0.50, 0.53));
            const double dead = db (energy (x, fs, 3.20, 3.30)) - db (energy (x, fs, 3.00, 3.03));
            const double deadPitchless = flatness (x, fs, 3.0, 3.06, 100, 4000);
            Result r; r.pass = dead < -30 && dead < open - 15;
            r.evidence << "level 0.2 s after the attack: open " << juce::String (open, 1) << " dB, dead (vel 10) " << juce::String (dead, 1)
                       << " dB; attack flatness " << juce::String (deadPitchless, 3);
            return r;
        };
        P.push_back (p);
    }

    // 5. Slide: hold E3, add A3 (5 semitones, overlapping) -> glide; then A3 -> E3 down.
    {
        Phrase p; p.name = "Slide";
        add (p.events, { { 0.5, 1, 52, 90 }, { 1.5, 1, 57, 90 }, { 1.6, 0, 52, 0 }, { 3.0, 0, 57, 0 },
                         { 3.5, 1, 57, 90 }, { 4.5, 1, 52, 90 }, { 4.6, 0, 57, 0 }, { 5.8, 0, 52, 0 } });
        p.measure = [] (const std::vector<float>& x, double fs)
        {
            int between = 0; double end = 0;
            for (double t = 1.5; t < 1.65; t += 0.005) { const double m = pitchAt (x, fs, t); if (m > 52.6 && m < 56.4) ++between; }
            end = pitchAt (x, fs, 2.0);
            const double sq = flatness (x, fs, 1.5, 1.6, 1200, 3800) / std::max (1e-9, flatness (x, fs, 2.2, 2.7, 1200, 3800));
            Result r; r.pass = between >= 3 && std::abs (end - 57) < 0.3;
            r.evidence << between << " pitch frames between E3 and A3 during the 90 ms glide (a jump has 0-1); lands on "
                       << juce::String (end, 2) << " (A3 = 57); squeak flatness x" << juce::String (sq, 1);
            return r;
        };
        P.push_back (p);
    }

    // 6. Hammer-on: hold A3, add B3 (+2) without a new pick.
    {
        Phrase p; p.name = "Hammer-on";
        add (p.events, { { 0.5, 1, 57, 90 }, { 1.5, 1, 59, 90 }, { 3.0, 0, 59, 0 }, { 3.0, 0, 57, 0 },
                         { 3.5, 1, 57, 90 }, { 4.0, 0, 57, 0 }, { 4.5, 1, 59, 90 }, { 5.5, 0, 59, 0 } });
        p.measure = [] (const std::vector<float>& x, double fs)
        {
            int between = 0;
            for (double t = 1.5; t < 1.6; t += 0.005) { const double m = pitchAt (x, fs, t); if (m > 57.4 && m < 58.6) ++between; }
            const double after = pitchAt (x, fs, 1.8);
            // Attack HF energy of the hammered note vs the same note picked at 4.5 s.
            const double hammerHF = bandEnergy (x, fs, 1.5, 1.52, 4000, 12000), pickHF = bandEnergy (x, fs, 4.5, 4.52, 4000, 12000);
            Result r; r.pass = std::abs (after - 59) < 0.3 && between <= 1 && db (hammerHF) < db (pickHF) - 3;
            r.evidence << "pitch steps A3 -> " << juce::String (after, 2) << " (B3 = 59) with " << between << " glide frames; attack >4 kHz "
                       << juce::String (db (hammerHF) - db (pickHF), 1) << " dB vs picking the same note";
            return r;
        };
        P.push_back (p);
    }

    // 7. Pull-off: hold A3, hammer B3, release B3 -> back to A3 without a pick;
    //    then hold B3 and press A3 (-2) -> pull-off by a new note.
    {
        Phrase p; p.name = "Pull-off";
        add (p.events, { { 0.5, 1, 57, 90 }, { 1.2, 1, 59, 90 }, { 2.0, 0, 59, 0 }, { 3.0, 0, 57, 0 },
                         { 3.5, 1, 59, 90 }, { 4.5, 1, 57, 90 }, { 4.6, 0, 59, 0 }, { 5.8, 0, 57, 0 } });
        p.measure = [] (const std::vector<float>& x, double fs)
        {
            const double a1 = pitchAt (x, fs, 2.3), a2 = pitchAt (x, fs, 4.8);
            const double sounding = db (energy (x, fs, 2.2, 2.5));
            const double hf = bandEnergy (x, fs, 2.0, 2.02, 4000, 12000), pickHF = bandEnergy (x, fs, 0.5, 0.52, 4000, 12000);
            Result r; r.pass = std::abs (a1 - 57) < 0.3 && std::abs (a2 - 57) < 0.3 && sounding > -50 && db (hf) < db (pickHF) - 3;
            r.evidence << "release of B3 pulls off to " << juce::String (a1, 2) << " still sounding at " << juce::String (sounding, 1)
                       << " dBFS, attack >4 kHz " << juce::String (db (hf) - db (pickHF), 1) << " dB vs a pick; new lower note pulls off to "
                       << juce::String (a2, 2);
            return r;
        };
        P.push_back (p);
    }

    // 8. Bend: pitch wheel to full (+2 semitones at the default Wheel Range).
    {
        Phrase p; p.name = "Bend";
        add (p.events, { { 0.5, 1, 52, 90 } });
        for (int i = 0; i <= 25; ++i) add (p.events, { { 1.0 + 0.02 * i, 3, 8192 + (int) std::lround (8191.0 * i / 25.0), 0 } });
        add (p.events, { { 3.5, 3, 8192, 0 }, { 5.0, 0, 52, 0 } });
        p.measure = [] (const std::vector<float>& x, double fs)
        {
            const double before = pitchAt (x, fs, 0.6), held = pitchAt (x, fs, 2.0), back = pitchAt (x, fs, 4.0);
            Result r; r.pass = std::abs (before - 52) < 0.15 && std::abs (held - 54) < 0.15 && std::abs (back - 52) < 0.15;
            r.evidence << "pitch " << juce::String (before, 2) << " -> " << juce::String (held, 2) << " bent (+"
                       << juce::String ((held - before) * 100, 0) << " c) -> " << juce::String (back, 2) << " released";
            return r;
        };
        P.push_back (p);
    }

    // 9. Natural harmonic: velocity 127 on E3 (12th fret of low E) and B3 (19th), vs fretted E3 at 126.
    {
        Phrase p; p.name = "Harmonic";
        add (p.events, { { 0.5, 1, 52, 127 }, { 2.0, 0, 52, 0 }, { 2.5, 1, 59, 127 }, { 4.0, 0, 59, 0 }, { 4.5, 1, 52, 126 }, { 5.9, 0, 52, 0 } });
        p.measure = [] (const std::vector<float>& x, double fs)
        {
            const double h12 = pitchAt (x, fs, 0.8), h19 = pitchAt (x, fs, 2.8);
            const double just19 = 40 + 12 * std::log2 (3.0);   // third partial of open E2
            const auto upper = [&] (double t, double f0)
            {
                const auto m = spectrum (x, fs, t, 14);
                const int N = 1 << 14;
                double fund = 0, up = 0;
                for (int k = 1; k < N / 2; ++k)
                {
                    const double f = k * fs / N, ratio = f / f0;
                    if (ratio > 0.9 && ratio < 1.1) fund += m[(size_t) k] * m[(size_t) k];
                    else if (ratio > 2.5 && f < 8000) up += m[(size_t) k] * m[(size_t) k];
                }
                return db (up) - db (fund);
            };
            const double hu = upper (0.6, hz (52)), fu = upper (4.6, hz (52));
            Result r; r.pass = std::abs (h12 - 52) < 0.1 && std::abs (h19 - just19) < 0.1 && hu < fu - 3;
            r.evidence << "12th-fret harmonic at " << juce::String ((h12 - 52) * 100, 1) << " c from E3, 19th at "
                       << juce::String ((h19 - just19) * 100, 1) << " c from 3 x E2; partials 3+ vs fundamental: harmonic "
                       << juce::String (hu, 1) << " dB, fretted " << juce::String (fu, 1) << " dB";
            return r;
        };
        P.push_back (p);
    }

    // 10. Vibrato: one held note, no mod wheel.
    {
        Phrase p; p.name = "Vibrato";
        add (p.events, { { 0.3, 1, 57, 90 }, { 5.5, 0, 57, 0 } });
        p.measure = [] (const std::vector<float>& x, double fs)
        {
            const auto dev = [&] (double t0, double t1, int& crossings)
            {
                std::vector<double> c;
                for (double t = t0; t < t1; t += 0.01) { const double m = pitchAt (x, fs, t); if (m > 0) c.push_back ((m - 57) * 100); }
                double mean = 0; for (double v : c) mean += v; mean /= std::max<size_t> (1, c.size());
                double s = 0; crossings = 0;
                for (size_t i = 0; i < c.size(); ++i) { s += (c[i] - mean) * (c[i] - mean); if (i && (c[i] - mean) * (c[i - 1] - mean) < 0) ++crossings; }
                return std::sqrt (s / std::max<size_t> (1, c.size()));
            };
            int c0 = 0, c1 = 0;
            const double early = dev (0.35, 0.8, c0), late = dev (2.0, 5.0, c1);
            const double rate = c1 / 2.0 / 3.0;
            Result r; r.pass = early < 3 && late > 3 && rate > 4 && rate < 8;
            r.evidence << "pitch wobble " << juce::String (early, 1) << " c before 0.6 s, " << juce::String (late, 1) << " c after (rate "
                       << juce::String (rate, 1) << " Hz)";
            return r;
        };
        P.push_back (p);
    }

    // 11. Fret noise: finger lifts at note-off.
    {
        Phrase p; p.name = "Fret noise";
        add (p.events, { { 0.5, 1, 52, 100 }, { 1.5, 0, 52, 0 }, { 2.5, 1, 55, 100 }, { 3.5, 0, 55, 0 } });
        p.measure = [] (const std::vector<float>& x, double fs)
        {
            const double burst = bandEnergy (x, fs, 1.5, 1.515, 2500, 8000), after = bandEnergy (x, fs, 1.55, 1.565, 2500, 8000);
            const double fl = flatness (x, fs, 1.5, 1.52, 2500, 8000);
            Result r; r.pass = db (burst) > db (after) + 6;
            r.evidence << "note-off: 2.5-8 kHz burst " << juce::String (db (burst) - db (after), 1) << " dB above 50 ms later, flatness "
                       << juce::String (fl, 3);
            return r;
        };
        P.push_back (p);
    }

    // 12. String squeak: a quick 7-fret position shift on a wound string.
    {
        Phrase p; p.name = "String squeak";
        add (p.events, { { 1.0, 1, 47, 90 }, { 1.2, 0, 47, 0 }, { 1.3, 1, 54, 90 }, { 2.5, 0, 54, 0 },
                         { 3.5, 1, 47, 90 }, { 3.7, 0, 47, 0 }, { 4.5, 1, 54, 90 }, { 5.7, 0, 54, 0 } });
        p.measure = [] (const std::vector<float>& x, double fs)
        {
            // Same two notes: a quick shift (0.3 s) squeaks, a slow one (1.0 s) does not.
            // 30-130 ms after the new note: past its pick transient.
            const double quick = flatness (x, fs, 1.33, 1.43, 1200, 3800), slow = flatness (x, fs, 4.53, 4.63, 1200, 3800);
            const double qE = bandEnergy (x, fs, 1.33, 1.43, 1200, 3800), sE = bandEnergy (x, fs, 4.53, 4.63, 1200, 3800);
            Result r; r.pass = quick > 1.5 * slow && db (qE) > db (sE) + 2;
            r.evidence << "1.2-3.8 kHz during a quick shift: flatness " << juce::String (quick, 3) << " vs " << juce::String (slow, 3)
                       << " for a slow shift, energy +" << juce::String (db (qE) - db (sE), 1) << " dB";
            return r;
        };
        P.push_back (p);
    }

    // 13. Pick attack: E3 at velocity 64 and 120.
    {
        Phrase p; p.name = "Pick attack";
        add (p.events, { { 0.5, 1, 52, 64 }, { 2.5, 0, 52, 0 }, { 3.0, 1, 52, 120 }, { 5.0, 0, 52, 0 } });
        p.measure = [] (const std::vector<float>& x, double fs)
        {
            const auto att = [&] (double t) { return db (bandEnergy (x, fs, t, t + 0.008, 4000, 12000)) - db (bandEnergy (x, fs, t + 0.1, t + 0.3, 4000, 12000)); };
            const double soft = att (0.5), hard = att (3.0);
            const double softAbs = db (bandEnergy (x, fs, 0.5, 0.508, 4000, 12000)), hardAbs = db (bandEnergy (x, fs, 3.0, 3.008, 4000, 12000));
            const double body = db (energy (x, fs, 3.1, 3.3)), tr = db (energy (x, fs, 3.0, 3.008));
            Result r; r.pass = soft > 6 && hard > 6 && hardAbs > softAbs + 3;
            r.evidence << ">4 kHz in the first 8 ms vs the sustain: +" << juce::String (soft, 1) << " dB (vel 64), +" << juce::String (hard, 1)
                       << " dB (vel 120); hard attack " << juce::String (hardAbs - softAbs, 1) << " dB brighter; transient "
                       << juce::String (tr - body, 1) << " dB vs body";
            return r;
        };
        P.push_back (p);
    }
    return P;
}

/** Spectrogram PNG (0-8 kHz, log magnitude) with note-on markers. */
inline void spectrogramPng (const std::vector<float>& x, double fs, const Phrase& p, const juce::File& png)
{
    const int W = 900, H = 300, order = 11, N = 1 << order;
    juce::Image img (juce::Image::RGB, W, H + 18, true, juce::SoftwareImageType());
    {
        juce::Graphics g (img);
        g.fillAll (juce::Colours::black);
        const double dur = (double) x.size() / fs;
        for (int c = 0; c < W; ++c)
        {
            const auto m = spectrum (x, fs, dur * c / W - 0.5 * N / fs, order);
            for (int r = 0; r < H; ++r)
            {
                const double f = 8000.0 * (H - 1 - r) / H;
                const double v = m[(size_t) std::min (N / 2 - 1, (int) (f * N / fs))];
                const float b = (float) juce::jlimit (0.0, 1.0, (20 * std::log10 (v + 1e-9) + 20) / 70.0);
                img.setPixelAt (c, r + 18, juce::Colour::fromHSV (0.7f - 0.7f * b, 0.9f, b, 1.f));
            }
        }
        g.setColour (juce::Colours::white);
        for (const auto& e : p.events) if (e.kind == 1) g.fillRect ((int) (e.t / dur * W), 14, 1, 4);
        g.setFont (12.f);
        g.drawText (p.name + "  (0-8 kHz, 0-" + juce::String (dur, 1) + " s; ticks = note-ons)", 4, 0, W - 8, 14, juce::Justification::left);
    }
    png.deleteFile();
    juce::FileOutputStream os (png);
    juce::PNGImageFormat().writeImageToStream (img, os);
}
} // namespace artic

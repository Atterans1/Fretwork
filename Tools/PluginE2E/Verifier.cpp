// Independent verification of a learned performance.
//
// Deliberately different methods from the learner, so a learner bug cannot
// grade itself:
//   onsets  - high-frequency-content (HFC) novelty, 1024-sample windows
//   render  - YIN fundamental (the render is a single synthesised guitar line)
//   source  - harmonic product spectrum over a fixed melody range (MIDI 48-84)
// Analysis constants fixed before the first run; tolerances come from Gates.

#include "E2E.h"

#include <algorithm>
#include <cmath>

namespace e2e
{
namespace
{
constexpr double kSourceLo = 48, kSourceHi = 84;

std::vector<double> hfcOnsets (const std::vector<float>& x, double fs)
{
    const int n = 1024, hop = 128;
    juce::dsp::FFT fft (10);
    std::vector<float> buf ((size_t) (2 * n));
    const int frames = x.size() > (size_t) n ? (int) ((x.size() - (size_t) n) / hop) : 0;
    std::vector<double> d ((size_t) frames, 0.0);
    double prev = 0;
    for (int f = 0; f < frames; ++f)
    {
        std::fill (buf.begin(), buf.end(), 0.f);
        for (int i = 0; i < n; ++i)
            buf[(size_t) i] = x[(size_t) f * hop + (size_t) i] * (0.5f - 0.5f * std::cos (2.f * juce::MathConstants<float>::pi * (float) i / (float) n));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        double hfc = 0;
        for (int k = 1; k < n / 2; ++k) hfc += (double) k * buf[(size_t) k] * buf[(size_t) k];
        const double l = std::log (1e-9 + hfc);
        d[(size_t) f] = std::max (0.0, l - prev);
        prev = l;
    }
    // Peak-pick: local max over +-40 ms, above the 0.5 s running mean plus a margin.
    std::vector<double> on;
    const int w = (int) (0.04 * fs / hop), m = (int) (0.25 * fs / hop);
    for (int f = 0; f < frames; ++f)
    {
        bool peak = d[(size_t) f] > 0;
        for (int j = std::max (0, f - w); j <= std::min (frames - 1, f + w) && peak; ++j) if (d[(size_t) j] > d[(size_t) f]) peak = false;
        if (! peak) continue;
        double mean = 0; int c = 0;
        for (int j = std::max (0, f - m); j <= std::min (frames - 1, f + m); ++j) { mean += d[(size_t) j]; ++c; }
        if (d[(size_t) f] > mean / c + 0.3)
            on.push_back (((double) f * hop + n / 2) / fs);
    }
    return on;
}

double yinMidi (const std::vector<float>& x, double fs, double t0, double t1)
{
    const int a = (int) (t0 * fs), b = std::min ((int) x.size(), (int) (t1 * fs));
    const int tauMax = (int) (fs / 70.0), W = b - a - tauMax;
    if (a < 0 || W < 256) return 0;
    std::vector<double> d ((size_t) tauMax + 1, 0.0), cm ((size_t) tauMax + 1, 1.0);
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double s = 0;
        for (int i = 0; i < W; ++i) { const double e = x[(size_t) (a + i)] - x[(size_t) (a + i + tau)]; s += e * e; }
        d[(size_t) tau] = s;
    }
    double run = 0;
    for (int tau = 1; tau <= tauMax; ++tau) { run += d[(size_t) tau]; cm[(size_t) tau] = d[(size_t) tau] * tau / (run + 1e-12); }
    int best = -1;
    for (int tau = (int) (fs / 1200.0); tau < tauMax; ++tau)
        if (cm[(size_t) tau] < 0.15) { while (tau + 1 < tauMax && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau; best = tau; break; }
    if (best < 0) best = (int) (std::min_element (cm.begin() + (long) (fs / 1200.0), cm.end() - 1) - cm.begin());
    const double l = cm[(size_t) best - 1], c = cm[(size_t) best], r = cm[(size_t) best + 1];
    const double tau = best + 0.5 * (l - r) / (l - 2 * c + r + 1e-12);
    return 69.0 + 12.0 * std::log2 (fs / tau / 440.0);
}

double hpsMidi (const std::vector<float>& x, double fs, double t0, double t1)
{
    const int n = 16384;
    juce::dsp::FFT fft (14);
    std::vector<float> buf ((size_t) (2 * n), 0.f);
    const int a = (int) (t0 * fs), len = std::min ((int) (t1 * fs) - a, n);
    if (a < 0 || len < 256 || a + len > (int) x.size()) return 0;
    for (int i = 0; i < len; ++i)
        buf[(size_t) i] = x[(size_t) (a + i)] * (0.5f - 0.5f * std::cos (2.f * juce::MathConstants<float>::pi * (float) i / (float) len));
    fft.performFrequencyOnlyForwardTransform (buf.data());
    const double df = fs / n;
    int bestK = 0; double bestV = -1e30;
    for (int k = (int) (440.0 * std::pow (2.0, (kSourceLo - 69) / 12.0) / df); k <= (int) (440.0 * std::pow (2.0, (kSourceHi - 69) / 12.0) / df); ++k)
    {
        double v = 0;
        for (int h = 1; h <= 3; ++h) { const int kk = k * h; if (kk >= n / 2) { v = -1e30; break; } v += std::log (1e-9 + buf[(size_t) kk]); }
        if (v > bestV) { bestV = v; bestK = k; }
    }
    if (bestK <= 0) return 0;
    const double l = std::log (1e-9 + buf[(size_t) bestK - 1]), c = std::log (1e-9 + buf[(size_t) bestK]), r = std::log (1e-9 + buf[(size_t) bestK + 1]);
    const double k = bestK + 0.5 * (l - r) / (l - 2 * c + r + 1e-12);
    return 69.0 + 12.0 * std::log2 (k * df / 440.0);
}

double nearestOnset (const std::vector<double>& on, double t, double tol)
{
    double best = -1, bd = tol;
    for (double o : on) if (std::abs (o - t) <= bd) { bd = std::abs (o - t); best = o; }
    return best;
}

/** How long a pitch keeps sounding from t: HPS frames every 20 ms stay within
    a semitone of target, stopping at the first two consecutive misses. */
double trackDuration (const std::vector<float>& x, double fs, double t, double target, double limit)
{
    int miss = 0; double last = t;
    for (double u = t + 0.02; u < t + limit; u += 0.02)
    {
        const double p = hpsMidi (x, fs, u, u + 0.06);
        if (std::abs (p - target) <= 1.0) { miss = 0; last = u + 0.03; }
        else if (++miss >= 2) break;
    }
    return last - t;
}

double renderDuration (const std::vector<float>& x, double fs, double t, double limit)
{
    // Envelope in 5 ms blocks; the note ends where it falls 30 dB below its peak.
    const int blk = (int) (0.005 * fs);
    double peak = 0, end = t;
    std::vector<double> env;
    for (double u = t; u < t + limit; u += 0.005)
    {
        const int a = (int) (u * fs);
        if (a < 0 || a + blk >= (int) x.size()) break;
        double s = 0; for (int i = 0; i < blk; ++i) s += x[(size_t) (a + i)] * x[(size_t) (a + i)];
        env.push_back (std::sqrt (s / blk));
        peak = std::max (peak, env.back());
    }
    for (size_t i = 0; i < env.size(); ++i) if (env[i] > peak * 0.0316) end = t + 0.005 * (double) (i + 1);
    return end - t;
}
} // namespace

VerifyResult verify (const Pcm& source, const std::vector<float>& render, double renderFs, double offset,
                     const std::vector<LearnedNote>& notes, const Gates& gates)
{
    VerifyResult r;
    const auto srcOn = hfcOnsets (source.x, source.fs);
    const auto renOn = hfcOnsets (render, renderFs);

    double sumErr = 0, sumAbs = 0, sumDur = 0; int nOn = 0, nPitch = 0, nDur = 0, counted = 0;
    double cents1 = 0, cents2 = 0; int c1 = 0, c2 = 0;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& ln = notes[i];
        if (ln.skipped) continue;
        ++counted;
        VerifyNote v;
        v.tabIndex = ln.tabIndex;
        v.midi = ln.midi;
        v.scheduled = ln.onset;

        const double ro = nearestOnset (renOn, ln.onset - offset, 0.1);
        v.renderOnset = ro >= 0 ? ro + offset : -1;
        const double ref = ro >= 0 ? ro + offset : ln.onset;
        v.sourceOnset = nearestOnset (srcOn, ref, 0.15);

        const double win = std::min (0.15, std::max (0.06, ln.duration - 0.03));
        v.renderPitch = yinMidi (render, renderFs, ref - offset + 0.03, ref - offset + 0.03 + std::max (win, 0.08));
        const double sref = v.sourceOnset >= 0 ? v.sourceOnset : ln.onset;
        v.sourcePitch = hpsMidi (source.x, source.fs, sref + 0.02, sref + 0.02 + std::max (win, 0.08));

        v.pitchOk = v.renderPitch > 0 && v.sourcePitch > 0 && std::abs (v.renderPitch - v.sourcePitch) <= gates.pitchTolSemis;
        if (v.pitchOk) ++nPitch;
        const double dev = std::abs (v.renderPitch - v.sourcePitch);
        if (v.renderPitch > 0 && v.sourcePitch > 0 && std::abs (dev - 12.0) <= 1.0) ++r.octaveErrors;

        if (v.renderOnset >= 0 && v.sourceOnset >= 0)
        {
            const double e = (v.renderOnset - v.sourceOnset) * 1000.0;
            v.onsetOk = std::abs (e) <= gates.onsetTolMs;
            sumErr += e; sumAbs += std::abs (e);
            r.maxOnsetErrMs = std::max (r.maxOnsetErrMs, std::abs (e));
            if (v.onsetOk) ++nOn;
        }

        const double nextOn = i + 1 < notes.size() ? notes[i + 1].onset : ln.onset + 2.0;
        v.renderDur = renderDuration (render, renderFs, ref - offset, std::max (0.05, nextOn - ln.onset + 0.3));
        v.sourceDur = v.sourcePitch > 0 ? trackDuration (source.x, source.fs, sref, v.sourcePitch, std::max (0.05, nextOn - sref)) : 0;
        const double de = (v.renderDur - v.sourceDur) * 1000.0;
        v.durOk = std::abs (de) <= gates.durTolMs;
        if (v.durOk) ++nDur;
        sumDur += std::abs (de);
        r.maxDurErrMs = std::max (r.maxDurErrMs, std::abs (de));

        if (v.sourcePitch > 0)
        {
            const double cents = (v.sourcePitch - std::round (v.sourcePitch)) * 100.0;
            if (counted <= (int) notes.size() / 2) { cents1 += cents; ++c1; } else { cents2 += cents; ++c2; }
        }
        r.notes.push_back (v);
    }
    const double nn = std::max (1, counted);
    r.pitchRate = nPitch / nn;
    r.onsetRate = nOn / nn;
    r.durRate = nDur / nn;
    r.meanOnsetErrMs = sumErr / std::max (1.0, (double) r.notes.size());
    r.meanAbsOnsetErrMs = sumAbs / std::max (1.0, (double) r.notes.size());
    r.meanDurErrMs = sumDur / nn;
    r.sourceCentsFirstHalf = c1 ? cents1 / c1 : 0;
    r.sourceCentsSecondHalf = c2 ? cents2 / c2 : 0;
    return r;
}

void drawPianoRoll (const juce::File& file, const Pcm& source, double t0, double t1,
                    const std::vector<LearnedNote>& notes, const VerifyResult& v)
{
    const int W = 1600, lo = 48, hi = 84, rowH = 14, H = (hi - lo + 1) * rowH + 40;
    // Software image: JUCE 8's default native image on Windows is Direct2D,
    // whose drawing only lands when the Graphics context closes.
    juce::Image img (juce::Image::RGB, W, H, true, juce::SoftwareImageType());
    {
    juce::Graphics g (img);
    g.fillAll (juce::Colours::black);

    // Source energy per semitone (independent constant-Q style sum of FFT bins).
    const int n = 4096; juce::dsp::FFT fft (12);
    std::vector<float> buf ((size_t) (2 * n));
    const double df = source.fs / n;
    for (int x = 0; x < W; ++x)
    {
        const double t = t0 + (t1 - t0) * x / W;
        const int a = (int) (t * source.fs) - n / 2;
        if (a < 0 || a + n >= (int) source.x.size()) continue;
        std::fill (buf.begin(), buf.end(), 0.f);
        for (int i = 0; i < n; ++i) buf[(size_t) i] = source.x[(size_t) (a + i)] * (0.5f - 0.5f * std::cos (2.f * juce::MathConstants<float>::pi * (float) i / (float) n));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int m = lo; m <= hi; ++m)
        {
            const double f0 = 440.0 * std::pow (2.0, (m - 69 - 0.5) / 12.0), f1 = 440.0 * std::pow (2.0, (m - 69 + 0.5) / 12.0);
            double e = 0; for (int k = (int) (f0 / df); k <= (int) (f1 / df); ++k) e = std::max (e, (double) buf[(size_t) k]);
            const float b = juce::jlimit (0.f, 1.f, (float) std::log1p (100.0 * e / n) / 4.f);   // display only
            g.setColour (juce::Colour::fromFloatRGBA (b, b, b, 1.f));
            g.fillRect (x, 20 + (hi - m) * rowH, 1, rowH);
        }
    }
    const auto X = [&] (double t) { return (float) ((t - t0) / (t1 - t0) * W); };
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& ln = notes[i];
        if (ln.skipped) continue;
        const VerifyNote* vn = nullptr;
        for (const auto& q : v.notes) if (q.tabIndex == ln.tabIndex) vn = &q;
        const bool ok = vn != nullptr && vn->pitchOk && vn->onsetOk;
        g.setColour (ok ? juce::Colours::lime : juce::Colours::red);
        g.drawRect (juce::Rectangle<float> (X (ln.onset), 20.f + (float) (hi - ln.midi) * rowH, std::max (2.f, X (ln.onset + ln.duration) - X (ln.onset)), (float) rowH), 2.f);
        if (vn != nullptr && vn->sourcePitch > 0)
        {
            g.setColour (juce::Colours::cyan);
            const double t = vn->sourceOnset >= 0 ? vn->sourceOnset : ln.onset;
            g.fillEllipse (X (t) - 3, 20.f + (float) (hi - vn->sourcePitch) * rowH + rowH / 2.f - 3, 6, 6);
        }
    }
    g.setColour (juce::Colours::white);
    g.setFont (13.f);
    g.drawText ("source spectrum by semitone (grey), learned notes (green = pitch+onset pass, red = fail), verifier source pitch (cyan dot)",
                8, 2, W - 16, 16, juce::Justification::left);
    for (int m = lo; m <= hi; m += 12) g.drawText (juce::MidiMessage::getMidiNoteName (m, true, true, 4), 2, 20 + (hi - m) * rowH, 40, rowH, juce::Justification::left);
    }

    file.deleteFile();
    juce::FileOutputStream os (file);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, os);
}

} // namespace e2e

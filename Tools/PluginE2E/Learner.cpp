// Joint tab + audio learner.
//
// The tab supplies the note sequence and pitches; the audio supplies when each
// note happens, how long it lasts, the key (transpose) and the tuning
// reference, and may veto or correct a tab note it cannot hear. One dynamic
// programme scores every tab note against the audio at once, so neither input
// is processed on its own.
//
// All constants below were fixed before the first run on any real song.

#include "E2E.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <numeric>

namespace e2e
{
namespace
{
constexpr int    kFftOrder = 11;            // 2048 at 22.05 kHz: 10.8 Hz bins, 93 ms window
constexpr int    kN = 1 << kFftOrder;
constexpr int    kHop = 128;                // 5.8 ms
constexpr int    kMidiLo = 36, kMidiHi = 96;
constexpr int    kPitches = kMidiHi - kMidiLo + 1;
constexpr int    kHarmonics = 5;
constexpr double kMinIoiSec = 0.05, kMaxIoiSec = 1.5;
constexpr double kSkipPenalty = 3.0;
constexpr double kRefineSec = 0.08;

double hzOf (double midi, double cents) { return 440.0 * std::pow (2.0, (midi - 69.0 + cents / 100.0) / 12.0); }

struct Spectra
{
    int frames = 0;
    std::vector<float> sal;     // frames * kPitches, harmonic-sum salience (log magnitude)
    std::vector<float> flux;    // frames, broadband spectral flux
};

std::vector<float> hann (int n)
{
    std::vector<float> w ((size_t) n);
    for (int i = 0; i < n; ++i) w[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) n);
    return w;
}

/** Reference tuning in cents from spectral peaks across the whole recording. */
double estimateTuning (const Pcm& a)
{
    juce::dsp::FFT fft (12);
    const int n = 4096;
    const auto w = hann (n);
    std::vector<float> buf ((size_t) (2 * n));
    double sx = 0, sy = 0;
    const double df = a.fs / n;
    for (size_t start = 0; start + (size_t) n < a.x.size(); start += 2048)
    {
        std::fill (buf.begin(), buf.end(), 0.f);
        for (int i = 0; i < n; ++i) buf[(size_t) i] = a.x[start + (size_t) i] * w[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (buf.data());
        float mx = 0;
        for (int k = 1; k < n / 2; ++k) mx = std::max (mx, buf[(size_t) k]);
        if (mx <= 0) continue;
        for (int k = (int) (150 / df); k < (int) (2000 / df); ++k)
        {
            const float m = buf[(size_t) k];
            if (m < 0.1f * mx || m <= buf[(size_t) k - 1] || m < buf[(size_t) k + 1] || m <= buf[(size_t) k - 2] || m < buf[(size_t) k + 2]) continue;
            const double l = std::log (buf[(size_t) k - 1] + 1e-9), c = std::log (m + 1e-9), r = std::log (buf[(size_t) k + 1] + 1e-9);
            const double d = 0.5 * (l - r) / (l - 2 * c + r);
            const double f = (k + d) * df;
            const double semis = 12.0 * std::log2 (f / 440.0);
            const double frac = semis - std::round (semis);              // -0.5..0.5 semitone
            const double ang = frac * 2.0 * juce::MathConstants<double>::pi;
            sx += m * std::cos (ang);
            sy += m * std::sin (ang);
        }
    }
    return std::atan2 (sy, sx) / (2.0 * juce::MathConstants<double>::pi) * 100.0;
}

Spectra analyse (const Pcm& a, double cents)
{
    Spectra s;
    juce::dsp::FFT fft (kFftOrder);
    const auto w = hann (kN);
    std::vector<float> buf ((size_t) (2 * kN)), prev ((size_t) kN / 2, 0.f), cur ((size_t) kN / 2);
    const double df = a.fs / kN;
    s.frames = (int) ((a.x.size() - (size_t) kN) / kHop);
    s.sal.assign ((size_t) s.frames * kPitches, 0.f);
    s.flux.assign ((size_t) s.frames, 0.f);

    // Bin of each harmonic of each pitch, fixed once tuning is known.
    std::vector<int> bins (kPitches * kHarmonics);
    for (int p = 0; p < kPitches; ++p)
        for (int h = 0; h < kHarmonics; ++h)
            bins[(size_t) (p * kHarmonics + h)] = (int) std::lround (hzOf (kMidiLo + p, cents) * (h + 1) / df);

    const int fluxLo = (int) (80 / df), fluxHi = (int) (4000 / df);
    for (int f = 0; f < s.frames; ++f)
    {
        std::fill (buf.begin(), buf.end(), 0.f);
        const float* x = a.x.data() + (size_t) f * kHop;
        for (int i = 0; i < kN; ++i) buf[(size_t) i] = x[i] * w[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 0; k < kN / 2; ++k) cur[(size_t) k] = std::log1p (100.f * buf[(size_t) k] / (float) kN);

        float fl = 0;
        for (int k = fluxLo; k < fluxHi; ++k) fl += std::max (0.f, cur[(size_t) k] - prev[(size_t) k]);
        s.flux[(size_t) f] = fl;

        for (int p = 0; p < kPitches; ++p)
        {
            float sum = 0, wt = 1;
            for (int h = 0; h < kHarmonics; ++h, wt *= 0.8f)
            {
                const int b = bins[(size_t) (p * kHarmonics + h)];
                if (b + 1 >= kN / 2) break;
                sum += wt * std::max ({ cur[(size_t) b - 1], cur[(size_t) b], cur[(size_t) b + 1] });
            }
            s.sal[(size_t) f * kPitches + (size_t) p] = sum;
        }
        std::swap (prev, cur);
    }
    return s;
}

double frameTime (int f, double fs) { return ((double) f * kHop + kN / 2) / fs; }

struct Evidence
{
    // attack[p][f]: how strongly pitch p begins at frame f (rise + dominance + broadband onset)
    std::vector<float> attack;
    std::vector<float> dom;      // per-frame pitch dominance, z-scored across pitches
};

Evidence buildEvidence (const Spectra& s)
{
    Evidence e;
    const int F = s.frames;
    e.dom.assign ((size_t) F * kPitches, 0.f);
    for (int f = 0; f < F; ++f)
    {
        const float* r = &s.sal[(size_t) f * kPitches];
        std::vector<float> tmp (r, r + kPitches);
        std::nth_element (tmp.begin(), tmp.begin() + kPitches / 2, tmp.end());
        const float med = tmp[kPitches / 2];
        double var = 0;
        for (int p = 0; p < kPitches; ++p) var += (r[p] - med) * (r[p] - med);
        const float sd = (float) std::sqrt (var / kPitches) + 1e-4f;
        for (int p = 0; p < kPitches; ++p) e.dom[(size_t) f * kPitches + (size_t) p] = juce::jlimit (-2.f, 4.f, (r[p] - med) / sd);
    }

    // Robust z of broadband flux.
    std::vector<float> fl (s.flux);
    std::vector<float> t2 (fl);
    std::nth_element (t2.begin(), t2.begin() + (long) t2.size() / 2, t2.end());
    const float med = t2[t2.size() / 2];
    for (auto& v : t2) v = std::abs (v - med);
    std::nth_element (t2.begin(), t2.begin() + (long) t2.size() / 2, t2.end());
    const float mad = t2[t2.size() / 2] * 1.4826f + 1e-6f;

    // Global scale of salience change, for the rise term.
    double ss = 0; size_t cnt = 0;
    for (int f = 1; f < F; f += 7)
        for (int p = 0; p < kPitches; ++p, ++cnt)
        {
            const float d = s.sal[(size_t) f * kPitches + (size_t) p] - s.sal[(size_t) (f - 1) * kPitches + (size_t) p];
            ss += d * d;
        }
    const float riseScale = (float) std::sqrt (ss / (double) std::max<size_t> (1, cnt)) * 4.f + 1e-6f;

    e.attack.assign ((size_t) F * kPitches, -5.f);
    for (int f = 8; f + 12 < F; ++f)
    {
        const float flz = juce::jlimit (-1.f, 4.f, (fl[(size_t) f] - med) / mad);
        for (int p = 0; p < kPitches; ++p)
        {
            float after = 0, before = 0, dm = 0;
            for (int j = 0; j < 4; ++j)  after  += s.sal[(size_t) (f + j) * kPitches + (size_t) p];
            for (int j = 2; j < 8; ++j)  before += s.sal[(size_t) (f - j) * kPitches + (size_t) p];
            for (int j = 2; j < 12; ++j) dm += e.dom[(size_t) (f + j) * kPitches + (size_t) p];
            const float rise = juce::jlimit (-1.f, 3.f, (after / 4.f - before / 6.f) / riseScale);
            e.attack[(size_t) p * F + (size_t) f] = rise + dm / 10.f + 0.5f * flz;
        }
    }
    return e;
}

struct Path { double score = -1e30; std::vector<int> frame; std::vector<bool> skipped; };

/** Aligns the whole tab to the audio for one transpose: every tab note gets a
    start frame, consecutive starts kMinIoi..kMaxIoi apart; a tab note may be
    skipped at a fixed penalty when the audio does not support it. */
Path alignTab (const Evidence& e, int F, const std::vector<int>& pitch, double fs)
{
    const int N = (int) pitch.size();
    const int minG = (int) std::ceil (kMinIoiSec * fs / kHop), maxG = (int) (kMaxIoiSec * fs / kHop);
    const float NEG = -1e30f;
    std::vector<float> V ((size_t) N * F, NEG);
    std::vector<int>   back ((size_t) N * F, -1);
    std::vector<char>  viaSkip ((size_t) N * F, 0);

    const auto att = [&] (int n, int f) -> float
    {
        const int p = pitch[(size_t) n] - kMidiLo;
        if (p < 0 || p >= kPitches) return -5.f;
        return e.attack[(size_t) p * F + (size_t) f];
    };

    for (int f = 0; f < F; ++f) V[(size_t) f] = att (0, f);

    // Sliding-window maximum of row r over [f-hi, f-lo].
    const auto windowMax = [&] (int r, int lo, int hi, std::vector<float>& best, std::vector<int>& arg)
    {
        best.assign ((size_t) F, NEG); arg.assign ((size_t) F, -1);
        std::deque<int> dq;
        for (int f = 0; f < F; ++f)
        {
            const int add = f - lo;
            if (add >= 0)
            {
                while (! dq.empty() && V[(size_t) r * F + (size_t) dq.back()] <= V[(size_t) r * F + (size_t) add]) dq.pop_back();
                dq.push_back (add);
            }
            while (! dq.empty() && dq.front() < f - hi) dq.pop_front();
            if (! dq.empty()) { best[(size_t) f] = V[(size_t) r * F + (size_t) dq.front()]; arg[(size_t) f] = dq.front(); }
        }
    };

    std::vector<float> b1, b2; std::vector<int> a1, a2;
    for (int n = 1; n < N; ++n)
    {
        windowMax (n - 1, minG, maxG, b1, a1);
        if (n >= 2) windowMax (n - 2, 2 * minG, 2 * maxG, b2, a2);
        for (int f = 0; f < F; ++f)
        {
            float best = b1[(size_t) f]; int arg = a1[(size_t) f]; char sk = 0;
            if (n >= 2 && b2[(size_t) f] - (float) kSkipPenalty > best) { best = b2[(size_t) f] - (float) kSkipPenalty; arg = a2[(size_t) f]; sk = 1; }
            if (arg < 0) continue;
            V[(size_t) n * F + (size_t) f] = best + att (n, f);
            back[(size_t) n * F + (size_t) f] = arg;
            viaSkip[(size_t) n * F + (size_t) f] = sk;
        }
    }

    Path path;
    int endF = -1;
    for (int f = 0; f < F; ++f)
        if (V[(size_t) (N - 1) * F + (size_t) f] > path.score) { path.score = V[(size_t) (N - 1) * F + (size_t) f]; endF = f; }
    if (endF < 0) return path;

    path.frame.assign ((size_t) N, -1);
    path.skipped.assign ((size_t) N, false);
    int n = N - 1, f = endF;
    while (n >= 0 && f >= 0)
    {
        path.frame[(size_t) n] = f;
        const int pf = back[(size_t) n * F + (size_t) f];
        const bool sk = viaSkip[(size_t) n * F + (size_t) f] != 0;
        if (n == 0) break;
        if (sk) { path.skipped[(size_t) (n - 1)] = true; path.frame[(size_t) (n - 1)] = -1; n -= 2; }
        else n -= 1;
        f = pf;
    }
    return path;
}

/** Sharp broadband onset function: 512-sample windows, 64-sample hop. */
std::vector<float> fineFlux (const Pcm& a, int& hop, int& n)
{
    n = 512; hop = 64;
    juce::dsp::FFT fft (9);
    const auto w = hann (n);
    std::vector<float> buf ((size_t) (2 * n)), prev ((size_t) n / 2, 0.f), cur ((size_t) n / 2);
    const int frames = (int) ((a.x.size() - (size_t) n) / (size_t) hop);
    std::vector<float> out ((size_t) frames, 0.f);
    for (int f = 0; f < frames; ++f)
    {
        std::fill (buf.begin(), buf.end(), 0.f);
        for (int i = 0; i < n; ++i) buf[(size_t) i] = a.x[(size_t) f * (size_t) hop + (size_t) i] * w[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (buf.data());
        float fl = 0;
        for (int k = 4; k < n / 2; ++k) { cur[(size_t) k] = std::log1p (100.f * buf[(size_t) k] / (float) n); fl += std::max (0.f, cur[(size_t) k] - prev[(size_t) k]); }
        out[(size_t) f] = fl;
        std::swap (prev, cur);
    }
    return out;
}
} // namespace

LearnResult learn (const Pcm& audio, const std::vector<TabNote>& tab)
{
    LearnResult res;
    res.tuningCents = estimateTuning (audio);
    const auto spectra = analyse (audio, res.tuningCents);
    const auto ev = buildEvidence (spectra);
    const int F = spectra.frames;

    // The audio decides the key: align the tab at every transpose within an
    // octave either way and keep the best-supported one.
    Path best; int bestT = 0; double second = -1e30; int secondT = 0;
    for (int t = -12; t <= 12; ++t)
    {
        std::vector<int> pitch;
        for (const auto& n : tab) pitch.push_back (n.midi + t);
        auto p = alignTab (ev, F, pitch, audio.fs);
        if (p.score > best.score) { second = best.score; secondT = bestT; best = std::move (p); bestT = t; }
        else if (p.score > second) { second = p.score; secondT = t; }
    }
    res.transpose = bestT;
    res.score = best.score;
    res.runnerUpScore = second;
    res.runnerUpTranspose = secondT;

    int fhop = 0, fn = 0;
    const auto ff = fineFlux (audio, fhop, fn);

    for (size_t i = 0; i < tab.size(); ++i)
    {
        LearnedNote ln;
        ln.tabIndex = tab[i].index;
        ln.tabMidi = tab[i].midi + bestT;
        ln.midi = ln.tabMidi;
        if (best.frame.empty() || best.frame[i] < 0) { ln.skipped = true; res.notes.push_back (ln); continue; }

        const int f = best.frame[i];
        const int p = ln.tabMidi - kMidiLo;

        // The audio may correct the tab by a semitone when the neighbour is
        // clearly the note that sounds and the tab's pitch is not.
        const auto domAt = [&] (int pp)
        {
            if (pp < 0 || pp >= kPitches) return -2.0;
            double d = 0;
            for (int j = 2; j < 12 && f + j < F; ++j) d += ev.dom[(size_t) (f + j) * kPitches + (size_t) pp];
            return d / 10.0;
        };
        const double dSelf = domAt (p), dDown = domAt (p - 1), dUp = domAt (p + 1);
        if (dSelf < 0.5 && std::max (dDown, dUp) > dSelf + 1.0)
        {
            ln.corrected = true;
            ln.midi += dUp > dDown ? 1 : -1;
        }
        ln.confidence = std::max (dSelf, ln.corrected ? std::max (dDown, dUp) : dSelf);

        // Onset: refine the coarse frame to the sharpest broadband attack nearby.
        const double coarse = frameTime (f, audio.fs);
        double bestT2 = coarse, bestV = -1;
        for (int k = (int) ((coarse - kRefineSec) * audio.fs / fhop); k <= (int) ((coarse + kRefineSec) * audio.fs / fhop); ++k)
        {
            if (k < 1 || k >= (int) ff.size()) continue;
            const double t = ((double) k * fhop + fn / 2) / audio.fs;
            const double v = ff[(size_t) k] * (1.0 - 0.5 * std::abs (t - coarse) / kRefineSec);
            if (v > bestV) { bestV = v; bestT2 = t; }
        }
        ln.onset = bestT2;
        res.notes.push_back (ln);
    }

    // Duration: until the next learned onset, or earlier if the pitch fades
    // out of the audio (salience 2 log-units below its attack peak or no longer
    // dominant).
    for (size_t i = 0; i < res.notes.size(); ++i)
    {
        auto& ln = res.notes[i];
        if (ln.skipped) continue;
        double next = ln.onset + 2.0;
        for (size_t j = i + 1; j < res.notes.size(); ++j) if (! res.notes[j].skipped) { next = res.notes[j].onset; break; }
        const int p = ln.midi - kMidiLo;
        const int f0 = (int) std::lround ((ln.onset * audio.fs - kN / 2) / kHop);
        double end = next;
        if (p >= 0 && p < kPitches && f0 >= 0)
        {
            float peak = -1e9f;
            for (int j = 0; j < 12 && f0 + j < F; ++j) peak = std::max (peak, spectra.sal[(size_t) (f0 + j) * kPitches + (size_t) p]);
            for (int f = f0 + 12; f < F && frameTime (f, audio.fs) < next; ++f)
                if (spectra.sal[(size_t) f * kPitches + (size_t) p] < peak - 2.f || ev.dom[(size_t) f * kPitches + (size_t) p] < 0.f)
                { end = frameTime (f, audio.fs); break; }
        }
        ln.duration = std::max (0.05, end - ln.onset);
    }
    return res;
}

} // namespace e2e

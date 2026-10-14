#include "dsp/ArticulationNoise.h"

#include <algorithm>
#include <cmath>

namespace fw::dsp {

namespace {
    float clampf (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    constexpr int kFilterHop = 16;   // band-pass centre updated every 16 samples while sweeping
}

void ArticulationNoise::prepare (double sampleRate)
{
    fs_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
}

void ArticulationNoise::reset() noexcept
{
    for (auto& v : voices_) { v.on = false; v.bp.reset(); }
    for (auto& t : ticks_)  { t.on = false; t.hp.reset(); }
}

bool ArticulationNoise::active() const noexcept
{
    for (const auto& v : voices_) if (v.on) return true;
    for (const auto& t : ticks_)  if (t.on) return true;
    return false;
}

float ArticulationNoise::white() noexcept
{
    seed_ ^= seed_ << 13; seed_ ^= seed_ >> 17; seed_ ^= seed_ << 5;
    return (float) ((double) seed_ * (2.0 / 4294967295.0) - 1.0);
}

ArticulationNoise::Voice& ArticulationNoise::freeVoice() noexcept
{
    Voice* oldest = &voices_[0];
    for (auto& v : voices_)
    {
        if (! v.on) return v;
        if (v.age < oldest->age) oldest = &v;
    }
    return *oldest;
}

ArticulationNoise::Tick& ArticulationNoise::freeTick() noexcept
{
    Tick* best = &ticks_[0];
    for (auto& t : ticks_)
    {
        if (! t.on) return t;
        if (t.pos > best->pos) best = &t;   // furthest along = nearly finished
    }
    return *best;
}

void ArticulationNoise::stroke (int dir, float durSec, float velocity, const float* panL, const float* panR) noexcept
{
    const float vel = clampf (velocity, 0.f, 1.f);
    if (vel <= 0.f) return;
    const float dur = clampf (durSec, 0.012f, 0.12f);
    const bool down = dir >= 0;
    const int first = down ? 0 : kNumStrings - 1, last = down ? kNumStrings - 1 : 0;

    Voice& v = freeVoice();
    v = Voice {};
    v.on = true;
    v.age = ++clock_;
    v.attack = std::max (1, (int) (0.002 * fs_));
    v.decay  = std::max (1, (int) (0.018 * fs_));
    v.len    = (int) (dur * fs_) + v.decay;
    // Pick across wound and plain strings is mostly 0.6-2.5 kHz, not hiss:
    // a darker, slightly narrower sweep at a lower level reads as strings.
    v.amp    = 0.022f * vel * (0.4f + 0.6f * vel);
    v.f0 = down ? 900.f : 2300.f;
    v.f1 = down ? 2300.f : 900.f;
    v.q  = 1.6f;
    v.pl0 = panL[first]; v.pr0 = panR[first];
    v.pl1 = panL[last];  v.pr1 = panR[last];
    v.bp.setBandPass (fs_, v.f0, v.q);

    for (int k = 0; k < kNumStrings; ++k)
    {
        const int s = down ? k : kNumStrings - 1 - k;
        Tick& t = freeTick();
        t = Tick {};
        t.on = true;
        t.delay = (int) (dur * fs_ * (float) k / (float) (kNumStrings - 1));
        t.len = std::max (2, (int) (0.005 * fs_));
        t.amp = 0.03f * vel * (s < 3 ? 1.f : 0.8f);
        t.pl = panL[s]; t.pr = panR[s];
        t.hp.setHighPass (fs_, 1100.0, 0.7071);   // a soft tick, not a 2.5 kHz click
    }
}

void ArticulationNoise::squeak (int string, int dir, float durSec, float velocity, float panL, float panR) noexcept
{
    const float vel = clampf (velocity, 0.f, 1.f);
    if (vel <= 0.f) return;
    const bool wound = string <= 2;

    Voice& v = freeVoice();
    v = Voice {};
    v.on = true;
    v.age = ++clock_;
    v.len = std::max (8, (int) (clampf (durSec, 0.06f, 0.15f) * fs_));
    v.raisedCosine = true;
    // A Q-7 band-pass keeps ~1 % of white noise's power, so the level is set
    // for the filtered result: about 18 dB under a picked note on wound strings.
    v.amp = 0.8f * vel * (wound ? 1.f : 0.25f);
    v.f0 = dir >= 0 ? 1200.f : 3800.f;
    v.f1 = dir >= 0 ? 3800.f : 1200.f;
    v.q  = 7.f;
    v.pl0 = v.pl1 = panL;
    v.pr0 = v.pr1 = panR;
    v.bp.setBandPass (fs_, v.f0, v.q);
}

void ArticulationNoise::render (float* left, float* right, int n) noexcept
{
    if (left == nullptr || right == nullptr) return;

    for (auto& v : voices_)
    {
        if (! v.on) continue;
        for (int i = 0; i < n && v.pos < v.len; ++i, ++v.pos)
        {
            const float u = (float) v.pos / (float) v.len;
            if (v.pos % kFilterHop == 0 && v.f0 != v.f1)
                v.bp.setBandPass (fs_, (double) (v.f0 * std::pow (v.f1 / v.f0, u)), (double) v.q);

            float env;
            if (v.raisedCosine) env = 0.5f - 0.5f * std::cos (6.2831853f * u);
            else if (v.pos < v.attack) env = (float) v.pos / (float) v.attack;
            else if (v.pos > v.len - v.decay) env = (float) (v.len - v.pos) / (float) v.decay;
            else env = 1.f;

            const float y = v.bp.process (white()) * env * v.amp;
            left[i]  += y * (v.pl0 + (v.pl1 - v.pl0) * u);
            right[i] += y * (v.pr0 + (v.pr1 - v.pr0) * u);
        }
        if (v.pos >= v.len) v.on = false;
    }

    for (auto& t : ticks_)
    {
        if (! t.on) continue;
        int i = 0;
        if (t.delay > 0)
        {
            const int skip = std::min (t.delay, n);
            t.delay -= skip;
            i = skip;
        }
        for (; i < n && t.pos < t.len; ++i, ++t.pos)
        {
            const float env = 1.f - (float) t.pos / (float) t.len;
            const float y = t.hp.process (white()) * env * t.amp;
            left[i]  += y * t.pl;
            right[i] += y * t.pr;
        }
        if (t.delay <= 0 && t.pos >= t.len) t.on = false;
    }
}

} // namespace fw::dsp

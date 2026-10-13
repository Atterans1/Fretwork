#include "dsp/AmpFx.h"

#include <algorithm>
#include <cmath>

namespace fw::dsp {

namespace {
    constexpr double kTwoPi = 6.283185307179586;
    constexpr double kChorusBaseMs = 7.0, kChorusDepthMs = 2.5, kChorusMaxMs = 20.0;
    constexpr double kDelayMaxMs = 1600.0;

    float clamp01 (float v) noexcept { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

    // Drive into the shaper per model, from Gain 0..1.
    float driveFor (int model, float g) noexcept
    {
        switch (model)
        {
            case 1:  return 1.0f + 3.0f * g;                        // Clean: barely breaks up
            case 2:  return 2.0f + 28.0f * g * std::sqrt (g);        // Crunch
            case 3:  return 6.0f + 194.0f * g * g;                   // Lead
            default: return 1.0f;
        }
    }

    // Output trim: at the default Gain (40 %) and Level 0 dB each model sits at
    // the dry instrument's loudness (measured mean RMS over E2/E3/E4 power
    // chords), so switching the amp in does not jump in volume. More Gain is
    // louder, as on a real amp.
    float makeupFor (int model) noexcept
    {
        switch (model)
        {
            case 1:  return 0.524f;
            case 2:  return 0.222f;
            case 3:  return 0.145f;
            default: return 1.0f;
        }
    }

    float shape (int model, float x) noexcept
    {
        switch (model)
        {
            case 1:  return std::tanh (x);
            case 2:  return x >= 0.f ? std::tanh (x) : std::tanh (0.6f * x) / 0.6f * 0.8f;
            case 3:
            {
                const float s1 = x >= 0.f ? std::tanh (x) : std::tanh (0.5f * x) * 1.6f;
                return std::tanh (2.5f * s1);
            }
            default: return x;
        }
    }
}

void AmpFx::prepare (double sampleRate, int maxBlockSize)
{
    fs_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock_ = std::max (1, maxBlockSize);

    os_ = std::make_unique<juce::dsp::Oversampling<float>> (1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    os_->initProcessing ((size_t) maxBlock_);
    mono_.assign ((size_t) maxBlock_, 0.f);

    chL_.assign ((size_t) std::ceil (kChorusMaxMs * 0.001 * fs_) + 4, 0.f);
    chR_.assign (chL_.size(), 0.f);
    dlL_.assign ((size_t) std::ceil (kDelayMaxMs * 0.001 * fs_) + 4, 0.f);
    dlR_.assign (dlL_.size(), 0.f);

    dcR_ = (float) std::exp (-kTwoPi * 10.0 / fs_);
    fbCoef_ = (float) (1.0 - std::exp (-kTwoPi * 4500.0 / fs_));
    lastCab_ = -1;
    reset();
    updateFilters();
}

void AmpFx::reset() noexcept
{
    if (os_ != nullptr) os_->reset();
    for (auto* b : { &inHp_, &bass_, &mid_, &treble_ }) b->reset();
    for (auto& b : cab_) b.reset();
    dcX_ = dcY_ = 0.f;
    std::fill (chL_.begin(), chL_.end(), 0.f);
    std::fill (chR_.begin(), chR_.end(), 0.f);
    std::fill (dlL_.begin(), dlL_.end(), 0.f);
    std::fill (dlR_.begin(), dlR_.end(), 0.f);
    chW_ = dlW_ = 0;
    lfo_ = 0.0;
    fbLpL_ = fbLpR_ = 0.f;
    delaySamplesNow_ = (float) (p_.delayMs * 0.001 * fs_);
    gainNow_ = driveFor (p_.ampModel, clamp01 (p_.gain01));
    levelNow_ = (float) std::pow (10.0, p_.levelDb / 20.0) * makeupFor (p_.ampModel);
}

bool AmpFx::active() const noexcept
{
    return p_.ampModel > 0 || p_.chorusMix01 > 0.f || p_.delayMix01 > 0.f;
}

int AmpFx::latencySamples() const noexcept
{
    return p_.ampModel > 0 && os_ != nullptr ? (int) std::lround (os_->getLatencyInSamples()) : 0;
}

void AmpFx::setParams (const AmpFxParams& p) noexcept
{
    const bool modelChanged = p.ampModel != p_.ampModel;
    p_ = p;
    p_.ampModel = std::clamp (p_.ampModel, 0, 3);
    p_.cab = std::clamp (p_.cab, 0, 3);

    // Fresh state when the amp is switched in, so no stale filter memory pops.
    if (modelChanged && lastModel_ == 0 && p_.ampModel > 0)
    {
        if (os_ != nullptr) os_->reset();
        for (auto* b : { &inHp_, &bass_, &mid_, &treble_ }) b->reset();
        for (auto& b : cab_) b.reset();
        dcX_ = dcY_ = 0.f;
        gainNow_ = driveFor (p_.ampModel, clamp01 (p_.gain01));
        levelNow_ = (float) std::pow (10.0, p_.levelDb / 20.0) * makeupFor (p_.ampModel);
    }
    lastModel_ = p_.ampModel;
    updateFilters();
}

void AmpFx::updateFilters() noexcept
{
    const auto db = [] (float v01) { return (double) ((clamp01 (v01) - 0.5f) * 24.f); };   // +-12 dB
    inHp_.setHighPass (fs_, p_.ampModel == 3 ? 120.0 : 70.0, 0.7071);
    bass_.setLowShelf (fs_, 120.0, db (p_.bass01));
    mid_.setPeak (fs_, 700.0, 0.7, db (p_.mid01));
    treble_.setHighShelf (fs_, 3200.0, db (p_.treble01));

    if (p_.cab == lastCab_) return;
    lastCab_ = p_.cab;
    for (auto& b : cab_) b.setPassthrough();
    switch (p_.cab)
    {
        case 1:   // 1x12 open back: lighter low end
            cab_[0].setHighPass (fs_, 95.0, 0.7071);
            cab_[1].setPeak (fs_, 2200.0, 1.2, 3.0);
            cab_[2].setLowPass (fs_, 5500.0, 0.7071);
            cab_[3].setLowPass (fs_, 7000.0, 0.5);
            break;
        case 2:   // 2x12
            cab_[0].setHighPass (fs_, 80.0, 0.7071);
            cab_[1].setPeak (fs_, 120.0, 1.0, 2.0);
            cab_[2].setPeak (fs_, 2500.0, 1.3, 3.0);
            cab_[3].setLowPass (fs_, 5000.0, 0.7071);
            cab_[4].setLowPass (fs_, 6000.0, 0.5);
            break;
        case 3:   // 4x12 closed back: thump, scooped low mids, darker top
            cab_[0].setHighPass (fs_, 70.0, 0.7071);
            cab_[1].setPeak (fs_, 100.0, 1.0, 4.0);
            cab_[2].setPeak (fs_, 400.0, 0.9, -3.0);
            cab_[3].setPeak (fs_, 2800.0, 1.5, 4.0);
            cab_[4].setLowPass (fs_, 4200.0, 0.7071);
            break;
        default:
            break;
    }
}

void AmpFx::process (float* left, float* right, int n) noexcept
{
    if (left == nullptr || right == nullptr || n <= 0) return;

    const bool chorusOn = p_.chorusMix01 > 0.f, delayOn = p_.delayMix01 > 0.f;
    // Effects that come back on start from silence, not from an old tail.
    if (chorusOn && ! chorusWasOn_) { std::fill (chL_.begin(), chL_.end(), 0.f); std::fill (chR_.begin(), chR_.end(), 0.f); }
    if (delayOn && ! delayWasOn_)
    {
        std::fill (dlL_.begin(), dlL_.end(), 0.f); std::fill (dlR_.begin(), dlR_.end(), 0.f);
        fbLpL_ = fbLpR_ = 0.f;
        delaySamplesNow_ = (float) (p_.delayMs * 0.001 * fs_);
    }
    chorusWasOn_ = chorusOn;
    delayWasOn_ = delayOn;

    for (int done = 0; done < n; )
    {
        const int chunk = std::min (maxBlock_, n - done);
        if (p_.ampModel > 0) processAmp (left + done, right + done, chunk);
        if (chorusOn)        processChorus (left + done, right + done, chunk);
        if (delayOn)         processDelay (left + done, right + done, chunk);
        done += chunk;
    }
}

void AmpFx::processAmp (float* left, float* right, int n) noexcept
{
    const int model = p_.ampModel;
    const float gainTarget = driveFor (model, clamp01 (p_.gain01));
    const float levelTarget = (float) std::pow (10.0, p_.levelDb / 20.0) * makeupFor (model);

    for (int i = 0; i < n; ++i)
        mono_[(size_t) i] = inHp_.process (0.5f * (left[i] + right[i]));

    float* chans[] = { mono_.data() };
    juce::dsp::AudioBlock<float> block (chans, 1, (size_t) n);
    auto up = os_->processSamplesUp (block);
    float* u = up.getChannelPointer (0);
    const size_t un = up.getNumSamples();
    const float gStep = (gainTarget - gainNow_) / (float) std::max<size_t> (1, un);
    float g = gainNow_;
    for (size_t i = 0; i < un; ++i)
    {
        g += gStep;
        u[i] = shape (model, u[i] * g);
    }
    gainNow_ = gainTarget;
    os_->processSamplesDown (block);

    const float lStep = (levelTarget - levelNow_) / (float) n;
    float lv = levelNow_;
    for (int i = 0; i < n; ++i)
    {
        // DC blocker: asymmetric clipping leaves an offset.
        const float x = mono_[(size_t) i];
        dcY_ = x - dcX_ + dcR_ * dcY_;
        dcX_ = x;
        float y = treble_.process (mid_.process (bass_.process (dcY_)));
        if (p_.cab > 0)
            for (auto& b : cab_) y = b.process (y);
        lv += lStep;
        y *= lv;
        left[i] = right[i] = y;
    }
    levelNow_ = levelTarget;
}

void AmpFx::processChorus (float* left, float* right, int n) noexcept
{
    const int size = (int) chL_.size();
    const double inc = kTwoPi * std::clamp ((double) p_.chorusRateHz, 0.05, 10.0) / fs_;
    const float mix = clamp01 (p_.chorusMix01);
    const auto read = [size] (const std::vector<float>& buf, int w, double delay)
    {
        double pos = (double) w - delay;
        while (pos < 0.0) pos += size;
        const int i0 = (int) pos;
        const float fr = (float) (pos - i0);
        const float a = buf[(size_t) (i0 % size)], b = buf[(size_t) ((i0 + 1) % size)];
        return a + (b - a) * fr;
    };
    for (int i = 0; i < n; ++i)
    {
        chL_[(size_t) chW_] = left[i];
        chR_[(size_t) chW_] = right[i];
        const double dL = (kChorusBaseMs + kChorusDepthMs * std::sin (lfo_)) * 0.001 * fs_;
        const double dR = (kChorusBaseMs + kChorusDepthMs * std::sin (lfo_ + kTwoPi * 0.25)) * 0.001 * fs_;
        const float wl = read (chL_, chW_, dL), wr = read (chR_, chW_, dR);
        // Mix 1 is the classic 50/50 chorus blend.
        left[i]  += (wl - left[i]) * 0.5f * mix;
        right[i] += (wr - right[i]) * 0.5f * mix;
        chW_ = (chW_ + 1) % size;
        lfo_ += inc;
        if (lfo_ > kTwoPi) lfo_ -= kTwoPi;
    }
}

void AmpFx::processDelay (float* left, float* right, int n) noexcept
{
    const int size = (int) dlL_.size();
    const float target = (float) std::clamp (p_.delayMs * 0.001 * fs_, 1.0, (double) size - 2.0);
    const float smooth = (float) (1.0 - std::exp (-1.0 / (0.05 * fs_)));   // 50 ms glide on time changes
    const float mix = clamp01 (p_.delayMix01), fb = std::clamp (p_.feedback01, 0.f, 0.9f);
    for (int i = 0; i < n; ++i)
    {
        delaySamplesNow_ += (target - delaySamplesNow_) * smooth;
        double pos = (double) dlW_ - delaySamplesNow_;
        while (pos < 0.0) pos += size;
        const int i0 = (int) pos;
        const float fr = (float) (pos - i0);
        const float l = dlL_[(size_t) (i0 % size)] + (dlL_[(size_t) ((i0 + 1) % size)] - dlL_[(size_t) (i0 % size)]) * fr;
        const float r = dlR_[(size_t) (i0 % size)] + (dlR_[(size_t) ((i0 + 1) % size)] - dlR_[(size_t) (i0 % size)]) * fr;
        fbLpL_ += (l - fbLpL_) * fbCoef_;
        fbLpR_ += (r - fbLpR_) * fbCoef_;
        dlL_[(size_t) dlW_] = left[i] + fbLpL_ * fb;
        dlR_[(size_t) dlW_] = right[i] + fbLpR_ * fb;
        left[i]  += l * mix;
        right[i] += r * mix;
        dlW_ = (dlW_ + 1) % size;
    }
}

} // namespace fw::dsp

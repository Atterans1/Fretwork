#include "dsp/GuitarEngine.h"

#include <algorithm>
#include <cmath>

namespace fw::dsp {

namespace {
    constexpr double kPi = 3.14159265358979323846;

    inline float clampf (float v, float lo, float hi) noexcept
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    inline float dbToLin (float db) noexcept
    {
        return (float) std::pow (10.0, (double) db * 0.05);
    }

    // Equal-power pan for a normalised position in [0, 1].
    inline void equalPower (float pos, float& l, float& r) noexcept
    {
        const double a = clampf (pos, 0.f, 1.f) * 0.5 * kPi;
        l = (float) std::cos (a);
        r = (float) std::sin (a);
    }
}

//==============================================================================
void GuitarEngine::prepare (double sampleRate, int maxBlockSize)
{
    fs_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    const int maxBlock = maxBlockSize > 0 ? maxBlockSize : 512;
    deadFadeCoef_ = (float) (1.0 - std::exp (-1.0 / (0.012 * fs_)));
    deadOnly_ = false; sinceLive_ = 1000000000LL; sinceDead_ = 0; bodyFade_ = 1.f;

    for (auto& s : strings_) s.prepare (fs_);

    pickup_.prepare (fs_);
    body_.prepare   (fs_);
    noise_.prepare  (fs_);
    amp_.prepare    (fs_, maxBlock);
    out_.prepare    (fs_, maxBlock);

    debug_.assign (kNumStrings, std::vector<float> ((std::size_t) maxBlock, 0.f));
    bridgeSum_.assign ((std::size_t) maxBlock, 0.f);
    bridgeSide_.assign((std::size_t) maxBlock, 0.f);
    sideLp_ = 0.f;
    sideCoef_ = (float) (1.0 - std::exp (-6.283185307179586 * 500.0 / fs_));
    piezo_.assign     ((std::size_t) maxBlock, 0.f);
    mic_.assign       ((std::size_t) maxBlock, 0.f);
    el_.assign        ((std::size_t) maxBlock, 0.f);
    er_.assign        ((std::size_t) maxBlock, 0.f);

    for (auto& sm : { &sSympathetic_, &sWidth_, &sTone_, &sDrive_,
                      &sRoom_, &sMicBlend_, &sBodyRes_, &sTopDamp_, &sOutput_ })
        sm->setCoefficient (fs_, 0.02);

    for (int s = 0; s < kNumStrings; ++s)
    {
        panBase_[s] = (float) s / 5.f;
        bendNow_[s] = bendTarget_[s] = bendStep_[s] = 0.f;
    }

    globalBend_ = 0.f;
    modWheel_ = 0.f;
    lfoPhase_ = 0.0;
    prevBridge_.fill (0.f);
    prevBridgeSum_ = 0.f;
    debugCount_ = 0;

    prepared_ = true;
    refreshModel();
}

void GuitarEngine::reset()
{
    for (auto& s : strings_) s.reset();
    pickup_.reset();
    body_.reset();
    noise_.reset();
    amp_.reset();
    out_.reset();
    prevBridge_.fill (0.f);
    prevBridgeSum_ = 0.f;
    debugCount_ = 0;
}

void GuitarEngine::updatePan()
{
    const float w = clampf (sWidth_.value, 0.f, 1.f);

    for (int s = 0; s < kNumStrings; ++s)
        // Spread around the centre: width 0 is mono, width 1 is the full arc.
        // Scaling the raw 0..1 position collapsed everything hard left at 0.
        equalPower (0.5f + (panBase_[s] - 0.5f) * w, panL_[(std::size_t) s], panR_[(std::size_t) s]);
}

void GuitarEngine::refreshModel()
{
    for (int s = 0; s < kNumStrings; ++s)
        strings_[(std::size_t) s].setModel (p_.strings[(std::size_t) s]);

    const int bi = (int) clampf ((float) p_.body, 0.f, 3.f);
    const BodyTable& bt = kBodyTables[bi];

    acousticLevel_ = dbToLin (bt.mixDb);
    // The magnetic pickup sums every string with little body loss, so electric
    // bodies ran ~3 dB hotter than the acoustics at the same settings and a
    // hard six-string strum clipped. 0.7 (-3 dB) matches their loudness.
    electricLevel_ = (bi == (int) BodyKind::SolidElectric
                   || bi == (int) BodyKind::SemiHollow) ? 0.7f : 0.25f;

    PickupParams pp {};
    pp.type     = p_.pickupType;
    pp.sel      = p_.pickupSel;
    pp.height01 = clampf (p_.pickupHeight, 0.f, 100.f) * 0.01f;
    pp.tone01   = clampf (p_.pickupTone,   0.f, 100.f) * 0.01f;
    pp.outDb    = p_.pickupOut;
    pickup_.setParams (pp);

    body_.setBody (bi, clampf (p_.bodySize, 0.05f, 4.f),
                        clampf (p_.bodyRes, 0.f, 400.f) * 0.01f,
                        clampf (p_.topDamp, 0.f, 100.f) * 0.01f);

    const float bendSec = clampf (p_.bendTime, 1.f, 4000.f) * 0.001f;
    const float bendAmt = clampf (p_.bendAmount, 0.f, 24.f);

    for (int s = 0; s < kNumStrings; ++s)
        bendStep_[(std::size_t) s] = bendAmt / (float) (bendSec * fs_);
}

void GuitarEngine::setParams (const EngineParams& p)
{
    p_ = p;

    AmpFxParams ap;
    ap.ampModel     = p_.ampModel;
    ap.gain01       = clampf (p_.ampGain, 0.f, 100.f) * 0.01f;
    ap.bass01       = clampf (p_.ampBass, 0.f, 100.f) * 0.01f;
    ap.mid01        = clampf (p_.ampMid, 0.f, 100.f) * 0.01f;
    ap.treble01     = clampf (p_.ampTreble, 0.f, 100.f) * 0.01f;
    ap.levelDb      = clampf (p_.ampLevel, -24.f, 12.f);
    ap.cab          = p_.cab;
    ap.chorusMix01  = clampf (p_.chorusMix, 0.f, 100.f) * 0.01f;
    ap.chorusRateHz = clampf (p_.chorusRate, 0.05f, 10.f);
    ap.delayMix01   = clampf (p_.delayMix, 0.f, 100.f) * 0.01f;
    ap.delayMs      = clampf (p_.delayTime, 1.f, 1500.f);
    ap.feedback01   = clampf (p_.delayFeedback, 0.f, 90.f) * 0.01f;
    amp_.setParams (ap);

    sSympathetic_.target = clampf (p_.sympathetic, 0.f, 100.f) * 0.01f;
    sWidth_.target      = clampf (p_.width, 0.f, 100.f) * 0.01f;
    sTone_.target       = clampf (p_.tone, 20.f, (float) fs_ * 0.49f);
    sDrive_.target      = clampf (p_.drive, 0.f, 100.f) * 0.01f;
    sRoom_.target       = clampf (p_.room, 0.f, 100.f) * 0.01f;
    sMicBlend_.target   = clampf (p_.micBlend, 0.f, 100.f) * 0.01f;
    sBodyRes_.target    = clampf (p_.bodyRes, 0.f, 400.f) * 0.01f;
    sTopDamp_.target    = clampf (p_.topDamp, 0.f, 100.f) * 0.01f;
    sOutput_.target     = p_.output;

    outParams_.width01 = sWidth_.target;
    outParams_.toneHz  = sTone_.target;
    outParams_.drive01 = sDrive_.target;
    outParams_.room01  = sRoom_.target;
    outParams_.outDb   = sOutput_.target;
    outParams_.power   = p_.power;

    if (! prepared_) prepare (fs_, (int) debug_.empty() ? 512 : (int) debug_[0].size());
    else               refreshModel();

    out_.setParams (outParams_);
    updatePan();
}

void GuitarEngine::handle (const StringEvent& e)
{
    const int s = (int) clampf ((float) e.string, 0.f, (float) (kNumStrings - 1));

    switch (e.type)
    {
        case EventType::Pluck:
            if (e.art.damp == Damping::Dead)
            {
                if (sinceLive_ > (long long) (2.0 * fs_)) deadOnly_ = true;
                sinceDead_ = 0;
            }
            else
            {
                deadOnly_ = false;
                sinceLive_ = 0;
            }
            strings_[(std::size_t) s].pluck (e, s);
            // Bend transition: the note is picked Bend Amount below and bent
            // up INTO the played pitch, so it always ends in tune.
            bendNow_[(std::size_t) s]    = (e.art.trans == Transition::Bend)
                ? -clampf (p_.bendAmount, 0.f, 24.f) : 0.f;
            bendTarget_[(std::size_t) s] = 0.f;
            break;

        case EventType::Legato:
            strings_[(std::size_t) s].legato (e);
            bendTarget_[(std::size_t) s] = 0.f;
            break;

        case EventType::Release:
            strings_[(std::size_t) s].release (e.releaseSec);
            bendTarget_[(std::size_t) s] = 0.f;
            break;

        case EventType::StrokeNoise:
            noise_.stroke (e.dir, e.durSec, e.velocity, panL_.data(), panR_.data());
            break;

        case EventType::Squeak:
            noise_.squeak (s, e.dir, e.durSec, e.velocity, panL_[(std::size_t) s], panR_[(std::size_t) s]);
            break;
    }
}

void GuitarEngine::setPitchBend (float semis)
{
    // Wheel Range goes up to 12 semitones each way.
    globalBend_ = clampf (semis, -12.f, 12.f);
}

void GuitarEngine::setModWheel (float value01)
{
    modWheel_ = clampf (value01, 0.f, 1.f);
}

void GuitarEngine::clickAt (int sampleOffset, bool accent)
{
    pendingClickOffset_ = sampleOffset;
    pendingClickAccent_ = accent;
    hasPendingClick_ = true;
}

const float* GuitarEngine::debugStringOutput (int stringIndex) const noexcept
{
    if (stringIndex < 0 || stringIndex >= kNumStrings) return nullptr;
    return debug_[(std::size_t) stringIndex].data();
}

int GuitarEngine::debugStringOutputSize() const noexcept
{
    return debugCount_;
}

//==============================================================================
void GuitarEngine::render (float* left, float* right, int n)
{
    if (left == nullptr || right == nullptr || n <= 0) return;

    if (! prepared_) prepare (fs_, n);

    const int maxBlock = (int) bridgeSum_.size();
    if (n > maxBlock) n = maxBlock;

    if (hasPendingClick_)
    {
        out_.clickAt (pendingClickOffset_, pendingClickAccent_);
        hasPendingClick_ = false;
    }

    const bool on = p_.power != 0;

    // Vibrato LFO, 0..1 phase.
    const double lfoInc = clampf (p_.vibRate, 0.f, 40.f) * (1.0 / fs_);
    const float  vibCents01 = modWheel_ * (clampf (p_.vibDepth, 0.f, 200.f) * 0.01f);

    if (! on)
    {
        for (int i = 0; i < n; ++i) { left[i] = 0.f; right[i] = 0.f; }
        debugCount_ = n;
        for (int s = 0; s < kNumStrings; ++s)
            std::fill (debug_[(std::size_t) s].begin(), debug_[(std::size_t) s].begin() + n, 0.f);
        return;
    }

    for (int i = 0; i < n; ++i)
    {
        sSympathetic_.step();
        sWidth_.step();
        sTone_.step();
        sDrive_.step();
        sRoom_.step();
        sMicBlend_.step();
        sBodyRes_.step();
        sTopDamp_.step();
        sOutput_.step();

        lfoPhase_ += lfoInc;
        if (lfoPhase_ >= 1.0) lfoPhase_ -= std::floor (lfoPhase_);

        const float lfo = (float) std::sin (2.0 * kPi * lfoPhase_);
        const float vibCents = vibCents01 * lfo;

        updatePan();

        // Sympathetic coupling through the bridge, kept passive: each string's
        // loop input becomes (1 - c) * own + c * mean(other strings). Every row
        // of that mix sums to at most the loop gain (< 1), so no setting can
        // self-oscillate. Adding c * mean(all) on top of the own signal did,
        // and at full Sympathetic the output ran away to inf/NaN.
        // The mix is applied once per loop period, so c drains c of the
        // string's energy per period: 0.004 at full Sympathetic is ~-6 dB/s
        // extra decay on E3, ~-2 dB/s at the default 30%.
        const float couple = sSympathetic_.value * 0.004f;

        float el = 0.f, er = 0.f, bridgeSum = 0.f, mixSum = 0.f, side = 0.f;

        for (int s = 0; s < kNumStrings; ++s)
        {
            // Bend ramp toward its target at the configured rate.
            float& bn = bendNow_[(std::size_t) s];
            const float bt = bendTarget_[(std::size_t) s];
            const float st = bendStep_[(std::size_t) s];

            if (bn < bt) { bn = std::min (bt, bn + st); }
            else if (bn > bt) { bn = std::max (bt, bn - st); }

            strings_[(std::size_t) s].setBendSemis (bn + globalBend_);
            // The LFO-modulated offset, not the constant depth: passing the
            // depth produced a fixed detune with no wobble at all.
            strings_[(std::size_t) s].setVibrato (vibCents);

            const float others = (prevBridgeSum_ - prevBridge_[(std::size_t) s]) / (float) (kNumStrings - 1);
            const float b = strings_[(std::size_t) s].tick (couple * others, couple);
            prevBridge_[(std::size_t) s] = b;

            mixSum += b;
            bridgeSum += b;
            side += b * (panL_[(std::size_t) s] - panR_[(std::size_t) s]);

            debug_[(std::size_t) s][(std::size_t) i] = b;

            const float v = pickup_.processString (s, b, strings_[(std::size_t) s].periodSamples());
            el += v * panL_[(std::size_t) s];
            er += v * panR_[(std::size_t) s];
        }

        prevBridgeSum_ = mixSum;

        el_[(std::size_t) i] = el * electricLevel_;
        er_[(std::size_t) i] = er * electricLevel_;
        bridgeSum_[(std::size_t) i] = bridgeSum;
        bridgeSide_[(std::size_t) i] = side;
    }

    // Body modes are a parallel bank, so they work on the summed block.
    body_.setBody ((int) clampf ((float) p_.body, 0.f, 3.f),
                       clampf (p_.bodySize, 0.05f, 4.f),
                       sBodyRes_.value,
                       sTopDamp_.value);
    body_.process (bridgeSum_.data(), piezo_.data(), mic_.data(), n);

    const float mb = clampf (sMicBlend_.value, 0.f, 1.f);

    for (int i = 0; i < n; ++i)
    {
        // Dead-note body fade: hold 60 ms (the thump), then ~12 ms time constant.
        ++sinceLive_; ++sinceDead_;
        const bool fading = deadOnly_ && sinceDead_ > (long long) (0.060 * fs_);
        bodyFade_ += ((fading ? 0.f : 1.f) - bodyFade_) * (fading ? deadFadeCoef_ : 0.01f);

        const float acoustic = ((1.f - mb) * 0.6f * piezo_[(std::size_t) i]
                              + mb * mic_[(std::size_t) i]) * acousticLevel_ * bodyFade_;

        // A recorded acoustic is stereo: each string sits at its own position, so
        // the high part of the string difference goes L-R (Width scales it). The
        // body stays mono underneath, so the sum is unchanged.
        const float d = bridgeSide_[(std::size_t) i];
        sideLp_ += (d - sideLp_) * sideCoef_;
        const float spread = (d - sideLp_) * 0.9f * sWidth_.value * acousticLevel_ * (1.f - mb * 0.3f);

        left[i]  = el_[(std::size_t) i] + acoustic + spread;
        right[i] = er_[(std::size_t) i] + acoustic - spread;
    }

    // Pick scrape, string ticks and squeaks join the instrument before the amp.
    noise_.render (left, right, n);

    // Amp, cabinet, chorus and delay sit between the instrument and the master
    // stage; bypassed (untouched) with the amp Off and both mixes at 0.
    amp_.process (left, right, n);

    outParams_.width01 = sWidth_.value;
    outParams_.toneHz  = sTone_.value;
    outParams_.drive01 = sDrive_.value;
    outParams_.room01  = sRoom_.value;
    outParams_.outDb   = sOutput_.value;
    outParams_.power   = p_.power;
    out_.setParams (outParams_);

    out_.process (left, right, n);

    debugCount_ = n;
}

} // namespace fw::dsp

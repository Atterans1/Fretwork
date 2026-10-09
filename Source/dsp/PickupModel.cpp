#include "dsp/PickupModel.h"

#include <algorithm>
#include <cmath>

namespace fw::dsp {

void PickupModel::prepare (double sampleRate)
{
    fs_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    dirty_ = true;
    reset();
}

void PickupModel::reset() noexcept
{
    for (auto& s : strings_)
    {
        s.hist.fill (0.f);
        s.write = 0;
        s.tone.reset();
    }
}

void PickupModel::setParams (const PickupParams& p)
{
    if (p.type != params_.type || p.sel != params_.sel)
        dirty_ = true;
    else if (p.tone01 != params_.tone01 || p.outDb != params_.outDb
             || p.height01 != params_.height01)
        dirty_ = true;

    params_ = p;
}

void PickupModel::rebuildTaps() noexcept
{
    numTaps_ = 0;

    const int type = (params_.type < 0) ? 0 : (params_.type > 2 ? 2 : params_.type);
    const int sel  = (params_.sel  < 0) ? 0 : (params_.sel  > 3 ? 3 : params_.sel);

    const bool  humbucker = type == (int) PickupType::Humbucker;
    const float spread    = humbucker ? kHumbuckerSpread : 0.f;
    const float coilGain  = humbucker ? 0.5f : 1.f;

    auto addPickup = [this, humbucker, spread, coilGain] (float x)
    {
        if (humbucker)
        {
            if (numTaps_ + 2 > kMaxTaps) return;

            tapFrac_[(std::size_t) numTaps_] = x;
            tapGain_[(std::size_t) numTaps_] = coilGain;
            ++numTaps_;

            tapFrac_[(std::size_t) numTaps_] = x + spread;
            tapGain_[(std::size_t) numTaps_] = coilGain;
            ++numTaps_;
            return;
        }

        if (numTaps_ + 1 > kMaxTaps) return;

        tapFrac_[(std::size_t) numTaps_] = x;
        tapGain_[(std::size_t) numTaps_] = 1.f;
        ++numTaps_;
    };

    switch ((PickupSel) sel)
    {
        case PickupSel::Bridge:    addPickup (kPickupPosBridge); break;
        case PickupSel::Middle:    addPickup (kPickupPosMiddle); break;
        case PickupSel::Neck:      addPickup (kPickupPosNeck);   break;
        case PickupSel::BridgeNeck:
            addPickup (kPickupPosBridge);
            addPickup (kPickupPosNeck);
            break;
    }

    const PickupTone& pt = kPickupTone[type];
    const float fc = pt.hz * (0.6f + 0.8f * std::clamp (params_.tone01, 0.f, 1.f));

    for (auto& s : strings_)
        s.tone.setLowPass (fs_, fc, pt.q);

    gain_ = std::pow (10.f, params_.outDb * 0.05f)
          * (0.5f + std::clamp (params_.height01, 0.f, 1.f));
}

float PickupModel::processString (int stringIndex, float bridge, float periodSamples) noexcept
{
    if (stringIndex < 0 || stringIndex >= kNumStrings) return 0.f;

    if (dirty_)
    {
        rebuildTaps();
        dirty_ = false;
    }

    StringState& s = strings_[(std::size_t) stringIndex];

    s.hist[(std::size_t) s.write] = bridge;

    float sum = 0.f;

    for (int t = 0; t < numTaps_; ++t)
    {
        int d = (int) (tapFrac_[(std::size_t) t] * periodSamples + 0.5f);
        if (d < 1) d = 1;
        if (d >= kTapHistory) d = kTapHistory - 1;

        const int idx = s.write - d;
        const int rd = idx < 0 ? idx + kTapHistory : idx;

        sum += tapGain_[(std::size_t) t] * (bridge - s.hist[(std::size_t) rd]);
    }

    s.write = (s.write + 1) % kTapHistory;

    // Height also drives the saturation, so a closer pickup compresses more.
    const float h = std::clamp (params_.height01, 0.f, 1.f);
    float v = s.tone.process (sum) * gain_;

    const float drive = 0.6f + 1.8f * h;
    v = std::tanh (v * drive) / std::tanh (drive);

    return v;
}

} // namespace fw::dsp

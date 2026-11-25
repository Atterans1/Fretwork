#include "dsp/BodyModel.h"

#include <algorithm>
#include <cmath>

namespace fw::dsp {

void BodyModel::prepare (double sampleRate)
{
    fs_ = sampleRate > 0.0 ? sampleRate : 44100.0;

    piezoHp_.setHighPass (fs_, 40.0, 0.707);
    topDamp_.setLowPass (fs_, 14000.0, 0.707);

    cachedBody_ = -1;
    cachedSize_ = -1.f;
    cachedRes_  = -1.f;
    cachedDamp_ = -1.f;

    reset();
}

void BodyModel::reset() noexcept
{
    for (auto& m : modes_) m.reset();
    piezoHp_.reset();
    topDamp_.reset();
}

void BodyModel::rebuild (int bodyIndex, float sizeScale, float resScale)
{
    const int bi = (bodyIndex < 0) ? 0 : (bodyIndex > 3 ? 3 : bodyIndex);
    const BodyTable& table = kBodyTables[bi];

    const float size = sizeScale > 0.05f ? sizeScale : 1.f;
    const float res  = resScale > 0.f ? resScale : 1.f;

    count_ = std::min (table.count, kMaxBodyModes);

    for (int i = 0; i < count_; ++i)
    {
        const BodyMode& m = table.modes[i];
        const float hz  = m.hz * size;
        const float q   = std::max (0.5f, m.q * res);

        // Per-mode decay: a bare bandpass rings out in t60 = 2.2*Q/hz, which
        // starves the upper modes (about 4 ms at 3 kHz, where a real top still
        // rings 40-80 ms). Floor the decay time and re-derive Q from it; low
        // modes keep their table Q because their implicit t60 is already longer.
        const float t60      = 2.198f * q / hz;
        const float t60Floor = 0.05f;
        const float qDerived = std::min (t60Floor * hz / 2.198f, 25.0f); // cap: a 68-Q biquad whistles, it does not sound like wood
        const float qEff     = t60 < t60Floor ? qDerived : q;

        modes_[i].setBandPass (fs_, hz, qEff, m.db + table.mixDb);
    }
}

void BodyModel::setBody (int bodyIndex, float sizeScale, float resScale, float topDamp01)
{
    if (bodyIndex != cachedBody_ || sizeScale != cachedSize_ || resScale != cachedRes_)
    {
        rebuild (bodyIndex, sizeScale, resScale);
        cachedBody_ = bodyIndex;
        cachedSize_ = sizeScale;
        cachedRes_  = resScale;
    }

    if (topDamp01 != cachedDamp_)
    {
        const float t = std::clamp (topDamp01, 0.f, 1.f);
        const float fc = 14000.0f + (4000.0f - 14000.0f) * t;
        topDamp_.setLowPass (fs_, fc, 0.707);
        cachedDamp_ = topDamp01;
    }
}

void BodyModel::processSample (float in, float& piezoOut, float& micOut) noexcept
{
    piezoOut = piezoHp_.process (in);

    float mic = 0.f;
    for (int m = 0; m < count_; ++m)
        mic += modes_[(std::size_t) m].process (piezoOut);

    micOut = topDamp_.process (mic);
}

void BodyModel::process (const float* in, float* piezoOut, float* micOut, int n) noexcept
{
    if (in == nullptr || micOut == nullptr || n <= 0) return;

    for (int i = 0; i < n; ++i)
    {
        float p = 0.f, m = 0.f;
        processSample (in[i], p, m);
        if (piezoOut != nullptr) piezoOut[i] = p;
        micOut[i] = m;
    }
}

} // namespace fw::dsp

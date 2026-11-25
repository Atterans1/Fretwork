#include "dsp/PickupModel.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace fw::dsp {

namespace {
    constexpr double kTwoPi = 6.283185307179586;

    // Electrical data of the three pickup families. Inductance and DC
    // resistance are the middle of the published ranges (Lawing, "Pickup
    // Inductance", Vintage Vibe Tech Tips 2017: Strat single coil 1.8-2.5 H and
    // 5.5-6.5 kOhm, PAF humbucker 2.8-4.6 H and 6.5-12 kOhm, P-90 4.9-6.8 H).
    // Single coils go into 250k pots, humbuckers and P90s into 500k pots; both
    // see a 1 MOhm amp input and ~500 pF of cable on top of the winding's own
    // ~100 pF.
    struct CoilData
    {
        double henry, ohm, selfPf, potOhm;
        float  level;   // relative output at the same string motion (more turns)
    };

    constexpr CoilData kCoil[3] =
    {
        { 2.3, 6000.0, 110.0, 250.0e3, 1.00f },   // Single-Coil
        { 4.0, 8200.0, 140.0, 500.0e3, 1.45f },   // Humbucker (series pair)
        { 5.8, 8000.0, 120.0, 500.0e3, 1.35f },   // P90
    };

    constexpr double kCablePf  = 500.0;
    constexpr double kAmpInOhm = 1.0e6;

    // Tone pot (same value as the volume pot) in series with a 22 nF cap. Above
    // ~1 kHz the cap is close to a short, so the branch is the pot resistance,
    // which loads the coil in parallel with the volume pot and the amp.
    constexpr double kToneFloorOhm = 3000.0;
}

void PickupModel::prepare (double sampleRate)
{
    fs_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    dirty_ = true;
    reset();
    rebuild();
}

void PickupModel::reset() noexcept
{
    for (auto& s : strings_)
    {
        s.hist.fill (0.f);
        s.write = 0;
        s.rlc.reset();
    }
}

void PickupModel::setParams (const PickupParams& p)
{
    PickupParams q = p;
    q.type = std::clamp (q.type, 0, 2);
    q.sel  = std::clamp (q.sel, 0, 3);

    // Rebuild only on a real change; the engine pushes parameters every refresh.
    if (q.type != params_.type || q.sel != params_.sel || q.height01 != params_.height01
        || q.tone01 != params_.tone01 || q.outDb != params_.outDb)
    {
        params_ = q;
        dirty_ = true;
    }
}

void PickupModel::rebuild() noexcept
{
    const int type = std::clamp (params_.type, 0, 2);
    const int sel  = std::clamp (params_.sel, 0, 3);
    const bool humbucker = type == (int) PickupType::Humbucker;

    // ---- positions -------------------------------------------------------
    numTaps_ = 0;
    const auto addPickup = [this, humbucker] (float x)
    {
        if (humbucker)
        {
            if (numTaps_ + 2 > kMaxTaps) return;
            tapFrac_[(std::size_t) numTaps_] = x;                    tapGain_[(std::size_t) numTaps_++] = 0.5f;
            tapFrac_[(std::size_t) numTaps_] = x + kHumbuckerSpread; tapGain_[(std::size_t) numTaps_++] = 0.5f;
            return;
        }
        if (numTaps_ + 1 > kMaxTaps) return;
        tapFrac_[(std::size_t) numTaps_] = x;
        tapGain_[(std::size_t) numTaps_++] = 1.f;
    };

    switch ((PickupSel) sel)
    {
        case PickupSel::Bridge:     addPickup (kPickupPosBridge); break;
        case PickupSel::Middle:     addPickup (kPickupPosMiddle); break;
        case PickupSel::Neck:       addPickup (kPickupPosNeck);   break;
        case PickupSel::BridgeNeck: addPickup (kPickupPosBridge); addPickup (kPickupPosNeck); break;
    }

    // Two pickups in parallel each see the other as part of their load, which
    // halves the level; the gains above already sum each pickup to unity.
    if (sel == (int) PickupSel::BridgeNeck)
        for (int t = 0; t < numTaps_; ++t) tapGain_[(std::size_t) t] *= 0.5f;

    // ---- loaded coil -----------------------------------------------------
    const CoilData& cd = kCoil[type];
    const double tone   = std::clamp ((double) params_.tone01, 0.0, 1.0);
    const double toneR  = kToneFloorOhm + cd.potOhm * tone;
    const double par    = 1.0 / cd.potOhm + 1.0 / kAmpInOhm + 1.0 / toneR;
    // Two pickups on one switch position share the load and add their windings'
    // capacitance; their inductances sit in parallel.
    const double nCoils = sel == (int) PickupSel::BridgeNeck ? 2.0 : 1.0;
    const double L  = cd.henry / nCoils;
    const double R  = cd.ohm / nCoils;
    const double C  = (cd.selfPf * nCoils + kCablePf) * 1.0e-12;
    const double Rl = 1.0 / par;

    // H(s) = (1 / (1 + R/Rl)) / ((LC/k) s^2 + ((RC + L/Rl)/k) s + 1),  k = 1 + R/Rl
    const double k  = 1.0 + R / Rl;
    const double w0 = std::sqrt (k / (L * C));
    const double q  = w0 * (L * C) / (R * C + L / Rl);

    resHz_ = w0 / kTwoPi;
    resQ_  = q;

    // The bilinear filter must stay below Nyquist; above it the coil is flat
    // anyway as far as the host can hear.
    const double fc = std::min (resHz_, 0.45 * fs_);
    for (auto& s : strings_)
        s.rlc.setLowPass (fs_, fc, q);

    const float h = std::clamp (params_.height01, 0.0f, 1.0f);
    gain_ = std::pow (10.0f, params_.outDb * 0.05f) * (0.5f + h) * cd.level / (float) k;

    dirty_ = false;
}

float PickupModel::processString (int stringIndex, float bridge, float periodSamples) noexcept
{
    if (stringIndex < 0 || stringIndex >= kPickupChannels) return 0.f;

    if (dirty_) rebuild();

    StringState& s = strings_[(std::size_t) stringIndex];
    s.hist[(std::size_t) s.write] = bridge;

    float sum = 0.f;
    for (int t = 0; t < numTaps_; ++t)
    {
        // Fractional delay, so the comb's notches sit where the pickup really
        // is on high notes too, where a period is only a few dozen samples.
        const float dly = std::clamp (tapFrac_[(std::size_t) t] * periodSamples, 1.f, (float) (kTapHistory - 2));
        const int   d   = (int) dly;
        const float fr  = dly - (float) d;
        int i0 = s.write - d;     if (i0 < 0) i0 += kTapHistory;
        int i1 = i0 - 1;          if (i1 < 0) i1 += kTapHistory;
        const float past = s.hist[(std::size_t) i0] + fr * (s.hist[(std::size_t) i1] - s.hist[(std::size_t) i0]);
        sum += tapGain_[(std::size_t) t] * (bridge - past);
    }
    s.write = (s.write + 1) % kTapHistory;

    float v = s.rlc.process (sum) * gain_;

    // A closer pickup pulls harder on the string and compresses the attack.
    const float h = std::clamp (params_.height01, 0.0f, 1.0f);
    const float drive = 0.6f + 1.8f * h;
    v = std::tanh (v * drive) / std::tanh (drive);

    return v;
}

} // namespace fw::dsp

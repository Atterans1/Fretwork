#include "dsp/OutputStage.h"

#include <algorithm>
#include <cmath>

namespace fw::dsp {

namespace {
    inline float clampf (float v, float lo, float hi) noexcept
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }
}

void OutputStage::prepare (double sampleRate, int maxBlockSize)
{
    fs_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    (void) maxBlockSize;

    reverbParams_.roomSize = 0.3f;
    reverbParams_.damping  = 0.5f;
    reverbParams_.wetLevel = 1.0f;
    reverbParams_.dryLevel = 1.0f;
    reverbParams_.width    = 1.0f;

    reverb_.setSampleRate (fs_);

    setParams (p_);
    reset();
}

void OutputStage::reset() noexcept
{
    toneState_ = 0.f;
    reverb_.reset();
    clickLen_ = clickPos_ = 0;
    clickOffset_ = -1;
}

void OutputStage::setParams (const OutputParams& p)
{
    p_ = p;

    const float nyquist = (float) (fs_ * 0.49);
    const float hz = clampf (p_.toneHz, 20.f, nyquist);
    // One-pole low-pass: y += (x - y) * (1 - exp(-2*pi*f/fs)). Using the bare
    // exponential inverted the knob (higher Tone was darker).
    toneCoef_ = (float) (1.0 - std::exp (-2.0 * 3.14159265358979323846 * hz / fs_));

    const float room = clampf (p_.room01, 0.f, 1.f);

    // Room types: size, damping and brightness from a small measured-style set.
    // Booth: tiny and dead. Room: mid room. Hall: big and long. Plate: bright
    // and dense with almost no early-reflection colour.
    switch (std::clamp (p_.roomType, 0, 3))
    {
        case 0:  reverbParams_.roomSize = 0.10f + 0.15f * room; reverbParams_.damping = 0.85f; break;   // Booth
        case 1:  reverbParams_.roomSize = 0.25f + 0.30f * room; reverbParams_.damping = 0.55f; break;   // Room
        case 2:  reverbParams_.roomSize = 0.60f + 0.35f * room; reverbParams_.damping = 0.35f; break;   // Hall
        default: reverbParams_.roomSize = 0.50f + 0.30f * room; reverbParams_.damping = 0.15f; break;   // Plate
    }
    float wetScale = 1.0f;
    switch (std::clamp (p_.roomType, 0, 3))
    {
        case 0: wetScale = 0.6f; break;    // Booth: dry-ish
        case 1: wetScale = 1.0f; break;    // Room
        case 2: wetScale = 1.6f; break;    // Hall: big and washy
        default: wetScale = 1.3f; break;   // Plate
    }
    reverbParams_.wetLevel = 0.60f * room * wetScale;
    reverbParams_.dryLevel = 1.0f - 0.30f * room;
    // Width also narrows the room: a full-width reverb made "Width 0" stereo.
    reverbParams_.width    = clampf (p_.width01, 0.f, 1.f);

    reverb_.setParameters (reverbParams_);
}

void OutputStage::renderClick (bool accent) noexcept
{
    const double hz = accent ? 2400.0 : 1600.0;
    int n = (int) (0.005 * fs_);
    if (n < 2) n = 2;
    if (n > kMaxClick) n = kMaxClick;

    const double inc = 2.0 * 3.14159265358979323846 * hz / fs_;
    const float amp = (float) std::pow (10.0, -18.0 / 20.0);

    // Fade both ends so the blip does not click itself.
    const int edge = std::max (1, n / 16);

    for (int i = 0; i < n; ++i)
    {
        const double env = i < edge    ? (double) i / edge
                         : i > n - edge ? (double) (n - i) / edge
                                        : 1.0;

        click_[(std::size_t) i] = (float) (std::sin (inc * i) * env * amp);
    }

    clickLen_ = n;
    clickPos_ = 0;
}

void OutputStage::clickAt (int sampleOffset, bool accent)
{
    clickOffset_ = sampleOffset;
    clickAccent_ = accent;
}

// Drive loudness compensation exponent: output ~ tanh(pre x) / pre^k.
static constexpr float kDriveComp = 0.75f;

void OutputStage::process (float* left, float* right, int n) noexcept
{
    if (left == nullptr || right == nullptr || n <= 0) return;

    // Headroom: a six-string strum sums six strings in phase at the attack.
    // -4 dB keeps a full strum at the default Output below about -6 dBFS.
    constexpr float kHeadroom = 0.631f;
    const float gain = (float) std::pow (10.0, (double) p_.outDb * 0.05) * kHeadroom;

    // Drive is saturation, not volume: tanh with the pre-gain, then level
    // compensation so turning it up adds harmonics and compression at about
    // the same loudness. (It used to be a plain x7 gain into the hard clamp.)
    const float drive = clampf (p_.drive01, 0.f, 1.f);
    const float pre   = 1.0f + 6.0f * drive;
    const float comp  = 1.0f / std::pow (pre, kDriveComp);
    const auto  shape = [drive, pre, comp] (float x) noexcept
    {
        return drive > 0.f ? std::tanh (x * pre) * comp : x;
    };

    for (int i = 0; i < n; ++i)
    {
        if (clickOffset_ == i)
        {
            renderClick (clickAccent_);
            clickOffset_ = -1;
        }

        const float l = left[i];
        const float r = right[i];

        // Tone is a shared one-pole lowpass; the stereo difference is kept.
        const float mono = (l + r) * 0.5f;
        toneState_ += (mono - toneState_) * toneCoef_;

        left[i]  = shape (toneState_ + (l - mono));
        right[i] = shape (toneState_ + (r - mono));
    }

    const float room = clampf (p_.room01, 0.f, 1.f);
    if (room > 0.f)
        reverb_.processStereo (left, right, (int) n);

    for (int i = 0; i < n; ++i)
    {
        float l = left[i] * gain;
        float r = right[i] * gain;

        // Gentle soft clip, then a hard guard so nothing escapes.
        l = l / (1.0f + std::fabs (l) * 0.1f);
        r = r / (1.0f + std::fabs (r) * 0.1f);

        if (clickPos_ < clickLen_)
        {
            const float c = click_[(std::size_t) clickPos_++];
            l += c;
            r += c;
        }

        left[i]  = std::clamp (l, -0.98f, 0.98f);
        right[i] = std::clamp (r, -0.98f, 0.98f);
    }
}

} // namespace fw::dsp

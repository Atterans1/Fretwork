#pragma once
#include <cmath>

namespace fw::dsp {

// Direct-form-I biquad whose coefficient updates allocate nothing.
//
// juce::dsp::IIR::Coefficients owns a std::vector, so refreshing one on the
// audio thread would allocate. Parameter smoothing happens once per block in
// the engine, and this class keeps every coefficient in plain members.
class Biquad
{
public:
    void reset() noexcept { x1_ = x2_ = y1_ = y2_ = 0.f; }

    // RBJ cookbook. q is the quality factor; gainDb is only used by bandPass.
    void setLowPass  (double fs, double hz, double q) noexcept { set (fs, hz, q, 0.0, Kind::LowPass); }
    void setHighPass (double fs, double hz, double q) noexcept { set (fs, hz, q, 0.0, Kind::HighPass); }
    void setBandPass (double fs, double hz, double q, double gainDb = 0.0) noexcept { set (fs, hz, q, gainDb, Kind::BandPass); }

    float process (float x) noexcept
    {
        const float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_;
        x1_ = x;
        y2_ = y1_;
        y1_ = y;
        return y;
    }

    void setPassthrough() noexcept { b0_ = 1.f; b1_ = b2_ = a1_ = a2_ = 0.f; }

private:
    enum class Kind { LowPass, HighPass, BandPass };

    void set (double fs, double hz, double q, double gainDb, Kind kind) noexcept
    {
        if (! (fs > 0.0) || ! (hz > 0.0) || ! (q > 0.0))
        {
            setPassthrough();
            return;
        }

        // Keep the cutoff strictly inside the usable band.
        const double nyq = 0.49 * fs;
        const double f = hz > nyq ? nyq : hz;

        const double w0 = 6.283185307179586 * f / fs;
        const double cw = std::cos (w0);
        const double sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);

        double b0 = 1.0, b1 = 0.0, b2 = 0.0;

        switch (kind)
        {
            case Kind::LowPass:
                b0 = (1.0 - cw) * 0.5;
                b1 = 1.0 - cw;
                b2 = b0;
                break;

            case Kind::HighPass:
                b0 = (1.0 + cw) * 0.5;
                b1 = -(1.0 + cw);
                b2 = b0;
                break;

            case Kind::BandPass:
                b0 = alpha;
                b1 = 0.0;
                b2 = -alpha;
                break;
        }

        const double a0 = 1.0 + alpha;
        const double a1 = -2.0 * cw;
        const double a2 = 1.0 - alpha;

        // Constant-peak bandpass carries the mode gain in dB.
        const double gain = kind == Kind::BandPass ? std::pow (10.0, gainDb * 0.05) : 1.0;

        b0_ = (float) (b0 / a0 * gain);
        b1_ = (float) (b1 / a0 * gain);
        b2_ = (float) (b2 / a0 * gain);
        a1_ = (float) (a1 / a0);
        a2_ = (float) (a2 / a0);
    }

    float b0_ = 1.f, b1_ = 0.f, b2_ = 0.f, a1_ = 0.f, a2_ = 0.f;
    float x1_ = 0.f, x2_ = 0.f, y1_ = 0.f, y2_ = 0.f;
};

} // namespace fw::dsp

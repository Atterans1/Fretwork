#include "dsp/GuitarString.h"
#include "dsp/Rng.h"

#include <algorithm>
#include <cmath>

namespace fw::dsp {

namespace {
    constexpr double kPi = 3.14159265358979323846;

    inline double clampd (double v, double lo, double hi) noexcept
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    inline float clampf (float v, float lo, float hi) noexcept
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    inline double midiToHz (double pitch) noexcept
    {
        return 440.0 * std::pow (2.0, (pitch - 69.0) / 12.0);
    }
}

//==============================================================================
void GuitarString::prepare (double sampleRate)
{
    fs_ = sampleRate > 0.0 ? sampleRate : 44100.0;

    baseF0_ = midiToHz (40.0);
    design_ = LoopDesignResult {};
    design_.f0     = baseF0_;
    design_.period = fs_ / baseF0_;
    design_.fracPart = design_.period;
    design_.delay  = design_.period;
    design_.lpC    = 0.0;
    design_.lpG    = 0.99;

    // 5 ms one-pole smoothing on the bend/vibrato offset.
    semiIncCoeff_ = (float) (1.0 - std::exp (-1.0 / (fs_ * 0.005)));

    prepared_ = true;
    reset();
}

void GuitarString::reset()
{
    line_.fill (0.f);
    writePos_ = 0.0;

    lpPrev_ = 0.f;
    apX_.fill (0.f);
    apY_.fill (0.f);

    releaseGain_ = 1.f;
    releaseTarget_ = 1.f;
    releaseInc_ = 0.f;

    semiSmoothed_ = 0.f;
    glideFrom_ = 0.f;
    glideTo_ = 0.f;
    glidePos_ = 0.f;
    glideStep_ = 0.f;

    clickLen_ = clickPos_ = 0;
    thumpLen_ = thumpPos_ = 0;
    fretLen_ = fretPos_ = 0;

    injectPos_ = 0;
    injectLen_ = 0;
}

void GuitarString::setModel (const StringModelParams& p)
{
    model_ = p;
}

//==============================================================================
// Fractional delay read with linear interpolation.
//
// A four-point Catmull-Rom read was tried here and measured worse: its extra
// phase shifted high pitches up by ~11 cents against linear's ~3.5, so the
// two-tap form is what stays.
float GuitarString::readInterp (double pos) const noexcept
{
    double p = std::fmod (pos, (double) kMaxStringDelay);
    if (p < 0.0) p += (double) kMaxStringDelay;

    const int i0 = (int) p;
    const double f = p - (double) i0;
    const int i1 = (i0 + 1) % kMaxStringDelay;

    const float a = line_[(std::size_t) i0];
    const float b = line_[(std::size_t) i1];
    return a + (float) f * (b - a);
}

void GuitarString::writeAt (double pos, float v) noexcept
{
    double p = std::fmod (pos, (double) kMaxStringDelay);
    if (p < 0.0) p += (double) kMaxStringDelay;
    line_[(std::size_t) ((int) p)] = v;
}

//==============================================================================
void GuitarString::startBurst (std::array<float, kMaxBurst>& buf,
                               int& len, int& pos,
                               int samples, float amp,
                               bool highpass, double centreHz)
{
    int n = samples;
    if (n < 1) n = 1;
    if (n > kMaxBurst) n = kMaxBurst;

    Rng rng (seed_ + 0x51ed2701u);
    for (int i = 0; i < n; ++i)
        buf[(std::size_t) i] = rng.bipolar();

    if (highpass)
    {
        // Pick / fret contact: band-limited between ~250 Hz and centreHz.
        // Every corner is specified in Hz so the transient sounds the same at
        // any sample rate; raw per-sample coefficients let it reach Nyquist.
        const double hpA = std::exp (-2.0 * kPi * 250.0 / fs_);
        const double lpA = std::exp (-2.0 * kPi * clampd (centreHz, 500.0, 0.45 * fs_) / fs_);
        double hpY = 0.0, xPrev = 0.0, lp1 = 0.0, lp2 = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double x = buf[(std::size_t) i];
            hpY = hpA * (hpY + x - xPrev);
            xPrev = x;
            lp1 += (1.0 - lpA) * (hpY - lp1);
            lp2 += (1.0 - lpA) * (lp1 - lp2);
            buf[(std::size_t) i] = (float) lp2;
        }
    }
    else
    {
        const double w = 2.0 * kPi * clampd (centreHz, 20.0, 0.45 * fs_) / fs_;
        // Bandwidth in Hz (~30 Hz) rather than a fixed per-sample radius.
        const double r = std::exp (-kPi * 30.0 / fs_);
        double y1 = 0.0, y2 = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const double x = buf[(std::size_t) i];
            const double y = 2.0 * r * std::cos (w) * y1 - r * r * y2 + (1.0 - r) * x;
            y2 = y1;
            y1 = y;
            buf[(std::size_t) i] = (float) y;
        }
    }

    // Normalise to a fixed RMS so 'amp' means the same thing at every rate and
    // filter setting, then apply a raised-cosine envelope: fast rise, smooth
    // tail, no rectangular edges to splatter energy across the spectrum.
    double e = 0.0;
    for (int i = 0; i < n; ++i) e += (double) buf[(std::size_t) i] * buf[(std::size_t) i];
    // 0.5 RMS matches the level the excitation amplitudes were tuned against.
    const double norm = e > 0.0 ? 0.5 / std::sqrt (e / n) : 0.0;

    const int rise = std::max (1, n / 8);
    for (int i = 0; i < n; ++i)
    {
        const double env = i < rise
            ? 0.5 - 0.5 * std::cos (kPi * (double) i / rise)
            : 0.5 + 0.5 * std::cos (kPi * (double) (i - rise) / std::max (1, n - rise));
        buf[(std::size_t) i] = (float) (buf[(std::size_t) i] * norm * env * amp);
    }

    len = n;
    pos = 0;
}

float GuitarString::readBurst (std::array<float, kMaxBurst>& buf, int& len, int& pos) noexcept
{
    if (pos >= len) return 0.f;
    return buf[(std::size_t) pos++];
}

//==============================================================================
void GuitarString::design (const StringEvent& e)
{
    baseF0_ = midiToHz ((double) e.pitch);

    harmonicN_ = (e.harmonicN >= 2 && e.harmonicN <= 5) ? e.harmonicN : 0;

    LoopDesignParams lp {};
    lp.f0             = baseF0_;
    lp.fs             = fs_;
    lp.string         = stringIdx_;
    lp.material       = model_.material;
    lp.sustain01      = (double) clampf (model_.sustain,   0.f, 200.f) * 0.01;
    lp.age01          = (double) clampf (model_.age,       0.f, 100.f) * 0.01;
    lp.bodyLossScale  = model_.bodyLossScale;
    lp.stiffness01    = (double) clampf (model_.stiffness, 0.f, 400.f) * 0.01;
    lp.pickPos01      = (double) clampf (model_.pickPos,   0.f,  50.f) * 0.01;
    lp.harmonicActive = harmonicN_ >= 2;
    lp.harmonicN      = harmonicN_;

    designLoop (lp, (int) e.art.damp, design_);

    chainDelay_ = design_.delay - design_.fracPart;
    if (chainDelay_ < 0.0) chainDelay_ = 0.0;

    // A new note starts with clean filter state so the burst is not coloured
    // by whatever the previous note left ringing.
    lpPrev_ = 0.f;
    apX_.fill (0.f);
    apY_.fill (0.f);
}

void GuitarString::inject (float scale)
{
    injectLen_   = excite_.length();
    injectPos_   = 0;
    injectScale_ = scale;
}

void GuitarString::pluck (const StringEvent& e, int stringIdx)
{
    if (! prepared_) prepare (fs_);

    stringIdx_ = (int) clampd (double (stringIdx), 0.0, 5.0);

    // Advance the per-string seed so successive plucks decorrelate.
    seed_ = seed_ * 1664525u + 1013904223u;

    design (e);

    ExcitationRequest req {};
    req.f0          = baseF0_;
    req.fs          = fs_;
    req.period      = design_.period;
    req.pickPos01   = (double) clampf (model_.pickPos,  0.f,  50.f) * 0.01;
    req.pickHard01  = (double) clampf (model_.pickHard, 0.f, 100.f) * 0.01;
    req.vel         = (double) clampf (e.velocity, 0.f, 1.f);
    req.fretNoise01 = (double) clampf (model_.fretNoise, 0.f, 100.f) * 0.01;
    req.exc         = (int) e.art.exc;
    req.damp        = (int) e.art.damp;
    req.seed        = seed_;

    ExcitationResult info {};
    renderExcitation (req, excite_, info);

    // Superpose rather than replace, so re-plucking a ringing string works.
    inject (1.f);

    const double vel01 = clampd ((double) e.velocity, 0.0, 1.0);
    startBurst (clickBuf_, clickLen_, clickPos_,
                (int) (0.0015 * fs_), info.clickAmp, true, 2500.0 + 3500.0 * vel01);

    if (info.thumpAmp > 0.f)
        startBurst (thumpBuf_, thumpLen_, thumpPos_,
                    (int) (0.025 * fs_), info.thumpAmp, false, baseF0_);

    naturalGain_   = (float) loopGainAt (baseF0_, design_.t60Fund);
    releaseGain_   = naturalGain_;
    releaseTarget_ = releaseGain_;
    lastVel_       = clampf (e.velocity, 0.f, 1.f);
    releaseInc_    = 0.f;

    bendTarget_     = 0.f;
    vibTargetCents_ = 0.f;
    semiSmoothed_   = 0.f;
    glideFrom_ = glideTo_ = glidePos_ = glideStep_ = 0.f;
}

void GuitarString::legato (const StringEvent& e)
{
    if (! prepared_) prepare (fs_);

    const double newF0 = midiToHz ((double) e.pitch);
    const float targetSemi = (float) (12.0 * std::log2 (newF0 / baseF0_));

    const bool slide = e.pitchFrom >= 0.f;
    const double secs = slide ? (double) glideSec_ : 0.003;
    const double steps = secs * fs_;

    glideFrom_ = glidePos_;
    glideTo_   = targetSemi;
    glideStep_ = steps > 0.0 ? (float) ((targetSemi - glideFrom_) / steps) : 0.f;
    if (steps <= 0.0) glidePos_ = targetSemi;

    // A hammer-on is a stronger transient than a pull-off.
    const bool rising = newF0 > baseF0_;
    const float scale = rising ? 0.35f : 0.25f;

    ExcitationRequest req {};
    req.f0          = baseF0_;
    req.fs          = fs_;
    req.period      = design_.period;
    req.pickPos01   = (double) clampf (model_.pickPos,  0.f,  50.f) * 0.01;
    req.pickHard01  = (double) clampf (model_.pickHard, 0.f, 100.f) * 0.01;
    req.vel         = (double) clampf (e.velocity, 0.f, 1.f) * (double) scale;
    req.fretNoise01 = (double) clampf (model_.fretNoise, 0.f, 100.f) * 0.01;
    req.exc         = 1;   // fingers
    req.damp        = (int) e.art.damp;
    req.seed        = seed_ + 0x2545f491u;

    ExcitationResult info {};
    renderExcitation (req, excite_, info);

    inject (1.f);

    if (info.fretNoiseAmp > 0.f)
        startBurst (fretBuf_, fretLen_, fretPos_,
                    (int) (0.008 * fs_), info.fretNoiseAmp, true, 4000.0);
}

void GuitarString::release (float releaseSec)
{
    if (! prepared_) return;

    // Releasing can only damp the string, never make it ring longer than it
    // would on its own; the top of the range means "let it ring".
    releaseTarget_ = releaseSec >= 20.f
        ? naturalGain_
        : std::min (naturalGain_, (float) loopGainAt (baseF0_, (double) releaseSec));

    const int ramp = (int) (0.010 * fs_);
    releaseInc_ = (releaseTarget_ - releaseGain_) / (float) (ramp > 0 ? ramp : 1);

    // Finger-lift squeak scales with how hard the note was played.
    const float amp = 0.006f * lastVel_ * clampf (model_.fretNoise, 0.f, 100.f) * 0.01f;
    if (amp > 0.f)
        startBurst (fretBuf_, fretLen_, fretPos_, (int) (0.008 * fs_), amp, true, 4000.0);
}

//==============================================================================
void GuitarString::setBendSemis (float semis)
{
    bendTarget_ = clampf (semis, -24.f, 24.f);
}

void GuitarString::setVibrato (float cents01)
{
    // Signed: the vibrato LFO swings both ways around the note.
    vibTargetCents_ = clampf (cents01, -1.f, 1.f) * 100.f;
}

float GuitarString::fundamentalHz() const noexcept
{
    const double semi = (double) (semiSmoothed_ + glidePos_);
    return (float) (baseF0_ * std::pow (2.0, semi / 12.0));
}

float GuitarString::periodSamples() const noexcept
{
    const float f = fundamentalHz();
    return f > 0.f ? (float) (fs_ / f) : (float) fs_;
}

//==============================================================================
float GuitarString::tick (float sympatheticIn, float coupling)
{
    if (! prepared_) return 0.f;

    const double semi = (double) (semiSmoothed_ + glidePos_);

    // Bending scales the whole loop period; the filter group delay stays put,
    // so the loop filters are never retuned mid note.
    const double bendScale = std::pow (2.0, -semi / 12.0);

    double dLive = design_.loopPeriod * bendScale - chainDelay_;
    if (dLive < 4.0) dLive = 4.0;

    const float bridge = readInterp (writePos_ - dLive);
    float x = bridge;

    // Natural harmonic: n-tap comb across the fractional delay.
    if (harmonicN_ >= 2)
    {
        const int n = harmonicN_;
        const double p = design_.period;

        float acc = 0.f;
        for (int k = 0; k < n; ++k)
            acc += readInterp (writePos_ - (double) k * p / (double) n);

        x = acc / (float) n;
    }

    // Loop lowpass: y[n] = k*x[n] + c*y[n-1], k = g*(1-c).
    {
        const float k = (float) (design_.lpG * (1.0 - design_.lpC));
        const float y = k * x + (float) design_.lpC * lpPrev_;
        lpPrev_ = y;
        x = y;
    }

    // Dispersion: four identical first-order allpasses. At a == 0 each stage
    // is an exact one-sample delay, and the delay budget assumes they are
    // present, so they always run.
    {
        const float a = (float) design_.dispA;

        for (std::size_t i = 0; i < 4; ++i)
        {
            const float in = x;
            const float outp = a * in + apX_[i];
            apX_[i] = in;
            const float y = outp - a * apY_[i];
            apY_[i] = y;
            x = y;
        }
    }

    // Release damping, ramped so the mute does not click.
    if (releaseInc_ != 0.f)
    {
        releaseGain_ += releaseInc_;

        if ((releaseInc_ > 0.f && releaseGain_ >= releaseTarget_) ||
            (releaseInc_ < 0.f && releaseGain_ <= releaseTarget_))
        {
            releaseGain_ = releaseTarget_;
            releaseInc_ = 0.f;
        }
    }
    x *= releaseGain_;

    float input = x * (1.f - coupling) + sympatheticIn;

    if (injectPos_ < injectLen_)
        input += excite_[injectPos_++] * injectScale_;

    writeAt (writePos_, input);
    writePos_ += 1.0;
    if (writePos_ >= (double) kMaxStringDelay)
        writePos_ -= (double) kMaxStringDelay;

    // Advance the glide ramp.
    if (glideStep_ != 0.f)
    {
        glidePos_ += glideStep_;

        const float lo = glideFrom_ < glideTo_ ? glideFrom_ : glideTo_;
        const float hi = glideFrom_ < glideTo_ ? glideTo_ : glideFrom_;

        if (glidePos_ <= lo)      { glidePos_ = lo; glideStep_ = 0.f; }
        else if (glidePos_ >= hi) { glidePos_ = hi; glideStep_ = 0.f; }
    }

    // Smooth the bend and vibrato offset.
    semiSmoothed_ += (bendTarget_ + vibTargetCents_ * 0.01f - semiSmoothed_) * semiIncCoeff_;

    return bridge
         + readBurst (clickBuf_, clickLen_, clickPos_)
         + readBurst (thumpBuf_, thumpLen_, thumpPos_)
         + readBurst (fretBuf_,  fretLen_,  fretPos_);
}

} // namespace fw::dsp

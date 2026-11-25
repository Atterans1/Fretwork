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
    semiIncCoeff_ = (float) (1.0 - std::exp (-1.0 / (fs_ * 0.025)));   // 25 ms: a wheel bend glides, 5 ms stepped

    prepared_ = true;
    reset();
}

void GuitarString::reset()
{
    line_.fill (0.f);
    writePos_ = 0.0;

    lpPrev_ = 0.f;
    readPrev_ = readPrevH_ = 0.f;
    apX_.fill (0.f);
    apY_.fill (0.f);

    lineH_.fill (0.f);
    lpPrevH_ = 0.f;
    apXH_.fill (0.f);
    apYH_.fill (0.f);
    attackSemi_ = 0.f;

    releaseGain_ = 1.f;
    releaseTarget_ = 1.f;
    releaseInc_ = 0.f;

    semiSmoothed_ = 0.f;
    glideFrom_ = 0.f;
    glideTo_ = 0.f;
    glidePos_ = 0.f;
    glideU_ = 1.f;
    glideDu_ = 0.f;
    glideFretted_ = false;

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
// Fractional delay read: four-point (cubic) Lagrange interpolation.
//
// A two-tap linear read loses high frequencies by an amount that depends on
// the fraction it reads at (none at 0, |cos(w/2)| at 0.5), and inside the loop
// that loss compounds every pass. So each note's upper partials died at a rate
// set by where its delay happened to fall: neighbouring notes on one string
// differed by 2x in how long their treble rang. Cubic Lagrange is maximally
// flat in both gain and phase delay at low frequencies, so its loss is small
// and nearly the same at every fraction, and it keeps the pitch accurate.
// (A Catmull-Rom read, which is not Lagrange, was tried earlier and shifted
// high pitches up by ~11 cents; this one does not, see the tuning test.)
// The loop was tuned by ear with the linear read, though, and its average
// loss is part of that tuning. So after the Lagrange read the loop runs a
// fixed two-tap smoother, (1 - a) x[n] + a x[n-1]: the linear read's own
// filter at one fraction a, the same on every note. Averaged over all
// fractions the linear loss is about f (1 - f) (1 - cos w), mean 1/6, so
// a (1 - a) = 1/6, a = 0.2113: every note now loses what the average note
// lost before, at every frequency, and the smoother's a samples of delay come
// off the read.
namespace
{
    inline float lagrange4 (const float* line, double pos) noexcept
    {
        double p = std::fmod (pos, (double) kMaxStringDelay);
        if (p < 0.0) p += (double) kMaxStringDelay;

        const int i0 = (int) p;
        const float f = (float) (p - (double) i0);
        const int im = i0 == 0 ? kMaxStringDelay - 1 : i0 - 1;
        const int i1 = i0 + 1 >= kMaxStringDelay ? i0 + 1 - kMaxStringDelay : i0 + 1;
        const int i2 = i1 + 1 >= kMaxStringDelay ? i1 + 1 - kMaxStringDelay : i1 + 1;

        const float xm = line[im], x0 = line[i0], x1 = line[i1], x2 = line[i2];
        const float fm1 = f - 1.f, fm2 = f - 2.f, fp1 = f + 1.f;
        const float hm = -f * fm1 * fm2 * (1.f / 6.f);
        const float h0 =  fp1 * fm1 * fm2 * 0.5f;
        const float h1 = -fp1 * f * fm2 * 0.5f;
        const float h2 =  fp1 * f * fm1 * (1.f / 6.f);
        return hm * xm + h0 * x0 + h1 * x1 + h2 * x2;
    }
}

float GuitarString::readInterp (double pos) const noexcept
{
    return lagrange4 (line_.data(), pos);
}

float GuitarString::readInterpH (double pos) const noexcept
{
    return lagrange4 (lineH_.data(), pos);
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
    // A natural harmonic: the string touched at its 1/n node vibrates in n
    // segments, so it sounds exactly n times the open pitch (e.pitch is the
    // open string). The loop is designed at that frequency.
    const int harmonicN = (e.harmonicN >= 2 && e.harmonicN <= 5) ? e.harmonicN : 1;
    baseF0_ = midiToHz ((double) e.pitch) * (double) harmonicN;

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
    // The touching finger also drains the upper partials while the harmonic rings.
    if (harmonicN >= 2) lp.age01 = 1.0;

    // How much longer the horizontal plane rings than the vertical one (see
    // below). The sustain the single-plane loop was tuned to is what the ear
    // hears of both planes together, so with the second plane on, the vertical
    // plane, the one the top drains, decays a little faster than that: the
    // total sustain stays where it was and the decay gains its two stages.
    const int damp = (int) e.art.damp;
    const double topDrain = clampd ((1.5 - (double) model_.bodyLossScale) / 0.5, 0.0, 1.2);
    const double kH = (damp == 0 && harmonicN < 2) ? 1.15 + 0.65 * topDrain : 1.0;
    if (secondPlane_) lp.sustain01 /= 1.0 + 0.12 * (kH - 1.0);

    designLoop (lp, (int) e.art.damp, design_);
    chainDelay_ = design_.delay - design_.fracPart;
    if (chainDelay_ < 0.0) chainDelay_ = 0.0;


    // A new note starts with clean filter state so the burst is not coloured
    // by whatever the previous note left ringing.
    lpPrev_ = 0.f;
    apX_.fill (0.f);
    apY_.fill (0.f);

    // Horizontal plane. It hardly loads the bridge, so on an acoustic top the
    // horizontal swing rings on far longer than the vertical one: about 1.8x
    // at the fundamental on a dreadnought, and a little less extra in the
    // upper partials, so the tail is mellower than the attack. A solid body
    // hardly drains either plane (that is its long sustain), so its two planes
    // decay almost alike. A palm, a dead note's fretting hand, or the finger on
    // a harmonic node damps both planes the same. Its own lowpass sets the
    // decay; it keeps the vertical loop's dispersion so its partials stay
    // where the vertical ones are, a cent or so apart.
    // 0.5 to 1.3 cents apart, sharp or flat, new on every pluck: the saddle
    // and the pick angle never sit quite the same way twice.
    Rng rng (seed_ ^ 0x7f4a7c15u);
    const double cents = (rng.uniform() < 0.5f ? -1.0 : 1.0) * (0.5 + 0.8 * (double) rng.uniform());
    detuneH_ = std::pow (2.0, -cents / 1200.0);

    const double kHarmH = 0.8;
    solveLoopOnePole (baseF0_, fs_, design_.t60Fund * kH, design_.t60Harm * kHarmH, lpCH_, lpGH_);
    chainDelayH_ = onePolePhaseDelay (baseF0_, fs_, lpCH_)
                 + 4.0 * allpassPhaseDelay (baseF0_, fs_, design_.dispA);
    if (chainDelayH_ < 0.0) chainDelayH_ = 0.0;

    const double gV = loopGainAt (baseF0_, design_.t60Fund);
    const double gH = loopGainAt (baseF0_, design_.t60Fund * kH);
    gainBoostH_ = gV > 1e-9 ? (float) std::min (gH / gV, 1.01) : 1.f;

    // A pick drives the string mostly square to the top; a finger or thumb
    // rolls it, putting more into the horizontal plane.
    injectH_ = (int) e.art.exc == 0 ? 0.3f : 0.4f;
    outH_    = 0.35f;
    // The two planes start in step, so the attack would come out louder by
    // the horizontal share; trim both so the note starts at its old level.
    planeTrim_ = secondPlane_ ? 1.f / (1.f + outH_ * injectH_) : 1.f;

    lpPrevH_ = 0.f;
    apXH_.fill (0.f);
    apYH_.fill (0.f);
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

    // Natural harmonic: the touching finger damps the upper partials and the
    // note speaks softer than a fretted one. Low-pass the burst at 1.5 x the
    // sounding pitch (three passes of a one-pole) and inject it at -5 dB.
    const bool harmonic = e.harmonicN >= 2;
    if (harmonic)
    {
        const float a = (float) (1.0 - std::exp (-6.283185307179586 * 2.2 * baseF0_ / fs_));
        for (int pass = 0; pass < 2; ++pass)
        {
            float y = 0.f;
            for (int i = 0; i < excite_.length(); ++i) { y += (excite_[i] - y) * a; excite_[i] = y; }
        }
        info.clickAmp *= 0.5f;
    }

    // Superpose rather than replace, so re-plucking a ringing string works.
    inject (harmonic ? 1.0f : 1.f);

    const double vel01 = clampd ((double) e.velocity, 0.0, 1.0);
    startBurst (clickBuf_, clickLen_, clickPos_,
                (int) (0.0025 * fs_), info.clickAmp, true, 900.0 + 1800.0 * vel01);

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
    glideFrom_ = glideTo_ = glidePos_ = 0.f;
    glideU_ = 1.f;
    glideDu_ = 0.f;
    glideFretted_ = false;

    // Tension modulation. A pick pulls the string sideways, which stretches
    // it: the note starts sharp and falls back to pitch as the swing dies.
    // Heavy wound strings swing widest and go sharpest. Only a dig-in is
    // clearly heard: about 11 cents on the low E at full force, 3 on a medium
    // (velocity 0.8) pick, none on a soft one, settling in under half a
    // second. A dead note has no swing; a harmonic barely moves.
    {
        static constexpr float kDepthCents[6] = { 14.f, 12.f, 10.f, 8.f, 6.f, 5.f };
        // Below about half velocity the swing is too small to stretch the
        // string by an audible amount, so the curve starts there.
        const float v = clampf ((lastVel_ - 0.45f) / 0.55f, 0.f, 1.f);
        const float hard = clampf (model_.pickHard, 0.f, 100.f) * 0.01f;
        float force = v * v * ((int) e.art.exc == 0 ? (0.6f + 0.4f * hard) : 0.65f);
        double tau = 0.18;
        if (e.art.damp == Damping::PalmMute) { force *= 0.5f; tau = 0.05; }
        if (e.art.damp == Damping::Dead || harmonic) force = 0.f;
        attackSemi_ = kDepthCents[stringIdx_] * force * 0.01f;
        attackCoef_ = (float) (1.0 - std::exp (-1.0 / (tau * fs_)));
    }
}

void GuitarString::legato (const StringEvent& e)
{
    if (! prepared_) prepare (fs_);

    const double newF0 = midiToHz ((double) e.pitch);
    const float targetSemi = (float) (12.0 * std::log2 (newF0 / baseF0_));

    const bool slide = e.pitchFrom >= 0.f;
    const double secs = slide ? (double) glideSec_ : 0.003;
    const double steps = secs * fs_;

    glideFrom_    = glidePos_;
    glideTo_      = targetSemi;
    glideFretted_ = slide;
    glideU_       = 0.f;
    glideDu_      = steps > 1.0 ? (float) (1.0 / steps) : 1.f;
    if (steps <= 1.0) { glideU_ = 1.f; glidePos_ = targetSemi; }

    // A hammer-on is a fingertip striking the string; a pull-off is the
    // finger snapping off it sideways, which plucks the string almost as hard
    // as a light pick (about -6 to -12 dB). At 0.25 the pulled-off note sat
    // ~25 dB under a picked one and vanished in a mix.
    const bool rising = newF0 > baseF0_;
    const float scale = rising ? 0.55f : 0.9f;

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
                    (int) (0.012 * fs_), info.fretNoiseAmp, true, 1800.0);   // soft finger contact, not a pick-like 4 kHz tick
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

    // A damped string stops swinging, so any pick sharpness goes with it.
    if (releaseSec < 20.f)
        attackCoef_ = std::max (attackCoef_, (float) (1.0 - std::exp (-1.0 / (0.03 * fs_))));

    // Finger-lift squeak scales with how hard the note was played.
    const float amp = 0.006f * lastVel_ * clampf (model_.fretNoise, 0.f, 100.f) * 0.01f;
    if (amp > 0.f)
        // A finger leaving the string is a short low rustle, not a 4 kHz hiss.
        startBurst (fretBuf_, fretLen_, fretPos_, (int) (0.022 * fs_), amp * 1.4f, true, 2000.0);
}

//==============================================================================
void GuitarString::setBendSemis (float semis)
{
    bendTarget_ = clampf (semis, -24.f, 24.f);
}

void GuitarString::setVibrato (float cents01)
{
    // Signed: a classical roll swings both ways; a steel-string bend vibrato
    // (GuitarEngine) only ever passes values at or above zero.
    vibTargetCents_ = clampf (cents01, -1.f, 1.f) * 100.f;
}

float GuitarString::glideShape (float u) const noexcept
{
    const float d = glideTo_ - glideFrom_;
    if (! glideFretted_) return glideFrom_ + d * u;

    // A finger slide eases off the first fret and brakes into the target, and
    // on a fretted neck the pitch steps through the frets it passes: it sits
    // near each fret and moves quickly between them, instead of gliding like
    // a synth portamento. Counted back from the target, so it lands exactly.
    const float eased = 0.5f - 0.5f * (float) std::cos (kPi * (double) u);
    const float left  = std::fabs (d) * (1.f - eased);
    const float n     = std::floor (left);
    const float f     = left - n;
    constexpr float kFretDwell = 0.8f;   // 0 = smooth glide, 1 = stops dead on each fret
    const float q = n + f - kFretDwell * (float) std::sin (2.0 * kPi * (double) f) / (float) (2.0 * kPi);
    return glideTo_ - (d < 0.f ? -q : q);
}

float GuitarString::fundamentalHz() const noexcept
{
    const double semi = (double) (semiSmoothed_ + glidePos_ + attackSemi_);
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

    const double semi = (double) (semiSmoothed_ + glidePos_ + attackSemi_);

    // Bending scales the whole loop period; the filter group delay stays put,
    // so the loop filters are never retuned mid note.
    const double bendScale = std::pow (2.0, -semi / 12.0);

    double dLive = design_.loopPeriod * bendScale - chainDelay_ - kReaderTap;
    if (dLive < 4.0) dLive = 4.0;

    const float raw = readInterp (writePos_ - dLive);
    const float bridge = (1.f - (float) kReaderTap) * raw + (float) kReaderTap * readPrev_;
    readPrev_ = raw;
    float x = bridge;

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

    // A damped string is not a sympathetic resonator: scale the string-string
    // drive by the live loop gain share, so a released string stops being
    // driven by its neighbours.
    const float gate = releaseGain_ < naturalGain_ ? releaseGain_ / naturalGain_ : 1.f;
    float input = x * (1.f - coupling) + sympatheticIn * gate;

    float exc = 0.f;
    if (injectPos_ < injectLen_)
        exc = excite_[injectPos_++] * injectScale_;
    // The vertical plane keeps the whole pluck, so the attack keeps all of its
    // brightness; the horizontal share is small enough (about -19 dB at the
    // bridge) that the attack is no more than ~1 dB louder.
    input += exc;

    // Horizontal plane: the same loop, a cent or so off, ringing longer, fed
    // by its share of the pluck and heard more quietly at the bridge. It takes
    // no sympathetic drive; that arrives through the bridge, vertically.
    float bridgeH = 0.f;
    if (secondPlane_)
    {
        double dLiveH = design_.loopPeriod * bendScale * detuneH_ - chainDelayH_ - kReaderTap;
        if (dLiveH < 4.0) dLiveH = 4.0;

        const float rawH = readInterpH (writePos_ - dLiveH);
        bridgeH = (1.f - (float) kReaderTap) * rawH + (float) kReaderTap * readPrevH_;
        readPrevH_ = rawH;
        float h = bridgeH;

        const float k = (float) (lpGH_ * (1.0 - lpCH_));
        h = k * h + (float) lpCH_ * lpPrevH_;
        lpPrevH_ = h;

        const float a = (float) design_.dispA;
        for (std::size_t i = 0; i < 4; ++i)
        {
            const float in = h;
            const float outp = a * in + apXH_[i];
            apXH_[i] = in;
            const float y = outp - a * apYH_[i];
            apYH_[i] = y;
            h = y;
        }

        h *= releaseGain_ * gainBoostH_;

        double p = std::fmod (writePos_, (double) kMaxStringDelay);
        if (p < 0.0) p += (double) kMaxStringDelay;
        lineH_[(std::size_t) ((int) p)] = h + exc * injectH_;
    }

    writeAt (writePos_, input);
    writePos_ += 1.0;
    if (writePos_ >= (double) kMaxStringDelay)
        writePos_ -= (double) kMaxStringDelay;

    // Advance the glide.
    if (glideU_ < 1.f)
    {
        glideU_ += glideDu_;
        if (glideU_ >= 1.f) { glideU_ = 1.f; glidePos_ = glideTo_; }
        else                  glidePos_ = glideShape (glideU_);
    }

    // Let the pick's tension sharpness settle.
    if (attackSemi_ != 0.f)
    {
        attackSemi_ -= attackSemi_ * attackCoef_;
        if (std::fabs (attackSemi_) < 1e-5f) attackSemi_ = 0.f;
    }

    // Smooth the bend and vibrato offset.
    semiSmoothed_ += (bendTarget_ + vibTargetCents_ * 0.01f - semiSmoothed_) * semiIncCoeff_;

    return planeTrim_ * (bridge + outH_ * bridgeH)
         + readBurst (clickBuf_, clickLen_, clickPos_)
         + readBurst (thumpBuf_, thumpLen_, thumpPos_)
         + readBurst (fretBuf_,  fretLen_,  fretPos_);
}

} // namespace fw::dsp

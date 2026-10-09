#pragma once

namespace fw::dsp {

// Physical string loop design.
//
// One pass around the loop is
//     [fractional delay D] -> [1-pole lowpass] -> [4 identical allpasses]
//     -> [optional harmonic comb] -> [release gain]
// and the loop gain must fall from 1 to G(f) = 10^(-3 / (f * T60(f))) across
// the band. Everything here is pure math: no allocation, no state, no JUCE.

// Per-string inputs to the design. All 01 fields are already normalised.
struct LoopDesignParams
{
    double f0             = 82.41;   // Hz
    double fs             = 44100.0;
    int    string         = 0;       // 0 = low E
    int    material       = 0;       // 0 Steel Light, 1 Steel Medium, 2 Nylon, 3 Flatwound
    double sustain01      = 1.0;
    double age01          = 0.2;
    double bodyLossScale  = 1.0;
    double stiffness01    = 1.0;
    double pickPos01      = 0.15;
    bool   harmonicActive = false;
    int    harmonicN      = 0;       // 2..5
};

struct LoopDesignResult
{
    double f0       = 0.0;
    double period   = 0.0;   // fs / f0, in samples
    double t60Fund  = 0.0;
    double t60Harm  = 0.0;
    double lpC      = 0.0;   // 1-pole lowpass coefficient, 0..0.95
    double lpG      = 0.0;   // 1-pole lowpass gain
    double dispA    = 0.0;   // allpass coefficient, <= 0
    double delay    = 0.0;   // total loop delay in samples
    double loopPeriod = 0.0;  // period the LOOP is tuned to (openPeriod / n for harmonics)
    double fracPart = 0.0;   // the fractional delay on its own
    double combDelay= 0.0;   // harmonic comb group delay
    double inharmB  = 0.0;   // effective stiffness parameter B
    double loopGain = 0.0;   // per-pass gain at f0
};

// damping: 0 Open, 1 PalmMute, 2 Dead (matches fw::Damping).

double baseT60         (int stringIndex) noexcept;
double materialScale   (int material) noexcept;
double materialInharm  (int material) noexcept;
double harmonicRatio   (int material) noexcept;

double computeT60Fund  (const LoopDesignParams& p, int damping) noexcept;
double computeT60Harm  (const LoopDesignParams& p, int damping) noexcept;

// |1 - c e^-jw|: the one-pole numerator/denominator, gain removed.
double onePoleMagNoGain (double c, double w) noexcept;
double onePoleMag       (double c, double g, double w) noexcept;
double onePolePhaseDelay(double f, double fs, double c) noexcept;
double allpassPhaseDelay(double f, double fs, double a) noexcept;

// Bisects c so the lowpass reproduces the T60(f0)/T60(6*f0) decay ratio.
void solveLoopOnePole (double f0, double fs, double t60Fund, double t60Harm,
                       double& c, double& g) noexcept;

// Per-pass gain at f for the given T60.
double loopGainAt (double f, double t60) noexcept;

// Inharmonicity constant B for this string.
double inharmonicityB (const LoopDesignParams& p) noexcept;

// Harmonic comb group delay, (n-1)*(P/n)/2, or 0 when inactive.
double harmonicCombDelay (double period, bool active, int harmonicN) noexcept;

// Total phase delay of the loop at f: fractional delay + lowpass + allpasses.
double totalLoopDelay (double f, double fs, double fracPart, double c, double a) noexcept;

// Picks the allpass coefficient whose partials best match the stiff-string
// targets. Returns 0 when B is negligible.
double chooseDispersionA (double f0, double fs, double fracPart, double c, double B) noexcept;

// Full design for one pluck.
void designLoop (const LoopDesignParams& p, int damping, LoopDesignResult& out) noexcept;

} // namespace fw::dsp

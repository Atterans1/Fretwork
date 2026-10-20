#pragma once
#include "../Contracts.h"
#include <cstddef>

namespace fw::dsp {

// Body selection.
enum class BodyKind : int
{
    Dreadnought = 0,
    Classical    = 1,
    SolidElectric = 2,
    SemiHollow   = 3
};

// One parallel resonant mode of a guitar body, from a physical measurement
// style analysis: centre frequency, quality factor, and relative level.
struct BodyMode
{
    float hz;
    float q;
    float db;
};

inline constexpr int kMaxBodyModes = 12;

inline constexpr BodyMode kDreadnoughtModes[kMaxBodyModes] =
{
    {  98.0f, 18.0f,  6.0f }, { 190.0f, 25.0f,  4.0f }, { 280.0f, 20.0f,  2.0f },
    { 400.0f, 15.0f,  2.0f }, { 520.0f, 12.0f,  0.0f }, { 690.0f, 12.0f,  2.0f },
    { 950.0f, 10.0f,  1.0f }, {1250.0f, 10.0f,  0.0f }, {1700.0f,  8.0f,  1.0f },
    {2300.0f,  8.0f,  1.0f }, {3100.0f,  6.0f,  0.0f }, {4200.0f,  5.0f, -1.0f }
};

inline constexpr BodyMode kClassicalModes[kMaxBodyModes] =
{
    { 105.0f, 15.0f,  5.0f }, { 170.0f, 20.0f,  4.0f }, { 240.0f, 16.0f,  2.0f },
    { 330.0f, 14.0f,  2.0f }, { 450.0f, 12.0f,  1.0f }, { 620.0f, 10.0f,  1.0f },
    { 850.0f,  9.0f,  0.0f }, {1150.0f,  8.0f,  1.0f }, {1600.0f,  7.0f,  0.0f },
    {2200.0f,  6.0f,  0.0f }, {3000.0f,  5.0f, -1.0f }
};

inline constexpr BodyMode kSemiHollowModes[kMaxBodyModes] =
{
    { 110.0f, 8.0f, 3.0f }, { 230.0f, 6.0f, 2.0f }, { 420.0f, 5.0f, 1.0f },
    { 800.0f, 4.0f, 1.0f }
};

inline constexpr BodyMode kSolidElectricModes[kMaxBodyModes] =
{
    { 180.0f, 3.0f, 1.0f }
};

struct BodyTable
{
    const BodyMode* modes;
    int    count;
    float  mixDb;    // how loud this body sits in the overall blend
};

inline constexpr BodyTable kBodyTables[4] =
{
    { kDreadnoughtModes,  12,   0.0f },   // Dreadnought
    { kClassicalModes,    11,   0.0f },   // Classical
    { kSolidElectricModes, 1, -20.0f },   // Solid Electric
    { kSemiHollowModes,    4,  -8.0f }    // Semi-Hollow
};

// Sustain multiplier applied to every string for the selected body
// (Dreadnought, Classical, Solid Electric, Semi-Hollow). A resonant acoustic
// top drains the string fastest; a solid body barely couples, so its notes ring
// clearly longer, and a semi-hollow's centre block puts it between the two.
inline constexpr float kBodyLossScale[4] = { 1.0f, 0.9f, 1.5f, 1.2f };

// Highest fret reachable for each body.
// 24 frets everywhere: a 6-string already spans three octaves; the per-body
// variation (19-22) dropped high notes the model can actually render.
inline constexpr int kBodyMaxFret[4] = { 24, 24, 24, 24 };

// Pickup placement, as a fraction of the scale length from the bridge. These
// fractions are what turn the bridge signal into a comb-filtered pickup
// voltage, which is where the in-between-string character comes from.
enum class PickupSel : int { Bridge = 0, Middle, Neck, BridgeNeck };

inline constexpr float kPickupPosBridge = 0.060f;
inline constexpr float kPickupPosMiddle = 0.120f;
inline constexpr float kPickupPosNeck   = 0.190f;

// The two coils of a humbucker sit this far apart in scale-length fraction,
// and cancel part of each other, which is why it sounds thicker.
inline constexpr float kHumbuckerSpread = 0.028f;

enum class PickupType : int { SingleCoil = 0, Humbucker, P90 };

struct PickupTone
{
    float hz;
    float q;
};

inline constexpr PickupTone kPickupTone[3] =
{
    { 4500.0f, 1.2f },   // Single-Coil
    { 2800.0f, 1.0f },   // Humbucker
    { 3400.0f, 1.1f }    // P90
};

// Scale length per string in metres, used to turn the pickup fractions above
// into an absolute distance for anything that needs one.
// Multi-scale per course; index kMaxStrings-1 is the 7-string's low B, whose
// longer scale is why 7-strings ship with heavier gauges.
inline constexpr float kScaleLengthM[kMaxStrings] = { 0.686f, 0.648f, 0.648f, 0.648f, 0.628f, 0.648f, 0.686f };

} // namespace fw::dsp

namespace fw::dsp {

// Extended material list. Indices 0-3 are the original four (Steel Light,
// Steel Medium, Nylon, Flatwound) and must keep their meaning for saved state.
// 4+ are named string sets; each entry overrides the physical tables per string.
struct MaterialSet
{
    const char* name;
    double      t60Scale;      // sustain multiplier on baseT60
    double      inharm;        // inharmonicity B multiplier
    double      harmRatio;     // T60(higher partials)/T60(fundamental)
    const char* bodyDefault;   // UI hint only, not used by DSP
};

inline constexpr MaterialSet kMaterialSets[] =
{
    { "Steel Light",        1.00, 1.00, 0.12, "Dreadnought"   },
    { "Steel Medium",       1.10, 1.40, 0.12, "Dreadnought"   },
    { "Nylon",              0.90, 0.25, 0.10, "Classical"     },
    { "Flatwound",          0.90, 1.60, 0.05, "Solid Electric"},
    // Named sets. Pure Nickel: round-wound feel but softer attack and a
    // darker, sweeter top than nickel-plated steel; slightly longer sustain
    // than plain steel because pure nickel is less elastic (higher internal
    // damping) but its lower stiffness keeps inharmonicity low.
    { "Pure Nickel Slinky", 1.05, 0.85, 0.10, "Solid Electric"},
    { "Nickel Plated Steel",1.00, 1.10, 0.12, "Dreadnought"   },
    { "Stainless Steel",    0.98, 1.20, 0.13, "Dreadnought"   },
    { "80/20 Bronze",       1.00, 0.90, 0.11, "Dreadnought"   },
    { "Phosphor Bronze",    1.08, 0.95, 0.11, "Dreadnought"   },
    { "Flatwound Jazz",     0.88, 1.70, 0.05, "Semi-Hollow"   },
    { "Silk & Steel",       0.85, 0.70, 0.09, "Classical"     },
};

inline constexpr int kNumMaterials = (int) (sizeof (kMaterialSets) / sizeof (kMaterialSets[0]));

// Named-gauge string sets. Gauges in inches, index 5 = high E ... index 0 =
// low E, matching every other per-string array (see Contracts.h). Wound flags
// use the same order. Gauge sets the per-string sustain/attack character
// through the loop design (heavier strings sustain longer, are less bright).
// The 7th entry is the 7-string low B: 7-string sets run the low B at
// .054-.062; .054 keeps tension sane at the longer 7-string scale.
struct StringGaugeSet
{
    const char* name;
    float       gauges[kMaxStrings];
    bool        wound[kMaxStrings];
    double      sustainPerGauge;  // T60 multiplier per 0.001 inch above 0.010
    double      brightness;       // excitation lowpass shift (1 = neutral)
};

// Gauges are real product data. Ernie Ball Slinky Classic Rock n Roll Pure
// Nickel, 9-42: .009 .011 .016 .024w .032 .042 (ernieball.com P02253).
inline constexpr StringGaugeSet kStringSets[] =
{
    { "Standard 10s",       { 0.054f, 0.046f, 0.036f, 0.026f, 0.017f, 0.013f, 0.010f }, { true, true, true, true,false,false,false }, 0.9,  1.00 },
    { "Pure Nickel Slinky", { 0.054f, 0.042f, 0.032f, 0.024f, 0.016f, 0.011f, 0.009f }, { true, true, true, true,false,false,false }, 0.90, 0.88 },
    { "Super Slinky 9s",    { 0.054f, 0.042f, 0.032f, 0.024f, 0.016f, 0.011f, 0.009f }, { true, true, true, true,false,false,false }, 0.90, 1.05 },
    { "Power Slinky 11s",   { 0.058f, 0.048f, 0.038f, 0.028f, 0.018f, 0.014f, 0.011f }, { true, true, true, true,false,false,false }, 1.00, 0.95 },
    { "Heavy 12s",          { 0.062f, 0.054f, 0.042f, 0.032f, 0.020f, 0.016f, 0.012f }, { true, true, true, true,false,false,false }, 1.05, 0.90 },
    { "Classical Tension",  { 0.043f, 0.033f, 0.028f, 0.030f, 0.040f, 0.032f, 0.028f }, { false,false,false,true, true, true, true }, 0.90, 0.75 },
};

inline constexpr int kNumStringSets = (int) (sizeof (kStringSets) / sizeof (kStringSets[0]));

} // namespace fw::dsp

// Compile-time guards: array order is low E first, so the low E must be the
// heaviest (or wound) string and the high E the lightest (or plain).
static_assert (fw::dsp::kStringSets[1].gauges[0] >= fw::dsp::kStringSets[1].gauges[5], "gauge order: string 0 must be the low E");
static_assert (fw::dsp::kStringSets[1].wound[0] && ! fw::dsp::kStringSets[1].wound[5], "wound flags follow the same order");

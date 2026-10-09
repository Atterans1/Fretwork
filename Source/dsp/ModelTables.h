#pragma once
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

// Loop-loss multiplier applied to every string for the selected body.
inline constexpr float kBodyLossScale[4] = { 1.0f, 0.9f, 1.15f, 0.95f };

// Highest fret reachable for each body.
inline constexpr int kBodyMaxFret[4] = { 20, 19, 22, 21 };

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
inline constexpr float kScaleLengthM[6] = { 0.648f, 0.628f, 0.648f, 0.648f, 0.628f, 0.648f };

} // namespace fw::dsp

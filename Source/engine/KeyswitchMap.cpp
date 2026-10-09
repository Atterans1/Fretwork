
#include "engine/KeyswitchMap.h"

#include <cstddef>

namespace fw::ks {

namespace {
    constexpr const char* kNames[kCount] =
    {
        "Pick", "Finger", "Thumb",
        "Open", "PalmMute", "Dead",
        "Normal", "Slide", "Legato", "Bend",
        "HarmonicOff", "HarmonicNatural",
        "ModePoly", "ModeStrum", "ModeArp", "ModeSeq",
        "StrumDown", "StrumUp", "MutedDown", "MutedUp", "PalmDown", "PalmUp",
        "PickLowest", "PickHighest",
        "TremoloOff", "TremoloOn"
    };

    static_assert ((int) Key::TremoloOff == 24, "keyswitch table drift");
    static_assert ((int) Key::StrumDown  == 16, "keyswitch table drift");
    static_assert (kGroupFirstNote[(int) Group::Tremolo] == 36, "tremolo base");
    static_assert (kGroupFirstNote[(int) Group::Mode]    == 24, "mode base");
}

const char* name (Key k) noexcept
{
    const int i = (int) k;
    return (i >= 0 && i < kCount) ? kNames[i] : "None";
}

Articulation applyGroup (Articulation a, const int* ks) noexcept
{
    if (ks == nullptr) return a;

    auto clamp = [] (int v, int hi) { return v < 0 ? 0 : (v > hi ? hi : v); };

    a.exc   = (Excitation)  clamp (ks[(int) Group::Excitation],  2);
    a.damp  = (Damping)     clamp (ks[(int) Group::Damping],     2);
    a.trans = (Transition)  clamp (ks[(int) Group::Transition],  3);
    a.harm  = (Harmonic)    clamp (ks[(int) Group::Harmonic],    1);
    a.tremolo = ks[(int) Group::Tremolo] > 0;
    return a;
}

} // namespace fw::ks

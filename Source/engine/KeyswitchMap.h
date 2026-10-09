
#pragma once
#include "Contracts.h"

namespace fw::ks {

// Keyswitch MIDI notes 12..37. Note-ons never sound a string on their own:
// they either latch an articulation/mode group or fire a momentary stroke.
inline constexpr int kFirstNote = 12;
inline constexpr int kLastNote  = 37;
inline constexpr int kCount     = kLastNote - kFirstNote + 1; // 26

enum class Key : int
{
    Pick = 0, Finger, Thumb,               // 12..14  excitation
    Open, PalmMute, Dead,                  // 15..17  damping
    Normal, Slide, Legato, Bend,           // 18..21  transition
    HarmonicOff, HarmonicNatural,          // 22..23  harmonic
    ModePoly, ModeStrum, ModeArp, ModeSeq, // 24..27  play mode
    StrumDown, StrumUp, MutedDown, MutedUp, PalmDown, PalmUp,
    PickLowest, PickHighest,               // 28..35  momentary strokes
    TremoloOff, TremoloOn,                 // 36..37  tremolo
    Count
};

// Momentary actions, undone on note-off.
enum class Stroke : int
{
    None = 0,
    Down, Up, MutedDown, MutedUp, PalmDown, PalmUp,
    PickLowest, PickHighest
};

// Latched groups, in the exact order of UiState::ks[].
enum class Group : int
{
    Excitation = 0,
    Damping,
    Transition,
    Harmonic,
    Tremolo,
    Mode,
    Count
};

inline constexpr int kNumGroups = (int) Group::Count;

// First MIDI note of each group; group value == note - first[group].
inline constexpr int kGroupFirstNote[kNumGroups] =
{
    12,  // Key::Pick
    15,  // Key::Open
    18,  // Key::Normal
    22,  // Key::HarmonicOff
    36,  // Key::TremoloOff
    24   // Key::ModePoly
};

constexpr bool isKeyswitch (int note) noexcept
{
    return note >= kFirstNote && note <= kLastNote;
}

constexpr Key key (int note) noexcept
{
    return isKeyswitch (note) ? (Key) (note - kFirstNote) : Key::Count;
}

constexpr int noteOf (Key k) noexcept { return kFirstNote + (int) k; }

// True for the momentary stroke keys, 28..35.
constexpr bool isStroke (int note) noexcept
{
    return note >= kFirstNote + (int) Key::StrumDown
        && note <= kFirstNote + (int) Key::PickHighest;
}

// Latched keys only: everything except the momentary strokes.
constexpr bool isLatched (int note) noexcept
{
    return isKeyswitch (note) && ! isStroke (note);
}

constexpr Stroke stroke (int note) noexcept
{
    return isStroke (note) ? (Stroke) (1 + (note - (kFirstNote + (int) Key::StrumDown)))
                           : Stroke::None;
}

constexpr Group group (int note) noexcept
{
    if (! isLatched (note)) return Group::Mode; // undefined for strokes
    const Key k = key (note);
    if (k <= Key::Thumb)            return Group::Excitation;
    if (k <= Key::Dead)             return Group::Damping;
    if (k <= Key::Bend)             return Group::Transition;
    if (k <= Key::HarmonicNatural) return Group::Harmonic;
    if (k >= Key::TremoloOff)       return Group::Tremolo;
    return Group::Mode;
}

// Group value (index within its group) of a latched keyswitch.
constexpr int groupValue (int note) noexcept
{
    return note - kGroupFirstNote[(int) group (note)];
}

// Human readable name. Diagnostics and asserts only, never on the audio thread.
const char* name (Key k) noexcept;

// Fold the six latched group values into the live articulation.
Articulation applyGroup (Articulation a, const int* ks) noexcept;

} // namespace fw::ks

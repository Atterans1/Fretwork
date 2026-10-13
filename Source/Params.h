#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace fw {

namespace id {
inline constexpr const char* strumTime   = "strumTime";
inline constexpr const char* release     = "release";
inline constexpr const char* strokeVol   = "strokeVol";
inline constexpr const char* timeOffset  = "timeOffset";
inline constexpr const char* separation  = "separation";
inline constexpr const char* velOffset   = "velOffset";
inline constexpr const char* humanize    = "humanize";
inline constexpr const char* quantize    = "quantize";
inline constexpr const char* signature   = "signature";
inline constexpr const char* bpm         = "bpm";
inline constexpr const char* transpose   = "transpose";
inline constexpr const char* hold        = "hold";
inline constexpr const char* power       = "power";
inline constexpr const char* metronome   = "metronome";
inline constexpr const char* seqPlay     = "seqPlay";
inline constexpr const char* seqSlot     = "seqSlot";

inline constexpr const char* excitation  = "excitation";
inline constexpr const char* damping     = "damping";
inline constexpr const char* transition  = "transition";
inline constexpr const char* harmonic    = "harmonic";
inline constexpr const char* tremolo     = "tremolo";
inline constexpr const char* mode        = "mode";
inline constexpr const char* voicing     = "voicing";

inline constexpr const char* glideTime   = "glideTime";
inline constexpr const char* bendAmount  = "bendAmount";
inline constexpr const char* bendTime    = "bendTime";
inline constexpr const char* wheelRange  = "wheelRange";
inline constexpr const char* vibRate     = "vibRate";
inline constexpr const char* vibDepth    = "vibDepth";
inline constexpr const char* tremRate    = "tremRate";

inline constexpr const char* arpStrings  = "arpStrings";
inline constexpr const char* arpMask0    = "arpMask0";
inline constexpr const char* arpMask1    = "arpMask1";
inline constexpr const char* arpMask2    = "arpMask2";
inline constexpr const char* arpMask3    = "arpMask3";
inline constexpr const char* arpMask4    = "arpMask4";
inline constexpr const char* arpMask5    = "arpMask5";
inline constexpr const char* arpOrder    = "arpOrder";
inline constexpr const char* arpRate     = "arpRate";
inline constexpr const char* arpGate     = "arpGate";
inline constexpr const char* arpSwing    = "arpSwing";

inline constexpr const char* body        = "body";
inline constexpr const char* tuning      = "tuning";
inline constexpr const char* capo        = "capo";
inline constexpr const char* tune0       = "tune0";
inline constexpr const char* tune1       = "tune1";
inline constexpr const char* tune2       = "tune2";
inline constexpr const char* tune3       = "tune3";
inline constexpr const char* tune4       = "tune4";
inline constexpr const char* tune5       = "tune5";
inline constexpr const char* bodySize    = "bodySize";
inline constexpr const char* bodyRes     = "bodyRes";
inline constexpr const char* topDamp     = "topDamp";
inline constexpr const char* micBlend    = "micBlend";

inline constexpr const char* material    = "material";
inline constexpr const char* stringAge   = "stringAge";
inline constexpr const char* stiffness   = "stiffness";
inline constexpr const char* pickPos     = "pickPos";
inline constexpr const char* pickHard    = "pickHard";
inline constexpr const char* sympathetic = "sympathetic";
inline constexpr const char* fretNoise   = "fretNoise";
inline constexpr const char* sustain     = "sustain";

inline constexpr const char* pickupType  = "pickupType";
inline constexpr const char* pickupSel   = "pickupSel";
inline constexpr const char* pickupHeight= "pickupHeight";
inline constexpr const char* pickupTone  = "pickupTone";
inline constexpr const char* pickupOut   = "pickupOut";

inline constexpr const char* tone        = "tone";
inline constexpr const char* drive       = "drive";
inline constexpr const char* room        = "room";
inline constexpr const char* width       = "width";
inline constexpr const char* output      = "output";
} // namespace id

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace fw

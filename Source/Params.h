#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace fw {

/** Tone was inverted before state version "toneMapping" 2: the master filter
    used exp(-2*pi*f/fs) as its one-pole coefficient instead of 1 - that. This
    returns the cutoff that sounds the same as an old Tone setting (at 48 kHz),
    so projects and presets saved before the fix keep their sound. */
inline float legacyToneHz (double oldHz)
{
    constexpr double fs = 48000.0, twoPi = 6.283185307179586;
    const double alpha = std::exp (-twoPi * oldHz / fs);
    const double hz = -std::log (1.0 - alpha) * fs / twoPi;
    return (float) juce::jlimit (1000.0, 16000.0, hz);
}
inline constexpr int kToneMappingVersion = 2;

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
inline constexpr const char* stringGain0 = "stringGain0";
inline constexpr const char* stringGain1 = "stringGain1";
inline constexpr const char* stringGain2 = "stringGain2";
inline constexpr const char* stringGain3 = "stringGain3";
inline constexpr const char* stringGain4 = "stringGain4";
inline constexpr const char* stringGain5 = "stringGain5";
inline constexpr const char* bodySize    = "bodySize";
inline constexpr const char* bodyRes     = "bodyRes";
inline constexpr const char* topDamp     = "topDamp";
inline constexpr const char* micBlend    = "micBlend";

inline constexpr const char* material    = "material";
inline constexpr const char* stringSet   = "stringSet";
inline constexpr const char* guitarType  = "guitarType";
inline constexpr const char* courseLevel = "courseLevel";
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
inline constexpr const char* roomType    = "roomType";
inline constexpr const char* width       = "width";
inline constexpr const char* output      = "output";
inline constexpr const char* ampModel    = "ampModel";
inline constexpr const char* ampGain     = "ampGain";
inline constexpr const char* ampBass     = "ampBass";
inline constexpr const char* ampMid      = "ampMid";
inline constexpr const char* ampTreble   = "ampTreble";
inline constexpr const char* ampLevel    = "ampLevel";
inline constexpr const char* cab         = "cab";
inline constexpr const char* chorusMix   = "chorusMix";
inline constexpr const char* chorusRate  = "chorusRate";
inline constexpr const char* delayMix    = "delayMix";
inline constexpr const char* delayTime   = "delayTime";
inline constexpr const char* delayFeedback = "delayFeedback";
} // namespace id

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace fw

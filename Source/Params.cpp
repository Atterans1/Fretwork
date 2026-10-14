#include "Params.h"

#include <cmath>

namespace fw
{
namespace
{
    juce::AudioParameterFloat* floatParam (const char* pid, const char* name,
                                           float lo, float hi, float def,
                                           const char* unit, float skew)
    {
        juce::NormalisableRange<float> range (lo, hi, 0.0f, skew);

        // Without this the editor and any generic host UI print raw floats like
        // "29.0000000". Scale the decimals to the magnitude instead.
        const juce::String unitString (unit != nullptr ? unit : "");

        const auto toText = [unitString] (float v, int)
        {
            const float magnitude = v < 0.f ? -v : v;
            const int decimals = magnitude >= 100.f ? 0 : (magnitude >= 10.f ? 1 : 2);

            // Values that round to zero print as "0.00", not "-0.00".
            if (magnitude < 0.5f * std::pow (10.f, (float) -decimals))
                v = 0.f;

            juce::String s (v, decimals);
            if (unitString.isNotEmpty())
                s << " " << unitString;
            return s;
        };

        return new juce::AudioParameterFloat ({ juce::String (pid) }, juce::String (name), range, def,
                                              juce::AudioParameterFloatAttributes()
                                                  .withLabel (unit)
                                                  .withStringFromValueFunction (toText));
    }

    juce::AudioParameterInt* intParam (const char* pid, const char* name, int lo, int hi, int def)
    {
        return new juce::AudioParameterInt ({ juce::String (pid) }, juce::String (name), lo, hi, def);
    }

    juce::AudioParameterBool* boolParam (const char* pid, const char* name, bool def)
    {
        return new juce::AudioParameterBool ({ juce::String (pid) }, juce::String (name), def);
    }

    juce::AudioParameterChoice* choiceParam (const char* pid, const char* name,
                                             std::initializer_list<const char*> items, int def)
    {
        juce::StringArray names;
        for (auto* s : items)
            names.add (s);

        return new juce::AudioParameterChoice ({ juce::String (pid) }, juce::String (name), names, def);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto add = [&layout] (juce::RangedAudioParameter* p)
    {
        layout.add (std::unique_ptr<juce::RangedAudioParameter> (p));
    };

    // ---------------------------------------------------------------- performance
    add (floatParam  (id::strumTime,  "Strum Time",    0.f, 200.f, 29.f,   "ms", 0.5f));
    add (floatParam  (id::release,    "Release",       0.05f, 20.f, 0.4f, "s",  0.4f));
    add (floatParam  (id::strokeVol,  "Stroke Volume", 0.f, 1.f,   0.9f,   "",   0.5f));
    add (floatParam  (id::timeOffset, "Time",        -50.f, 50.f,  0.f,    "ms", 0.5f));
    add (floatParam  (id::separation, "Separation",    0.f, 100.f, 0.f,    "%",  0.5f));
    add (intParam    (id::velOffset,  "Velocity",     -64,  63,   0));
    add (floatParam  (id::humanize,   "Humanization",  0.f, 100.f, 40.f,    "%",  0.5f));
    add (choiceParam (id::quantize,   "Quantize",   { "1/8", "1/16", "1/32" }, 1));
    add (choiceParam (id::signature,  "Signature",  { "2/4", "3/4", "4/4", "5/4", "6/8", "7/8", "12/8", "8/4", "9/8" }, 2));
    add (floatParam  (id::bpm,        "Tempo",        40.f, 240.f, 100.f,  "BPM", 0.5f));
    add (intParam    (id::transpose,  "Transpose",    -12, 12,   0));
    add (boolParam   (id::hold,       "Hold",         false));
    add (boolParam   (id::power,      "Power",        true));
    add (boolParam   (id::metronome,  "Metronome",    false));
    add (boolParam   (id::seqPlay,    "Seq Play",     false));
    add (intParam    (id::seqSlot,    "Seq Slot",     1, 8,     1));

    // ------------------------------------------------------- articulation / mode
    add (choiceParam (id::excitation, "Excitation", { "Pick", "Finger", "Thumb" }, 0));
    add (choiceParam (id::damping,    "Damping",    { "Open", "Palm", "Dead" },    0));
    add (choiceParam (id::transition, "Transition", { "Normal", "Slide", "Legato", "Bend" }, 0));
    add (choiceParam (id::harmonic,   "Harmonic",   { "Off", "Natural" },          0));
    add (choiceParam (id::tremolo,    "Tremolo",    { "Off", "On" },               0));
    add (choiceParam (id::mode,       "Mode",       { "Poly", "Strum", "Arp", "Seq" }, 0));
    // How held keys become a guitar chord in Strum/Arp/Seq. Auto voices
    // recognised chords (3+ notes) as guitar shapes and plays anything else as
    // held; Guitar always shapes recognised chords; Played never re-voices.
    add (choiceParam (id::voicing,    "Voicing",    { "Auto", "Played", "Guitar" }, 0));

    // -------------------------------------------------------------- transitions
    add (floatParam  (id::glideTime,  "Glide Time",   20.f,  400.f, 90.f,   "ms", 0.5f));
    add (floatParam  (id::bendAmount, "Bend Amount",  0.5f,  4.f,   2.f,    "st", 0.5f));
    add (floatParam  (id::bendTime,   "Bend Time",    30.f,  400.f, 120.f,  "ms", 0.5f));
    add (intParam    (id::wheelRange, "Wheel Range",  1,    12,    2));
    add (floatParam  (id::vibRate,    "Vibrato Rate", 3.f,   8.f,   5.5f,   "Hz", 0.5f));
    add (floatParam  (id::vibDepth,   "Vibrato Depth",0.f,   100.f, 25.f,   "c",  0.5f));
    add (choiceParam (id::tremRate,   "Tremolo Rate", { "1/16", "1/16T", "1/32" }, 0));

    // ---------------------------------------------------------------------- arp
    add (intParam    (id::arpStrings, "Arp Strings",  1, 6, 6));
    add (boolParam   (id::arpMask0,   "Arp String 1", true));
    add (boolParam   (id::arpMask1,   "Arp String 2", true));
    add (boolParam   (id::arpMask2,   "Arp String 3", true));
    add (boolParam   (id::arpMask3,   "Arp String 4", true));
    add (boolParam   (id::arpMask4,   "Arp String 5", true));
    add (boolParam   (id::arpMask5,   "Arp String 6", true));
    add (choiceParam (id::arpOrder,   "Arp Order",    { "Up", "Down", "UpDown", "Random" }, 0));
    add (choiceParam (id::arpRate,    "Arp Rate",     { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 3));
    add (floatParam  (id::arpGate,    "Arp Gate",     10.f, 100.f, 80.f,  "%",  0.5f));
    add (floatParam  (id::arpSwing,   "Swing",        0.f,  60.f,  0.f,   "%",  0.5f));

    // -------------------------------------------------------------- body/tuning
    add (choiceParam (id::body,       "Body",       { "Dreadnought Steel", "Classical Nylon", "Solid Electric", "Semi-Hollow" }, 0));
    add (choiceParam (id::tuning,     "Tuning",     { "Standard", "Drop D", "Half-Step Down", "DADGAD", "Open G" }, 0));
    add (intParam    (id::capo,       "Capo",       0, 7, 0));
    add (floatParam  (id::tune0,      "String 6 Fine", -50.f, 50.f, 0.f, "c", 0.5f));
    add (floatParam  (id::tune1,      "String 5 Fine", -50.f, 50.f, 0.f, "c", 0.5f));
    add (floatParam  (id::tune2,      "String 4 Fine", -50.f, 50.f, 0.f, "c", 0.5f));
    add (floatParam  (id::tune3,      "String 3 Fine", -50.f, 50.f, 0.f, "c", 0.5f));
    add (floatParam  (id::tune4,      "String 2 Fine", -50.f, 50.f, 0.f, "c", 0.5f));
    add (floatParam  (id::tune5,      "String 1 Fine", -50.f, 50.f, 0.f, "c", 0.5f));
    add (floatParam  (id::bodySize,   "Body Size",   0.85f, 1.15f, 1.f,  "x", 0.5f));
    add (floatParam  (id::bodyRes,    "Body Resonance", 0.f, 150.f, 100.f, "%", 0.5f));
    add (floatParam  (id::topDamp,    "Top Damp",    0.f, 100.f, 30.f,  "%",  0.5f));
    add (floatParam  (id::micBlend,   "Mic Blend",   0.f, 100.f, 70.f,  "%",  0.5f));

    // ------------------------------------------------------------------ strings
    add (choiceParam (id::material,   "Material",    { "Steel Light", "Steel Medium", "Nylon", "Flatwound" }, 0));
    add (floatParam  (id::stringAge,  "String Age",  0.f, 100.f, 20.f,   "%", 0.5f));
    add (floatParam  (id::stiffness,  "Stiffness",   0.f, 200.f, 100.f,  "%", 0.5f));
    add (floatParam  (id::pickPos,    "Pick Position", 5.f, 50.f, 15.f, "%", 0.5f));
    add (floatParam  (id::pickHard,   "Pick Hardness", 0.f, 100.f, 50.f, "%", 0.5f));
    add (floatParam  (id::sympathetic,"Sympathetic", 0.f, 100.f, 30.f,   "%", 0.5f));
    add (floatParam  (id::fretNoise,  "Fret Noise",  0.f, 100.f, 30.f,   "%", 0.5f));
    add (floatParam  (id::sustain,    "Sustain",     25.f, 200.f, 100.f, "%", 0.5f));

    // ----------------------------------------------------------------- pickups
    add (choiceParam (id::pickupType, "Pickup Type", { "Single-Coil", "Humbucker", "P90" }, 0));
    add (choiceParam (id::pickupSel,  "Pickup",      { "Bridge", "Middle", "Neck", "Bridge+Neck" }, 0));
    add (floatParam  (id::pickupHeight, "Pickup Height", 0.f, 100.f, 50.f, "%", 0.5f));
    add (floatParam  (id::pickupTone, "Pickup Tone", 0.f, 100.f, 50.f,   "%", 0.5f));
    add (floatParam  (id::pickupOut,  "Pickup Out", -12.f, 12.f, 0.f,    "dB", 0.5f));

    // -------------------------------------------------------------- tone/output
    // Default 2811 Hz is the old default (9000 on the inverted mapping) as it
    // actually sounded; the range reaches 1000 Hz so every old setting maps in.
    add (floatParam  (id::tone,       "Tone",     1000.f, 16000.f, 2811.f, "Hz", 0.5f));
    add (floatParam  (id::drive,      "Drive",    0.f, 100.f, 0.f,       "%",  0.5f));
    add (floatParam  (id::room,       "Room",     0.f, 100.f, 20.f,      "%",  0.5f));
    add (floatParam  (id::width,      "Width",    0.f, 100.f, 60.f,      "%",  0.5f));
    add (floatParam  (id::output,     "Output",   -24.f, 6.f,  -6.f,      "dB", 0.5f));

    // ------------------------------------------------------- amp, cab, effects
    // Amp Off and both mixes at 0 leave the sound untouched.
    add (choiceParam (id::ampModel,   "Amp",          { "Off", "Clean", "Crunch", "Lead" }, 0));
    add (floatParam  (id::ampGain,    "Amp Gain",     0.f, 100.f, 40.f,  "%",  1.f));
    add (floatParam  (id::ampBass,    "Amp Bass",     0.f, 100.f, 50.f,  "%",  1.f));
    add (floatParam  (id::ampMid,     "Amp Mid",      0.f, 100.f, 50.f,  "%",  1.f));
    add (floatParam  (id::ampTreble,  "Amp Treble",   0.f, 100.f, 50.f,  "%",  1.f));
    add (floatParam  (id::ampLevel,   "Amp Level",    -24.f, 12.f, 0.f,  "dB", 1.f));
    add (choiceParam (id::cab,        "Cabinet",      { "Off", "1x12", "2x12", "4x12" }, 2));
    add (floatParam  (id::chorusMix,  "Chorus Mix",   0.f, 100.f, 0.f,   "%",  1.f));
    add (floatParam  (id::chorusRate, "Chorus Rate",  0.1f, 5.f, 0.8f,   "Hz", 0.5f));
    add (floatParam  (id::delayMix,   "Delay Mix",    0.f, 100.f, 0.f,   "%",  1.f));
    add (floatParam  (id::delayTime,  "Delay Time",   20.f, 1500.f, 380.f, "ms", 0.5f));
    add (floatParam  (id::delayFeedback, "Delay Feedback", 0.f, 90.f, 30.f, "%", 1.f));

    return layout;
}

} // namespace fw

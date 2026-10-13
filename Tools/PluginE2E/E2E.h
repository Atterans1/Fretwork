#pragma once
// PluginE2E: learn a real recording, play it through any VST3 instrument and
// verify the playback against the recording.
//
// Audio.cpp     - MP3/WAV/FLAC decode, WAV write, tab and MIDI reference parsing.
// Learner.cpp   - joint reference + audio alignment.
// Verifier.cpp  - independent measurement of the source and the render, using
//                 different analysis methods from the learner.
// PluginE2E.cpp - CLI: decode, learn, host the plugin, render, verify, report.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_graphics/juce_graphics.h>

#include <vector>

namespace e2e
{

struct Pcm
{
    std::vector<float> x;      // mono
    double fs = 0.0;
};

/** MP3 (or any registered format) -> mono PCM at targetFs. Low-passes before
    the Lagrange resampler so nothing above the new Nyquist aliases. */
Pcm decodeToMono (const juce::File& file, double targetFs, juce::String& formatName, double& sourceFs, int& sourceChannels);

void writeWav (const juce::File& file, const std::vector<float>& left, const std::vector<float>& right, double fs);

struct TabNote
{
    int index = 0;      // order in the tab
    int line = 0;       // tab system (1-based)
    int column = 0;
    int stringNo = 0;   // 1 = high e ... 6 = low E (tab convention)
    int fret = 0;
    int midi = 0;
    char technique = 0; // '\\' '/' 'p' 'h' when the note is reached legato, else 0
};

/** ASCII tab with any number of strings per system (lines like "G|--9--|").
    openMidi lists the open-string pitches from the TOP tab line down; empty
    means: take the tuning from the string count (4 = bass E1 A1 D2 G2,
    5 = bass B0.., 6 = guitar standard, 7 = 7-string with low B1). */
std::vector<TabNote> parseTab (const juce::String& text, const std::vector<int>& openMidi, juce::String& error);

/** Reference note order from a MIDI file (all tracks, chords in pitch order).
    Only the pitch sequence is used; the learner takes timing from the audio. */
std::vector<TabNote> parseMidiReference (const juce::File& file, juce::String& error);

/** "E2 A2 D3 G3 B3 E4" (low to high) -> MIDI numbers; empty on error. */
std::vector<int> parseTuning (const juce::String& text);

struct LearnedNote
{
    int    tabIndex = 0;
    int    tabMidi = 0;        // tab pitch + learned transpose
    int    midi = 0;           // pitch the learner commits to
    double onset = 0.0;        // seconds in the source
    double duration = 0.0;
    double confidence = 0.0;   // audio dominance of the committed pitch at the attack
    bool   skipped = false;    // the audio vetoed this tab note
    bool   corrected = false;  // the audio moved the pitch off the tab's value
};

struct LearnResult
{
    int    transpose = 0;      // semitones, learned from the audio
    double tuningCents = 0.0;  // reference offset from A440, learned from the audio
    double score = 0.0;
    double runnerUpScore = 0.0;
    int    runnerUpTranspose = 0;
    std::vector<LearnedNote> notes;
};

LearnResult learn (const Pcm& audio, const std::vector<TabNote>& tab);

// ------------------------------------------------------------------ verifier
struct VerifyNote
{
    int    tabIndex = 0;
    int    midi = 0;               // committed pitch
    double scheduled = 0.0;        // learned onset (source time)
    double renderOnset = -1.0;     // detected in the render, source time
    double sourceOnset = -1.0;     // detected in the source independently
    double renderPitch = 0.0;      // fractional MIDI, YIN on the render
    double sourcePitch = 0.0;      // fractional MIDI, HPS on the source
    double renderDur = 0.0, sourceDur = 0.0;
    bool   pitchOk = false, onsetOk = false, durOk = false;
    bool   heardInMix = false;     // diagnostic: the played pitch is among the strongest semitones in the source (chords included)
};

struct VerifyResult
{
    std::vector<VerifyNote> notes;
    double pitchRate = 0.0, onsetRate = 0.0, durRate = 0.0;
    double heardRate = 0.0;        // diagnostic, never gated
    double meanOnsetErrMs = 0.0, maxOnsetErrMs = 0.0, meanAbsOnsetErrMs = 0.0;
    double meanDurErrMs = 0.0, maxDurErrMs = 0.0;
    int    octaveErrors = 0;
    double sourceCentsFirstHalf = 0.0, sourceCentsSecondHalf = 0.0;
};

struct Gates
{
    double pitchTolSemis = 1.0;    // render vs source pitch
    double onsetTolMs = 50.0;      // render vs source onset
    double durTolMs = 50.0;        // reported, not gated
    double minRate = 0.8;          // fraction of notes that must pass pitch and onset
};

/** render is aligned to the source: render sample 0 == source time offset. */
VerifyResult verify (const Pcm& source, const std::vector<float>& render, double renderFs, double renderOffsetSec,
                     const std::vector<LearnedNote>& notes, const Gates& gates);

void drawPianoRoll (const juce::File& png, const Pcm& source, double t0, double t1,
                    const std::vector<LearnedNote>& notes, const VerifyResult& v);

} // namespace e2e

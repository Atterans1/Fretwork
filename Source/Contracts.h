#pragma once
namespace fw {
// Storage maximum: every per-string array is sized with kMaxStrings. The ACTIVE
// course count travels separately (PerfParams::activeStringCount) so a 7-string
// guitar raises it while six-string instances never process a phantom course.
constexpr int kMaxStrings = 7;
constexpr int kNumStrings = 6;                       // index 0 = low E ... 5 = high E (UI "string 1" = index 5)
constexpr int kLowestPlayable = 38;                  // MIDI 12..37 are keyswitches, >=38 are playable
// Guitar is written an octave above where it sounds: incoming MIDI is the
// written note, so MIDI 52 (written E3) plays the low E string (E2, 82 Hz).
constexpr int kWrittenOctave = 12;
enum class Excitation : int { Pick, Finger, Thumb };
enum class Damping    : int { Open, PalmMute, Dead };
enum class Transition : int { Normal, Slide, Legato, Bend };
enum class Harmonic   : int { Off, Natural };
enum class PlayMode   : int { Poly, Strum, Arp, Seq };
// 12-string pairs: the four lower courses (E A D G, indices 0..3) carry an
// octave course; B and high E (indices 4,5) pair in unison. 0 = no courses.
enum class GuitarType : int { SixString, TwelveString, Baritone, NylonTwelve, SevenString };
// Legato = hammer-on / pull-off / slide target (no full pluck).
// StrokeNoise = pick scraping across the strings in a strum (dir, durSec).
// Squeak = fingertip sliding along a string (string, dir, durSec).
enum class EventType  : int { Pluck, Legato, Release, StrokeNoise, Squeak };
struct Articulation { Excitation exc{}; Damping damp{}; Transition trans{}; Harmonic harm{}; bool tremolo{false}; };
struct StringEvent {
  int   sampleOffset = 0;      // offset inside current block, 0..numSamples-1
  int   string = 0;            // 0..5
  EventType type = EventType::Pluck;
  float pitch = 40.f;          // MIDI (float): pitch of the fretted string fundamental (capo+transpose included). For natural harmonics: OPEN string pitch.
  float pitchFrom = -1.f;      // Slide start pitch; <0 = none
  float velocity = 0.8f;       // 0..1
  int   harmonicN = 0;         // 0 = none; 2..5 = natural-harmonic partial
  float releaseSec = 0.4f;      // used by Release events (T60 of the damped string)
  int   dir = 0;               // StrokeNoise: +1 down-stroke, -1 up-stroke; Squeak: +1 shifting up the neck
  float durSec = 0.f;          // StrokeNoise / Squeak duration
  Articulation art{};
};
struct MidiMsg { int offset; unsigned char status, d1, d2; };
struct Transport { bool hostPlaying=false; double ppq=0.0; double bpm=100.0; bool hostValid=false; };
}

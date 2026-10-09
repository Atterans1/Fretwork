#pragma once
namespace fw {
constexpr int kNumStrings = 6;                       // index 0 = low E ... 5 = high E (UI "string 1" = index 5)
constexpr int kLowestPlayable = 38;                  // D2; MIDI 12..37 are keyswitches, >=38 are playable
enum class Excitation : int { Pick, Finger, Thumb };
enum class Damping    : int { Open, PalmMute, Dead };
enum class Transition : int { Normal, Slide, Legato, Bend };
enum class Harmonic   : int { Off, Natural };
enum class PlayMode   : int { Poly, Strum, Arp, Seq };
enum class EventType  : int { Pluck, Legato, Release };   // Legato = hammer-on / pull-off / slide target (no full pluck)
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
  Articulation art{};
};
struct MidiMsg { int offset; unsigned char status, d1, d2; };
struct Transport { bool hostPlaying=false; double ppq=0.0; double bpm=100.0; bool hostValid=false; };
}

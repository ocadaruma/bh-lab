// Chord generation logic. Free of any plugin API types so it can be unit tested on its
// own.
#pragma once

#include <cstdint>

namespace bhl
{

enum class Quality : int
{
  Major = 0,
  Minor,
  Maj7,
  Min7,
  Six,
  Dom7,
  Sus4,
  Count
};

enum class Inversion : int
{
  Root = 0,
  First,
  Second,
  Third,
  OctaveDouble,
  Count
};

// A close-position voicing uses at most five notes (four plus a doubled root).
static constexpr int kMaxVoices = 5;

struct Chord
{
  int notes[kMaxVoices] = {};
  int count = 0;
};

// Builds a close-position voicing for `rootKey` (a MIDI note number). Voices that fall
// outside 0..127 are dropped rather than wrapped.
Chord buildChord(int rootKey, Quality quality, Inversion inversion);

// How many notes the chord type has: three for the triads, four for the sevenths and 6.
int chordToneCount(Quality quality);

// A three-note chord has no third inversion -- there is no fourth voice to put in the
// bass -- so that setting is not offered for one.
bool inversionAvailable(Quality quality, Inversion inversion);

// Chord type in the compact notation players read at a glance: maj, m, maj7, m7, 6, 7,
// sus4.
const char *chordTypeName(Quality quality);

// The suffix used when the chord is written out with its root, so Major is empty and the
// chord reads "E" rather than "Emaj".
const char *chordTypeSymbol(Quality quality);

const char *inversionName(Inversion inversion);       // "1st inversion"
const char *inversionShortName(Inversion inversion);  // "1st", for drawing on a key

// What a key in the control octave does. `semitoneOffset` is 0..11 relative to the
// control zone root.
enum class ControlAction : int
{
  None = 0,
  SetQuality,
  SetInversion
};

struct ControlHit
{
  ControlAction action = ControlAction::None;
  int value = 0;  // Quality or Inversion, depending on `action`.
};

// White keys select quality, black keys select inversion.
ControlHit mapControlKey(int semitoneOffset);

}  // namespace bhl

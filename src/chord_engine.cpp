#include "chord_engine.h"

#include <algorithm>

namespace bhl
{

namespace
{

struct QualityDef
{
  const char *name;
  const char *symbol;
  int intervals[4];
  int count;
};

// Indexed by Quality. The name is the compact notation shown in the interface; the
// symbol is what follows the root when the chord is written out.
constexpr QualityDef kQualities[] = {
    {"maj", "", {0, 4, 7, 0}, 3},        {"m", "m", {0, 3, 7, 0}, 3},
    {"maj7", "maj7", {0, 4, 7, 11}, 4},  {"m7", "m7", {0, 3, 7, 10}, 4},
    {"6", "6", {0, 4, 7, 9}, 4},         {"7", "7", {0, 4, 7, 10}, 4},
    {"sus4", "sus4", {0, 5, 7, 0}, 3},
};

static_assert(sizeof(kQualities) / sizeof(kQualities[0]) == static_cast<size_t>(Quality::Count),
              "quality table out of sync with the enum");

constexpr const char *kInversionNames[] = {"Root position", "1st inversion", "2nd inversion",
                                           "3rd inversion", "Octave shift"};

// Short enough to print on a black key.
constexpr const char *kInversionShortNames[] = {"Root", "1st", "2nd", "3rd", "Oct"};

static_assert(sizeof(kInversionNames) / sizeof(kInversionNames[0]) ==
                  static_cast<size_t>(Inversion::Count),
              "inversion table out of sync with the enum");

bool inRange(int q, int count)
{
  return q >= 0 && q < count;
}

}  // namespace

const char *chordTypeName(Quality quality)
{
  const int q = static_cast<int>(quality);
  return inRange(q, static_cast<int>(Quality::Count)) ? kQualities[q].name : "?";
}

const char *chordTypeSymbol(Quality quality)
{
  const int q = static_cast<int>(quality);
  return inRange(q, static_cast<int>(Quality::Count)) ? kQualities[q].symbol : "";
}

int chordToneCount(Quality quality)
{
  const int q = static_cast<int>(quality);
  return inRange(q, static_cast<int>(Quality::Count)) ? kQualities[q].count : 0;
}

bool inversionAvailable(Quality quality, Inversion inversion)
{
  // The octave shift is not an inversion and applies to any chord.
  if (inversion == Inversion::OctaveDouble) return true;
  return static_cast<int>(inversion) < chordToneCount(quality);
}

const char *inversionName(Inversion inversion)
{
  const int i = static_cast<int>(inversion);
  return inRange(i, static_cast<int>(Inversion::Count)) ? kInversionNames[i] : "?";
}

const char *inversionShortName(Inversion inversion)
{
  const int i = static_cast<int>(inversion);
  return inRange(i, static_cast<int>(Inversion::Count)) ? kInversionShortNames[i] : "?";
}

Chord buildChord(int rootKey, Quality quality, Inversion inversion)
{
  Chord chord;

  const int q = static_cast<int>(quality);
  if (!inRange(q, static_cast<int>(Quality::Count))) return chord;

  const QualityDef &def = kQualities[q];

  int voices[kMaxVoices] = {};
  int n = def.count;
  for (int i = 0; i < n; ++i) voices[i] = rootKey + def.intervals[i];

  if (inversion == Inversion::OctaveDouble)
  {
    voices[n] = rootKey + 12;
    ++n;
  }
  else
  {
    // A triad has no 3rd inversion; fall back to the highest one it does have.
    int lift = std::min(static_cast<int>(inversion), n - 1);
    for (int i = 0; i < lift; ++i) voices[i] += 12;
  }

  std::sort(voices, voices + n);

  for (int i = 0; i < n; ++i)
  {
    if (voices[i] < 0 || voices[i] > 127) continue;
    chord.notes[chord.count++] = voices[i];
  }

  return chord;
}

ControlHit mapControlKey(int semitoneOffset)
{
  switch (semitoneOffset)
  {
    // White keys: chord quality.
    case 0:
      return {ControlAction::SetQuality, static_cast<int>(Quality::Major)};
    case 2:
      return {ControlAction::SetQuality, static_cast<int>(Quality::Minor)};
    case 4:
      return {ControlAction::SetQuality, static_cast<int>(Quality::Maj7)};
    case 5:
      return {ControlAction::SetQuality, static_cast<int>(Quality::Min7)};
    case 7:
      return {ControlAction::SetQuality, static_cast<int>(Quality::Six)};
    case 9:
      return {ControlAction::SetQuality, static_cast<int>(Quality::Dom7)};
    case 11:
      return {ControlAction::SetQuality, static_cast<int>(Quality::Sus4)};

    // Black keys: inversion.
    case 1:
      return {ControlAction::SetInversion, static_cast<int>(Inversion::Root)};
    case 3:
      return {ControlAction::SetInversion, static_cast<int>(Inversion::First)};
    case 6:
      return {ControlAction::SetInversion, static_cast<int>(Inversion::Second)};
    case 8:
      return {ControlAction::SetInversion, static_cast<int>(Inversion::Third)};
    case 10:
      return {ControlAction::SetInversion, static_cast<int>(Inversion::OctaveDouble)};

    default:
      return {};
  }
}

}  // namespace bhl

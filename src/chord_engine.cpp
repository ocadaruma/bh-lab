#include "chord_engine.h"

#include <algorithm>

namespace bhl
{

namespace
{

struct FamilyDef
{
  const char *name;
  const char *shortName;
  int offsets[kScaleNoteCount];
  int named[kStackedChordNotes];
};

// The four C-root chord scales, from the Barry Harris method (see design.md). `named`
// is the 4-note chord that belongs to the family; all four families share the
// diminished chord {0, 3, 6, 9} (a Ddim in the C-root keys).
constexpr FamilyDef kFamilies[static_cast<int>(ScaleFamily::Count)] = {
    {"major 6th dim", "6dim",   {0, 2, 4, 5, 7, 8, 9, 11}, {0, 4, 7, 9}},
    {"minor 6th dim", "m6dim",  {0, 2, 3, 5, 7, 8, 9, 11}, {0, 3, 7, 9}},
    {"7th dim",       "7dim",   {0, 2, 4, 5, 7, 8, 10, 11}, {0, 4, 7, 10}},
    {"7th ♭5 dim",    "7♭5dim", {0, 2, 4, 5, 6, 8, 10, 11}, {0, 4, 6, 10}},
};

constexpr int kDimIntervals[kStackedChordNotes] = {0, 3, 6, 9};

static_assert(sizeof(kFamilies) / sizeof(kFamilies[0]) == static_cast<size_t>(ScaleFamily::Count),
              "family table out of sync with the enum");

constexpr const char *kVoicingNames[static_cast<int>(VoicingType::Count)] = {
    "Close", "Drop2", "Drop3", "Drop2&4"};

static_assert(sizeof(kVoicingNames) / sizeof(kVoicingNames[0]) ==
                  static_cast<size_t>(VoicingType::Count),
              "voicing table out of sync with the enum");

bool inRange(int v, int count)
{
  return v >= 0 && v < count;
}

// A 12-bit pitch class mask shifted by `by` semitones.
uint16_t rotateMask(uint16_t mask, int by)
{
  by %= 12;
  if (by < 0) by += 12;
  return static_cast<uint16_t>(((mask << by) | (mask >> (12 - by))) & 0xfff);
}

uint16_t maskOfIntervals(const int *intervals, int count)
{
  uint16_t mask = 0;
  for (int i = 0; i < count; ++i) mask |= 1u << (intervals[i] % 12);
  return mask;
}

// The family's 4-note chord at a given root, as a pitch class mask.
uint16_t namedMaskAt(ScaleFamily family, int rootPc)
{
  const FamilyDef &def = kFamilies[static_cast<int>(family)];
  return rotateMask(maskOfIntervals(def.named, kStackedChordNotes), rootPc % 12);
}

// True when every pressed class is in the chord.
bool covers(uint16_t mask, uint16_t chord)
{
  return (mask & chord) == mask;
}

}  // namespace

const int *scaleOffsets(ScaleFamily family)
{
  const int f = static_cast<int>(family);
  return inRange(f, static_cast<int>(ScaleFamily::Count)) ? kFamilies[f].offsets : nullptr;
}

uint16_t scalePitchClasses(ScaleFamily family)
{
  const int f = static_cast<int>(family);
  return inRange(f, static_cast<int>(ScaleFamily::Count)) ? maskOfIntervals(kFamilies[f].offsets,
                                                                         kScaleNoteCount)
                                                          : 0;
}

uint16_t namedChordPitchClasses(ScaleFamily family)
{
  const int f = static_cast<int>(family);
  return inRange(f, static_cast<int>(ScaleFamily::Count))
             ? maskOfIntervals(kFamilies[f].named, kStackedChordNotes)
             : 0;
}

uint16_t dimChordPitchClasses(int rootPc)
{
  return rotateMask(maskOfIntervals(kDimIntervals, kStackedChordNotes), rootPc % 12);
}

bool selectScale(uint16_t pitchClassMask, ScaleSelection *out)
{
  if (!out || !pitchClassMask) return false;

  // The lowest pressed class is the chord's root: a stacked chord is read from its
  // bass note, and a partial press is most informative with the root named. A named
  // chord match therefore outranks the shared diminished chord, which outranks a
  // match at some other key. Families are tried in preference order within each tier.
  int minPc = 0;
  while (!((pitchClassMask >> minPc) & 1u)) ++minPc;

  const int count = static_cast<int>(ScaleFamily::Count);

  // 1. A named chord rooted at the lowest pressed note.
  for (int f = 0; f < count; ++f)
  {
    if (covers(pitchClassMask, namedMaskAt(static_cast<ScaleFamily>(f), minPc)))
    {
      out->family = static_cast<ScaleFamily>(f);
      out->keyPc = minPc;
      return true;
    }
  }

  // 2. The dim chord rooted at the lowest pressed note. It belongs to every family,
  // so the preference order picks the first one and the family key is root - 2.
  const uint16_t dimAtMin = dimChordPitchClasses(minPc);
  if (covers(pitchClassMask, dimAtMin))
  {
    out->family = ScaleFamily::Major6;
    out->keyPc = (minPc + 10) % 12;
    return true;
  }

  // 3. A named chord at any key.
  for (int f = 0; f < count; ++f)
  {
    for (int keyPc = 0; keyPc < 12; ++keyPc)
    {
      if (covers(pitchClassMask, namedMaskAt(static_cast<ScaleFamily>(f), keyPc)))
      {
        out->family = static_cast<ScaleFamily>(f);
        out->keyPc = keyPc;
        return true;
      }
    }
  }

  // 4. The dim chord at any key.
  for (int f = 0; f < count; ++f)
  {
    for (int dimRoot = 0; dimRoot < 12; ++dimRoot)
    {
      if (covers(pitchClassMask, dimChordPitchClasses(dimRoot)))
      {
        out->family = static_cast<ScaleFamily>(f);
        out->keyPc = (dimRoot + 10) % 12;
        return true;
      }
    }
  }

  return false;
}

Chord buildStackedChord(const ScaleSelection &sel, int topKey, VoicingType voicing)
{
  Chord chord;

  if (!inRange(static_cast<int>(sel.family), static_cast<int>(ScaleFamily::Count)))
    return chord;
  if (topKey < 0 || topKey > 127) return chord;
  const int pc = topKey % 12;

  // Which stacked chord the pressed note belongs to: named or diminished. The two
  // partitions of a scale's eight notes are disjoint, so at most one can contain it.
  uint16_t named = namedMaskAt(sel.family, sel.keyPc);
  uint16_t covering;
  if (covers(1u << pc, named))
    covering = named;
  else if (covers(1u << pc, dimChordPitchClasses((sel.keyPc + 2) % 12)))
    covering = dimChordPitchClasses((sel.keyPc + 2) % 12);
  else
    return chord;

  int voices[kStackedChordNotes] = {};
  int count = 0;
  for (int c = 0; c < 12; ++c)
  {
    if (!((covering >> c) & 1u)) continue;

    // The pressed note is the top voice; every other voice is the same class strictly
    // below it, which keeps the stack close-position.
    const int voice = (c == pc) ? topKey : topKey - ((pc - c + 12) % 12);
    if (voice < 0 || voice > 127) continue;
    voices[count++] = voice;
  }

  // Drop voicings move specific voices, counted from the top, down an octave. When
  // the stack sits low, a moved voice falls below key 0 and is dropped rather than
  // wrapped.
  if (voicing != VoicingType::Close && count >= 2)
  {
    // Voice indices ordered by pitch, highest first.
    int order[kStackedChordNotes] = {};
    for (int i = 0; i < count; ++i) order[i] = i;
    std::sort(order, order + count,
              [&](int a, int b) { return voices[a] > voices[b]; });

    int dropPos[kStackedChordNotes] = {0, 1, 2, 3};
    int dropCount = 0;
    switch (voicing)
    {
      case VoicingType::Drop2:
        dropPos[0] = 1;
        dropCount = 1;
        break;  // 2nd from the top
      case VoicingType::Drop3: dropPos[0] = 2; dropCount = 1; break;  // 3rd from the top
      case VoicingType::Drop2_4:
        dropPos[0] = 1;
        dropPos[1] = 3;
        dropCount = 2;
        break;                                                  // 2nd and 4th
      default: break;
    }

    bool removed[kStackedChordNotes] = {};
    for (int i = 0; i < dropCount; ++i)
    {
      const int idx = order[dropPos[i]];
      if (voices[idx] - 12 < 0)
        removed[idx] = true;
      else
        voices[idx] -= 12;
    }
    int kept = 0;
    for (int i = 0; i < count; ++i)
      if (!removed[i]) voices[kept++] = voices[i];
    count = kept;
  }

  std::sort(voices, voices + count);
  for (int i = 0; i < count; ++i) chord.notes[i] = voices[i];
  chord.count = count;
  return chord;
}

const char *scaleFamilyName(ScaleFamily family)
{
  const int f = static_cast<int>(family);
  return inRange(f, static_cast<int>(ScaleFamily::Count)) ? kFamilies[f].name : "?";
}

const char *scaleFamilyShortName(ScaleFamily family)
{
  const int f = static_cast<int>(family);
  return inRange(f, static_cast<int>(ScaleFamily::Count)) ? kFamilies[f].shortName : "?";
}

const char *voicingTypeName(VoicingType voicing)
{
  const int v = static_cast<int>(voicing);
  return inRange(v, static_cast<int>(VoicingType::Count)) ? kVoicingNames[v] : "?";
}

}  // namespace bhl

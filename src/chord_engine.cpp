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
  return buildBorrowedChord(sel, topKey, voicing, kNoBorrow);
}

// ---------------------------------------------------------------------------
// Borrowing
// ---------------------------------------------------------------------------

namespace
{

// The one-scale-degree shift a borrow direction applies: the adjacent chord in the
// scale, below or above.
int degreeShift(BorrowDir dir)
{
  switch (dir)
  {
    case BorrowDir::Prev: return -1;
    case BorrowDir::Next: return 1;
    default: return 0;
  }
}

// The root-position stack of degree `degree`, ascending: [root, 3rd, 5th, 7th]. The
// root is the degree's own scale note; the other three classes come from the chord the
// degree belongs to -- the family's named chord on even degrees, the diminished chord
// on odd degrees -- and always sit above the root, within its octave.
void rootPositionStack(const FamilyDef &def, int keyPc, int degree, int classes[4])
{
  const int root = (keyPc + def.offsets[degree]) % 12;
  const uint16_t mask = (degree % 2 == 0) ? rotateMask(maskOfIntervals(def.named,
                                                                       kStackedChordNotes),
                                                       keyPc % 12)
                                          : rotateMask(
                                                maskOfIntervals(kDimIntervals, kStackedChordNotes),
                                                (keyPc + 2) % 12);

  classes[0] = root;
  int idx = 1;
  for (int c = root + 1; idx < kStackedChordNotes; ++c)
    if ((mask >> (c % 12)) & 1u) classes[idx++] = c % 12;
}

void applyDropVoicing(int voices[], int &count, VoicingType voicing);

// The stack built from four pitch classes with `topKey` on top and the requested
// voicing applied. Any voice sharing topKey's pitch class sounds on topKey itself: a
// borrowed voice may coincide with the top, in which case two voices sit on one key
// (the plugin's voice pool ref-counts the single physical note) and a drop voicing
// moves one of them like any other voice.
Chord stackWithVoicing(const int classes[4], int topKey, VoicingType voicing)
{
  Chord chord;
  if (topKey < 0 || topKey > 127) return chord;
  const int pc = topKey % 12;

  int voices[kStackedChordNotes] = {};
  int count = 0;
  for (int j = 0; j < kStackedChordNotes; ++j)
  {
    const int c = classes[j] % 12;
    const int voice = (c == pc) ? topKey : topKey - ((pc - c + 12) % 12);
    if (voice < 0 || voice > 127) continue;
    voices[count++] = voice;
  }

  applyDropVoicing(voices, count, voicing);

  std::sort(voices, voices + count);
  for (int i = 0; i < count; ++i) chord.notes[i] = voices[i];
  chord.count = count;
  return chord;
}

// Drop voicings move specific voices, counted from the top, down an octave. When the
// stack sits low, a moved voice falls below key 0 and is dropped rather than wrapped.
void applyDropVoicing(int voices[], int &count, VoicingType voicing)
{
  if (voicing == VoicingType::Close || count < 2) return;

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
      break;  // 2nd and 4th
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

}  // namespace

void rootPositionChordClasses(const ScaleSelection &sel, int degree, int classes[4])
{
  const int f = static_cast<int>(sel.family);
  if (!inRange(f, static_cast<int>(ScaleFamily::Count))) return;
  rootPositionStack(kFamilies[f], sel.keyPc, inRange(degree, kScaleNoteCount) ? degree : 0,
                    classes);
}

void borrowedChordClasses(const ScaleSelection &sel, int degree, const BorrowSettings &borrow,
                          int classes[4])
{
  const int f = static_cast<int>(sel.family);
  if (!inRange(f, static_cast<int>(ScaleFamily::Count))) return;
  const int d = inRange(degree, kScaleNoteCount) ? degree : 0;

  int rootPos[kScaleNoteCount][kStackedChordNotes];
  for (int i = 0; i < kScaleNoteCount; ++i) rootPositionStack(kFamilies[f], sel.keyPc, i,
                                                              rootPos[i]);

  // Voice j (0..3 from the root) is taken from the chord one scale degree away when
  // its switch is set: the adjacent chord's voice in the same stack position.
  const int shift[kStackedChordNotes] = {degreeShift(borrow.fourth),
                                         degreeShift(borrow.third),
                                         degreeShift(borrow.second),
                                         degreeShift(borrow.top)};
  for (int j = 0; j < kStackedChordNotes; ++j)
    classes[j] = rootPos[(d + shift[j] + kScaleNoteCount) % kScaleNoteCount][j];
}

Chord buildDegreeChord(const ScaleSelection &sel, int degree, int topKey, VoicingType voicing,
                       const BorrowSettings &borrow)
{
  const int f = static_cast<int>(sel.family);
  if (!inRange(f, static_cast<int>(ScaleFamily::Count))) return Chord();
  if (!inRange(degree, kScaleNoteCount)) return Chord();

  int classes[kStackedChordNotes];
  borrowedChordClasses(sel, degree, borrow, classes);
  return stackWithVoicing(classes, topKey, voicing);
}

Chord buildBorrowedChord(const ScaleSelection &sel, int topKey, VoicingType voicing,
                         const BorrowSettings &borrow, int *outDegree)
{
  if (outDegree) *outDegree = -1;

  const int f = static_cast<int>(sel.family);
  if (!inRange(f, static_cast<int>(ScaleFamily::Count))) return Chord();
  if (topKey < 0 || topKey > 127) return Chord();
  const int pc = topKey % 12;

  // The degree whose borrowed top note is the pressed class. The eight borrowed tops
  // are eight distinct pitch classes, so at most one named and one diminished degree
  // can match; the named one wins, mirroring selectScale's named-over-dim tiers.
  for (int parity = 0; parity < 2; ++parity)
    for (int d = parity; d < kScaleNoteCount; d += 2)
    {
      int classes[kStackedChordNotes];
      borrowedChordClasses(sel, d, borrow, classes);
      if (classes[kStackedChordNotes - 1] % 12 == pc)
      {
        if (outDegree) *outDegree = d;
        return stackWithVoicing(classes, topKey, voicing);
      }
    }

  return Chord();  // Outside the scale: the trigger key passes through.
}

Chord buildOverrideChord(const ScaleSelection &sel, int degree, int triggerKey, int pressKey,
                         VoicingType voicing)
{
  Chord chord;

  const int f = static_cast<int>(sel.family);
  if (!inRange(f, static_cast<int>(ScaleFamily::Count))) return chord;
  if (!inRange(degree, kScaleNoteCount)) return chord;
  if (triggerKey < 0 || triggerKey > 127 || pressKey < 0 || pressKey > 127) return chord;

  int u[kStackedChordNotes];
  rootPositionStack(kFamilies[f], sel.keyPc, degree, u);

  const int T = triggerKey % 12;
  const int l = pressKey % 12;

  // The pressed note selects the revert; it must be one of the degree's un-borrowed
  // voices. The chord itself is re-voiced in place, as the un-borrowed stack
  // at the trigger key's octave: each voice sits where the borrowed form had it, and
  // the top goes to the side of the trigger key the un-borrowed top belongs to (below
  // for a next borrow, above for a previous one). The pressed key's own octave is not
  // moved to -- the chord keeps the octave it was triggered in. When the press sits in
  // that octave, its note is the chord's own voice.
  int pressedSlot = -1;
  for (int j = 0; j < kStackedChordNotes; ++j)
    if (u[j] % 12 == l) pressedSlot = j;
  if (pressedSlot < 0) return chord;  // Not an un-borrowed voice of this degree.

  int voices[kStackedChordNotes] = {};
  int count = 0;
  for (int j = 0; j < kStackedChordNotes; ++j)
  {
    int key;
    if (j == kStackedChordNotes - 1)
    {
      const int d = ((u[j] - T) % 12 + 12) % 12;
      key = triggerKey + (d > 6 ? d - 12 : d);
    }
    else if (u[j] % 12 == T)
      key = triggerKey - 12;
    else
      key = triggerKey - ((T - u[j] + 12) % 12);
    if (key < 0 || key > 127) continue;
    voices[count++] = key;
  }

  applyDropVoicing(voices, count, voicing);

  std::sort(voices, voices + count);
  for (int i = 0; i < count; ++i) chord.notes[i] = voices[i];
  chord.count = count;
  return chord;
}

bool isCancelledBorrowVoice(const ScaleSelection &sel, int degree, int pc,
                            const BorrowSettings &borrow)
{
  const int f = static_cast<int>(sel.family);
  if (!inRange(f, static_cast<int>(ScaleFamily::Count))) return false;
  if (!inRange(degree, kScaleNoteCount)) return false;

  int classes[kStackedChordNotes];
  rootPositionStack(kFamilies[f], sel.keyPc, degree, classes);

  const BorrowDir dir[kStackedChordNotes] = {borrow.fourth, borrow.third, borrow.second,
                                             borrow.top};
  for (int j = 0; j < kStackedChordNotes; ++j)
    if (dir[j] != BorrowDir::None && classes[j] % 12 == pc) return true;

  return false;
}

int contraryNote(const ScaleSelection &sel, int sessionKey, int bottomKey, int pressKey)
{
  const int f = static_cast<int>(sel.family);
  if (!inRange(f, static_cast<int>(ScaleFamily::Count))) return -1;
  if (sessionKey < 0 || sessionKey > 127 || bottomKey < 0 || bottomKey > 127 ||
      pressKey < 0 || pressKey > 127)
    return -1;
  if (pressKey <= sessionKey) return -1;

  const uint16_t pcs = rotateMask(scalePitchClasses(sel.family), sel.keyPc);
  auto inScale = [&](int key) { return (pcs >> (key % 12)) & 1u; };
  if (!inScale(sessionKey) || !inScale(pressKey)) return -1;

  // The offset: the scale steps between the session key and the pressed key,
  // walking the scale up. The loop always terminates at pressKey, an in-scale note.
  int steps = 0;
  for (int k = sessionKey; k < pressKey;)
  {
    int next = k + 1;
    while (!inScale(next)) ++next;
    k = next;
    if (++steps > 128) return -1;  // Safety: eight scale notes per octave.
  }

  // Walk `steps` down the scale from the session chord's bottom note. Only scale
  // notes are ever visited, so the contrary note is always in the scale.
  int note = bottomKey;
  for (int i = 0; i < steps; ++i)
  {
    int prev = note - 1;
    while (prev >= 0 && !inScale(prev)) --prev;
    if (prev < 0) return -1;  // Below the bottom of the keyboard: no contrary note.
    note = prev;
  }
  return note;
}

const char *borrowDirName(BorrowDir dir)
{
  static const char *names[static_cast<int>(BorrowDir::Count)] = {"None", "Prev", "Next"};
  const int d = static_cast<int>(dir);
  return inRange(d, static_cast<int>(BorrowDir::Count)) ? names[d] : "?";
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

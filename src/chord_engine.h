// Barry Harris's stacked diminished scales.
//
// Free of any plugin API types so it can be unit tested on its own.
#pragma once

#include <cstdint>

namespace bhl
{

// The four chord scales of the Barry Harris method. Each is an eight-note diminished
// scale; stacked one note at a time (skipping a note between voices) it yields 4-note
// chords alternating between the family's own chord and the diminished chord, each in
// its four inversions.
enum class ScaleFamily : int
{
  Major6 = 0,
  Minor6,
  Seven,
  SevenB5,
  Count
};

// How a trigger chord is stacked. `Close` is close position with the pressed note
// as the top note of the chord; `Drop2` does the same but moves the second note
// from the top down an octave.
enum class VoicingType : int
{
  Close = 0,
  Drop2 = 1,
  Count
};

static constexpr int kScaleNoteCount = 8;
static constexpr int kStackedChordNotes = 4;

struct Chord
{
  int notes[kStackedChordNotes] = {};
  int count = 0;
};

// Semitone offsets of the family's eight-note scale, relative to its root.
const int *scaleOffsets(ScaleFamily family);

// The eight pitch classes of the family's scale, as a 12-bit mask relative to pc 0 (C).
uint16_t scalePitchClasses(ScaleFamily family);

// The 4-note chord that belongs to the family, as pitch classes relative to its root
// (e.g. {0, 4, 7, 9} for major 6th).
uint16_t namedChordPitchClasses(ScaleFamily family);

// The diminished chord shared by all four families, as pitch classes relative to its
// own root: {0, 3, 6, 9}.
uint16_t dimChordPitchClasses(int rootPc);

// The scale that a set of pressed notes identifies.
struct ScaleSelection
{
  ScaleFamily family = ScaleFamily::Major6;
  int keyPc = 0;  // Pitch class of the family root.
};

// Identifies the scale from a 12-bit mask of held pitch classes.
//
// Matching is tiered: a named chord rooted at the lowest pressed class, then the
// dim chord rooted there, then a named chord at any key, then the dim chord at any
// key -- families in preference order (major 6th > minor 6th > 7th > 7th ♭5)
// within each tier. Returns false when the mask matches no scale; in that case
// *out is left as it was, so the caller keeps the last valid selection.
bool selectScale(uint16_t pitchClassMask, ScaleSelection *out);

// The stacked chord of the selected scale in the requested voicing, with `topKey`
// as its top note. Returns a chord of count 0 if the note is not in the scale;
// voices that fall outside 0..127 are dropped rather than wrapped.
Chord buildStackedChord(const ScaleSelection &sel, int topKey, VoicingType voicing);

// "major 6th dim", for the interface.
const char *scaleFamilyName(ScaleFamily family);
// "6dim", "m6dim", "7dim", "7♭5dim", for a button.
const char *scaleFamilyShortName(ScaleFamily family);
// "Close", "Drop2", for a voicing button.
const char *voicingTypeName(VoicingType voicing);

}  // namespace bhl

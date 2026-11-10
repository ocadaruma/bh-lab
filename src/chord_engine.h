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
// as the top note of the chord. The drop voicings use the same stack but move
// specific voices, counted from the top, down an octave: `Drop2` moves the second
// voice, `Drop3` the third, `Drop2_4` the second and fourth.
enum class VoicingType : int
{
  Close = 0,
  Drop2 = 1,
  Drop3 = 2,
  Drop2_4 = 3,
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
// as its top note and no borrowing: the equivalent of buildBorrowedChord with
// kNoBorrow. Returns a chord of count 0 if the note is not in the scale; voices
// that fall outside 0..127 are dropped rather than wrapped.
Chord buildStackedChord(const ScaleSelection &sel, int topKey, VoicingType voicing);

// "major 6th dim", for the interface.
const char *scaleFamilyName(ScaleFamily family);
// "6dim", "m6dim", "7dim", "7♭5dim", for a button.
const char *scaleFamilyShortName(ScaleFamily family);
// "Close", "Drop2", "Drop3", "Drop2&4", for a voicing button.
const char *voicingTypeName(VoicingType voicing);

// One borrow switch's state: a voice of the chord is replaced by the corresponding
// voice of the adjacent chord in the scale, one scale degree below (Prev) or above
// (Next) it. None leaves the voice as is.
enum class BorrowDir : int
{
  None = 0,
  Prev = 1,
  Next = 2,
  Count
};

// The borrow switches for the top three voices of a chord. Borrowing transforms the
// stacked chords of a scale into new chords; the trigger rules then apply against
// the transformed chords.
struct BorrowSettings
{
  BorrowDir third = BorrowDir::None;   // Third voice from the top.
  BorrowDir second = BorrowDir::None;  // Second voice from the top.
  BorrowDir top = BorrowDir::None;     // Top voice.
};

static constexpr BorrowSettings kNoBorrow = BorrowSettings();

// The root-position stack of degree `degree` of the selection's scale (degree 0 is
// the scale's root), as pitch classes ascending: [root, 3rd, 5th, 7th], the 7th being
// the chord's top note. Even degrees are the family's chord in inversion; odd degrees
// are the diminished chord in inversion.
void rootPositionChordClasses(const ScaleSelection &sel, int degree, int classes[4]);

// The root-position stack of degree `degree` after the borrow switches are applied,
// in the same layout. A borrowed voice may share a pitch class with another voice.
void borrowedChordClasses(const ScaleSelection &sel, int degree, const BorrowSettings &borrow,
                          int classes[4]);

// The chord the trigger key `topKey` sounds: the degree whose borrowed top note has
// topKey's pitch class (the named chord's degrees are preferred over the diminished
// chord's), stacked with topKey on top and voiced. *outDegree, when non-null,
// receives that degree; -1 when nothing matched (chord count 0).
Chord buildBorrowedChord(const ScaleSelection &sel, int topKey, VoicingType voicing,
                         const BorrowSettings &borrow, int *outDegree = nullptr);

// The chord of a fixed degree with `topKey` as its top note: the degree's root-position
// stack, borrowed and voiced. The degree's own top class sits on topKey when it equals
// topKey's pitch class, as it does for a freshly triggered chord; otherwise all four
// classes sit strictly below, which is how a held chord is re-voiced when the user
// releases the note that cancelled its borrowing.
Chord buildDegreeChord(const ScaleSelection &sel, int degree, int topKey, VoicingType voicing,
                       const BorrowSettings &borrow);

// The un-borrowed form of a held chord: the degree's root-position stack with the
// borrowed voices swapped back to their un-borrowed counterparts in place. `triggerKey`
// is the chord's top as it was triggered (its pitch class is the borrowed top's) and
// `pressKey` is an un-borrowed top-three note of the degree that the user pressed.
// The pressed note sounds at its own key; it takes the top when it is the un-borrowed
// top note -- which sits below the borrowed top when the top is borrowed from the
// next chord, above it when borrowed from the previous one -- otherwise the top
// stays put and the borrowed top lands one scale degree away from where the borrowed
// form had it.
Chord buildOverrideChord(const ScaleSelection &sel, int degree, int triggerKey, int pressKey,
                         VoicingType voicing);

// True when pc is one of degree `degree`'s un-borrowed top three classes, for a voice
// whose borrow switch is active -- pressing that note cancels the chord's borrowing.
bool isCancelledBorrowVoice(const ScaleSelection &sel, int degree, int pc,
                            const BorrowSettings &borrow);

// "None", "Prev", "Next", for a borrow button.
const char *borrowDirName(BorrowDir dir);

}  // namespace bhl

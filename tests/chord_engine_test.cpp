// Unit tests for the chord engine. No framework -- plain asserts keep the build tiny.
#include <cstdio>
#include <initializer_list>

#include "chord_engine.h"

using namespace bhl;

namespace
{

int failures = 0;

// A 12-bit pitch class mask from semitone offsets.
uint16_t mask(std::initializer_list<int> offsets)
{
  uint16_t m = 0;
  for (int o : offsets) m |= 1u << (o % 12);
  return m;
}

void expectChord(const char *what, const Chord &got, const int want[], int wantCount)
{
  bool ok = got.count == wantCount;
  for (int i = 0; ok && i < got.count; ++i) ok = got.notes[i] == want[i];

  if (ok) return;

  ++failures;
  printf("FAIL %s\n  got: ", what);
  for (int i = 0; i < got.count; ++i) printf("%d ", got.notes[i]);
  printf("\n  want: ");
  for (int i = 0; i < wantCount; ++i) printf("%d ", want[i]);
  printf("\n");
}

void expectTrue(const char *what, bool cond)
{
  if (cond) return;
  ++failures;
  printf("FAIL %s\n", what);
}

void expectSelection(const char *what, const ScaleSelection &got, ScaleFamily family, int keyPc)
{
  if (got.family == family && got.keyPc == keyPc) return;
  ++failures;
  printf("FAIL %s\n  got: family %d key %d, want family %d key %d\n", what,
         static_cast<int>(got.family), got.keyPc, static_cast<int>(family), keyPc);
}

}  // namespace

int main()
{
  // --- the tables -------------------------------------------------------------
  expectTrue("major 6th scale",
             scalePitchClasses(ScaleFamily::Major6) == mask({0, 2, 4, 5, 7, 8, 9, 11}));
  expectTrue("minor 6th scale",
             scalePitchClasses(ScaleFamily::Minor6) == mask({0, 2, 3, 5, 7, 8, 9, 11}));
  expectTrue("7th scale", scalePitchClasses(ScaleFamily::Seven) == mask({0, 2, 4, 5, 7, 8, 10, 11}));
  expectTrue("7♭5 scale", scalePitchClasses(ScaleFamily::SevenB5) == mask({0, 2, 4, 5, 6, 8, 10, 11}));
  expectTrue("major 6th named chord", namedChordPitchClasses(ScaleFamily::Major6) == mask({0, 4, 7, 9}));
  expectTrue("minor 6th named chord", namedChordPitchClasses(ScaleFamily::Minor6) == mask({0, 3, 7, 9}));
  expectTrue("7th named chord", namedChordPitchClasses(ScaleFamily::Seven) == mask({0, 4, 7, 10}));
  expectTrue("7♭5 named chord", namedChordPitchClasses(ScaleFamily::SevenB5) == mask({0, 4, 6, 10}));
  expectTrue("dim chord", dimChordPitchClasses(2) == mask({2, 5, 8, 11}));
  expectTrue("dim chord wraps", dimChordPitchClasses(11) == mask({11, 2, 5, 8}));

  // --- scale selection ---------------------------------------------------------
  // A full named chord identifies its scale unambiguously (see chord-to-scale-uniqueness.md).
  {
    ScaleSelection sel;
    expectTrue("CEGA -> C major 6th", selectScale(mask({0, 4, 7, 9}), &sel));
    expectSelection("CEGA selection", sel, ScaleFamily::Major6, 0);
  }
  {
    ScaleSelection sel;
    expectTrue("EGAC is the same set", selectScale(mask({4, 7, 9, 0}), &sel));
    expectSelection("EGAC selection", sel, ScaleFamily::Major6, 0);
  }
  {
    ScaleSelection sel;
    expectTrue("CEbGA -> C minor 6th", selectScale(mask({0, 3, 7, 9}), &sel));
    expectSelection("CEbGA selection", sel, ScaleFamily::Minor6, 0);
  }
  {
    ScaleSelection sel;
    expectTrue("CEF#A -> A minor 6th", selectScale(mask({0, 4, 6, 9}), &sel));
    expectSelection("CEF#A selection", sel, ScaleFamily::Minor6, 9);
  }
  {
    ScaleSelection sel;
    expectTrue("CEGBb -> C 7th", selectScale(mask({0, 4, 7, 10}), &sel));
    expectSelection("CEGBb selection", sel, ScaleFamily::Seven, 0);
  }
  {
    ScaleSelection sel;
    expectTrue("CEGbBb -> C 7♭5", selectScale(mask({0, 4, 6, 10}), &sel));
    expectSelection("CEGbBb selection", sel, ScaleFamily::SevenB5, 0);
  }

  // The dim chord belongs to all four families; the preference order picks the first.
  {
    ScaleSelection sel;
    expectTrue("DFAbB -> major 6th", selectScale(mask({2, 5, 8, 11}), &sel));
    expectSelection("DFAbB selection", sel, ScaleFamily::Major6, 0);
  }

  // Partial presses.
  {
    ScaleSelection sel;
    expectTrue("C alone -> major 6th", selectScale(mask({0}), &sel));
    expectSelection("C selection", sel, ScaleFamily::Major6, 0);
  }
  {
    ScaleSelection sel;
    expectTrue("C+Eb -> minor 6th", selectScale(mask({0, 3}), &sel));
    expectSelection("C+Eb selection", sel, ScaleFamily::Minor6, 0);
  }
  {
    // E and G: the root-named E minor 6th chord {E G B C} covers them.
    ScaleSelection sel;
    expectTrue("E+G -> E minor 6th", selectScale(mask({4, 7}), &sel));
    expectSelection("E+G selection", sel, ScaleFamily::Minor6, 4);
  }
  {
    // C and B match no stacked chord: the previous selection is kept.
    ScaleSelection sel = {ScaleFamily::Seven, 4};
    expectTrue("C+B matches nothing", !selectScale(mask({0, 11}), &sel));
    expectSelection("kept selection", sel, ScaleFamily::Seven, 4);
  }
  {
    ScaleSelection sel;
    expectTrue("empty mask matches nothing", !selectScale(0, &sel));
  }

  // --- stacked chords ------------------------------------------------------------
  const ScaleSelection c6 = {ScaleFamily::Major6, 0};
  const ScaleSelection am6 = {ScaleFamily::Minor6, 9};

  // design.md's example: C major 6th, E4 on top -> G3 A3 C4 E4.
  expectChord("E4 on top of C6", buildStackedChord(c6, 64, VoicingType::Close),
              (const int[]){55, 57, 60, 64}, 4);
  // D3 on top: the dim chord, F2 Ab2 B2 D3.
  expectChord("D3 on top of C6", buildStackedChord(c6, 50, VoicingType::Close),
              (const int[]){41, 44, 47, 50}, 4);
  // C3 on top: the named chord root position, E3 G3 A3 C3.
  expectChord("C3 on top of C6", buildStackedChord(c6, 60, VoicingType::Close),
              (const int[]){52, 55, 57, 60}, 4);
  // Am6 with C4 on top: E3 F#3 A3 C4.
  expectChord("C4 on top of Am6", buildStackedChord(am6, 60, VoicingType::Close),
              (const int[]){52, 54, 57, 60}, 4);

  // Drop2: the same stacks, but the second note from the top moves down an octave.
  // C4 on top of C6: E3 G3 A3 C4 -> A2 E3 G3 C4.
  expectChord("C4 drop2 on top of C6", buildStackedChord(c6, 60, VoicingType::Drop2),
              (const int[]){45, 52, 55, 60}, 4);
  // E4 on top of C6: G3 A3 C4 E4 -> C3 G3 A3 E4.
  expectChord("E4 drop2 on top of C6", buildStackedChord(c6, 64, VoicingType::Drop2),
              (const int[]){48, 55, 57, 64}, 4);
  // D3 on top of C6 (the dim chord): F2 Ab2 B2 D3 -> B1 F2 Ab2 D3.
  expectChord("D3 drop2 on top of C6", buildStackedChord(c6, 50, VoicingType::Drop2),
              (const int[]){35, 41, 44, 50}, 4);
  // When the moved note falls below key 0 it is dropped, not wrapped: C2 on top of
  // C6 is E1 G1 A1 C2, and dropping A1 leaves E1 G1 C2.
  expectChord("drop2 below range loses a voice", buildStackedChord(c6, 12, VoicingType::Drop2),
              (const int[]){4, 7, 12}, 3);

  // Drop3: the third note from the top moves down an octave.
  // C4 on top of C6: E3 G3 A3 C4 -> E3 G2 A3 C4.
  expectChord("C4 drop3 on top of C6", buildStackedChord(c6, 60, VoicingType::Drop3),
              (const int[]){43, 52, 57, 60}, 4);
  // E4 on top of C6: G3 A3 C4 E4 -> A2 G3 C4 E4.
  expectChord("E4 drop3 on top of C6", buildStackedChord(c6, 64, VoicingType::Drop3),
              (const int[]){45, 55, 60, 64}, 4);
  // D3 on top of C6 (the dim chord): F2 Ab2 B2 D3 -> Ab1 F2 B2 D3.
  expectChord("D3 drop3 on top of C6", buildStackedChord(c6, 50, VoicingType::Drop3),
              (const int[]){32, 41, 47, 50}, 4);
  // G1 on top of C2 falls below key 0 and is dropped: E1 G1 A1 C2 -> E1 A1 C2.
  expectChord("drop3 below range loses a voice", buildStackedChord(c6, 12, VoicingType::Drop3),
              (const int[]){4, 9, 12}, 3);

  // Drop2&4: the second and fourth notes from the top move down an octave.
  // C4 on top of C6: E3 G3 A3 C4 -> E2 A2 G3 C4.
  expectChord("C4 drop2&4 on top of C6", buildStackedChord(c6, 60, VoicingType::Drop2_4),
              (const int[]){40, 45, 55, 60}, 4);
  // E4 on top of C6: G3 A3 C4 E4 -> G2 C3 A3 E4.
  expectChord("E4 drop2&4 on top of C6", buildStackedChord(c6, 64, VoicingType::Drop2_4),
              (const int[]){43, 48, 57, 64}, 4);
  // D3 on top of C6 (the dim chord): F2 Ab2 B2 D3 -> F1 B1 Ab2 D3.
  expectChord("D3 drop2&4 on top of C6", buildStackedChord(c6, 50, VoicingType::Drop2_4),
              (const int[]){29, 35, 44, 50}, 4);
  // A1 and E1 on top of C2 fall below key 0 and are dropped: G1 C2 remains.
  expectChord("drop2&4 below range loses two voices", buildStackedChord(c6, 12, VoicingType::Drop2_4),
              (const int[]){7, 12}, 2);

  // A note outside the scale produces nothing.
  expectTrue("Bb is not in C6", buildStackedChord(c6, 70, VoicingType::Close).count == 0);
  expectTrue("root out of range", buildStackedChord(c6, -1, VoicingType::Close).count == 0);
  expectTrue("root past range", buildStackedChord(c6, 128, VoicingType::Close).count == 0);

  // --- borrowing ---------------------------------------------------------------
  // B4 on top of C6 with no borrowing is the dim chord, D4 F4 Ab4 B4.
  expectChord("no borrow: B4 on top of C6", buildBorrowedChord(c6, 71, VoicingType::Close, kNoBorrow),
              (const int[]){62, 65, 68, 71}, 4);

  // Borrow the top note from the next chord: the C E G A stack becomes C E G B, so B4
  // now triggers C4 E4 G4 B4 instead of the dim chord.
  {
    const BorrowSettings topNext = {BorrowDir::None, BorrowDir::None, BorrowDir::Next};
    int degree = -1;
    expectChord("top borrowed next: B4 on top of C6",
                buildBorrowedChord(c6, 71, VoicingType::Close, topNext, &degree),
                (const int[]){60, 64, 67, 71}, 4);
    expectTrue("top borrowed next: named degree wins", degree == 0);

    // The same stack with drop2: B4 G4 E4 C4 -> B4 G3 E4 C4.
    expectChord("top borrowed next + drop2: B4 on top of C6",
                buildBorrowedChord(c6, 71, VoicingType::Drop2, topNext),
                (const int[]){55, 60, 64, 71}, 4);

    // C# is not a borrowed top of any stack and passes through.
    expectTrue("top borrowed next: C#4 is outside",
               buildBorrowedChord(c6, 61, VoicingType::Close, topNext).count == 0);
  }

  // Borrow the top and the second note from the next: C E G A becomes C E Ab B, and the
  // B D F Ab stack becomes B D G A.
  {
    const BorrowSettings topSecondNext = {BorrowDir::None, BorrowDir::Next, BorrowDir::Next};
    expectChord("top and 2nd borrowed next: B4 on top of C6",
                buildBorrowedChord(c6, 71, VoicingType::Close, topSecondNext),
                (const int[]){60, 64, 68, 71}, 4);
    // A4 now triggers the borrowed B D F Ab stack, B3 D4 G4 A4.
    expectChord("top and 2nd borrowed next: A4 on top of C6",
                buildBorrowedChord(c6, 69, VoicingType::Close, topSecondNext),
                (const int[]){59, 62, 67, 69}, 4);

    // The cancel rule: pressing one of the degree's own un-borrowed top notes (A or G,
    // not E or C, whose switches are off) cancels the borrowing.
    expectTrue("A cancels the borrow", isCancelledBorrowVoice(c6, 0, 9, topSecondNext));
    expectTrue("G cancels the borrow", isCancelledBorrowVoice(c6, 0, 7, topSecondNext));
    expectTrue("E does not cancel", !isCancelledBorrowVoice(c6, 0, 4, topSecondNext));
    expectTrue("C does not cancel", !isCancelledBorrowVoice(c6, 0, 0, topSecondNext));
  }

  // Borrow the top note from the previous chord: the D F Ab B stack becomes D F Ab A, so
  // A4 triggers D4 F4 Ab4 A4.
  {
    const BorrowSettings topPrev = {BorrowDir::None, BorrowDir::None, BorrowDir::Prev};
    expectChord("top borrowed prev: A4 on top of C6",
                buildBorrowedChord(c6, 69, VoicingType::Close, topPrev),
                (const int[]){62, 65, 68, 69}, 4);
  }

  // buildDegreeChord re-voices a fixed degree with the pressed note on top: degree 0 of
  // C6 (C E G A) with A4 on top is C4 E4 G4 A4, with no re-derivation of the degree.
  expectChord("degree chord: A4 on degree 0 of C6",
              buildDegreeChord(c6, 0, 69, VoicingType::Close, kNoBorrow),
              (const int[]){60, 64, 67, 69}, 4);
  expectChord("degree chord: borrowed degree 0 with B4 on top",
              buildDegreeChord(c6, 0, 71, VoicingType::Close,
                               (BorrowSettings){BorrowDir::None, BorrowDir::Next,
                                                BorrowDir::Next}),
              (const int[]){60, 64, 68, 71}, 4);

  // --- the override (revert) ------------------------------------------------------------
  // Borrow the 3rd top note of C E G A from the previous chord: E becomes D, so A4 on
  // top of degree 0 sounds C4 D4 G4 A4.
  {
    const BorrowSettings thirdPrev = {BorrowDir::Prev, BorrowDir::None, BorrowDir::None};
    expectChord("3rd borrowed prev: A4 on top of C6",
                buildBorrowedChord(c6, 69, VoicingType::Close, thirdPrev),
                (const int[]){60, 62, 67, 69}, 4);
  }

  // buildOverrideChord swaps the borrowed voices back in place: pressing E4 (the
  // un-borrowed 3rd-top note) while A4 is the top gives C4 E4 G4 A4 -- only D4 moves.
  expectChord("override: E4 under A4 on degree 0",
              buildOverrideChord(c6, 0, 69, 64, VoicingType::Close),
              (const int[]){60, 64, 67, 69}, 4);
  // Pressing the un-borrowed top (A4 under the borrowed top B4) takes the top itself.
  expectChord("override: A4 under B4 on degree 0",
              buildOverrideChord(c6, 0, 71, 69, VoicingType::Close),
              (const int[]){60, 64, 67, 69}, 4);
  // The override is in place: a cancel note struck an octave away reverts the chord
  // where it was triggered, not where the note was struck.
  expectChord("override: A3 under B4 reverts in place",
              buildOverrideChord(c6, 0, 71, 57, VoicingType::Close),
              (const int[]){60, 64, 67, 69}, 4);
  // Same for a prev-borrowed top: the un-borrowed top lands above the borrowed one,
  // wherever the cancel note was struck.
  expectChord("override: C4 over B4 on degree 2 reverts in place",
              buildOverrideChord(c6, 2, 71, 60, VoicingType::Close),
              (const int[]){64, 67, 69, 72}, 4);
  // Pressing the un-borrowed 2nd-top note (G4) under the borrowed top: G4 stays put, the
  // borrowed top B4 lands on A4.
  expectChord("override: G4 under B4 on degree 0",
              buildOverrideChord(c6, 0, 71, 67, VoicingType::Close),
              (const int[]){60, 64, 67, 69}, 4);
  // The voicing in force at trigger time applies to the override too.
  expectChord("override with drop2",
              buildOverrideChord(c6, 0, 69, 64, VoicingType::Drop2),
              (const int[]){55, 60, 64, 69}, 4);
  // A press that is not an un-borrowed voice of the degree, or not below the top, reverts
  // nothing.
  expectTrue("override: F# is not an un-borrowed voice",
             buildOverrideChord(c6, 0, 69, 65, VoicingType::Close).count == 0);
  expectTrue("override: press not below the top",
             buildOverrideChord(c6, 0, 69, 71, VoicingType::Close).count == 0);

  // When the top is borrowed from the previous chord, the un-borrowed top sits ABOVE
  // the borrowed one: pressing it over the trigger key reverts the borrow too.
  // Degree 2 of C6 is E G A C; with the top borrowed prev its top is B, so B4
  // triggers E4 G4 A4 B4 and pressing C5 over it reverts to E4 G4 A4 C5.
  expectChord("override above the top: C5 over B4 on degree 2",
              buildOverrideChord(c6, 2, 71, 72, VoicingType::Close),
              (const int[]){64, 67, 69, 72}, 4);
  expectTrue("override: press at the trigger key reverts nothing",
             buildOverrideChord(c6, 2, 71, 71, VoicingType::Close).count == 0);
  // A middle un-borrowed voice above the trigger key is not a cancel note.
  // The engine does not judge the pressed note's position relative to the chord's top
  // (the plugin does): it still reverts in place.
  expectChord("override: in place even when the press is above the top",
              buildOverrideChord(c6, 2, 71, 79, VoicingType::Close),
              (const int[]){64, 67, 69, 72}, 4);

  if (failures)
  {
    printf("\n%d test(s) failed\n", failures);
    return 1;
  }
  printf("all chord engine tests passed\n");
  return 0;
}

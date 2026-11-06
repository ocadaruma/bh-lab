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

  // A note outside the scale produces nothing.
  expectTrue("Bb is not in C6", buildStackedChord(c6, 70, VoicingType::Close).count == 0);
  expectTrue("root out of range", buildStackedChord(c6, -1, VoicingType::Close).count == 0);
  expectTrue("root past range", buildStackedChord(c6, 128, VoicingType::Close).count == 0);

  if (failures)
  {
    printf("\n%d test(s) failed\n", failures);
    return 1;
  }
  printf("all chord engine tests passed\n");
  return 0;
}

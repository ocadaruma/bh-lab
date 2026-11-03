// Unit tests for the chord engine. No framework -- plain asserts keep the build tiny.
#include <cstdio>
#include <vector>

#include "chord_engine.h"

using namespace bhl;

namespace
{

int failures = 0;

void expectChord(const char *what, const Chord &got, const std::vector<int> &want)
{
  bool ok = got.count == static_cast<int>(want.size());
  for (int i = 0; ok && i < got.count; ++i) ok = got.notes[i] == want[i];

  if (ok) return;

  ++failures;
  printf("FAIL %s\n  got: ", what);
  for (int i = 0; i < got.count; ++i) printf("%d ", got.notes[i]);
  printf("\n  want: ");
  for (int n : want) printf("%d ", n);
  printf("\n");
}

void expectTrue(const char *what, bool cond)
{
  if (cond) return;
  ++failures;
  printf("FAIL %s\n", what);
}

constexpr int kC2 = 48;

}  // namespace

int main()
{
  // Qualities, root position.
  expectChord("C major", buildChord(kC2, Quality::Major, Inversion::Root), {48, 52, 55});
  expectChord("C minor", buildChord(kC2, Quality::Minor, Inversion::Root), {48, 51, 55});
  expectChord("C maj7", buildChord(kC2, Quality::Maj7, Inversion::Root), {48, 52, 55, 59});
  expectChord("C min7", buildChord(kC2, Quality::Min7, Inversion::Root), {48, 51, 55, 58});
  expectChord("C 6", buildChord(kC2, Quality::Six, Inversion::Root), {48, 52, 55, 57});
  expectChord("C dom7", buildChord(kC2, Quality::Dom7, Inversion::Root), {48, 52, 55, 58});
  expectChord("C sus4", buildChord(kC2, Quality::Sus4, Inversion::Root), {48, 53, 55});

  // Inversions lift the lowest voices by an octave and stay sorted.
  expectChord("C major 1st", buildChord(kC2, Quality::Major, Inversion::First), {52, 55, 60});
  expectChord("C major 2nd", buildChord(kC2, Quality::Major, Inversion::Second), {55, 60, 64});
  expectChord("C maj7 3rd", buildChord(kC2, Quality::Maj7, Inversion::Third), {59, 60, 64, 67});

  // A triad has no 3rd inversion; it falls back to the 2nd rather than dropping a voice.
  expectChord("C major 3rd falls back", buildChord(kC2, Quality::Major, Inversion::Third),
              {55, 60, 64});

  // Octave doubling keeps root position and adds the octave on top.
  expectChord("C major octave", buildChord(kC2, Quality::Major, Inversion::OctaveDouble),
              {48, 52, 55, 60});

  // Voices above 127 are dropped, not wrapped.
  {
    const Chord high = buildChord(126, Quality::Maj7, Inversion::Root);
    expectTrue("high root drops out-of-range voices", high.count == 1 && high.notes[0] == 126);
  }

  // Inversions are only offered when the chord has a voice to put in the bass.
  expectTrue("triads have no 3rd inversion",
             !inversionAvailable(Quality::Major, Inversion::Third) &&
                 inversionAvailable(Quality::Maj7, Inversion::Third));
  expectTrue("octave double applies to any chord",
             inversionAvailable(Quality::Major, Inversion::OctaveDouble) &&
                 inversionAvailable(Quality::Maj7, Inversion::OctaveDouble));

  // Control zone mapping: white keys are qualities, black keys are inversions.
  expectTrue("C sets major", mapControlKey(0).action == ControlAction::SetQuality &&
                                 mapControlKey(0).value == static_cast<int>(Quality::Major));
  expectTrue("D sets minor", mapControlKey(2).action == ControlAction::SetQuality &&
                                 mapControlKey(2).value == static_cast<int>(Quality::Minor));
  expectTrue("B sets sus4", mapControlKey(11).action == ControlAction::SetQuality &&
                                mapControlKey(11).value == static_cast<int>(Quality::Sus4));
  expectTrue("C# sets root position",
             mapControlKey(1).action == ControlAction::SetInversion &&
                 mapControlKey(1).value == static_cast<int>(Inversion::Root));
  expectTrue("G# sets 3rd inversion",
             mapControlKey(8).action == ControlAction::SetInversion &&
                 mapControlKey(8).value == static_cast<int>(Inversion::Third));
  expectTrue("A# sets octave double",
             mapControlKey(10).action == ControlAction::SetInversion &&
                 mapControlKey(10).value == static_cast<int>(Inversion::OctaveDouble));
  expectTrue("out of range offset does nothing",
             mapControlKey(12).action == ControlAction::None &&
                 mapControlKey(-1).action == ControlAction::None);

  if (failures)
  {
    printf("\n%d test(s) failed\n", failures);
    return 1;
  }
  printf("all chord engine tests passed\n");
  return 0;
}

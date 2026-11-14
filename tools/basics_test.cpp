// The basics of the plugin, checked from the outside: scale selection in the control
// octave, stacked chords in the trigger zone, and pass-through of out-of-scale notes.

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include "clap_test_host.h"

using namespace bhl_test;

namespace
{

// Sets the plugin to a known state: close voicing, no borrowing, default zones (C
// major 6th is the built-in default selection).
void reset(const clap_plugin_t *plugin, Timeline *timeline)
{
  InputEvents setup;
  setup.addParam(kVoicing, 0);
  setup.addParam(kControlRoot, 48);   // C2
  setup.addParam(kTriggerStart, 60);  // C3
  setup.addParam(kBorrowTop, 0);      // None
  setup.addParam(kBorrowSecond, 0);   // None
  setup.addParam(kBorrowThird, 0);    // None
  setup.addParam(kBorrowFourth, 0);   // None
  setup.addParam(kInversion, 0);      // Root
  setup.addParam(kContraryMotion, 0); // Off
  setup.addParam(kVelBottom, 1.0);
  setup.addParam(kVelSecond, 1.0);
  setup.addParam(kVelThird, 1.0);
  setup.addParam(kVelTop, 1.0);
  setup.addParam(kVelContrary, 1.0);
  (void)plugin;
  timeline->run(&setup);
  timeline->runFor(2);
  g_emitted.clear();
}

// Holds the given control-zone notes, so the scale they identify is in force.
void holdControl(Timeline *timeline, std::initializer_list<int> keys)
{
  InputEvents press;
  for (int k : keys) press.addNote(CLAP_EVENT_NOTE_ON, 0, static_cast<int16_t>(k), 0.8);
  timeline->run(&press);
}

std::string describe(const std::vector<Emitted> &events)
{
  std::string text;
  for (const auto &e : events)
  {
    if (!text.empty()) text += " ";
    text += (e.on ? "on:" : "off:") + std::to_string(e.key);
  }
  return text;
}

// describe, with each event's velocity in percent, for the velocity checks.
std::string describeVel(const std::vector<Emitted> &events)
{
  std::string text;
  for (const auto &e : events)
  {
    if (!text.empty()) text += " ";
    text += std::string(e.on ? "on" : "off") + ":" + std::to_string(e.key) + "@"
            + std::to_string((int)std::lround(e.velocity * 100.0));
  }
  return text;
}

std::vector<int> onKeys()
{
  std::vector<int> keys;
  for (const auto &e : noteOns()) keys.push_back(e.key);
  std::sort(keys.begin(), keys.end());
  return keys;
}

}  // namespace

int main()
{
  const clap_plugin_t *plugin = makePlugin();
  Timeline timeline(plugin);

  // --- stacked chords of the default scale ------------------------------------
  // The default selection is C major 6th diminished; C2 held keeps it in force. C3 on
  // top -> E3 G3 A3 C3.
  reset(plugin, &timeline);
  holdControl(&timeline, {48});  // C2
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 60, 0.8);
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool chord = first.size() == 4 && first[0].key == 52 && first[1].key == 55 &&
                       first[2].key == 57 && first[3].key == 60 &&
                       first[0].on && first[1].on && first[2].on && first[3].on;
    expect("C3 sounds the C6 stack", chord, describe(first));
  }

  // design.md's example: E4 on top of C major 6th -> G3 A3 C4 E4.
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 60, 0.0);
    timeline.run(&release);
  }
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 64, 0.8);
    timeline.run(&press);
  }
  {
    const std::vector<int> keys = onKeys();
    const bool top = keys.size() == 4 && keys[0] == 55 && keys[1] == 57 && keys[2] == 60 &&
                     keys[3] == 64;
    expect("E4 sounds G3 A3 C4 E4", top, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 64, 0.0);
    timeline.run(&release);
  }
  expect("releasing E4 stops the chord", !g_emitted.empty(), describe(g_emitted));
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }

  // A note outside the selected scale passes through untouched.
  reset(plugin, &timeline);
  holdControl(&timeline, {48});  // C2: C major 6th in force
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 70, 0.7);  // Bb3, not in C major 6th
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    expect("an out-of-scale note passes through",
           first.size() == 1 && first[0].on && first[0].key == 70, describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 70, 0.0);
    timeline.run(&release);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    expect("its note-off passes through too",
           first.size() == 1 && !first[0].on && first[0].key == 70, describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }

  // With nothing held in the control zone the plugin is in pass-through mode: a
  // trigger key sounds nothing, even though a scale was latched before.
  reset(plugin, &timeline);
  holdControl(&timeline, {48});
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }
  expect("the last control release is silent", g_emitted.empty(), describe(g_emitted));
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 60, 0.8);
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    expect("with nothing held, C3 passes through untouched",
           first.size() == 1 && first[0].on && first[0].key == 60, describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 60, 0.0);
    timeline.run(&release);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    expect("its note-off passes through too",
           first.size() == 1 && !first[0].on && first[0].key == 60, describe(first));
  }

  // Drop2 voicing: the second note from the top moves down an octave. C4 on top of
  // C major 6th is E3 G3 A3 C4, so drop2 sounds A2 E3 G3 C4.
  reset(plugin, &timeline);
  {
    InputEvents setup;
    setup.addParam(kVoicing, 1);  // Drop2
    timeline.run(&setup);
  }
  holdControl(&timeline, {48});
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 60, 0.8);
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool drop2 = first.size() == 4 && first[0].key == 45 && first[1].key == 52 &&
                       first[2].key == 55 && first[3].key == 60;
    expect("drop2 moves the second note from the top down an octave", drop2, describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 60, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }

  // Inversion: the trigger key matches the inverted chord's top note. With the top
  // borrowed from the next, degree 0 is C E G B: root position, B4 triggers it
  // (C4 E4 G4 B4). In 2nd inversion the 2nd voice (E) is the top, so E5 triggers
  // it instead: G4 B4 C5 E5.
  reset(plugin, &timeline);
  {
    InputEvents setup;
    setup.addParam(kBorrowTop, 2);  // Next
    timeline.run(&setup);
  }
  holdControl(&timeline, {48});
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 71, 0.8);  // B4
    timeline.run(&press);
  }
  {
    const std::vector<int> keys = onKeys();
    const bool rootPos = keys.size() == 4 && keys[0] == 60 && keys[1] == 64 && keys[2] == 67 &&
                         keys[3] == 71;
    expect("root position: B4 triggers C4 E4 G4 B4", rootPos, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 71, 0.0);
    timeline.run(&release);
  }
  {
    InputEvents setup;
    setup.addParam(kInversion, 2);  // 2nd
    timeline.run(&setup);
  }
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 76, 0.8);  // E5
    timeline.run(&press);
  }
  {
    const std::vector<int> keys = onKeys();
    const bool second = keys.size() == 4 && keys[0] == 67 && keys[1] == 71 && keys[2] == 72 &&
                        keys[3] == 76;
    expect("2nd inversion: E5 triggers G4 B4 C5 E5", second, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 76, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }

  // --- scale selection from the control octave ---------------------------------
  // C E G A identifies C major 6th (it is the default already, but the press must not
  // disturb it).
  reset(plugin, &timeline);
  holdControl(&timeline, {48, 52, 55, 57});  // C2 E2 G2 A2
  expect("control notes are silent", g_emitted.empty(), describe(g_emitted));
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 64, 0.8);  // E4
    timeline.run(&press);
  }
  {
    const std::vector<int> keys = onKeys();
    const bool c6 = keys.size() == 4 && keys[0] == 55 && keys[1] == 57 && keys[2] == 60 &&
                    keys[3] == 64;
    expect("CEGA identifies C major 6th", c6, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 64, 0.0);
    timeline.run(&release);
  }
  expect("releasing E4 stops the chord", onKeys().empty() && g_emitted.size() == 4,
         describe(g_emitted));
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 52, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 55, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 57, 0.0);
    timeline.run(&release);
  }
  expect("control note-offs are silent too", g_emitted.empty(), describe(g_emitted));

  // C and Eb identify C minor 6th; its named chord with A4 on top is C4 Eb4 G4 A4.
  reset(plugin, &timeline);
  holdControl(&timeline, {48, 51});  // C2 Eb2
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 69, 0.8);  // A4
    timeline.run(&press);
  }
  {
    const std::vector<int> keys = onKeys();
    const bool m6 = keys.size() == 4 && keys[0] == 60 && keys[1] == 63 && keys[2] == 67 &&
                    keys[3] == 69;
    expect("C+Eb identifies C minor 6th", m6, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 69, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 51, 0.0);
    timeline.run(&release);
  }

  // With the control octave moved to A2, A C E F# identifies A minor 6th.
  reset(plugin, &timeline);
  {
    InputEvents setup;
    setup.addParam(kControlRoot, 57);  // A2
    setup.addParam(kTriggerStart, 72);  // C4
    timeline.run(&setup);
  }
  holdControl(&timeline, {57, 60, 64, 66});  // A2 C3 E3 F#3
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 72, 0.8);  // C4
    timeline.run(&press);
  }
  {
    const std::vector<int> keys = onKeys();
    const bool am6 = keys.size() == 4 && keys[0] == 64 && keys[1] == 66 && keys[2] == 69 &&
                     keys[3] == 72;
    expect("A C E F# identifies A minor 6th", am6, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 72, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 66, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 57, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 60, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 64, 0.0);
    timeline.run(&release);
  }

  // The selection follows the held notes live: design.md's example, C alone selects
  // major 6th, and adding Eb switches to minor 6th while the notes are still held.
  reset(plugin, &timeline);
  holdControl(&timeline, {48});  // C2
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 69, 0.8);  // A4
    timeline.run(&press);
  }
  {
    const std::vector<int> keys = onKeys();
    const bool c6 = keys.size() == 4 && keys[0] == 60 && keys[1] == 64 && keys[2] == 67 &&
                    keys[3] == 69;
    expect("C alone selects C major 6th", c6, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 69, 0.0);
    timeline.run(&release);
  }
  holdControl(&timeline, {51});  // now holding C2 Eb2
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 69, 0.8);  // A4
    timeline.run(&press);
  }
  {
    const std::vector<int> keys = onKeys();
    const bool m6 = keys.size() == 4 && keys[0] == 60 && keys[1] == 63 && keys[2] == 67 &&
                    keys[3] == 69;
    expect("adding Eb switches to C minor 6th", m6, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 69, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 51, 0.0);
    timeline.run(&release);
  }

  // --- chord handling ----------------------------------------------------------
  // Each scenario presses C2 in the control zone first, so the selection is C major
  // 6th rather than whatever the previous scenario latched.
  reset(plugin, &timeline);
  holdControl(&timeline, {48});
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 60, 0.8);
    timeline.run(&press);
  }
  // Holding the key must produce nothing further.
  g_emitted.clear();
  timeline.runFor(200);
  expect("holding the key sounds nothing more", g_emitted.empty(), describe(g_emitted));

  // A second chord while the first is held: shared keys are not re-triggered, only the
  // genuinely new note sounds.
  {
    InputEvents second;
    second.addNote(CLAP_EVENT_NOTE_ON, 0, 64, 0.8);
    timeline.run(&second);
  }
  {
    const bool shared = g_emitted.size() == 1 && g_emitted[0].on && g_emitted[0].key == 64;
    expect("a second key adds only its new note", shared, describe(g_emitted));
  }

  // Releasing one of two overlapping chords: the shared keys keep sounding, owned by
  // the chord that is still held. C3's stack is E3 G3 A3 C3; E3's is G3 A3 C3 E3.
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 60, 0.0);
    timeline.run(&release);
  }
  {
    const bool sharedHeld = g_emitted.size() == 1 && !g_emitted[0].on && g_emitted[0].key == 52;
    expect("releasing C3 keeps the shared keys ringing", sharedHeld, describe(g_emitted));
  }

  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 64, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }
  {
    int offs = 0;
    for (const auto &e : g_emitted)
      if (!e.on) ++offs;
    expect("the last chord release stops the shared keys too", offs == 4, describe(g_emitted));
  }
  g_emitted.clear();

  // Re-triggering a held key restarts its chord without leaking notes.
  reset(plugin, &timeline);
  holdControl(&timeline, {48});
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 60, 0.8);
    timeline.run(&press);
  }
  g_emitted.clear();
  {
    InputEvents again;
    again.addNote(CLAP_EVENT_NOTE_ON, 0, 60, 0.9);
    timeline.run(&again);
  }
  {
    int offs = 0, ons = 0;
    for (const auto &e : g_emitted)
      (e.on ? ons : offs)++;
    expect("re-triggering replaces the chord cleanly", offs == 4 && ons == 4,
           std::to_string(offs) + " off, " + std::to_string(ons) + " on");
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 60, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }

  // The selection changes while a chord is held: re-triggering picks up the new scale.
  reset(plugin, &timeline);
  holdControl(&timeline, {48});
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 69, 0.8);  // A4, C major 6th
    timeline.run(&press);
  }
  g_emitted.clear();
  holdControl(&timeline, {51});  // C2 Eb2 -> C minor 6th
  {
    InputEvents again;
    again.addNote(CLAP_EVENT_NOTE_ON, 0, 69, 0.8);
    timeline.run(&again);
  }
  {
    int offs = 0, ons = 0;
    for (const auto &e : g_emitted)
      (e.on ? ons : offs)++;
    const bool swapped = offs == 4 && ons == 4 && onKeys().size() == 4 &&
                         onKeys()[0] == 60 && onKeys()[1] == 63 && onKeys()[2] == 67 &&
                         onKeys()[3] == 69;
    expect("re-triggering uses the new selection", swapped, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 69, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 51, 0.0);
    timeline.run(&release);
  }
  {
    int offs = 0;
    for (const auto &e : g_emitted)
      if (!e.on) ++offs;
    expect("the chord and control notes release cleanly", offs == 4, describe(g_emitted));
  }

  // A choke also stops the chord.
  reset(plugin, &timeline);
  holdControl(&timeline, {48});
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 60, 0.8);
    timeline.run(&press);
  }
  g_emitted.clear();
  {
    InputEvents choke;
    choke.addNote(CLAP_EVENT_NOTE_CHOKE, 0, 60, 0.0);
    timeline.run(&choke);
  }
  {
    int offs = 0;
    for (const auto &e : g_emitted)
      if (!e.on) ++offs;
    expect("a choke releases the chord", offs == 4, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }

  // Moving the trigger zone moves what triggers.
  reset(plugin, &timeline);
  {
    InputEvents setup;
    setup.addParam(kControlRoot, 36);
    setup.addParam(kTriggerStart, 48);
    timeline.run(&setup);
  }
  holdControl(&timeline, {36});  // C1 -> C major 6th
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 48, 0.8);  // C2
    timeline.run(&press);
  }
  {
    const std::vector<int> keys = onKeys();
    const bool c2 = keys.size() == 4 && keys[0] == 40 && keys[1] == 43 && keys[2] == 45 &&
                    keys[3] == 48;
    expect("the trigger zone start decides what triggers", c2, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 36, 0.0);
    timeline.run(&release);
  }

  // --- borrowing ----------------------------------------------------------------
  // design.md's example: C major 6th, top and 2nd borrowed from the next. B4 sounds
  // C4 E4 Ab4 B4 instead of the dim chord; pressing A4 (the chord's own un-borrowed top
  // note) releases the borrowed Ab4 B4 and sounds G4 A4 instead.
  reset(plugin, &timeline);
  {
    InputEvents setup;
    setup.addParam(kBorrowTop, 2);     // Next
    setup.addParam(kBorrowSecond, 2);  // Next
    timeline.run(&setup);
  }
  holdControl(&timeline, {48});  // C2: C major 6th in force
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 71, 0.8);  // B4
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool borrowed = first.size() == 4 && first[0].key == 60 && first[1].key == 64 &&
                          first[2].key == 68 && first[3].key == 71;
    expect("borrowed chord sounds C4 E4 Ab4 B4", borrowed, describe(first));
  }
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 69, 0.8);  // A4, the un-borrowed top note
    timeline.run(&press);
  }
  {
    int ons = 0, offs = 0;
    for (const auto &e : g_emitted) (e.on ? ons : offs)++;
    const std::vector<Emitted> first = g_emitted;
    const bool cancelled = ons == 2 && offs == 2 &&
                           first[0].on == false && first[0].key == 68 &&
                           first[1].on == false && first[1].key == 71 &&
                           first[2].on && first[2].key == 67 && first[3].on && first[3].key == 69;
    expect("pressing A4 cancels the borrow (Ab4 B4 off, G4 A4 on)", cancelled,
           describe(first));
  }

  // Releasing the cancel note must not re-trigger the borrowed chord: a key release
  // sounds no new notes. Only G4 A4 go quiet; C4 E4 keep ringing (the later B4 release
  // releasing them proves the claim).
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 69, 0.0);
    timeline.run(&release);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool silentRestore = first.size() == 2 && !first[0].on && !first[1].on &&
                               first[0].key == 67 && first[1].key == 69;
    expect("releasing A4 sounds no notes (only G4 A4 go quiet)", silentRestore,
           describe(first));
  }

  // The session's single revert is spent: pressing the cancel note again passes
  // through rather than re-entering the un-borrowed form.
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 69, 0.8);
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool passed = first.size() == 1 && first[0].on && first[0].key == 69;
    expect("pressing A4 again passes through (the revert is used up)", passed,
           describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 69, 0.0);
    timeline.run(&release);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool passed = first.size() == 1 && !first[0].on && first[0].key == 69;
    expect("the pass-through note-off passes through too", passed, describe(first));
  }

  // Releasing the trigger key stops whatever the chord still sounds.
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 71, 0.0);
    timeline.run(&release);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool done = first.size() == 2 && !first[0].on && !first[1].on &&
                      first[0].key == 60 && first[1].key == 64;
    expect("releasing B4 stops C4 E4", done, describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }
  expect("the control release is silent", g_emitted.empty(), describe(g_emitted));

  // --- borrowing: the cancel note takes over the chord ----------------------------
  // While the chord is in its un-borrowed form, releasing the original trigger key does
  // not stop it: the cancel key, still pressed, takes over ownership. The notes keep
  // sounding until that key is released.
  reset(plugin, &timeline);
  {
    InputEvents setup;
    setup.addParam(kBorrowTop, 2);     // Next
    setup.addParam(kBorrowSecond, 2);  // Next
    timeline.run(&setup);
  }
  holdControl(&timeline, {48});
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 71, 0.8);  // B4 -> C4 E4 Ab4 B4
    timeline.run(&press);
  }
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 69, 0.8);  // A4 cancels the borrow
    timeline.run(&press);
  }
  {
    int ons = 0, offs = 0;
    for (const auto &e : g_emitted) (e.on ? ons : offs)++;
    expect("A4 cancels the borrow", ons == 2 && offs == 2, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 71, 0.0);  // B4, the trigger key
    timeline.run(&release);
  }
  expect("releasing B4 keeps the chord while A4 is held", g_emitted.empty(),
         describe(g_emitted));
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 69, 0.0);  // A4, now the owner
    timeline.run(&release);
  }
  {
    int offs = 0;
    for (const auto &e : g_emitted)
      if (!e.on) ++offs;
    expect("releasing A4 stops the whole chord", offs == 4, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }
  expect("the control release is silent", g_emitted.empty(), describe(g_emitted));

  // --- borrowing: keys below a borrowed top pass through --------------------------
  // With the top borrowed from the next, B4 sounds C4 E4 G4 B4. While it is held, any
  // key below B4 that does not cancel the borrow passes through untouched -- in-scale
  // or not. Only the cancel note (A) reverts the chord.
  reset(plugin, &timeline);
  {
    InputEvents setup;
    setup.addParam(kBorrowTop, 2);  // Next
    timeline.run(&setup);
  }
  holdControl(&timeline, {48});
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 71, 0.8);  // B4 -> C4 E4 G4 B4
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool borrowed = first.size() == 4 && first[0].key == 60 && first[1].key == 64 &&
                          first[2].key == 67 && first[3].key == 71;
    expect("borrowed chord sounds C4 E4 G4 B4", borrowed, describe(first));
  }
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 65, 0.8);  // F4, in-scale, below B4
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool passed = first.size() == 1 && first[0].on && first[0].key == 65;
    expect("F4 below the borrowed top passes through", passed, describe(first));
  }
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 70, 0.8);  // Bb3, out of scale, below B4
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool passed = first.size() == 1 && first[0].on && first[0].key == 70;
    expect("an out-of-scale key below the borrowed top passes through too", passed,
           describe(first));
  }
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 69, 0.8);  // A4 cancels the borrow
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool cancelled = first.size() == 2 && !first[0].on && first[0].key == 71 &&
                           first[1].on && first[1].key == 69;
    expect("A4 below the borrowed top still cancels", cancelled, describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 69, 0.0);
    timeline.run(&release);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool silent = first.size() == 1 && !first[0].on && first[0].key == 69;
    expect("releasing A4 sounds no notes (B4 stays silent)", silent, describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 65, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 70, 0.0);
    timeline.run(&release);
  }
  {
    int offs = 0;
    for (const auto &e : g_emitted)
      if (!e.on) ++offs;
    expect("the passed-through note-offs pass through too", offs == 2, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 71, 0.0);
    timeline.run(&release);
  }
  {
    int offs = 0;
    for (const auto &e : g_emitted)
      if (!e.on) ++offs;
    expect("releasing B4 stops C4 E4 G4", offs == 3, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }
  expect("the control release is silent", g_emitted.empty(), describe(g_emitted));

  // --- borrowing: revert works for any switch, not only the top ----------------------
  // Borrow the 3rd top note from the previous: E becomes D, so A4 on top of degree 0
  // sounds C4 D4 G4 A4. Pressing E4 (the un-borrowed 3rd-top note) reverts the chord in
  // place: only D4 moves, and the pressed note sounds at its own key.
  reset(plugin, &timeline);
  {
    InputEvents setup;
    setup.addParam(kBorrowThird, 1);  // Prev
    timeline.run(&setup);
  }
  holdControl(&timeline, {48});
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 69, 0.8);  // A4 -> C4 D4 G4 A4
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool borrowed = first.size() == 4 && first[0].key == 60 && first[1].key == 62 &&
                          first[2].key == 67 && first[3].key == 69;
    expect("3rd borrowed prev: A4 sounds C4 D4 G4 A4", borrowed, describe(first));
  }

  // Suppression does not depend on the top being borrowed: with any borrow switch on,
  // keys below the top pass through. D4 is the borrowed 3rd-top note itself, so it is
  // not a cancel and does not trigger a chord either. The chord already sounds D4, so
  // the press also takes a claim on that shared voice: its release drops the claim
  // while the forwarded note-off goes through.
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 62, 0.8);  // D4, below A4
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool passed = first.size() == 1 && first[0].on && first[0].key == 62;
    expect("D4 below the top passes through (top not borrowed)", passed,
           describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 62, 0.0);
    timeline.run(&release);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool passed = first.size() == 1 && !first[0].on && first[0].key == 62;
    expect("its note-off passes through too", passed, describe(first));
  }

  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 64, 0.8);  // E4, the un-borrowed 3rd-top note
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool reverted = first.size() == 2 && !first[0].on && first[0].key == 62 &&
                          first[1].on && first[1].key == 64;
    expect("pressing E4 reverts the chord in place (D4 off, E4 on)", reverted,
           describe(first));
  }

  // The original trigger key is still the user's: releasing it transfers the chord to
  // the cancel key (E4) instead of stopping it.
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 69, 0.0);
    timeline.run(&release);
  }
  expect("releasing A4 keeps the chord while E4 is held", g_emitted.empty(),
         describe(g_emitted));
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 64, 0.0);  // E4, now the owner
    timeline.run(&release);
  }
  {
    int offs = 0;
    for (const auto &e : g_emitted)
      if (!e.on) ++offs;
    expect("releasing E4 stops the whole chord", offs == 4, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }
  expect("the control release is silent", g_emitted.empty(), describe(g_emitted));

  // --- borrowing: with the top borrowed from the previous, revert comes from above --
  // Borrow the top from the previous: degree 2's stack E G A C becomes E G A B, so B4
  // sounds E4 G4 A4 B4. The un-borrowed top (C) now sits ABOVE the borrowed one: the
  // only way to revert the chord is to press it over the trigger key.
  reset(plugin, &timeline);
  {
    InputEvents setup;
    setup.addParam(kBorrowTop, 1);  // Prev
    timeline.run(&setup);
  }
  holdControl(&timeline, {48});
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 71, 0.8);  // B4
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool borrowed = first.size() == 4 && first[0].key == 64 && first[1].key == 67 &&
                          first[2].key == 69 && first[3].key == 71;
    expect("top borrowed prev: B4 sounds E4 G4 A4 B4", borrowed, describe(first));
  }
  // The whole space above the top belongs to the session: any note over B4 passes
  // through rather than triggering another chord, in-scale or not.
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 74, 0.8);  // D5, above B4, in-scale
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool passed = first.size() == 1 && first[0].on && first[0].key == 74;
    expect("D5 above the prev-borrowed top passes through", passed, describe(first));
  }
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 76, 0.8);  // E5, above B4, out of scale
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool passed = first.size() == 1 && first[0].on && first[0].key == 76;
    expect("an out-of-scale note above the prev-borrowed top passes through too",
           passed, describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 74, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 76, 0.0);
    timeline.run(&release);
  }
  {
    int offs = 0;
    for (const auto &e : g_emitted)
      if (!e.on) ++offs;
    expect("the pass-through note-offs pass through too", offs == 2, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 72, 0.8);  // C5, the un-borrowed top, above B4
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool reverted = first.size() == 2 && !first[0].on && first[0].key == 71 &&
                          first[1].on && first[1].key == 72;
    expect("pressing C5 over B4 reverts the chord in place (B4 off, C5 on)", reverted,
           describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 72, 0.0);
    timeline.run(&release);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool silent = first.size() == 1 && !first[0].on && first[0].key == 72;
    expect("releasing C5 sounds no notes (B4 stays silent)", silent, describe(first));
  }
  // The revert is used up: pressing C5 again passes through, and releasing B4 stops
  // whatever the chord still sounds.
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 72, 0.8);
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool passed = first.size() == 1 && first[0].on && first[0].key == 72;
    expect("pressing C5 again passes through (the revert is used up)", passed,
           describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 72, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 71, 0.0);
    timeline.run(&release);
  }
  {
    int offs = 0;
    for (const auto &e : g_emitted)
      if (!e.on) ++offs;
    expect("the pass-through off plus releasing B4 stops E4 G4 A4 (4 offs)", offs == 4,
           describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }
  expect("the control release is silent", g_emitted.empty(), describe(g_emitted));

  // --- borrowing: a pass-through press of a chord note survives the chord --------------
  // With the top and second borrowed from the next, B4 sounds C4 E4 Ab4 B4. Pressing E4
  // while it is held does not re-sound the note: the press joins the chord's shared
  // voice, so releasing the trigger key cannot stop a note the press still holds, and
  // the note dies only with the press.
  reset(plugin, &timeline);
  {
    InputEvents setup;
    setup.addParam(kBorrowTop, 2);     // Next
    setup.addParam(kBorrowSecond, 2);  // Next
    timeline.run(&setup);
  }
  holdControl(&timeline, {48});
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 71, 0.8);  // B4 -> C4 E4 Ab4 B4
    timeline.run(&press);
  }
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 64, 0.8);  // E4, already sounded by the chord
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool passed = first.size() == 1 && first[0].on && first[0].key == 64;
    expect("E4 below the borrowed top passes through (and joins the chord's voice)",
           passed, describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 71, 0.0);  // B4, the trigger key
    timeline.run(&release);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool e4Kept = first.size() == 3 && !first[0].on && !first[1].on && !first[2].on &&
                        first[0].key != 64 && first[1].key != 64 && first[2].key != 64;
    expect("releasing B4 keeps E4 sounding while the press is held", e4Kept,
           describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 64, 0.0);  // E4, the press
    timeline.run(&release);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool e4Off = first.size() == 2 && !first[0].on && !first[1].on &&
                       first[0].key == 64 && first[1].key == 64;
    expect("releasing the E4 press stops the note", e4Off, describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }
  expect("the control release is silent", g_emitted.empty(), describe(g_emitted));

  // The other order: the press predates the chord. In pass-through mode E4 sounds as a
  // plain note; when the control octave is held and B4 is triggered, the chord claims
  // the note the press already sounds instead of re-sounding it, and the press's
  // release stops it even though the chord still holds it.
  reset(plugin, &timeline);
  {
    InputEvents setup;
    setup.addParam(kBorrowTop, 2);     // Next
    setup.addParam(kBorrowSecond, 2);  // Next
    timeline.run(&setup);
  }
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 64, 0.8);  // E4, nothing held in control
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool passed = first.size() == 1 && first[0].on && first[0].key == 64;
    expect("E4 passes through in pass-through mode", passed, describe(first));
  }
  holdControl(&timeline, {48});
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 71, 0.8);  // B4 -> C4 E4 Ab4 B4
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool noDup = first.size() == 3 && first[0].key == 60 && first[1].key == 68 &&
                       first[2].key == 71;
    expect("B4 claims the E4 the press already sounds (no duplicate note)", noDup,
           describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 64, 0.0);  // E4, the press
    timeline.run(&release);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool e4Off = first.size() == 1 && !first[0].on && first[0].key == 64;
    expect("releasing the E4 press stops the note while B4 is held", e4Off,
           describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 71, 0.0);  // B4
    timeline.run(&release);
  }
  {
    int offs = 0;
    for (const auto &e : g_emitted)
      if (!e.on) ++offs;
    expect("releasing B4 stops C4 Ab4 B4 (3 offs, E4 already gone)", offs == 3,
           describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }
  expect("the control release is silent", g_emitted.empty(), describe(g_emitted));

  // --- borrowing: the revert keeps the chord's octave -----------------------------------
  // The override is in place: the chord reverts to its un-borrowed stack at the octave
  // it was triggered, no matter where the cancel note was struck. Trigger B5 (C5 E5
  // Ab5 B5) and press A4: the revert lands G5 A5 in place, not an octave below.
  reset(plugin, &timeline);
  {
    InputEvents setup;
    setup.addParam(kBorrowTop, 2);     // Next
    setup.addParam(kBorrowSecond, 2);  // Next
    timeline.run(&setup);
  }
  holdControl(&timeline, {48});
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 83, 0.8);  // B5 -> C5 E5 Ab5 B5
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool borrowed = first.size() == 4 && first[0].key == 72 && first[1].key == 76 &&
                          first[2].key == 80 && first[3].key == 83;
    expect("borrowed chord sounds C5 E5 Ab5 B5", borrowed, describe(first));
  }
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 69, 0.8);  // A4, the un-borrowed top
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool reverted = first.size() == 4 && !first[0].on && first[0].key == 80 &&
                          !first[1].on && first[1].key == 83 && first[2].on &&
                          first[2].key == 79 && first[3].on && first[3].key == 81;
    expect("A4 reverts the borrow in place (Ab5 B5 off, G5 A5 on; C5 E5 stay)", reverted,
           describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 69, 0.0);  // A4
    timeline.run(&release);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool restored = first.size() == 2 && !first[0].on && !first[1].on &&
                          first[0].key == 79 && first[1].key == 81;
    expect("releasing A4 goes quiet (G5 A5 off, C5 E5 stay silent)", restored,
           describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 83, 0.0);  // B5
    timeline.run(&release);
  }
  {
    int offs = 0;
    for (const auto &e : g_emitted)
      if (!e.on) ++offs;
    expect("releasing B5 stops C5 E5 (2 offs)", offs == 2, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }
  expect("the control release is silent", g_emitted.empty(), describe(g_emitted));

  // --- borrowing: the cancel note works from the control zone ------------------------
  // A3 sits in the control octave (C2 root), below the trigger zone. It still cancels
  // the held borrowed chord: the revert is in place, and the press is consumed -- it
  // neither identifies the scale nor sounds.
  reset(plugin, &timeline);
  {
    InputEvents setup;
    setup.addParam(kBorrowTop, 2);  // Next
    timeline.run(&setup);
  }
  holdControl(&timeline, {48});
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 71, 0.8);  // B4 -> C4 E4 G4 B4
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool borrowed = first.size() == 4 && first[0].key == 60 && first[1].key == 64 &&
                          first[2].key == 67 && first[3].key == 71;
    expect("borrowed chord sounds C4 E4 G4 B4", borrowed, describe(first));
  }
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 57, 0.8);  // A3, the un-borrowed top, in C2
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool reverted = first.size() == 2 && !first[0].on && first[0].key == 71 &&
                          first[1].on && first[1].key == 69;
    expect("A3 in the control zone reverts the borrow in place (B4 off, A4 on)", reverted,
           describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 57, 0.0);  // A3
    timeline.run(&release);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool restored = first.size() == 1 && !first[0].on && first[0].key == 69;
    expect("releasing A3 goes quiet (A4 off, C4 E4 G4 stay silent)", restored,
           describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 71, 0.0);  // B4
    timeline.run(&release);
  }
  {
    int offs = 0;
    for (const auto &e : g_emitted)
      if (!e.on) ++offs;
    expect("releasing B4 stops C4 E4 G4 (3 offs)", offs == 3, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }
  expect("the control release is silent", g_emitted.empty(), describe(g_emitted));

  // --- borrowing: the 4th voice (the bottom) borrows too --------------------------
  // C major 6th, the 4th borrowed from the next: A4 on top of degree 0 sounds
  // D4 E4 G4 A4 instead of C4 E4 G4 A4. Pressing C4 (the chord's own un-borrowed 4th,
  // below the top) reverts the chord in place: D4 goes off, C4 on.
  reset(plugin, &timeline);
  {
    InputEvents setup;
    setup.addParam(kBorrowFourth, 2);  // Next
    timeline.run(&setup);
  }
  holdControl(&timeline, {48});  // C2: C major 6th in force
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 69, 0.8);  // A4
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool borrowed = first.size() == 4 && first[0].key == 62 && first[1].key == 64 &&
                          first[2].key == 67 && first[3].key == 69;
    expect("4th borrowed next: A4 sounds D4 E4 G4 A4", borrowed, describe(first));
  }
  // With a borrow switch on, keys below the top pass through rather than triggering
  // their own chord: F4 is in-scale but neither a cancel note nor the top.
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 65, 0.8);  // F4
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool passed = first.size() == 1 && first[0].on && first[0].key == 65;
    expect("F4 below the borrowed chord passes through", passed, describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 65, 0.0);  // F4
    timeline.run(&release);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool passed = first.size() == 1 && !first[0].on && first[0].key == 65;
    expect("the F4 pass-through note-off passes through too", passed, describe(first));
  }
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 60, 0.8);  // C4, the un-borrowed 4th
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool cancelled = first.size() == 2 && !first[0].on && first[0].key == 62 &&
                           first[1].on && first[1].key == 60;
    expect("pressing C4 reverts the 4th borrow (D4 off, C4 on)", cancelled,
           describe(first));
  }
  // Releasing the cancel note ends the un-borrowed form: C4 goes quiet, the borrowed
  // D4 stays silent (a release sounds no new notes).
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 60, 0.0);  // C4
    timeline.run(&release);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool silentRestore = first.size() == 1 && !first[0].on && first[0].key == 60;
    expect("releasing C4 goes quiet (C4 off, D4 stays silent)", silentRestore,
           describe(first));
  }
  // Releasing the trigger key stops whatever the chord still sounds.
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 69, 0.0);  // A4
    timeline.run(&release);
  }
  {
    int offs = 0;
    for (const auto &e : g_emitted)
      if (!e.on) ++offs;
    expect("releasing A4 stops E4 G4 A4 (3 offs)", offs == 3, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }
  expect("the control release is silent", g_emitted.empty(), describe(g_emitted));

  // --- contrary motion -----------------------------------------------------------
  // C major 6th, close voicing, no borrow: A4 sounds C4 E4 G4 A4, bottom C4. With
  // contrary motion on, an in-scale press above the session key sounds its own chord
  // plus the contrary note (the scale walked down from the bottom by the steps
  // between the keys), and the session's original chord yields: its notes go off.
  reset(plugin, &timeline);
  {
    InputEvents setup;
    setup.addParam(kContraryMotion, 1);  // On
    timeline.run(&setup);
  }
  holdControl(&timeline, {48});  // C2: C major 6th in force
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 69, 0.8);  // A4 -> C4 E4 G4 A4, session starts
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool chord = first.size() == 4 && first[0].key == 60 && first[1].key == 64 &&
                       first[2].key == 67 && first[3].key == 69;
    expect("contrary on: A4 sounds C4 E4 G4 A4", chord, describe(first));
  }
  // B4 is one scale step above A4 (A, B): one step down from C4 is B3, added to B4's
  // own chord D4 F4 Ab4 B4. The session's chord yields: C4 E4 G4 A4 go off.
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 71, 0.8);
    timeline.run(&press);
  }
  {
    int ons = 0, offs = 0;
    for (const auto &e : g_emitted) (e.on ? ons : offs)++;
    const std::vector<int> keys = onKeys();
    const bool withContrary = ons == 5 && offs == 4 && keys.size() == 5 &&
                              keys[0] == 59 && keys[1] == 62 && keys[2] == 65 &&
                              keys[3] == 68 && keys[4] == 71;
    expect("B4 over the session adds B3 and yields the session chord", withContrary,
           describe(g_emitted));
  }
  // C5 is two scale steps above A4 (A, B, C): two steps down from C4 is A3, added to
  // C5's own chord E3 G3 A4 C5. The session chord is already gone, and A4 is still
  // held, so the session (and its bottom) survive for the walk.
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 72, 0.8);
    timeline.run(&press);
  }
  {
    int ons = 0;
    for (const auto &e : g_emitted)
      if (e.on) ++ons;
    const std::vector<int> keys = onKeys();
    const bool newNotes = ons == 5 && keys.size() == 5 && keys[0] == 57 && keys[1] == 64 &&
                          keys[2] == 67 && keys[3] == 69 && keys[4] == 72;
    expect("C5 over the session adds A3 (the walk still runs from C4)", newNotes,
           describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 72, 0.0);
    timeline.run(&release);
  }
  {
    int offs = 0;
    for (const auto &e : g_emitted)
      if (!e.on) ++offs;
    expect("releasing C5 stops its chord and A3 (5 offs)", offs == 5, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 71, 0.0);
    timeline.run(&release);
  }
  {
    int offs = 0;
    for (const auto &e : g_emitted)
      if (!e.on) ++offs;
    expect("releasing B4 stops its chord and B3 (5 offs)", offs == 5, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 69, 0.0);  // A4, the session key: ends it
    timeline.run(&release);
  }
  expect("releasing A4 is silent (its chord already yielded)", g_emitted.empty(),
         describe(g_emitted));
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }
  expect("the control release is silent", g_emitted.empty(), describe(g_emitted));

  // An out-of-scale press during the session passes through and leaves the session
  // and its chord intact: B4 afterwards still gets its contrary note against A4 and
  // yields the session chord.
  reset(plugin, &timeline);
  {
    InputEvents setup;
    setup.addParam(kContraryMotion, 1);
    timeline.run(&setup);
  }
  holdControl(&timeline, {48});
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 69, 0.8);  // A4, session starts
    timeline.run(&press);
  }
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 73, 0.8);  // C#5, out of scale
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool passed = first.size() == 1 && first[0].on && first[0].key == 73;
    expect("an out-of-scale press passes through (no contrary note)", passed,
           describe(first));
  }
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 71, 0.8);  // B4, still over the A4 session
    timeline.run(&press);
  }
  {
    int ons = 0, offs = 0;
    for (const auto &e : g_emitted) (e.on ? ons : offs)++;
    const std::vector<int> keys = onKeys();
    const bool withContrary = ons == 5 && offs == 4 && keys.size() == 5 &&
                              keys[0] == 59 && keys[1] == 62 && keys[2] == 65 &&
                              keys[3] == 68 && keys[4] == 71;
    expect("B4 still gets B3 from the A4 session and yields its chord", withContrary,
           describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 73, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 71, 0.0);
    timeline.run(&release);
  }
  {
    int offs = 0;
    for (const auto &e : g_emitted)
      if (!e.on) ++offs;
    expect("releases stop the pass-through and B4's chord with B3 (6 offs)", offs == 6,
           describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 69, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }
  expect("releasing A4 (chord already yielded) and the control note is silent",
         g_emitted.empty(), describe(g_emitted));
  g_emitted.clear();

  // Contrary motion disables the borrow revert: with the top borrowed from the next,
  // B4 sounds C4 E4 G4 B4, and pressing A4 (the un-borrowed top, below B4) triggers
  // its own chord instead of reverting the borrow.
  reset(plugin, &timeline);
  {
    InputEvents setup;
    setup.addParam(kBorrowTop, 2);  // Next
    setup.addParam(kContraryMotion, 1);
    timeline.run(&setup);
  }
  holdControl(&timeline, {48});
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 71, 0.8);  // B4 -> C4 E4 G4 B4, session starts
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool borrowed = first.size() == 4 && first[0].key == 60 && first[1].key == 64 &&
                          first[2].key == 67 && first[3].key == 71;
    expect("borrowed chord sounds C4 E4 G4 B4 (contrary on)", borrowed, describe(first));
  }
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 69, 0.8);  // A4, the un-borrowed top
    timeline.run(&press);
  }
  {
    int offs = 0;
    for (const auto &e : g_emitted)
      if (!e.on) ++offs;
    const std::vector<Emitted> first = g_emitted;
    // A4's own borrowed chord (degree 7: C# D F A) is B3 D3 F3 A4. It sits below the
    // session key, so no contrary note joins it, and nothing is released -- the
    // revert is off.
    const bool triggered = offs == 0 && first.size() == 4 &&
                           first[0].key == 59 && first[1].key == 62 && first[2].key == 65 &&
                           first[3].key == 69;
    expect("pressing A4 triggers its chord instead of reverting the borrow",
           triggered, describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 69, 0.0);
    timeline.run(&release);
  }
  {
    int offs = 0;
    for (const auto &e : g_emitted)
      if (!e.on) ++offs;
    expect("releasing A4 stops its chord (4 offs)", offs == 4, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 71, 0.0);
    timeline.run(&release);
  }
  {
    int offs = 0;
    for (const auto &e : g_emitted)
      if (!e.on) ++offs;
    expect("releasing B4 stops C4 E4 G4 B4 (4 offs)", offs == 4, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }
  expect("the control release is silent", g_emitted.empty(), describe(g_emitted));

  // --- note velocity scaling ----------------------------------------------------
  // Each note a trigger generates sounds at the trigger note's velocity times its own
  // percentage: here 4th (bottom) 50%, 3rd 100%, 2nd 150% and top 200% (the numbers
  // count from the top, as in the Drop voicing names). A trigger at 0.8
  // therefore comes out at 0.4, 0.8, 1.0 and 1.0 -- the over-100% values amplify and
  // clamp at the top of the velocity range. The contrary note runs at 75%.
  reset(plugin, &timeline);
  {
    InputEvents setup;
    setup.addParam(kVelBottom, 0.5);
    setup.addParam(kVelSecond, 1.0);
    setup.addParam(kVelThird, 1.5);
    setup.addParam(kVelTop, 2.0);
    setup.addParam(kVelContrary, 0.75);
    setup.addParam(kContraryMotion, 1);  // On
    timeline.run(&setup);
  }
  holdControl(&timeline, {48});  // C2: C major 6th in force
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 69, 0.8);  // A4 -> C4 E4 G4 A4
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool scaled = first.size() == 4 && first[0].key == 60 && first[1].key == 64 &&
                        first[2].key == 67 && first[3].key == 69 &&
                        std::abs(first[0].velocity - 0.4) < 1e-6 &&
                        std::abs(first[1].velocity - 0.8) < 1e-6 &&
                        first[2].velocity == 1.0 && first[3].velocity == 1.0;
    expect("chord notes sound at the trigger velocity times their percentage (clamped to 1.0)",
           scaled, describeVel(first));
  }
  // B4 over the A4 session: B4's own chord D4 F4 Ab4 B4 at the scaled velocities, then
  // the contrary note B3 at 75% of the press (0.6). The session chord yields after.
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 71, 0.8);
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool scaled = first.size() == 9 &&
                        first[0].on && first[0].key == 62 &&
                        std::abs(first[0].velocity - 0.4) < 1e-6 &&
                        first[1].on && first[1].key == 65 &&
                        std::abs(first[1].velocity - 0.8) < 1e-6 &&
                        first[2].on && first[2].key == 68 && first[2].velocity == 1.0 &&
                        first[3].on && first[3].key == 71 && first[3].velocity == 1.0 &&
                        first[4].on && first[4].key == 59 &&
                        std::abs(first[4].velocity - 0.6) < 1e-6 &&
                        !first[5].on && !first[6].on && !first[7].on && !first[8].on;
    expect("the contrary note takes its own percentage; the session chord yields",
           scaled, describeVel(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 71, 0.0);
    timeline.run(&release);
  }
  {
    int offs = 0;
    for (const auto &e : g_emitted)
      if (!e.on) ++offs;
    expect("releasing B4 stops its chord and the contrary note (5 offs)", offs == 5,
           describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 69, 0.0);  // A4, the session key
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }
  expect("releasing A4 and the control note is silent (its chord already yielded)",
         g_emitted.empty(), describe(g_emitted));
  g_emitted.clear();

  plugin->stop_processing(plugin);
  plugin->destroy(plugin);
  bhl_entry_deinit();

  if (g_failures)
  {
    printf("\n%d basic check(s) failed\n", g_failures);
    return 1;
  }
  printf("\nall basic checks passed\n");
  return 0;
}

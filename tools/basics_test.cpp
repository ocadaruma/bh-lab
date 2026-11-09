// The basics of the plugin, checked from the outside: scale selection in the control
// octave, stacked chords in the trigger zone, and pass-through of out-of-scale notes.

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

std::vector<int> onKeys()
{
  std::vector<int> keys;
  for (const auto &e : noteOns()) keys.push_back(e.key);
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

  // Pressing the cancel note again re-enters the un-borrowed form; the shared notes are
  // not re-triggered.
  g_emitted.clear();
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 69, 0.8);
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool reEntered = first.size() == 2 && first[0].on && first[1].on &&
                           first[0].key == 67 && first[1].key == 69;
    expect("pressing A4 again sounds G4 A4 only", reEntered, describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 69, 0.0);
    timeline.run(&release);
  }
  {
    int ons = 0;
    for (const auto &e : g_emitted)
      if (e.on) ++ons;
    expect("releasing A4 again sounds no notes either", ons == 0, describe(g_emitted));
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
  // not a cancel and does not trigger a chord either.
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

// The basics of the plugin, checked from the outside: chords, the control octave, and
// the trigger zone.

#include "clap_test_host.h"

using namespace bhl_test;

namespace
{

// Sets the plugin to a known state: C major triad, root position, default zones.
void reset(const clap_plugin_t *plugin, Timeline *timeline)
{
  InputEvents setup;
  setup.addParam(kQuality, 0);      // maj
  setup.addParam(kInversion, 0);    // root position
  setup.addParam(kControlRoot, 48); // C2
  setup.addParam(kTriggerStart, 60); // C3
  (void)plugin;
  timeline->run(&setup);
  timeline->runFor(2);
  g_emitted.clear();
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

  // --- chords -----------------------------------------------------------------
  reset(plugin, &timeline);
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 10, 60, 0.8);
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    const bool chord = first.size() == 3 && first[0].key == 60 && first[1].key == 64 &&
                       first[2].key == 67 && first[0].on && first[1].on && first[2].on;
    expect("one key sounds the whole chord", chord, describe(first));

    const bool together = chord && first[0].sample == first[1].sample &&
                          first[1].sample == first[2].sample;
    expect("the notes sound together", together);
  }

  // Holding the key must produce nothing further.
  g_emitted.clear();
  timeline.runFor(200);
  expect("holding the key sounds nothing more", g_emitted.empty(), describe(g_emitted));

  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 60, 0.0);
    timeline.run(&release);
  }
  {
    const bool released = g_emitted.size() == 3 && !g_emitted[0].on && !g_emitted[1].on &&
                          !g_emitted[2].on;
    expect("releasing the key stops the chord", released, describe(g_emitted));
  }

  // A second chord while the first is held: both sound, both release.
  reset(plugin, &timeline);
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 60, 0.8);
    timeline.run(&press);
  }
  {
    InputEvents second;
    second.addNote(CLAP_EVENT_NOTE_ON, 0, 65, 0.8);
    timeline.run(&second);
  }
  expect("a second key adds its own chord", onKeys().size() == 6, describe(g_emitted));

  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 60, 0.0);
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 65, 0.0);
    timeline.run(&release);
  }
  {
    int offs = 0;
    for (const auto &e : g_emitted)
      if (!e.on) ++offs;
    expect("both chords release", offs == 6, describe(g_emitted));
  }

  // Re-triggering a held key restarts its chord without leaking notes.
  reset(plugin, &timeline);
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
    for (const auto &e : g_emitted) (e.on ? ons : offs)++;
    expect("re-triggering replaces the chord cleanly", offs == 3 && ons == 3,
           std::to_string(offs) + " off, " + std::to_string(ons) + " on");
  }

  // --- the control octave -----------------------------------------------------
  reset(plugin, &timeline);

  // A white control key latches the quality and sounds nothing.
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 50, 0.8);  // D -> minor
    timeline.run(&press);
  }
  expect("a control key is silent", g_emitted.empty(), describe(g_emitted));
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_OFF, 0, 50, 0.0);
    timeline.run(&press);
  }
  expect("a control key-off is silent too", g_emitted.empty(), describe(g_emitted));

  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 60, 0.8);
    timeline.run(&press);
  }
  {
    const std::vector<int> keys = onKeys();
    const bool minor = keys.size() == 3 && keys[0] == 60 && keys[1] == 63 && keys[2] == 67;
    expect("latched quality is used by the next chord", minor, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 60, 0.0);
    timeline.run(&release);
  }

  // A black control key latches the inversion.
  reset(plugin, &timeline);
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 51, 0.8);  // D# -> 1st inversion
    timeline.run(&press);
  }
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 60, 0.8);
    timeline.run(&press);
  }
  {
    const std::vector<int> keys = onKeys();
    const bool firstInv = keys.size() == 3 && keys[0] == 64 && keys[1] == 67 && keys[2] == 72;
    expect("latched inversion is used by the next chord", firstInv, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 60, 0.0);
    timeline.run(&release);
  }

  // A# in the control octave doubles the root an octave up.
  reset(plugin, &timeline);
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 58, 0.8);  // A# -> octave double
    timeline.run(&press);
  }
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 60, 0.8);
    timeline.run(&press);
  }
  {
    const std::vector<int> keys = onKeys();
    const bool doubled = keys.size() == 4 && keys[0] == 60 && keys[1] == 64 && keys[2] == 67 &&
                         keys[3] == 72;
    expect("octave double adds the root an octave up", doubled, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 60, 0.0);
    timeline.run(&release);
  }

  // --- the zones ---------------------------------------------------------------
  // Moving the trigger zone moves what triggers. The control zone has to move out of the
  // way first, since it wins where the two overlap.
  reset(plugin, &timeline);
  {
    InputEvents setup;
    setup.addParam(kControlRoot, 36);
    setup.addParam(kTriggerStart, 48);
    timeline.run(&setup);
  }
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 48, 0.8);
    timeline.run(&press);
  }
  {
    const std::vector<int> keys = onKeys();
    const bool c2 = keys.size() == 3 && keys[0] == 48 && keys[1] == 52 && keys[2] == 55;
    expect("the trigger zone start decides what triggers", c2, describe(g_emitted));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 48, 0.0);
    timeline.run(&release);
  }

  // Notes below both zones pass through untouched, on and off.
  reset(plugin, &timeline);
  {
    InputEvents press;
    press.addNote(CLAP_EVENT_NOTE_ON, 0, 36, 0.7);
    timeline.run(&press);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    expect("a low note passes through", first.size() == 1 && first[0].on && first[0].key == 36,
           describe(first));
  }
  g_emitted.clear();
  {
    InputEvents release;
    release.addNote(CLAP_EVENT_NOTE_OFF, 0, 36, 0.0);
    timeline.run(&release);
  }
  {
    const std::vector<Emitted> first = g_emitted;
    expect("its note-off passes through too",
           first.size() == 1 && !first[0].on && first[0].key == 36, describe(first));
  }

  // A choke also stops the chord.
  reset(plugin, &timeline);
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
    expect("a choke releases the chord", offs == 3, describe(g_emitted));
  }

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

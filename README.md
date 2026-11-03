# bh-lab

An Audio Unit note effect based on **Barry Harris's stacked diminished scales**
(see `design.md` and `chord-to-scale-uniqueness.md`).

- Notes **held in the control octave** identify the scale: which of the four chord
  scales (major 6th, minor 6th, 7th or 7th b5 diminished) and at which key. The
  identification follows the held notes live and is latched when they are released;
  when the held notes are ambiguous the families are preferred in the order
  6° > m6° > 7° > 7b5°.
- Keys in the **trigger zone** sound the close-position stack of the selected scale
  with the pressed note on top (e.g. C major 6th, E4 pressed → G3 A3 C4 E4). A
  trigger key outside the selected scale is passed through untouched.
- The **voicing type** is a parameter; the first version supports close position only.

Parameters (automatable):

| id | name | range | default |
|----|------|-------|---------|
| 0 | Voicing | Close | Close |
| 1 | Control Zone Root | C-2 .. G5 | C2 |
| 2 | Trigger Zone Start | C-1 .. G9 | C3 |

## Building

```sh
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build
```

The AUv2 `.component` lands in `build/bh-lab_assets/` and is copied to
`~/Library/Audio/Plug-Ins/Components` after each build. To look at the UI without a
host:

```sh
./build/gui-preview preview.png playing
```

For a live UI (and behaviour) without a host, `dev-host` runs the real plugin core and
view in a small window driven by the computer keyboard (home row = trigger zone, bottom
row = control octave; see `tools/dev_host.mm`). Logic is only needed for final
integration checks:

```sh
./build/dev-host
```

## Layout

- `src/chord_engine.{h,cpp}` — the four chord scales, scale identification and stacked
  chord building, free of any plugin API.
- `src/bhl_plugin.cpp` — the CLAP plugin (parameters, event routing, state, ports).
- `src/bhl_gui.{h,mm}` — the Cocoa view: the selected scale, the generated-chord
  keyboard and the control octave.
- `src/bhl_entry.{h,cpp}` — the CLAP entry point.
- `deps/` — git submodules: CLAP SDK, AudioUnit SDK and clap-wrapper (pinned in
  `.gitmodules`; clone with `--recursive`).
- `tools/` — a synthetic CLAP host (`basics-test`) and a view renderer (`gui-preview`).

## Design notes

- All plugin logic lives in the `bh-lab-impl` static library; the AU format is just its
  entry-point translation unit plus the wrapper.
- Parameter ids are frozen: automation written against an id replays into whatever that
  id means later, so parameters are only ever appended, never inserted.
- Chords are released by the note ids they were started with; note-offs the host's queue
  refuses are held and retried on the next block, so a release is never dropped.
- The window is fixed size; the view redraws on a timer only when the audio thread's
  snapshot changes.

# bh-lab

An Audio Unit note effect that turns single keys into chords.

- Keys in the **control octave** latch the chord **quality** (white keys) and **inversion**
  (black keys). They are silent and stay in effect until changed.
- Keys in the **trigger zone** each generate a close-position chord rooted on the key
  pressed.
- Notes below both zones pass through untouched.

Parameters (automatable):

| id | name | range | default |
|----|------|-------|---------|
| 0 | Chord Type | maj, m, maj7, m7, 6, 7, sus4 | maj |
| 1 | Inversion | root, 1st, 2nd, 3rd, octave double | root |
| 2 | Control Zone Root | C-2 .. B5 | C2 |
| 3 | Trigger Zone Start | C-1 .. G9 | C3 |

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

## Layout

- `src/chord_engine.{h,cpp}` — chord generation, free of any plugin API.
- `src/bhl_plugin.cpp` — the CLAP plugin (parameters, event routing, state, ports).
- `src/bhl_gui.{h,mm}` — the Cocoa view: the generated-chord keyboard and the control
  octave.
- `src/bhl_entry.{h,cpp}` — the CLAP entry point.
- `deps/` — git submodules: CLAP SDK, AudioUnit SDK and clap-wrapper (pinned in `.gitmodules`; clone with `--recursive`).
- `tools/` — a synthetic CLAP host (`basics-test`) and a view renderer (`gui-preview`).

## Design notes

- All plugin logic lives in the `bh-lab-impl` static library; the AU format is just its
  entry-point translation unit plus the wrapper.
- Parameter ids are frozen: automation written against an id replays into whatever that id
  means later, so parameters are only ever appended, never inserted.
- Chords are released by the note ids they were started with; note-offs the host's queue
  refuses are held and retried on the next block, so a release is never dropped.
- The window is fixed size; the view redraws on a timer only when the audio thread's
  snapshot changes.

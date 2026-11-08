# bh-lab

An Audio Unit note effect based on **Barry Harris's stacked diminished scales**
(see `design.md` and `chord-to-scale-uniqueness.md`).

- Notes **held in the control octave** identify the scale: which of the four chord
  scales (major 6th, minor 6th, 7th or 7th ♭5 diminished) and at which key. The
  identification follows the held notes live and is latched while they are held or
  released; when the held notes are ambiguous the families are preferred in the order
  6dim > m6dim > 7dim > 7♭5dim. While nothing is held in the control octave the plugin is in
  pass-through mode: trigger keys pass through untouched.
- Keys in the **trigger zone** sound the stack of the selected scale with the pressed
  note on top (e.g. C major 6th, E4 pressed → G3 A3 C4 E4). A trigger key outside the
  selected scale is passed through untouched.
- The **voicing type** is a parameter: close position, or the same stack with the
  second note from the top dropped an octave (drop2), the third (drop3), or the
  second and fourth (drop2&4).

Parameters (automatable):

| id | name | range | default |
|----|------|-------|---------|
| 0 | Voicing | Close, Drop2, Drop3, Drop2&4 | Close |
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
row = control octave; see `tools/dev_host.mm`):

```sh
./build/dev-host
```

For host-facing behaviour there is `au-midi-probe`, which drives the installed
`.component` the way a host does (state restore, enable-time renders). Logic is only
needed for final integration checks:

```sh
./build/au-midi-probe
```

## Credits

The plugin structure, UI and tools are based on [zzfutang/r-chord](https://github.com/zzfutang/r-chord).

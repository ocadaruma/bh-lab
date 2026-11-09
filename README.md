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
- Each of the **top three voices can be borrowed** from the chord above or below it in
  the scale (the borrow parameters). Borrowing transforms the scale's stacked chords
  into new chords, and the trigger rules apply against the transformed chords -- e.g.
  with the top voice borrowed from the next chord, C major 6th with B on top sounds
  C E G B instead of D F Ab B. While a borrowed chord is held, keys below its top
  note do not trigger: pressing one of its own un-borrowed top notes releases the
  borrowed notes and sounds the originals instead, and any other note below the top
  (in-scale or not) passes through untouched. Releasing the original trigger key while
  a cancel note is still held transfers the chord to that note, which then owns it;
  a key release never re-triggers notes.

Parameters (automatable):

| id | name | range | default |
|----|------|-------|---------|
| 0 | Voicing | Close, Drop2, Drop3, Drop2&4 | Close |
| 1 | Control Zone Root | C-2 .. G5 | C2 |
| 2 | Trigger Zone Start | C-1 .. G9 | C3 |
| 3 | Borrow Top | None, Prev, Next | None |
| 4 | Borrow 2nd | None, Prev, Next | None |
| 5 | Borrow 3rd | None, Prev, Next | None |

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

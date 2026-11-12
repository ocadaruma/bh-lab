Based on Barry Harris's diminished scale, the plugin triggers a chord

## what's Barry's stacked diminished scale

- There are 4 diminished scale family
  * Major 6th diminished scale
  * Minor 6th diminished scale
  * 7th diminished scale
  * 7th b5 diminished scale
- All of them have 8 notes, and when we play stacked 3 notes by skipping one note from any scale note, all of them follow the same pattern:
  * alternate 2 chords, with inversions
  * Major 6th diminished scale:
    * Notes: C D E F G Ab A B
    * Stacked (harmonized) scale:
      - C E G A (C major 6th)
      - D F Ab B (D diminished)
      - E G A C (C major 6th with E root)
      ...
  * Minor 6th diminished scale:
    * Notes: C D Eb F G Ab A B
    * Stacked (harmonized) scale:
      - C Eb G A (C minor 6th)
      - D F Ab B (D diminished)
      - Eb G A C (C minor 6th with Eb root)
      ...
  * 7th diminished scale:
    * Notes: C D E F G Ab Bb B
    * Stacked (harmonized) scale:
      - C E G Bb (C7)
      - D F Ab A (D diminished)
      - E G Bb C (C7 with E root)
      ...
  * 7th b5 diminished scale:
    * Notes: C D E F Gb Ab Bb B
    * Stacked (harmonized) scale:
      - C E Gb Bb (C7b5)
      - D F A B (D diminished)
      - E Gb Bb C (C7b5 with E root)
      ...

## control zone

- Depending on the pressed notes, it will select the key and the scale type
  * If C E G A is pressed,
    - it means the scale is C major 6th diminished scale
      * scale selection uniqueness is guaranteed. See chord-to-scale-uniqueness.md for more details.
  * If C E F# A is pressed,
    - it means the scale is A minor 6th diminished scale
  * If ambiguous notes are pressed, prefer major 6th > minor 6th > 7th > 7th b5
    * if only C is pressed, it falls into major 6th diminished scale
    * if C and Eb are pressed, it falls into minor 6th diminished scale
    * ...
- When no note is held in the control zone, the plugin is in pass-through mode: trigger
  keys pass through untouched instead of sounding the last selected scale's stack

## trigger zone

- When a note is pressed in this zone, it will trigger a chord based on the selected scale around the pressed note
  * How chord stacked is based on voicing type, described below
- If a note that is not in the scale is pressed, it will be passed through without any change

## voicing type
- This is a selectable for how the chord is stacked
- The options are close position and drop voicings, all with the pressed note as the top note of the chord
  * close: the close-position stack
    * i.e. If the scale is C major 6th diminished scale, and the pressed note is E4, the chord will be G3 A3 C4 E4
  * drop2: the same stack with the second note from the top moved down an octave (G2 A3 C4 E4)
  * drop3: the third note from the top moved down an octave (G3 A2 C4 E4)
  * drop2&4: the second and fourth notes from the top moved down an octave (G2 C3 A3 E4)

## Borrowing
- Borrowing is a concept to substitute a part of the chord with a note from next or previous chord in the scale, to make the chord sound more interesting.
  * e.g. Assuming C major 6th diminished scale is selected
    - For chord C E G A, if we "borrow" a note for A from next, it will become C E G B
    - Similarly, if we borrow notes for G and A from next, it will become C E Ab B
    - Borrowing is allowed for top 3 notes of the chord, and borrow direction is either from next or previous chord in the scale
- In this plugin, this is implemented by below:
  * Add 3 "borrow" switches for top 3 notes, each switch has 3 states: borrow from previous, no borrowing, borrow from next
    * Borrowing will transform the stacked chords to new chords
  * The trigger rule is same, but it against the new transformed chords. e.g.
    * Assume C major 6th diminished scale, and close voicing
    * Originally, when we press B, it will trigger D F Ab B
    * But if we set "borrow top note from next", C E G B will be triggered instead of D F Ab B
  * Also, when any borrow switch is changed, trigger-zone behavior changes a bit:
    * When any borrow switch is set and borrowed not is pressed, if (below-top) non borrow note is additionally pressed, all (already triggered) borrowed notes will be released, and the chord will be triggered with the non-borrow notes instead
    * The revert is in place: the chord becomes the un-borrowed stack at the octave it was triggered, no matter where the cancel note was struck
    * The cancel note works from any zone, including the control octave and below the trigger zone
    * e.g. Assume C major 6th diminished scale, and close voicing
      - If we set "borrow top and 2nd-top notes from next", and press B, it will trigger C E Ab B first
      - In the middle of holding B, if we additionally press A, it will release Ab B, and trigger G and A instead

## Contrary motion
- Contrary motion is a note movement where the right/left hands go in opposite directions: while the right hand moves up, the left hand moves down.
- It is an on/off switch. When on, the borrow revert behavior (above) is disabled.
- The first in-scale trigger-zone press starts a contrary-motion session, which lives until that key is released. While the session is held, in-scale presses above the session key trigger their own chord, plus an additional "contrary" note below:
  * The offset is the number of scale steps between the session key and the pressed key
  * The contrary note is found by walking the scale down by that offset from the session chord's bottom note (its dropped bottom for drop voicings); walking the scale guarantees the contrary note is always in-scale
  * The contrary note is released together with the pressed key, at the pressed key's velocity
  * Out-of-scale presses pass through without a contrary note, and do not disturb the session
- e.g. Assume C major 6th diminished scale, close voicing, no borrow
  - Press A => C4 E4 G4 A4 (the session starts; bottom note C4)
  - Press B while A is held => B3, plus B's own chord D4 F4 Ab4 B4 (A->B is one scale step; one step down from C4 is B)
  - Press C while A is held => A3, plus C's own chord E3 G3 A3 C5 (A->C is two scale steps; two steps down from C4 is A)

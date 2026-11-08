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

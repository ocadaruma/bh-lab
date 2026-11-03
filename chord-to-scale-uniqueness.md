# Chord → Scale identification in Barry's diminished-scale universe

**Question.** Given a four-note chord, which of Barry's chord-scales does it
belong to — and is the answer unambiguous?

Worked example: **E G A C** — a 6th-diminished chord (interval skeleton
m3–M2–m3), i.e. C major 6 with E in the bass.

**Answer.** Yes, unambiguous: **E G A C is the 6th chord of the C major 6th
diminished scale.** No other (family, key) in Barry's four-scale universe
produces this exact stacked chord.

---

## The universe

Barry's four chord-scales (verified against barryharrismethod.com).

All four C-root scales contain the **same** diminished chord,
**B° = {B, D, F♭, A♭}**.

They differ only in the 4-note chord they stack with:

| family | C-root scale (8 notes) | 4-note chord |
|---|---|---|
| Major 6th dim (6°) | C D E F G A♭ A B | C6 = {C E G A} |
| Minor 6th dim (m6°) | C D E♭ F G A♭ A B | Cm6 = {C E♭ G A} |
| 7th dim (7°) | C D E F G A♭ B♭ B | C7 = {C E G B♭} |
| 7th b5 dim (7♭5°) | C D E F G♭ A♭ B♭ B | C7♭5 = {C E G♭ B♭} |

Played as a "scale of chords", each scale yields skip-one stacked 4-note chords,
alternating the named chord and the shared diminished (each in 4 inversions).

Example, 6° in C:

```
C6    D°    E6    F°    G6    A♭°   A6    B°
C E G A   D F A♭ B   E G A C   F A♭ C E♭   G B♭ D F   …
```

E G A C is exactly the C6 with E in the bass.

---

## Exhaustive check

All **4 families × 12 keys = 48 scales** were tested, asking whether {C E G A}
appears as a skip-one stacked chord (positions p, p+2, p+4, p+6 of the 8-note
scale).

| family | keys giving {C E G A} as a stacked chord |
|---|---|
| Major 6th dim | **C only** |
| Minor 6th dim | — |
| 7th dim | — |
| 7th b5 dim | — |

**Why (structurally):**

- {C E G A} has interval skeleton **m3–M2–m3**.
- Only the 6th family's stacked 4-chord has that skeleton
  (Cm6 / C7 / C7♭5 are built from m3–m3–M2).
- Within 6°, the stacked 6-chords are always {X, X+4, X+7, X+9} for key X.
  That set equals {C E G A} **only for X = C**.

---

## Conclusion

- E G A C identifies its scale unambiguously:
  the **C major 6th diminished scale**.
- It is the C6, appearing at bass notes C, E, G, A as inversions,
  alternating with D°, F°, A♭°, B°.
- Chord→scale identification must be made in the **stacked-chord** sense
  (Barry's sense: "once you know which scale a chord belongs to, you know
  where you can move it").

# TinyVoc v0.9.0 — Public Preview (Beta)

A vocal processor for Ableton Move, as a [Schwung](https://github.com/charlesvestal/schwung) audio FX module. Sing into the Move, play the pads, and get a hard-tuned lead, a vocoder, a choir, a tape drop, a freeze loop and more — on 24 knobs over three pages.

This is the first public build. It works on a real Move and is covered by about two million automated checks, but it was tuned on synthetic voices and measured with a built-in mic, not yet polished by ear across many singers. Expect rough edges, and tell us what you hear.

## Highlights

- **7 characterful modes** — Snap, Robot, Chords, Morph, Prism, Double, Tape. One tracker, seven personalities.
- **Hands-free Chord Latch** (MORE knob 7) — play a chord once, take your hands off the pads, and sing. Works in Robot, Chords and Prism. A new chord replaces the old one; switching Latch Off lets go instantly.
- **Built-in mic conditioning** — a 120 Hz high-pass on the input so pad taps and chassis thumps don't open the gate or muddy the vocoder (a 40 Hz thump drops about 10 dB). Robot's consonant detector (2.5 kHz and up) keeps "s", "t" and "k" crisp.
- **Carrier octave folding (Robot)** — Move's pads sit around E5 and above, which made the vocoder whistle. Held pads now keep their note names and chord shape but are moved by whole octaves into C3–B4, where a vocoder carrier is intelligible.
- **Click-free mode switching** — the old mode fades out and the new one fades in over 5 ms, so you can spin the Mode knob while singing.
- **Instant Demo** (SPACE knob 6) — a built-in sung phrase tours all 7 modes, four bars each, at the Move's tempo. Turn the Mode knob to stop the tour on a mode you like.
- **One Gate knob** (MORE knob 6) — *Auto* is its lowest setting, then a manual threshold from −60 to 0 dB in 3 dB steps. Nothing hides or moves.
- **Context-aware Latch display** — in Snap, Morph, Double and Tape, where Latch does nothing, knob 7 shows a dimmed `---` in the same place.
- **Experimental Freeze / Loop** — a punch-in looper for instant vocal drones and glitchy micro-loops. See the README for what it is and isn't.

## Changelog

### Features
- Chord Latch (`latch`): Robot, Chords, Prism. Presses while another pad is held add to the chord (up to four, oldest drops out); a press after letting go replaces it; MIDI all-notes-off releases it. Lead modes never latch.
- Freeze: Off / Rec / Loop, up to 16 s, with a Freeze Len of Free, 1, 2 or 4 bars. With the Move's MIDI clock output on, Rec arms and starts exactly on the next downbeat; the take then loops by itself. OLED banner shows ARMED / REC / LOOP.
- Beauty macro (MORE knob 8): polish for Snap, Robot, Chords and Prism — stereo width, detune, air, warmth (Morph, Double and Tape ignore it).
- Plate reverb (SPACE knob 4).
- Demo player with a tempo-synced tour and a pin-on-Mode-turn.
- Per-mode knob names: the two Char knobs and Mod are renamed for the active mode (Formant, Shift, Level, Throat, Voicing, Detune, Drop …).
- `diag` readout for checking levels on a real device (developers).

### DSP & acoustic improvements
- 120 Hz one-pole input high-pass on the whole input.
- Robot: consonant detector now high-passes at 2.5 kHz (it was 5 kHz and missed "k"); envelopes release in 15 ms so fast syllables keep their gaps; output leveller and makeup re-gained for headroom.
- Robot: exact digital silence whenever the voice gate is closed, even with a chord held.
- Robot: pad carrier folded into C3–B4 as a block, keeping the chord's shape.
- Auto: a true 10 ms RMS voice gate at −42 dBFS, with a 50 ms hold and a −40 dB expander; the input auto-gain is capped at +12 dB and freezes when the gate closes, so pauses no longer pump room noise.
- Pitch tracker: fundamental-bias margin and an octave-glitch hold remove octave flips on vowels with a strong second harmonic.
- PSOLA voices: grains are checked against the write head, minimum grain 5 ms, 5 ms smooth cuts — removes the clicks on big pitch jumps. Retune 0 is a 4.5 ms snap.
- Chords and Prism voices are formant-preserving, kept within a sensible register of the sung note, with unvoiced sound passed through a high-pass so consonants don't bleed as pitch.
- 5 ms mode-switch crossfade; the pitch tracker keeps listening while Robot runs, so switching out of Robot reads the live voice instead of stale audio.

### UI / UX
- Three pages — Tune, Space, More — with fixed encoder positions. Nothing is hidden, nothing reorders.
- OLED pitch scope on Tune (target note and cents needle; Robot shows its 16 bands), with Auto-boost, held-pad and note-on indicators.
- Freeze banner across SPACE knobs 7–8; Latch indicator on MORE knob 7.
- Gate and Auto unified (see above).

## Known limitations
- Tuned on synthetic voices and measured on a built-in mic; real-voice tuning of every mode (Robot makeup and sibilance, tracker clarity, drive amounts) is still to do.
- The on-screen look of the newest widgets (the Latch and Gate cells, per-mode knob names) has been tested against a mock display, not yet by eye on the Move.
- Demo's on-screen mode stepping is not yet confirmed on the hardware.
- Freeze is experimental: mono, 16-bit, no overdub or editing; behaviour with real voices is not tuned.
- The consonant detector finds "s" and "t" reliably in tests; "k" is the weakest.
- Pitch tracking works from about 70 Hz to 1 kHz; shifted voices are limited to ±1 octave.
- Routing audio from other Move tracks into TinyVoc, and headset or external mics, are untested.
- No licence file yet — all rights reserved until one is added.

## Install

Download `tinyvoc-v0.9.0.tar.gz`, then, with Schwung on your Move and SSH working:

```bash
ssh ableton@move.local "tar -C /data/UserData/schwung/modules/audio_fx -xzf - && chmod 755 /data/UserData/schwung/modules/audio_fx/tinyvoc/tinyvoc.so" < tinyvoc-v0.9.0.tar.gz
ssh ableton@move.local "nohup /data/UserData/schwung/restart-move.sh >/dev/null 2>&1 &"
```

Restarting Schwung is needed the first time so the screen widgets load. Full manual: [README](README.md).

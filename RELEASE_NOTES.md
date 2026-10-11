# TinyVoice v1.0.0 — Public Preview (Beta)

TinyVoice (module id `tinyvoice`, library `tinyvoice.so`, archive `TinyVoice.tar.gz`) grows from seven modes to eleven (thirteen with the two older ones kept): the vocoder modes gain a talkbox, and five voice transformers join them in one flat Mode list.

**Upgrading.** This release uses a new module id, so it installs beside any earlier preview; `scripts/install.sh` removes the old install folders. A set saved with an earlier preview names the old id, so add TinyVoice to it again. A saved `state` loads: mode names are unchanged, and `Talkbox` still loads as the default mode.

## What is new

- **Nightcall** — a DigiTech-Talker-style talkbox on the vocoder engine: a narrow pulse carrier (12–28 % duty, 20 % at the middle of its knob) with about 10 % white noise, a steep 4th-order 250 Hz low-cut, a vocal-cavity peak at 1.2 kHz, a crisp high shelf, and 15–20 % of your own unvoiced breath (above 3.5 kHz) bled in. Pads play the carrier; a drone does if none is held.
- **Scream** — real-time pitch and formant change (pitch-synchronous, PSOLA): −5 semitones, formants x0.8, a steep 350 Hz–3.4 kHz telephone band and asymmetric soft clipping.
- **Dalek** — a single-sideband frequency shifter on a 4th-order IIR Hilbert pair (I and Q within 2° from 150 Hz to 6 kHz), linear −500 … +500 Hz, plus the classic 30 Hz ring modulator.
- **HardTune** — the robotic quantizer: zero slew, to Key and Scale or to a held pad, formant preserved, 0–200 % correction depth.
- **Harmony** — a three-tap granular stack (−12, +7, +12 st) with micro-delays, drift and panning. Held pads replace the taps with the chord's intervals above its lowest note. The grain is snapped to a whole number of your pitch periods, which keeps a held note phase-coherent through every cross-fade.
- **Walkie** — a 1.8 kHz resonant horn, diode clipping with unequal forward drops, and a PTT squelch burst on every gate open and close.
- **Prism is its own sound now**: the same hard-snapped harmony stack (Refract 0 is exactly the old engine), refracted — voices nudged off the semitone grid, a frequency-shifted pair so partials sit at non-integer multiples, and a slowly modulated comb (Shimmer). Chords stays strictly triadic.
- **One home page.** The first page is now **Voice**: Mode, five contextual knobs named per mode, Mix and **Level**. Pitch (Key, Scale, Retune, Glide, scope, Beauty), Space (Delay, Feedback, Reverb, Spread, Tone, Demo, Freeze) and More (Gate, Latch) hold what you set once. In the older modes the last two contextual knobs are the same value as the Retune / Beauty / Glide / Tone / Delay knobs elsewhere.
- **A −6.1 dBFS output ceiling** (`TINYVOICE_OUTPUT_HEADROOM`) on the new modes, after Spread, delay and reverb and before Level: nothing from a resonant peak, a squelch burst or distortion passes it at Level 0 dB. The older modes are unchanged.

## Under the hood

- New `voice.c` (Dalek, Harmony, Walkie) and `ssb.h` (Hilbert pair and phasor, shared with Prism); Nightcall is a flagged voicing of `daft.c` (checked against v0.9.0 on the same 2 s of input with pads: Robot's output is bit-identical, and so is every older mode at Spread 0, Prism at Refract 0; with Spread up the only change is the 10 ms smoothing of the chorus amount below); Scream and HardTune are modes of `modes.c`.
- Every wet path under 15 ms (Harmony 12.1 ms worst case, Scream and HardTune under 11 ms at 70 Hz, Dalek 0.6 ms, the vocoders and Walkie none beyond their filters); the dry path is delay-compensated to match.
- The Spread chorus amount is now smoothed (10 ms). It used to be zero while a vocoder ran and then jumped on, replaying the old engine's last 9 ms as a step on every Robot-to-pitch-mode switch.
- Tests: about 2.02 million checks, including the Hilbert phase and gain, sideband rejection, ring sidebands, the squelch on both edges, harmony intervals (default and MIDI), measured latency, the telephone and talkbox responses, Prism's refraction, MIDI deafness of the pitch-agnostic modes, the −6.1 dBFS ceiling at extreme knob settings, and a finite / bounded / no-denormal sweep of every mode against noise, square waves, DC and impulses. The whole suite also runs clean under UBSan.
- The build is `-std=c11 -Wall -Wextra -Werror -pedantic`. A `Makefile` adds `make test` and `make arm` (it just calls the scripts; pinned to LF line endings).

## Decisions to know about

- **Morph and Double are kept**, after Walkie in the list: they were working modes, and the brief said to keep working code. Drop them from the Mode list in one place (`k_mode_opts` in `dsp.c`, `MODES` in the generator) if you want exactly eleven.
- **Robot and Snap still follow pads.** The brief asked for them to ignore MIDI, but their pad behaviour is what they are (the carrier, the target note) and "retain exact existing behaviour" wins. Scream, Dalek, Walkie and Tape ignore pads, as asked; HardTune, Harmony and Nightcall use them.
- **Scream is PSOLA, not WSOLA.** The existing pitch-synchronous voice already moves pitch and formant independently and reads the tracker's delay line; it is what Snap, Chords and Prism run on.
- **The page layout is Voice / Pitch / Space / More**, not one page: the single page the brief describes does not exist in the codebase (it was three pages of 24 knobs), and Freeze, Reverb, Latch, Key, Scale, Gate and the rest are features. The home page is exactly the requested eight; the others are the menu behind the jog wheel, as before.
- **Verified on a Move, not yet by ear.** The package was installed on a Move and `scripts/smoke_on_device.sh` passes: all 13 modes load, are audible and take 0.10–0.50 ms per 128-frame block (Prism 0.50 the heaviest; Nightcall 0.18, Scream 0.33, Dalek 0.10, HardTune 0.31, Harmony 0.35, Walkie 0.11; the budget is ~2.3 ms). The on-screen layout and how the new modes sound on a real voice are still to be checked.

---

# TinyVoice v0.9.0 — Public Preview (Beta)

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
- Routing audio from other Move tracks into TinyVoice, and headset or external mics, are untested.
- No licence file yet — all rights reserved until one is added.

## Install

Download `tinyvoice-v0.9.0.tar.gz`, then, with Schwung on your Move and SSH working:

```bash
ssh ableton@move.local "tar -C /data/UserData/schwung/modules/audio_fx -xzf - && chmod 755 /data/UserData/schwung/modules/audio_fx/tinyvoice/tinyvoice.so" < tinyvoice-v0.9.0.tar.gz
ssh ableton@move.local "nohup /data/UserData/schwung/restart-move.sh >/dev/null 2>&1 &"
```

Restarting Schwung is needed the first time so the screen widgets load. Full manual: [README](README.md).

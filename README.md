# TinyVoc

**A vocal sound-design toy and live-performance box for Ableton Move.** Sing into the Move, lean on the pads, and turn your voice into a hard-tuned robot, a one-finger choir, a tape drop or a frozen drone. Seven modes, three pages of knobs, no menus.

TinyVoc is an audio FX module for [Schwung](https://github.com/charlesvestal/schwung). **v0.9.0 is a public preview (beta)** — it runs well on a Move, but it was tuned on synthetic voices and a built-in mic, not by ear across lots of singers. See [Known limitations](#known-limitations).

- [The seven modes](#the-seven-modes)
- [Setup and gain staging](#setup-and-gain-staging)
- [Performance walkthroughs](#performance-walkthroughs): Chord Latch · Instant Demo · Freeze / Loop
- [Parameter map](#parameter-map)
- [Install and build](#install-and-build)
- [Known limitations](#known-limitations)

## The seven modes

All the pitch modes share one tracker (about 70 Hz–1 kHz). **Pads** are Move's pads, played as MIDI notes: they are never snapped to Key or Scale — a pad plays exactly the note you press.

| Mode | Polyphonic pad chords? | Best used for | Vibe |
|---|---|---|---|
| **Snap** | No — a held pad sets the target note (newest wins) | Hard-tuned leads, "Believe"-style snaps, playing your voice from the pads | Glassy, instant, unmistakably tuned |
| **Robot** | **Yes** — up to 4 pads drive the carrier. With no pad held, a drone (root + fifth + octave in your Key) carries the voice | Intelligible vocoder leads, synth-choir pads you sing words through | Daft-Punk-ish vocoder with a warm ensemble option |
| **Chords** | **Yes** — one pad = the root of an in-key voicing; two or more pads play exactly those notes | Instant backing harmonies from one finger | A small, natural choir that follows your note |
| **Morph** | No | Formant shifting — deeper or smaller "throat" — pushed into drive | Gender-bending, creature-y, gritty |
| **Prism** | **Yes** — same pad rules as Chords | A 0 ms hard-snapped harmony stack with tape warmth | Tight, bright, slightly saturated choir |
| **Double** | No — a held pad sets the target note | Hard-tuned lead plus two detuned, slightly late doubles | Wide, thick, "tracked it twice" |
| **Tape** | No — pads do nothing | Varispeed drops with wow and flutter, dark top, saturation | Slowing reel, worn cassette |

### The three Char knobs change with the mode

SPACE knobs 1–2 and MORE knob 5 are renamed for the active mode:

| Mode | SPACE 1 | SPACE 2 | MORE 5 |
|---|---|---|---|
| Snap | **Formant** (±12 st) | **Sensitivity** (how readily it tracks) | **Vibrato** |
| Robot | **Shift** (formant, ±12 st) | **Noise** (consonant hiss) | **Ensemble** (chorus depth) |
| Chords | **Level** (harmony vs. lead) | **Voicing** (standard ↔ wide) | **Humanize** |
| Morph | **Throat** (±12 st) | **Drive** | **Wobble** |
| Prism | **Voicing** (spread and pan width) | **Warmth** | **Humanize** |
| Double | **Detune** (5–30 ct) | **Width** | **Drift** |
| Tape | **Drop** (pitch, 0 to −12 st) | **Flutter** | **Saturate** |

## Setup and gain staging

TinyVoc is an **audio FX**: it processes the audio in the Schwung slot it sits in. For your voice, put Schwung's **Line In** module in the slot as the sound generator (it brings the Move's physical input into the slot) and TinyVoc after it as the audio FX.

> **Feedback warning.** Singing into a mic with the speakers on is a feedback loop. Line In warns about it and holds the slot bypassed until it's safe. Use headphones for the first try.

### Built-in mic

- **Distance.** Start 4-24 inches from the mic. Closer gets you more voice and less room.
- **Enunciate.** Over-pronounce consonants: "s", "t" and "k" are what make a vocoder understandable, and the mic's small capsule needs the help.
- **Pad taps.** TinyVoc high-passes its input at **120 Hz**, so the thump of hitting a pad, and chassis vibration, doesn't open the gate or muddy the vocoder (a 40 Hz thump drops about 10 dB). It's a gentle filter: a hard pad hit still leaks a little.
- **Level.** In **Auto** the scope shows how much boost TinyVoc is adding (`+NN`, up to +12 dB). If it sits at `+12`, the mic is too quiet to be lifted fully — raise Line In's **Input Trim** until it falls below.

### Line-in and headsets

Line In has Line, Guitar and Phono input types and an Input Trim of −12 to +40 dB. A line-level source needs no trim; a weak source (a headset mic, say) may want Guitar or extra trim. **Headset and external mics are untested** with TinyVoc.

Because TinyVoc processes whatever audio arrives in its slot, a Schwung synth or sampler sitting in that slot can be processed too (the pitch modes want clearly pitched, fairly mono material). **Routing audio from other Move tracks into TinyVoc is untested.**

### Dialing the Gate (MORE knob 6)

One knob, two ideas:

- **Auto** (fully counter-clockwise): TinyVoc opens its voice gate at −42 dBFS (RMS) and lifts soft voices for you. Best for most rooms.
- **−60 … 0 dB** in 3 dB steps: you set the gate level; Auto's auto-gain is off. Turn it up until the room goes quiet between phrases, then back down a step until your quietest words still get through.

The gate matters most in **Robot**: when it is closed, Robot is exact digital silence, even with a chord held; when it opens, the carrier plays. If Robot hisses or bursts in your room between words, raise the Gate. (In the test room, ambient bursts peaked around −29 to −40 dBFS about once a second — above Auto's −42 dBFS, so Robot kept answering them. Only a manual gate above the loudest burst silenced it, at the price of needing a louder voice to open. Your room will differ: raise the gate until it's quiet, no further.)

## Performance walkthroughs

### Chord Latch — hands-free harmonies (MORE knob 7)

1. Pick **Robot**, **Chords** or **Prism** (Latch's knob shows `---` and does nothing in Snap, Morph, Double and Tape).
2. Turn **Latch** to **On** (the cell inverts).
3. Press a chord on the pads and **let go** — it keeps playing.
4. Lean into the mic. Both hands are free.
5. Press new pads to **replace** the chord. Press pads while still holding others to **add** to it (up to four notes; the oldest drops out).
6. Turn **Latch** Off, or send MIDI all-notes-off, to let go instantly.

Turning Latch On while pads are held takes those pads as the chord. In Robot the carrier octave-folds into C3–B4, so Move's high pads don't whistle.

### Instant Demo (SPACE knob 6)

Turn **Demo** On and TinyVoc replaces the mic with a built-in sung phrase and tours all seven modes, **four bars each** at the Move's tempo, playing a virtual Am–F–C–G chord progression in Robot, Chords and Prism. It's the fastest way to audition the modes without singing a note. **Turn the Mode knob to pin the tour** on the mode you like and tweak it. Turn Demo Off to go back to your own mode and voice. Demo is never saved with a set.

### Freeze / Loop — a playground, not a clip launcher (SPACE knobs 7–8)

Freeze is **delightfully experimental and unpredictable.** It's a punch-in looper that sits *before* every mode: it records what you sing, then loops it back through whatever mode, pads and knobs are active. Use it to:

- hold a vowel as an **instant vocal drone** and sing or play over it;
- make **unexpected micro-loops** — in **Free** length, stop a take after a fraction of a second and it becomes a buzzy, pitched loop;
- smear **glitchy textures** by changing the mode while a loop runs.

It is *not* a DAW clip: it is mono and 16-bit, has no overdub, editing or undo, holds up to 16 s, and has not been tuned on real voices. Treat it as a happy accident machine.

**How it works.** Knob 7 (**Freeze**) is Off / Rec / Loop. Knob 8 (**Freeze Len**) is Free / 1 Bar / 2 Bars / 4 Bars.

- **Free:** turn Freeze to Rec, sing, then to Loop to lock exactly what you recorded.
- **1, 2 or 4 Bars:** turn Freeze to Rec. With Move's transport running and MIDI clock output on, it **arms** (the banner flashes ARMED), starts **exactly on the next downbeat**, records for the chosen bars, then **loops by itself** (the knob moves to Loop). With the transport stopped it records at once.
- **Off** keeps the take; Off → Loop replays it. Loop → Rec records a new one.
- The loop seam is a 5 ms crossfade; changing between live and loop fades over 10 ms.

*Tempo sync needs Move's MIDI clock output enabled (`midiClockMode` set to `output` in Move's `Settings.json`).*

## Parameter map

Move has eight encoders; TinyVoc spreads **24 controls over three pages** (Tune, Space, More). Every control keeps its knob position in every mode and Gate state — nothing hides, nothing reorders.

### Page 1 — Tune

| Knob | Control | Range | Notes |
|---|---|---|---|
| 1 | **Mode** | Snap, Robot, Chords, Morph, Prism, Double, Tape | Mode changes crossfade over 5 ms |
| 2 | **Key** | C … B | Set by hand — Move doesn't pass its project key to modules. Also roots Robot's drone |
| 3 | **Scale** | Chromatic, Major, Minor, Pentatonic, Dorian | Applies to the sung note, not to pads |
| 4 | **Retune** | 0–500 ms | 0 = instant hard snap. Prism always snaps at 0 |
| 5–7 | **Pitch · Target · Cents** | read-only | The OLED scope: target note and cents needle, or Robot's 16 bands. Freeze state shows top right |
| 8 | **Mix** | 0–100 % | Wet/dry, delay-compensated; 100 % is fully wet |

### Page 2 — Space

| Knob | Control | Range | Notes |
|---|---|---|---|
| 1 | **Char 1** | 0–1 | Named per mode (see above) |
| 2 | **Char 2** | 0–1 | Named per mode |
| 3 | **Delay** | 0–1000 ms | 0 = off. Stereo, 3.5 kHz damped |
| 4 | **Reverb** | 0–100 % | Plate reverb; 0 = off |
| 5 | **Spread** | 0–100 % | Stereo width (chorus); in Robot it pans the odd and even bands; in Chords it pans the voices |
| 6 | **Demo** | Off, On | The built-in sung tour (see above). Not saved |
| 7 | **Freeze** | Off, Rec, Loop | The punch-in looper. Not saved |
| 8 | **Freeze Len** | Free, 1 Bar, 2 Bars, 4 Bars | Take length at the Move's tempo |

### Page 3 — More

| Knob | Control | Range | Notes |
|---|---|---|---|
| 1 | **Gain** | −12 … +12 dB | Output, soft-limited |
| 2 | **Feedback** | 0–0.95 | Delay feedback |
| 3 | **Tone** | 0–100 % | Low-pass on the wet signal; 100 % = open |
| 4 | **Glide** | 0–500 ms | Portamento between pad notes (and Robot's carrier) |
| 5 | **Mod** | 0–1 | Named per mode (Vibrato, Ensemble, Humanize, Wobble, Drift, Saturate …) |
| 6 | **Gate** | **Auto**, −60 … 0 dB (3 dB steps) | Auto = −42 dBFS gate plus auto-gain; a dB value = manual gate |
| 7 | **Latch** | Off, On | Holds the pad chord in **Robot, Chords, Prism**. **Shows a dimmed `---` in Snap, Morph, Double and Tape** (still turns; does nothing there) |
| 8 | **Beauty** | 0–100 % | Per-mode polish: Robot — detune, sub-octave, air and width; Snap — a gentle high shelf, harshness control and a micro-double; Chords and Prism — wider pan and micro-timing. **Morph, Double and Tape ignore it.** 0 = the plain engine |

### Jog wheel

TinyVoc defines no jog-wheel functions of its own. The jog wheel is Schwung's navigation for opening the Tune, Space and More pages, and no parameter lives only behind it.

## Install and build

You need [Schwung](https://github.com/charlesvestal/schwung) running on your Move and SSH access to it (`ableton@move.local`).

### From the release archive

```bash
ssh ableton@move.local "tar -C /data/UserData/schwung/modules/audio_fx -xzf - && chmod 755 /data/UserData/schwung/modules/audio_fx/tinyvoc/tinyvoc.so" < tinyvoc-v0.9.0.tar.gz
ssh ableton@move.local "nohup /data/UserData/schwung/restart-move.sh >/dev/null 2>&1 &"
```

Restarting Schwung is needed the first time (and after updates) so the screen widgets load. Then add **TinyVoc** as an audio FX in a slot, after a sound generator such as Line In.

### From source

Requires [zig](https://ziglang.org/download/) (the C compiler — it cross-builds for the Move's glibc 2.35 ARM64) and bash (Git Bash works on Windows).

```bash
bash scripts/test.sh              # ~2 million host-native checks
node tests/canvas_test.mjs        # the OLED widgets against a mock display
bash scripts/build.sh             # -> build/tinyvoc/tinyvoc.so + build/tinyvoc-module.tar.gz
bash scripts/install.sh --build   # build and sideload (MOVE_HOST=move.local MOVE_USER=ableton)
bash scripts/smoke_on_device.sh   # runs every mode on the Move itself and times it
```

The first build downloads Schwung's two ABI headers into `src/host/` (`scripts/fetch_host_headers.sh`; pin a version with `SCHWUNG_REF`). Every mode runs in about 0.2–0.6 ms per 128-frame audio block on the Move.

The architecture, signal flow, ABI notes and every parameter are in [CLAUDE.md](CLAUDE.md).

## Known limitations

- Tuned on synthetic voices and measured with a built-in mic; real-voice tuning of every mode is still to do.
- The newest on-screen widgets (Latch, Gate, per-mode knob names) are tested against a mock display, not yet by eye on a Move; Demo's on-screen mode stepping is unconfirmed on hardware.
- Pitch tracking covers about 70 Hz–1 kHz, shifted voices ±1 octave.
- Freeze is experimental (see above). Headsets, external mics and routing other tracks into TinyVoc are untested.
- Key and Scale are set on the Tune page; they don't follow Move's project.
- No licence file yet: all rights reserved until one is added.

See [RELEASE_NOTES.md](RELEASE_NOTES.md) for the changelog.

# TinyVoice

**A vocal sound-design toy and live-performance box for Ableton Move.** Sing into the Move, lean on the pads, and turn your voice into a hard-tuned robot, a talkbox, a one-finger choir, a Dalek, a walkie-talkie or a frozen drone. Eleven modes in one flat list, and one home page with eight knobs.

TinyVoice (formerly TinyVoc) is an audio FX module for [Schwung](https://github.com/charlesvestal/schwung). **v1.0.0 is a public preview (beta)** — it runs well on a Move, but it was tuned on synthetic voices and a built-in mic, not by ear across lots of singers. See [Known limitations](#known-limitations).

- [The modes](#the-modes)
- [Setup and gain staging](#setup-and-gain-staging)
- [Performance walkthroughs](#performance-walkthroughs): Chord Latch · Instant Demo · Freeze / Loop
- [Parameter map](#parameter-map)
- [Install and build](#install-and-build)
- [Known limitations](#known-limitations)

## The modes

One **Mode** knob steps through a single list: the vocoder and carrier modes first, then the voice transformers (they process your voice directly, with no carrier). Morph and Double, the two older PSOLA modes, are still at the end of the list.

**Pads** are Move's pads, played as MIDI notes: they are never snapped to Key or Scale — a pad plays exactly the note you press. Every mode works with knobs alone.

### Vocoder and carrier modes

| Mode | Pads | Best used for | Vibe |
|---|---|---|---|
| **Robot** | **Yes** — up to 4 pads drive the carrier. With no pad held, a drone (root + fifth + octave in your Key) carries the voice | Intelligible vocoder leads, synth-choir pads you sing words through | Daft-Punk-ish vocoder with a warm ensemble option |
| **Snap** | A held pad sets the target note (newest wins) | Hard-tuned leads, "Believe"-style snaps, playing your voice from the pads | Glassy, instant, telephone-band "space-age" tuned |
| **Chords** | **Yes** — one pad = the root of an in-key voicing; two or more pads play exactly those notes | Instant backing harmonies from one finger | A small, natural, strictly triadic choir (root, third, fifth, octave) that follows your note |
| **Prism** | **Yes** — same pad rules as Chords | The same hard-snapped stack, refracted: voices nudged off the grid, a frequency-shifted pair (partials at non-integer multiples) and a shimmering comb | Metallic, glassy, crystalline — not a chord, a shard of one |
| **Tape** | No — pads do nothing | Varispeed drops with wow and flutter, dark top, saturation, and a tape echo | Slowing reel, worn cassette |
| **Nightcall** | **Yes** — pads play the carrier, like Robot (a drone if none) | A DigiTech-Talker-style talkbox: narrow pulse carrier with a little white rasp, a steep 250 Hz low-cut, a 1.2 kHz mouth-cavity peak, a crisp top and a bleed of your own breath | Late-night, nasal, mechanical-warm |

### Voice transformers

| Mode | Pads | Best used for | Vibe |
|---|---|---|---|
| **Scream** | Ignored | Stalker-on-the-phone: pitch down 5 semitones, a bigger throat (formants x0.8), a steep 350 Hz–3.4 kHz telephone band, asymmetric soft clipping | Horror-movie voicemail |
| **Dalek** | Ignored | A single-sideband frequency shifter (a Hilbert pair, ±500 Hz) plus the classic 30 Hz ring modulator | Metallic, inharmonic, sci-fi villain |
| **HardTune** | A held pad is the target note | The robotic quantizer: zero slew, to your Key and Scale or to a pad, formant left alone | Unmistakable robotic tuning |
| **Harmony** | Held pads set the intervals | A three-tap granular stack at −12, +7 and +12 semitones, panned, micro-delayed and drifting. Hold a chord and the taps become its intervals above the lowest note (C-E-G gives +4, +7): the chord's *shape* follows your voice | A stack of yourself |
| **Walkie** | Ignored | A resonant 1.8 kHz horn, diode clipping, and a PTT squelch burst whenever the voice gate opens or closes | Intercom, megaphone, cheap radio |

### The five contextual knobs change with the mode

On the **Voice** page, knobs 2–6 are renamed for the active mode. The old modes' last two knobs are *the same controls as elsewhere* (marked *): the Retune knob on the Pitch page and the Retune knob here are one value.

| Mode | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|
| Robot | **Shift** (formant ±12 st) | **Noise** (consonant hiss) | **Ensemble** (chorus depth) | **Beauty*** | **Glide*** |
| Snap | **Formant** (±12 st on top of a fixed +2.5) | **Sens** (how readily it tracks) | **Vibrato** | **Retune*** | **Beauty*** |
| Chords | **Level** (harmony vs. lead) | **Voicing** (standard ↔ wide) | **Humanize** | **Retune*** | **Beauty*** |
| Prism | **Voicing** (spread and pan width) | **Warmth** | **Humanize** | **Refract** (0 = the plain harmonizer) | **Shimmer** (comb feedback) |
| Tape | **Drop** (pitch, 0 to −12 st) | **Flutter** | **Saturate** | **Tone*** | **Echo*** (the delay time) |
| Nightcall | **Formant** (±12 st) | **Breath** (breath bleed 0–35 %, 17.5 % at 0.5) | **Rasp** (carrier noise 10–30 %) | **Duty** (pulse width 12–28 %, 20 % at 0.5) | **Cavity** (peak at 1.2 kHz, 0–12 dB) |
| Scream | **Pitch** (−12 … +2 st, −5 at 0.5) | **Throat** (formant scale, x0.8 at 0.5) | **Grit** | **Narrow** | **Line** (phone-line hiss, above 0.5) |
| Dalek | **Shift** (−500 … +500 Hz, 0 at 0.5) | **Ring** (0–60 Hz, 30 at 0.5; 0 = off) | **Metal** (comb resonance) | **Drive** | **Width** (L and R shifted apart) |
| HardTune | **Amount** (0–200 %, 100 % at 0.5) | **Formant** (±12 st) | **Vibrato** | **Glide*** | **Sens** (tracking sensitivity) |
| Harmony | **Lead** (the dry-aligned voice) | **Stack** (the three taps) | **Detune** (slow drift) | **Grain** (8–18 ms) | **Micro** (0–4.5 ms between taps) |
| Walkie | **Peak** (resonance of the horn) | **Drive** (diode clipping) | **Static** (hiss while open) | **Squelch** (the PTT burst) | **Band** (horn at 1.2–2.6 kHz, 1.8 at 0.5) |
| Morph | **Throat** (±12 st) | **Drive** | **Wobble** | **Retune*** | **Glide*** |
| Double | **Detune** (5–30 ct) | **Width** | **Drift** | **Retune*** | **Glide*** |

The new transformers are held under a **−6.1 dBFS** output ceiling at Level 0 dB (resonant peaks, squelch bursts and distortion included); Level is the boost. The latency of each wet path is under 15 ms (Harmony, the largest, is about 12 ms at its slowest Grain and Micro).

## Setup and gain staging

TinyVoice is an **audio FX**: it processes the audio in the Schwung slot it sits in. For your voice, put Schwung's **Line In** module in the slot as the sound generator (it brings the Move's physical input into the slot) and TinyVoice after it as the audio FX.

> **Feedback warning.** Singing into a mic with the speakers on is a feedback loop. Line In warns about it and holds the slot bypassed until it's safe. Use headphones for the first try.

### Built-in mic

- **Distance.** Start 4-24 inches from the mic. Closer gets you more voice and less room.
- **Enunciate.** Over-pronounce consonants: "s", "t" and "k" are what make a vocoder understandable, and the mic's small capsule needs the help.
- **Pad taps.** TinyVoice high-passes its input at **120 Hz**, so the thump of hitting a pad, and chassis vibration, doesn't open the gate or muddy the vocoder (a 40 Hz thump drops about 10 dB). It's a gentle filter: a hard pad hit still leaks a little.
- **Level.** In **Auto** the scope shows how much boost TinyVoice is adding (`+NN`, up to +12 dB). If it sits at `+12`, the mic is too quiet to be lifted fully — raise Line In's **Input Trim** until it falls below.

### Line-in and headsets

Line In has Line, Guitar and Phono input types and an Input Trim of −12 to +40 dB. A line-level source needs no trim; a weak source (a headset mic, say) may want Guitar or extra trim. **Headset and external mics are untested** with TinyVoice.

Because TinyVoice processes whatever audio arrives in its slot, a Schwung synth or sampler sitting in that slot can be processed too (the pitch modes want clearly pitched, fairly mono material). **Routing audio from other Move tracks into TinyVoice is untested.**

### Dialing the Gate (MORE knob 1)

One knob, two ideas:

- **Auto** (fully counter-clockwise): TinyVoice opens its voice gate at −42 dBFS (RMS) and lifts soft voices for you. Best for most rooms.
- **−60 … 0 dB** in 3 dB steps: you set the gate level; Auto's auto-gain is off. Turn it up until the room goes quiet between phrases, then back down a step until your quietest words still get through.

The gate matters most in **Robot** and **Nightcall**: when it is closed, they are exact digital silence, even with a chord held; when it opens, the carrier plays. In **Walkie** the same gate fires the PTT squelch. If a vocoder hisses or bursts in your room between words, raise the Gate. (In the test room, ambient bursts peaked around −29 to −40 dBFS about once a second — above Auto's −42 dBFS, so Robot kept answering them. Only a manual gate above the loudest burst silenced it, at the price of needing a louder voice to open. Your room will differ: raise the gate until it's quiet, no further.)

## Performance walkthroughs

### Chord Latch — hands-free harmonies (MORE knob 2)

1. Pick **Robot**, **Chords**, **Prism**, **Nightcall** or **Harmony** (Latch's knob shows `---` and does nothing in the other modes).
2. Turn **Latch** to **On** (the cell inverts).
3. Press a chord on the pads and **let go** — it keeps playing.
4. Lean into the mic. Both hands are free.
5. Press new pads to **replace** the chord. Press pads while still holding others to **add** to it (up to four notes; the oldest drops out).
6. Turn **Latch** Off, or send MIDI all-notes-off, to let go instantly.

Turning Latch On while pads are held takes those pads as the chord. In Robot the carrier octave-folds into C3–B4, so Move's high pads don't whistle.

### Instant Demo (SPACE knob 6)

Turn **Demo** On and TinyVoice replaces the mic with a built-in sung phrase and tours every mode, **four bars each** at the Move's tempo, playing a virtual Am–F–C–G chord progression in Robot, Chords, Prism, Nightcall and Harmony. It's the fastest way to audition the modes without singing a note. **Turn the Mode knob to pin the tour** on the mode you like and tweak it. Turn Demo Off to go back to your own mode and voice. Demo is never saved with a set.

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

Move has eight encoders. The **Voice** page is the home page and carries everything you play with: **Mode, five contextual knobs, Mix, Level**. The jog wheel opens three more pages for the settings you set once (Pitch, Space, More). Every control keeps its knob position in every mode and Gate state — nothing hides, nothing reorders.

### Voice (home)

| Knob | Control | Range | Notes |
|---|---|---|---|
| 1 | **Mode** | Robot, Snap, Chords, Prism, Tape, Nightcall, Scream, Dalek, HardTune, Harmony, Walkie, Morph, Double | Mode changes crossfade over 5 ms |
| 2–6 | **Five contextual knobs** | 0–1 | Named per mode (tables above) |
| 7 | **Mix** | 0–100 % | Wet/dry, delay-compensated; 100 % is fully wet |
| 8 | **Level** | −12 … +12 dB | Output, soft-limited. The −6.1 dBFS ceiling of the new modes sits below it |

### Pitch

| Knob | Control | Range | Notes |
|---|---|---|---|
| 1 | **Key** | C … B | Set by hand — Move doesn't pass its project key to modules. Roots Robot's and Nightcall's drone; **N/A** in Tape, Scream, Dalek, Harmony, Walkie |
| 2 | **Scale** | Chromatic, Major, Minor, Pentatonic, Dorian | Applies to the sung note, not to pads. Snap, Chords, Prism, HardTune, Morph, Double |
| 3 | **Retune** | 0–500 ms | 0 = instant hard snap. Snap, Chords, Morph, Double |
| 4 | **Glide** | 0–500 ms | Portamento between pad notes (and the vocoders' carrier). N/A in Tape and the transformers except HardTune |
| 5–7 | **Pitch · Target · Cents** | read-only | The OLED scope: target note and cents needle, or the vocoders' 16 bands. Freeze state shows top right |
| 8 | **Beauty** | 0–100 % | Per-mode polish: Robot — detune, sub-octave, air and width; Snap — a gentle high shelf, harshness control and a micro-double; Chords and Prism — wider pan and micro-timing. Other modes ignore it (**N/A**). 0 = the plain engine |

### Space

| Knob | Control | Range | Notes |
|---|---|---|---|
| 1 | **Delay** | 0–1000 ms | 0 = off. Stereo, 3.5 kHz damped |
| 2 | **Feedback** | 0–0.95 | Delay feedback |
| 3 | **Reverb** | 0–100 % | Plate reverb; 0 = off |
| 4 | **Spread** | 0–100 % | Stereo width (chorus); in the vocoders it pans the odd and even bands; in Chords it pans the voices; in Harmony it widens the stack |
| 5 | **Tone** | 0–100 % | Low-pass on the wet signal; 100 % = open |
| 6 | **Demo** | Off, On | The built-in sung tour (see above). Not saved |
| 7 | **Freeze** | Off, Rec, Loop | The punch-in looper. Not saved |
| 8 | **Freeze Len** | Free, 1 Bar, 2 Bars, 4 Bars | Take length at the Move's tempo |

### More

| Knob | Control | Range | Notes |
|---|---|---|---|
| 1 | **Gate** | **Auto**, −60 … 0 dB (3 dB steps) | Auto = −42 dBFS gate plus auto-gain; a dB value = manual gate |
| 2 | **Latch** | Off, On | Holds the pad chord in **Robot, Chords, Prism, Nightcall, Harmony**. **Shows a dimmed `---` in the other modes** (still turns; does nothing there) |

### Jog wheel

TinyVoice defines no jog-wheel functions of its own. The jog wheel is Schwung's navigation for opening the Pitch, Space and More pages, and no parameter lives only behind it.

## Install and build

You need [Schwung](https://github.com/charlesvestal/schwung) running on your Move and SSH access to it (`ableton@move.local`).

### From the release archive

```bash
ssh ableton@move.local "tar -C /data/UserData/schwung/modules/audio_fx -xzf - && chmod 755 /data/UserData/schwung/modules/audio_fx/tinyvoice/tinyvoice.so" < TinyVoice.tar.gz
ssh ableton@move.local "nohup /data/UserData/schwung/restart-move.sh >/dev/null 2>&1 &"
```

Restarting Schwung is needed the first time (and after updates) so the screen widgets load. Then add **TinyVoice** as an audio FX in a slot, after a sound generator such as Line In. (Older TinyVoc / VocalTron installs are separate folders: `scripts/install.sh` removes them; by hand, delete `modules/audio_fx/tinyvoc` and `modules/audio_fx/vocaltron`. Sets saved with the old module still name the old id, so TinyVoice has to be added to them again.)

### From source

Requires [zig](https://ziglang.org/download/) (the C compiler — it cross-builds for the Move's glibc 2.35 ARM64) and bash (Git Bash works on Windows). `make` is optional: its targets just run the scripts.

```bash
make test                         # = bash scripts/test.sh: ~2 million host-native checks + the OLED widget test
make arm                          # = bash scripts/build.sh: cross-compile (-Wall -Wextra -Werror -pedantic) -> build/tinyvoice/tinyvoice.so + build/TinyVoice.tar.gz
make install                      # = bash scripts/install.sh --build: sideload (MOVE_HOST=move.local MOVE_USER=ableton)
bash scripts/smoke_on_device.sh   # runs every mode on the Move itself and times it
```

The first build downloads Schwung's two ABI headers into `src/host/` (`scripts/fetch_host_headers.sh`; pin a version with `SCHWUNG_REF`). On the Move every mode should run well inside the ~2.3 ms of a 128-frame audio block (the older modes measure 0.2–0.6 ms; the new ones cost the same or less natively).

The architecture, signal flow, ABI notes and every parameter are in [CLAUDE.md](CLAUDE.md).

## Known limitations

- Tuned on synthetic voices and measured with a built-in mic; real-voice tuning of every mode is still to do.
- The on-screen widgets, the Voice / Pitch / Space / More layout and the per-mode knob names are tested against a mock display, not yet by eye on a Move; Demo's on-screen mode stepping is unconfirmed on hardware. The v1.0 modes have **not** been run on the device yet (the host-native tests and the cross-compile pass; `scripts/smoke_on_device.sh` still has to be run).
- Pitch tracking covers about 70 Hz–1 kHz, shifted voices ±1 octave. Scream is a pitch-synchronous (PSOLA) shifter rather than WSOLA; Harmony's grain follows your pitch (it needs a tracked voice to stay phase-coherent).
- Robot and Snap still follow pads (their pad behaviour is part of what they are); only Scream, Dalek, Walkie and Tape ignore them.
- Freeze is experimental (see above). Headsets, external mics and routing other tracks into TinyVoice are untested.
- Key and Scale are set on the Pitch page; they don't follow Move's project.
- No licence file yet: all rights reserved until one is added.

See [RELEASE_NOTES.md](RELEASE_NOTES.md) for the changelog.

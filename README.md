# TinyVoice

**A vocal sound-design toy and live-performance box for Ableton Move, with two engines in one module.**

- **Vocoder** is the deep one: sing into the Move, lean on the pads, and turn your voice into a hard-tuned robot, a talkbox, a one-finger choir, a Dalek, a walkie-talkie or a frozen drone. Thirteen modes, three pages of controls.
- **Voice FX** is the quick one: a talking filter. Your voice (or a bass line) through a bank of vowel formants, with eight knobs on a single page, six ready-made presets, and a built-in funk groove so you can audition everything without routing a thing.

TinyVoice is an audio FX module for [Schwung](https://github.com/charlesvestal/schwung). **v1.0.1 is a public preview (beta)**: it runs well on a Move, but it was tuned on synthetic voices, a built-in groove and a built-in mic, not across lots of singers. See [Known limitations](#known-limitations).

- [Two engines, one switch](#two-engines-one-switch)
- [Vocoder](#vocoder): the modes · the contextual knobs
- [Voice FX](#voice-fx): the eight knobs · the presets · the Audition phrase
- [Setup and gain staging](#setup-and-gain-staging)
- [Performance walkthroughs](#performance-walkthroughs)
- [Parameter map](#parameter-map)
- [MIDI](#midi)
- [Performance and DSP budget](#performance-and-dsp-budget)
- [Install and build](#install-and-build)
- [Known limitations](#known-limitations)

## Two engines, one switch

The first page in the module is the **Mode** page, a list drawn by Schwung: pick **Vocoder** or **VoiceFX** and the rest of the module changes to match. The choice is saved with the set.

| | **Vocoder** | **Voice FX** |
|---|---|---|
| Pages | Mode, then **Main**, **Pitch**, **More** | Mode, **Presets**, **Main** (the eight knobs), **Audition** |
| What it does | Multi-band speech/carrier vocoder, plus the voice transformers: 13 modes | A four-formant vowel filter on the voice: no vocoder bands, no carrier, no pitch tracking |
| Pads | Play the carrier, set target notes, make chords (mode-dependent) | Not used |
| Gate / Level / Key / Scale / Latch | Yours to set | Hidden. Voice FX always runs with **Auto** on, Tone off and Level at 0 dB |
| Demo | A built-in sung phrase that tours the modes | A built-in 8-bar electro-funk phrase, with the knobs moving on their own |

Nothing you set in one engine changes the other: Voice FX has its own parameters, and a test checks that its output is bit-identical whatever the Vocoder's Mode, Key, Gate, Level, Mix, Tone, Spread or pads are set to.

---

## Vocoder

One **Mode** knob steps through a single list: the vocoder and carrier modes first, then the voice transformers (they process your voice directly, with no carrier). Morph and Double, the two older PSOLA modes, are still at the end of the list.

**Pads** are Move's pads, played as MIDI notes: they are never snapped to Key or Scale. A pad plays exactly the note you press. Every mode works with knobs alone.

### Vocoder and carrier modes

| Mode | Pads | Best used for | Vibe |
|---|---|---|---|
| **Robot** | **Yes**: up to 4 pads drive the carrier. With no pad held, a drone (root + fifth + octave in your Key) carries the voice | Intelligible vocoder leads, synth-choir pads you sing words through | Daft-Punk-ish vocoder with a warm ensemble option |
| **Snap** | A held pad sets the target note (newest wins) | Hard-tuned leads, "Believe"-style snaps, playing your voice from the pads | Glassy, instant, telephone-band "space-age" tuned |
| **Chords** | **Yes**: one pad = the root of an in-key voicing; two or more pads play exactly those notes | Instant backing harmonies from one finger | A small, natural, strictly triadic choir (root, third, fifth, octave) that follows your note |
| **Prism** | **Yes**: same pad rules as Chords | The same hard-snapped stack, with a frequency-shifted pair (partials at non-integer multiples) and a shimmering comb folded in | Metallic, glassy, crystalline |
| **Tape** | No | Varispeed drops with wow and flutter, a dark top, saturation and a touch of hiss | Slowing reel, worn cassette |
| **Nightcall** | **Yes**: pads play the carrier, like Robot (a drone if none) | A DigiTech-Talker-style talkbox: narrow pulse carrier with a little white rasp, a steep 250 Hz low-cut, a 1.2 kHz mouth-cavity peak, a crisp top and a bleed of your own breath | Late-night, nasal, mechanical-warm |

### Voice transformers

| Mode | Pads | Best used for | Vibe |
|---|---|---|---|
| **Scream** | Ignored | The prank-caller voice changer: down 8 semitones with a bigger throat (formants ×0.84), kept full by a wide 90 Hz–4.8 kHz band, clean until you raise Grit. Narrow closes it into a phone line | Deep, menacing, late-night caller |
| **Dalek** | Ignored | A single-sideband frequency shifter (a Hilbert pair, ±500 Hz) plus the classic 30 Hz ring modulator | Metallic, inharmonic, sci-fi villain |
| **HardTune** | A held pad is the target note | The robotic quantizer: zero slew, to your Key and Scale or to a pad, formant left alone | Unmistakable robotic tuning |
| **Harmony** | Held pads set the intervals | A three-tap granular stack at −12, +7 and +12 semitones, panned, micro-delayed and drifting. Hold a chord and the taps become its intervals above the lowest note (C-E-G gives +4, +7) | A stack of yourself |
| **Walkie** | Ignored | A handset: a 350 Hz–3 kHz band, a presence peak, a mic compressor that slams every phrase to one level, diode clipping, and a receiver that **mutes between phrases** and squelches (a short burst on open, a brief "kssh" on close, all inside the handset's band, with nothing metallic ringing on above it) | Intercom, megaphone, cheap radio |
| **Morph**, **Double** | Morph and Double follow a held pad | The two older PSOLA modes: a formant-decoupled shifter into drive; a hard-tuned lead with two detuned Haas doubles | Kept as they were |

### The five contextual knobs change with the mode

**Every mode keeps its own five.** Set HardTune's knobs to max, switch to Harmony, and Harmony is on its own values (its defaults, the first time); switch back and HardTune is exactly as you left it. They are saved with the set, mode by mode. The exception is the knobs marked \* below: those are the *same control* as one on another page (Retune, Beauty, Glide, Tone), so they are one value in every mode that has them.

On the **Main** page, knobs 2–6 are renamed for the active mode. Some of them are *the same controls as elsewhere* (marked \*): the Retune knob on the Pitch page and the Retune knob here are one value.

| Mode | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|
| Robot | **Shift** (formant ±12 st) | **Noise** (consonant hiss) | **Ensemble** (chorus depth) | **Beauty\*** | **Glide\*** |
| Snap | **Formant** (±12 st on top of a fixed +2.5) | **Sens** (how readily it tracks) | **Vibrato** | **Retune\*** | **Beauty\*** |
| Chords | **Level** (harmony vs. lead) | **Voicing** (standard ↔ wide) | **Humanize** | **Retune\*** | **Beauty\*** |
| Prism | **Voicing** (spread and pan width) | **Warmth** | **Humanize** | **Refract** (0 = the plain harmonizer; the glass comes in with its square) | **Shimmer** (comb feedback) |
| Tape | **Drop** (pitch, 0 to −12 st) | **Flutter** | **Saturate** | **Tone\*** | **Hiss** (tape hiss above 0.5) |
| Nightcall | **Formant** (±12 st) | **Breath** (breath bleed 0–35 %, 17.5 % at 0.5) | **Rasp** (carrier noise 10–30 %) | **Duty** (pulse width 12–28 %, 20 % at 0.5) | **Cavity** (peak at 1.2 kHz, 0–12 dB) |
| Scream | **Pitch** (−12 … −3 st, −8 at 0.5) | **Throat** (formant scale, ×0.84 at 0.5) | **Grit** (soft-clip drive; 0 = clean) | **Narrow** (band 90 Hz–4.8 kHz at 0.5, a phone line at 1) | **Line** (phone-line hiss, above 0.5) |
| Dalek | **Shift** (−500 … +500 Hz, 0 at 0.5) | **Ring** (0–60 Hz, 30 at 0.5; 0 = off) | **Metal** (comb resonance) | **Drive** | **Width** (L and R shifted apart) |
| HardTune | **Amount** (0–200 %, 100 % at 0.5) | **Formant** (±12 st) | **Vibrato** | **Glide\*** | **Sens** (tracking sensitivity) |
| Harmony | **Lead** (the dry-aligned voice) | **Stack** (the three taps) | **Detune** (slow drift) | **Grain** (8–18 ms) | **Micro** (0–4.5 ms between taps) |
| Walkie | **Peak** (presence peak, 0 … +12 dB, +8 at 0.5) | **Drive** (diode clipping) | **Static** (hiss under the voice; a faint floor at 0) | **Squelch** (the receiver burst) | **Band** (peak at 1.2–2.6 kHz, 1.8 at 0.5) |
| Morph | **Throat** (±12 st) | **Drive** | **Wobble** | **Retune\*** | **Glide\*** |
| Double | **Detune** (5–30 ct) | **Width** | **Drift** | **Retune\*** | **Glide\*** |

Scream, Dalek, HardTune, Harmony, Walkie, Nightcall and Voice FX are held under a **−6.1 dBFS** output ceiling at Level 0 dB (resonant peaks, squelch bursts and distortion included). Level is the boost. The latency of each vocoder wet path is under 15 ms (Harmony, the largest, is about 12 ms at its slowest Grain and Micro); the dry signal is delayed to match, so Mix never combs.

---

## Voice FX

Choose **VoiceFX** on the Mode page. TinyVoice becomes a **talking filter**: your voice goes through four resonant vowel formants that you sweep across **A · E · I · O · U**. There is no vocoder analysis, no carrier and no pitch tracking, so it works on anything with harmonics: a voice, a bass line, a synth. Latency is zero.

It is level-neutral on purpose. A leveller holds the output at the input's loudness, so Vowel, Tract and Peak change *tone*, not volume (within about ±2.5 dB across every knob on a sung phrase).

> Voice FX presets are named after classic talking-filter programs and are in that style. TinyVoice is **not affiliated with Sugar Bytes** or any other product; the settings are our own.

### The eight knobs (Main page)

Every value is 0–1 (shown as 0–100 %).

| Knob | Control | What it does |
|---|---|---|
| 1 | **Vowel** | Sweeps **A** (0) → **E** (0.25) → **I** (0.5) → **O** (0.75) → **U** (1), continuously. Formant centres glide in log frequency (A is 730 / 1090 / 2440 / 3400 Hz; I is 270 / 2290 / 3010 / 3500 Hz) |
| 2 | **Tract** | Formant scale ×2^((v−0.5)×1.2): **0.5 is natural**, 0 is −0.6 octave (a bigger throat, a monster), 1 is +0.6 octave (smaller, brighter, alien) |
| 3 | **Peak** | Formant Q ×0.4 … ×2.5 (×1 at 0.5): broad and natural at 0, narrow, intense and whistly at 1. Level is made up for you |
| 4 | **Choir** | Left and right banks detune apart by up to ±7 % per formant (in alternating directions), and the same knob drives the stereo chorus. 0 is mono-identical, 1 is a wide ensemble |
| 5 | **Breath** | Noise that follows your voice (so silence stays silent) goes through the formants for a whispered vowel, and the voice's own air above 3.5 kHz is bled past them so consonants and sibilance survive |
| 6 | **Attack** | Onset swell from 0.5 ms (punchy) to 120 ms (a slow bloom). A held note loses no level |
| 7 | **Release** | How long the vowel rings on after the voice stops: a formant-coloured tail whose level and length both rise with the knob (0 = none; the top is about 3 s) |
| 8 | **Mix/Drv** | One knob, two jobs. **0–70 %** is wet/dry (dry at 0, all wet at 70 %, with a touch of warmth). **70–100 %** stays all wet while the **drive** climbs from ×1.3 to a hard ×5 clip, level-compensated so it gets grittier, not louder |

Not on offer: an envelope that sweeps the vowel by itself. The "dynamic" curves of the presets come from Attack, Release and the drive; to sweep the vowel, ride knob 1 by hand.

### The six presets (Presets page)

The **Presets** page is Schwung's own browser: **turn the jog wheel** to step through the list. Each preset you land on writes its values into the eight knobs, so you can keep tweaking from there. The preset number isn't saved with a set; the knob values are. **Vowel Morph Solo** is also the default.

| # | Preset | Vowel | Tract | Peak | Choir | Breath | Attack | Release | Mix/Drv | Character |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | **Yeah Boy** | 0.45 | 0.50 | 0.80 | 0.30 | 0.10 | 0.02 | 0.15 | 0.95 | Hard drive, snappy, resonant. Starts near /i/: ride Vowel down to 0 for /a/ |
| 2 | **Robo Talk** | 0.875 | 0.50 | 0.85 | 0.10 | 0.05 | 0.05 | 0.20 | 0.80 | A narrow /o/→/u/ window, high Q, neutral tract |
| 3 | **Monster Throat** | 0.70 | 0.208 | 0.65 | 0.30 | 0.25 | 0.00 | 0.20 | 0.98 | Tract down 0.35 octave, hard drive, no attack at all |
| 4 | **Funky Talkbox** | 0.30 | 0.50 | 0.50 | 0.20 | 0.10 | 0.05 | 0.12 | 0.70 | Clean (no drive), medium Q, fast attack and release for rhythmic play |
| 5 | **Alien Choir** | 0.60 | 0.833 | 0.55 | 1.00 | 0.85 | 0.30 | 0.55 | 0.80 | Tract up 0.4 octave, full width, lots of air |
| 6 | **Vowel Morph Solo** | 0.00 | 0.50 | 0.50 | 0.35 | 0.15 | 0.12 | 0.30 | 0.75 | Neutral and balanced, for riding Vowel across all five vowels by hand |

### The Audition phrase (Audition page)

The **Audition** page has one knob: **Demo** (Off / On). With Demo **On**, Voice FX ignores its input and plays a built-in **eight-bar electro-funk phrase** through the filter, so you can hear what the engine does with nothing routed in. With Demo **Off**, your track's audio passes through your own knobs as normal.

- **Tone:** bright, harmonic-rich dual-saw voices (two saws a few cents apart) through a pluck filter that opens on each hit, with snappy 1-1.5 ms attacks, ideal for exciting formants.
- **Composition:** D minor, 128 sixteenths: **Dm7 · Dm7 · Bbmaj7 · Cadd9 · Dm7 · Dm7 · Gm7 · A7** (the turnaround), with a syncopated bass line (D2 up to C4: roots, octaves, a fifth and a seventh, in three patterns that answer one another) under **staccato four-note chord stabs**, all of them off the beat.
- **Tempo:** the Move's tempo. Every hit restarts its oscillators, so the loop repeats exactly (16 s at 120 bpm). It starts from bar 1 when you turn Demo on and is not locked to the transport.
- **Where it sits:** it replaces the input before the formants and drive. The bass stays above the 120 Hz input high-pass (see [Setup](#setup-and-gain-staging)), so its fundamentals are heard.
- **The knobs move by themselves:** while the phrase plays, Vowel, Tract, Peak, Choir and Mix/Drv sweep through their ranges over the eight bars (and you see them turn on the display), so a preset's values for those five don't apply while it runs. Breath, Attack and Release stay yours. Your own settings are never changed. Turn one of the five yourself, or choose a preset, and Demo switches off and that knob is yours. To hear a preset, turn Demo Off and play through it.

The Audition page is separate from the eight Main knobs on purpose: the Mode page and the preset browser are drawn by Schwung and have no room for a switch, so Demo has a page of its own. The Vocoder's Demo (the sung phrase) is untouched.

### Auditioning Voice FX in thirty seconds

1. On the Mode page, pick **VoiceFX**.
2. Go to the **Audition** page and turn **Demo** On. The funk phrase starts and the knobs sweep on their own: the raw synth, then a vowel sweep, drive and squelch, then chest voice, soprano and a wide choir, around its eight bars.
3. Turn Demo Off, then click through the six presets on the **Presets** page and sing (or play a bass line) through each.
4. On **Main**, ride **Vowel** (knob 1) across A-E-I-O-U, push **Peak** and **Mix/Drv**, and open **Release** for a tail. Turn Demo Off and sing.

### Chord Latch: hands-free harmonies (More page, knob 6; Vocoder)

1. Pick **Robot**, **Chords**, **Prism**, **Nightcall** or **Harmony** (Latch's knob shows `---` and does nothing in the other modes).
2. Turn **Latch** to **On** (the cell inverts).
3. Press a chord on the pads and **let go**: it keeps playing.
4. Lean into the mic. Both hands are free.
5. Press new pads to **replace** the chord. Press pads while still holding others to **add** to it (up to four notes; the oldest drops out).
6. Turn **Latch** Off, or send MIDI all-notes-off, to let go instantly.

Turning Latch On while pads are held takes those pads as the chord. In Robot the carrier octave-folds into C3–B4, so Move's high pads don't whistle.

### Instant Demo (More page, knob 8; Vocoder)

Turn **Demo** On and TinyVoice replaces the mic with a built-in sung phrase and tours every mode, **four bars each** at the Move's tempo, playing a virtual Am–F–C–G chord progression in Robot, Chords, Prism, Nightcall and Harmony. It's the fastest way to audition the modes without singing a note. **Turn the Mode knob to pin the tour** on the mode you like and tweak it. Turn Demo Off to go back to your own mode and voice. Demo is never saved with a set. (In Voice FX the same switch plays the groove instead; see above.)

### Freeze / Loop: a playground, not a clip launcher (More page, knobs 3–4; Vocoder)

Freeze is **delightfully experimental and unpredictable.** It's a punch-in looper that sits *before* every mode: it records what you sing, then loops it back through whatever mode, pads and knobs are active. Use it to:

- hold a vowel as an **instant vocal drone** and sing or play over it;
- make **short stutter loops**: in **Free** length, a take can be as short as **120 ms** (a take stopped sooner is extended to that, and a tap under 50 ms is ignored, so an accidental touch can't become a buzzy few-millisecond "pitch");
- smear **glitchy textures** by changing the mode while a loop runs.

It is *not* a DAW clip: it is mono and 16-bit, has no overdub, editing or undo, holds up to 16 s, and has not been tuned on real voices. Treat it as a happy accident machine. (Freeze sits after the input, so a looping take also replaces the Voice FX phrase.)

**How it works.** Knob 3 (**Freeze**) is Off / Rec / Loop. Knob 4 (**Freeze Len**) is Free / 1 Bar / 2 Bars / 4 Bars, on the top row of the grid, where a banner draws across both.

- **Free:** turn Freeze to Rec, sing, then to Loop to lock exactly what you recorded.
- **1, 2 or 4 Bars:** turn Freeze to Rec. With Move's transport running and MIDI clock output on, it **arms** (the banner flashes ARMED), starts **exactly on the next downbeat**, records for the chosen bars, then **loops by itself** (the knob moves to Loop). With the transport stopped it records at once.
- **Off** keeps the take; Off → Loop replays it. Loop → Rec records a new one.
- **Minimum loop:** a take stopped **under 50 ms** is rejected (it was an accidental tap: nothing loops, you stay live). A take stopped between 50 ms and 120 ms **keeps recording after you let go**, whatever you sing next, until the loop is 120 ms, then does what you asked (Loop or Off). Ask for Rec again meanwhile and it carries on as an ordinary take.
- The loop seam is a 5 ms equal-power crossfade; changing between live and loop fades over 10 ms.

*Tempo sync needs Move's MIDI clock output enabled (`midiClockMode` set to `output` in Move's `Settings.json`).*

---

## Parameter map

Move has eight encoders in a **4 × 2 grid** (cells 1–4 on the top row, 5–8 below), and a page can't have gaps. The **Mode** page comes first in both engines. Every control keeps its knob position in every mode and Gate state: nothing hides, nothing reorders.

### Vocoder: Main (home)

| Knob | Control | Range | Notes |
|---|---|---|---|
| 1 | **Mode** | Robot, Snap, Chords, Prism, Tape, Nightcall, Scream, Dalek, HardTune, Harmony, Walkie, Morph, Double | Mode changes crossfade over 5 ms |
| 2–6 | **Five contextual knobs** | 0–1 | Named per mode (tables above) |
| 7 | **Mix** | 0–100 % | Wet/dry, delay-compensated; 100 % is fully wet |
| 8 | **Level** | −12 … +12 dB | Output, soft-limited. The −6.1 dBFS ceiling sits below it |

### Vocoder: Pitch

| Knob | Control | Range | Notes |
|---|---|---|---|
| 1 | **Key** | C … B | Set by hand: Move doesn't pass its project key to modules. Roots Robot's and Nightcall's drone; **N/A** in Tape, Scream, Dalek, Harmony, Walkie |
| 2 | **Scale** | Chromatic, Major, Minor, Pentatonic, Dorian | Applies to the sung note, not to pads. Snap, Chords, Prism, HardTune, Morph, Double |
| 3 | **Retune** | 0–500 ms | 0 = instant hard snap. Snap, Chords, Morph, Double |
| 4 | **Glide** | 0–500 ms | Portamento between pad notes (and the vocoders' carrier). N/A in Tape and the transformers except HardTune |
| 5–7 | **Pitch · Target · Cents** | read-only | The OLED scope: target note and cents needle, or the vocoders' 16 bands. Freeze state shows top right |
| 8 | (empty) | | Beauty used to sit here; it moved to More |

### Vocoder: More

| Knob | Control | Range | Notes |
|---|---|---|---|
| 1 | **Spread** | 0–100 % | Stereo width (chorus); in the vocoders it pans the odd and even bands; in Chords it pans the voices; in Harmony it widens the stack |
| 2 | **Tone** | 0–100 % | Low-pass on the wet signal; 100 % = open |
| 3 | **Freeze** | Off, Rec, Loop | The punch-in looper. Not saved |
| 4 | **Freeze Len** | Free, 1 Bar, 2 Bars, 4 Bars | Take length at the Move's tempo |
| 5 | **Gate** | **Auto**, −60 … 0 dB (3 dB steps) | Auto = −42 dBFS gate plus auto-gain; a dB value = manual gate |
| 6 | **Latch** | Off, On | Holds the pad chord in **Robot, Chords, Prism, Nightcall, Harmony**. **Shows a dimmed `---` in the other modes** (still turns; does nothing there) |
| 7 | **Beauty** | 0–100 % | Per-mode polish: Robot: detune, sub-octave, air and width; Snap: a gentle high shelf, harshness control and a micro-double; Chords and Prism: wider pan and micro-timing. Other modes ignore it (**N/A**). 0 = the plain engine |
| 8 | **Demo** | Off, On | The built-in sung tour (see above). Not saved |

### Voice FX: Presets, Main, Audition

| Page | Knob | Control | Range |
|---|---|---|---|
| Presets | (browser) | **Preset** | the six presets above; not saved |
| Main | 1 | **Vowel** | 0–1: A, E, I, O, U |
| | 2 | **Tract** | 0–1, 0.5 natural, ±0.6 octave |
| | 3 | **Peak** | 0–1, Q ×0.4 … ×2.5 |
| | 4 | **Choir** | 0–1, ±7 % detune and chorus width |
| | 5 | **Breath** | 0–1 |
| | 6 | **Attack** | 0–1, 0.5 … 120 ms swell |
| | 7 | **Release** | 0–1, 0 = no tail |
| | 8 | **Mix/Drv** | 0–1, wet/dry to 0.7, then drive ×1.3 … ×5 |
| Audition | 1 | **Demo** | Off, On: the funk phrase replaces the input, and Vowel, Tract, Peak, Choir and Mix/Drv move on their own over its 8 bars. Not saved |

### Jog wheel

TinyVoice defines no jog-wheel functions of its own. The jog wheel is Schwung's navigation: it steps between pages and, on the Presets page, through the preset list. No parameter lives only behind it.

---

## MIDI

| | Vocoder | Voice FX |
|---|---|---|
| Notes | The slot's own pads: up to **4 voices** (the oldest is stolen). Velocity sets Robot's and Nightcall's carrier-voice level | **None.** Voice FX does not read MIDI |
| What a pad does | Robot, Nightcall: plays the carrier (octave-folded into C3–B4). Snap, Morph, Double, HardTune: the newest held pad is the target note. Chords, Prism: one pad = the chord root, two or more = exactly those notes. Harmony: the chord's intervals. Tape, Scream, Dalek, Walkie: ignored | n/a |
| Octave fold | A pad within ±12 semitones of your voice plays at its own octave; further away it folds by octaves so Move's high pads stay playable | n/a |
| All-notes-off | CC 120 and CC 123 release everything and empty the Latch | n/a |
| Ignored | Pitch bend, mod wheel, aftertouch, program change, CCs other than 120 and 123 | Everything |
| Other tracks | Schwung also broadcasts every track's MIDI to every audio FX; TinyVoice **drops** those, so only the slot's own pads play | Dropped as well |
| Tempo and transport | The host tempo drives the Demo tour (4 bars per mode), Freeze's bar lengths and the Audition phrase. The transport position (for Freeze's downbeat arming) needs Move's MIDI clock output on | The tempo drives the groove only |

---

## Performance and DSP budget

- **Zero run-time allocations.** The module makes **one** allocation when it is created (1.67 MB, of which Freeze's 16 s buffer is 1.5 MB) and frees it when it is destroyed. Nothing is allocated, locked or written to disk while audio runs: every buffer is a fixed-size member of that one structure. Both engines live in it, so switching between them costs nothing at run time.
- **CPU, measured on a Move** (per 128-frame block; the budget is about 2.3 ms at 44.1 kHz; `scripts/smoke_on_device.sh`):

  | | per block |
  |---|---|
  | **Voice FX** (any preset, phrase running) | **≈ 0.12 ms** (about 5 % of the block) |
  | Dalek, Walkie | 0.08–0.10 ms |
  | Nightcall, Robot | 0.18–0.20 ms |
  | HardTune, Tape, Morph, Double, Scream, Harmony | 0.31–0.38 ms |
  | Snap, Chords | 0.41–0.43 ms |
  | Prism (the heaviest) | 0.55 ms |
  | Freeze recording or looping, on top | +0.02 ms |
  | Vocoder Demo (the sung phrase) | 0.30 ms |

- **Latency.** Voice FX: 0 ms. Vocoder: about 0 ms for the vocoders and Walkie, under 15 ms for the pitch modes and Harmony. The dry signal is delayed to match.
- **Headroom.** Voice FX and the newer vocoder modes stay under a −6.1 dBFS output ceiling at Level 0 dB, whatever the knobs or the input.
- **Build.** The C is compiled with `-Wall -Wextra -Werror -pedantic`; no `-ffast-math`, so the module links only `libc` and `libm` on the Move. The host test suite is about 1.8 million checks (every mode against six kinds of input and extreme knobs, finite and bounded, no denormals), and it runs clean under UBSan.

---

## Install and build

You need [Schwung](https://github.com/charlesvestal/schwung) running on your Move and SSH access to it (`ableton@move.local`).

### From the release archive

```bash
ssh ableton@move.local "tar -C /data/UserData/schwung/modules/audio_fx -xzf - && chmod 755 /data/UserData/schwung/modules/audio_fx/tinyvoice/tinyvoice.so" < TinyVoice.tar.gz
ssh ableton@move.local "nohup /data/UserData/schwung/restart-move.sh >/dev/null 2>&1 &"
```

Restarting Schwung is needed the first time (and after updates) so the library and the screen widgets load. Then add **TinyVoice** as an audio FX in a slot, after a sound generator such as Line In.

### From source

Requires [zig](https://ziglang.org/download/) (the C compiler: it cross-builds for the Move's glibc 2.35 ARM64) and bash (Git Bash works on Windows). `make` is optional: its targets just run the scripts.

```bash
make test                         # = bash scripts/test.sh: ~1.8 million host-native checks + the OLED widget test
make arm                          # = bash scripts/build.sh: cross-compile (-Wall -Wextra -Werror -pedantic) -> build/tinyvoice/tinyvoice.so + build/TinyVoice.tar.gz
make install                      # = bash scripts/install.sh --build: sideload (MOVE_HOST=move.local MOVE_USER=ableton)
bash scripts/smoke_on_device.sh   # runs every mode, Voice FX and all six presets on the Move itself and times them
```

`install.sh` never overwrites the loaded `.so` in place: it stages the files and renames them into position, so a running Schwung keeps its old copy intact until you restart it.

The first build downloads Schwung's two ABI headers into `src/host/` (`scripts/fetch_host_headers.sh`; pin a version with `SCHWUNG_REF`).

The architecture, signal flow, ABI notes and every parameter are in [CLAUDE.md](CLAUDE.md).

## Known limitations

- Tuned on synthetic voices and tones, the built-in phrase and a built-in mic; real-voice tuning of every mode, and of Voice FX's formant table and drive curve, is still to do.
- Voice FX is a **four-formant filter**, not a vocoder: it shapes what harmonics it is given, so a very quiet or very noisy source sounds thin or hissy. It has no envelope-to-vowel modulation (ride Vowel by hand).
- The Mode page and the Presets browser are Schwung's own and use its list style. Only one other Schwung module is known to use the same engine-switch mechanism, so report any oddity.
- Pitch tracking covers about 70 Hz–1 kHz, shifted voices ±1 octave. Scream is a pitch-synchronous (PSOLA) shifter rather than WSOLA; Harmony's grain follows your pitch (it needs a tracked voice to stay phase-coherent).
- Robot and Snap still follow pads (their pad behaviour is part of what they are); only Scream, Dalek, Walkie and Tape ignore them.
- Freeze is experimental (see above). Headsets, external mics and routing other tracks into TinyVoice are untested.
- Key and Scale are set on the Pitch page; they don't follow Move's project.
- The Audition phrase starts when Demo is switched on; it is not locked to the transport.
- No licence file yet: all rights reserved until one is added.

See [RELEASE_NOTES.md](RELEASE_NOTES.md) for the changelog.

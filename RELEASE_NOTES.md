# TinyVoice v1.0.0 — Public Preview (Beta)

A vocal processor for Ableton Move, as a [Schwung](https://github.com/charlesvestal/schwung) audio FX module (module id `tinyvoice`, library `tinyvoice.so`, archive `TinyVoice.tar.gz`). Sing or talk into the Move and put the voice through one of **two engines**, chosen on the Mode page:

- **Vocoder:** 13 voice modes (robot, talkbox, auto-tune, harmonies, Dalek, walkie-talkie and more), played from the pads or sung alone.
- **Voice FX:** a talking filter that sweeps your voice (or a bass line) across vowels, with six presets.

Both engines have a built-in **Demo**, so you can hear them with nothing plugged in.

## What's in this release

### Vocoder: 13 modes

| Mode | What it does |
|---|---|
| **Robot** | 16-band vocoder. Pads play a 4-voice carrier; with no pad held a drone (root, fifth, octave of the Key) carries the voice. Formant shift, stereo band spread, ensemble. |
| **Snap** | Hard "space-age" auto-tune: a snappy retune, a raised formant and a telephone-bright band. Key and Scale choose the notes; Beauty 0 is instant. |
| **Chords** | A 4-voice harmoniser that keeps your formants: root, 3rd, 5th and octave, in key or rooted on the pad you hold. |
| **Prism** | The same four harmony lines with tape warmth, then **Refract**: a glassy, shifted-partial shimmer. Refract 0 is the plain harmoniser. |
| **Tape** | A varispeed drop with wow and flutter, a dark top, saturation and optional hiss. |
| **Nightcall** | A talkbox: a pulse carrier with a little noise, a low cut and a throaty 1.2 kHz cavity, with the consonants bled through. Pads or the drone carry it. |
| **Scream** | A prank-caller voice changer: a deep drop with a big throat, clean until you raise Grit; Narrow squeezes it into a phone line. |
| **Dalek** | A frequency shifter (±500 Hz) with a 30 Hz ring mod and a metallic comb. |
| **HardTune** | A zero-slew pitch quantiser to Key and Scale or the pad you hold; correction depth from 0 to 200 %. |
| **Harmony** | A three-voice granular stack (an octave down, a fifth up, an octave up), or the shape of the chord you hold. |
| **Walkie** | A handset: a 350 Hz–3 kHz band, a mic compressor, a diode clip and a receiver squelch burst on every open and close. |
| **Morph** | A formant shift decoupled from pitch, into drive. |
| **Double** | A hard-tuned lead with two detuned, slightly late doubles. |

**Pages.** *Main*: Mode, five contextual knobs that are renamed for each mode, Mix, Level. *Pitch*: Key, Scale, Retune, Glide, and a live pitch / band display. *More*: Spread, Tone, Freeze, Freeze Len, Gate, Latch, Beauty, Demo.

**Every mode keeps its own five knob values**, saved with the set: max out HardTune, switch to Harmony, and Harmony is on its own settings. (Retune, Beauty, Glide and Tone are the same control as on other pages, so they stay shared.)

Also in the Vocoder:

- **Freeze:** a 16 s punch-in looper before the engines. Pick a length in bars (1, 2, 4 or Free) and the take starts on the next downbeat of Move's transport and loops to the bar. A take shorter than 50 ms is ignored, and one under 120 ms is extended so a tap can't make a micro-loop.
- **Chord Latch:** play a chord once, take your hands off the pads and sing (Robot, Chords, Prism, Nightcall, Harmony).
- **One Gate knob:** Auto (input levelling plus a voice gate), or a manual threshold from −60 to 0 dB.
- **Built-in mic conditioning:** a 120 Hz high-pass on the input, so pad taps and chassis thumps don't open the gate or muddy the vocoder.
- **Pad handling:** pads play the exact notes you press (folded by octaves toward your voice); only the track's own pads are heard, not other tracks' MIDI.
- **Demo:** a built-in sung phrase that tours all 13 modes, four bars each, at the Move's tempo. Turn the Mode knob and the tour stays on that mode.
- **Click-free mode switching:** the old engine fades out and the new one fades in over 5 ms.
- **Level:** Nightcall, Scream, Dalek, HardTune, Harmony, Walkie and Voice FX stay under a −6.1 dBFS ceiling at Level 0 dB; Level is the boost above it.

There is no built-in delay or reverb: use the chain's own effects after TinyVoice.

### Voice FX: a talking filter

Your voice, or a bass line, through four vowel formants you sweep across **A · E · I · O · U**. One page of eight knobs:

| Knob | What it does |
|---|---|
| **Vowel** | Sweeps A · E · I · O · U |
| **Tract** | Scales the formants: a deep chest to a small throat (±0.6 octave) |
| **Peak** | The formants' sharpness |
| **Choir** | Detunes left and right apart and adds a stereo chorus |
| **Breath** | Noise that follows the voice, through the formants, plus the voice's own air |
| **Attack** | An onset swell, 0.5 to 120 ms |
| **Release** | A tail on the formant output |
| **Mix/Drv** | Wet/dry up to 70 %, then all wet with the drive climbing |

- **Presets:** Yeah Boy, Robo Talk, Monster Throat, Funky Talkbox, Alien Choir, Vowel Morph Solo.
- **Audition page:** **Demo** plays a built-in eight-bar electro-funk phrase (a bass and offbeat stabs) at the Move's tempo, with the Vowel, Tract, Peak, Choir and Mix/Drv knobs moving on their own so you can see and hear the range. Touch one of those knobs, or pick a preset, and Demo stops and the knob is yours. Choose a preset *before* starting Demo.
- **Level-neutral:** the knobs change tone, not loudness.

## Performance

One allocation at load, none while audio runs. Measured on a Move, per 128-frame block (budget about 2.3 ms): Voice FX about 0.12 ms, the vocoder modes 0.08–0.55 ms. About 1.9 million automated checks; the C is built with `-Wall -Wextra -Werror -pedantic` and the suite runs clean under UBSan.

## Install

```bash
ssh ableton@move.local "tar -C /data/UserData/schwung/modules/audio_fx -xzf - && chmod 755 /data/UserData/schwung/modules/audio_fx/tinyvoice/tinyvoice.so" < TinyVoice.tar.gz
ssh ableton@move.local "nohup /data/UserData/schwung/restart-move.sh >/dev/null 2>&1 &"
```

Then add **TinyVoice** as an audio FX in a slot, after a sound generator such as Line In (the Move's built-in mic works through Line In). The [README](README.md) has the full guide: every mode and knob, setup and gain staging, MIDI, and the DSP budget.

## Known limitations

This is a public preview. It was tuned on synthetic voices, tones and a built-in mic, and checked on a Move with the on-device smoke test, but not judged by ear across many singers. Headsets, external mics and routing other tracks into TinyVoice are untested. Key and Scale are set on the Pitch page and don't follow Move's project. No licence file yet: all rights reserved until one is added. See the README's [Known limitations](README.md#known-limitations).

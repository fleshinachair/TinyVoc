# TinyVoice v1.0.0 — Public Preview (Beta)

A vocal processor for Ableton Move, as a [Schwung](https://github.com/charlesvestal/schwung) audio FX module (module id `tinyvoice`, library `tinyvoice.so`, archive `TinyVoice.tar.gz`). Two engines in one module, chosen on the Mode page:

## Vocoder

Sing into the Move, play the pads, and turn your voice into a hard-tuned robot, a talkbox, a one-finger choir, a Dalek, a walkie-talkie or a frozen drone. **13 modes** in one list: the vocoder and carrier modes (Robot, Snap, Chords, Prism, Tape, Nightcall), the voice transformers (Scream, Dalek, HardTune, Harmony, Walkie), and Morph and Double. A home page of eight knobs (Mode, five contextual knobs named per mode, Mix, Level), a Pitch page and a More page.

- **Hands-free Chord Latch:** play a chord once, take your hands off the pads and sing (Robot, Chords, Prism, Nightcall, Harmony).
- **Freeze:** a 16 s punch-in looper before the engines, with bar-synced takes that start on the downbeat. A take can't be shorter than a 120 ms loop (an accidental tap is ignored).
- **Demo:** a built-in sung phrase that tours the modes, four bars each, at the Move's tempo.
- **One Gate knob:** Auto, or a manual threshold from −60 to 0 dB.
- **Built-in mic conditioning:** a 120 Hz high-pass on the input, so pad taps and chassis thumps don't open the gate or muddy the vocoder.
- **Click-free mode switching:** the old engine fades out and the new one in over 5 ms.
- **A −6.1 dBFS output ceiling** on Nightcall, Scream, Dalek, HardTune, Harmony, Walkie and Voice FX at Level 0 dB.

## Voice FX

A talking filter: your voice (or a bass line) through four vowel formants you sweep across A · E · I · O · U, on **one page of eight knobs** (Vowel, Tract, Peak, Choir, Breath, Attack, Release, Mix/Drv), with a Presets page (Yeah Boy, Robo Talk, Monster Throat, Funky Talkbox, Alien Choir, Vowel Morph Solo) and an Audition page whose **Demo** plays a built-in eight-bar electro-funk phrase so you can hear it with nothing routed in. Level-neutral by design: the knobs change tone, not loudness.

## Performance

One allocation at load, none while audio runs. Measured on a Move, per 128-frame block (budget about 2.3 ms): Voice FX about 0.12 ms, the vocoder modes 0.08–0.55 ms. About 1.9 million automated checks; the C is built with `-Wall -Wextra -Werror -pedantic` and the suite runs clean under UBSan.

## Install

```bash
ssh ableton@move.local "tar -C /data/UserData/schwung/modules/audio_fx -xzf - && chmod 755 /data/UserData/schwung/modules/audio_fx/tinyvoice/tinyvoice.so" < TinyVoice.tar.gz
ssh ableton@move.local "nohup /data/UserData/schwung/restart-move.sh >/dev/null 2>&1 &"
```

Then add **TinyVoice** as an audio FX in a slot, after a sound generator such as Line In. The [README](README.md) has the full guide: every mode and knob, setup and gain staging, MIDI, and the DSP budget.

## Known limitations

This is a public preview. It was tuned on synthetic voices, tones and a built-in mic, and checked on a Move with the on-device smoke test, but not judged by ear across many singers. Headsets, external mics and routing other tracks into TinyVoice are untested. Key and Scale are set on the Pitch page and don't follow Move's project. No licence file yet: all rights reserved until one is added. See the README's [Known limitations](README.md#known-limitations).

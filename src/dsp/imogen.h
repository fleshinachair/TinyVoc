/*
 * Imogen engine (Chords): 4-voice harmonizer in the spirit of the DigiTech
 * Vocalist on "Hide and Seek". Every voice is the singer's own voice,
 * re-pitched by a formant-preserving PSOLA voice on the Cher tracker's delay
 * line, steered from the tracked pitch to its own target note.
 *
 * Targets, per block:
 *   one pad held        -> the chord below, rooted on the pad
 *   several pads held   -> exactly those notes, moved as a block
 *   no pads, Auto on    -> a chord built on the sung note, snapped to Key/Scale:
 *                          root, the 3rd and 5th above (diatonic), octave below
 *   no pads, Auto off   -> just the snapped lead (a Cher-style single voice)
 * Every target is then fitted into the singer's register: at most +11 st
 * above the sung note, at most an octave below (vt_fit_register).
 * Unvoiced input (consonants, breath) passes on voice 0 at ratio 1,
 * high-passed so only the consonants come through, never unshifted pitch.
 *
 * No allocation; embedded in vt_t next to the cher_t it reads from.
 */
#ifndef TINYVOC_IMOGEN_H
#define TINYVOC_IMOGEN_H

#include "cher.h"
#include "psola.h"

#define VT_REG_UP          11       /* highest voice: +11 st over the sung note */
#define VT_REG_DOWN        12       /* lowest: an octave under (PSOLA's floor) */
#define VT_UNVOICED_HP_HZ  2500.0f  /* unvoiced passthrough keeps only consonants */
#define VT_HARMONY_TRIM    1.1885f  /* +1.5 dB on the Chords / Prism wet bus (was +4.5; -3 dB for headroom) */

#ifdef __cplusplus
extern "C" {
#endif

#define IMOGEN_VOICES 4

typedef struct {
    int   note;         /* target MIDI note, -1 = silent */
    float glide_off;    /* semitones still to glide (decays to 0) */
    float lr;           /* log2 pitch ratio, smoothed */
    float amp;          /* fade in/out */
    psola_voice pv;     /* the shifter */
} imogen_voice;

typedef struct {
    float sr;
    float retune_a, glide_a, fade_a;
    float hp_a, hp_z[3], voiced_w;   /* voice 0's unvoiced high-pass and its blend */
    float pan_l[IMOGEN_VOICES], pan_r[IMOGEN_VOICES];
    int   active;       /* number of voices with a target this block */
    int   auto_chord;   /* targets came from the auto chord (voice 0 = lead) */
    int   pad_oct;      /* octaves the held pad chord is folded by, toward the voice */
    int   pad_last[IMOGEN_VOICES];   /* the pads pad_oct was worked out for */
    float harmony_level, humanize;
    float beauty;                       /* 0..1: micro-timing and detune on the harmonies */
    float mt[IMOGEN_VOICES][1024], mt_d[IMOGEN_VOICES];
    int   mt_w;
    float t;            /* seconds, for the humanize drift */
    imogen_voice v[IMOGEN_VOICES];
} imogen_t;

void imogen_init(imogen_t *im, float sample_rate);

/* held[i] is MIDI slot i's note (-1 = free). The tracker c must already have
 * Key/Scale set (cher_set_params) so c->note is the snapped sung note. */
void imogen_set_targets(imogen_t *im, const cher_t *c, const int held[IMOGEN_VOICES],
                        int auto_on, int root, int scale,
                        float retune_ms, float glide_ms, float spread, int voicing);

/* Push one input sample through the tracker and render the stereo voices
 * (already trimmed by VT_HARMONY_TRIM). */
void imogen_process(imogen_t *im, cher_t *c, float in, float *out_l, float *out_r);

/* Samples the voices lag the input by (what the dry path is delayed by). */
float imogen_latency(const imogen_t *im);

/* Shared with Prism: fit targets into [ref - 12, ref + 11] by octaves (ref < 0:
 * leave them), and the two-pole high-pass for the unvoiced passthrough
 * (z: 3 floats of state, a from vt_unvoiced_hp_coef). */
void  vt_fit_register(int *notes, int n, int ref);
float vt_unvoiced_hp_coef(float sr);
float vt_unvoiced_hp(float *z, float x, float a);

/* The auto chord for a sung (snapped) note; fills IMOGEN_VOICES notes. */
void imogen_auto_chord(int note, int root, int scale, int out[IMOGEN_VOICES]);

/* Chord voicings built on a snapped note, in key (chromatic = major triad):
 *   VT_VOICING_CLOSE    root, 3rd, 5th, octave above
 *   VT_VOICING_STANDARD root, 3rd, 5th, octave below   (= imogen_auto_chord)
 *   VT_VOICING_WIDE     root, octave below, 5th, 10th (3rd an octave up) */
enum { VT_VOICING_CLOSE = 0, VT_VOICING_STANDARD, VT_VOICING_WIDE };
void vt_chord(int note, int root, int scale, int voicing, int out[IMOGEN_VOICES]);

/* Chords-mode character: lead_level scales voice 0 (the sung, snapped lead)
 * against the harmonies in auto mode; voicing as above; humanize 0..1 detunes
 * each voice by a few cents and drifts it slowly. Call after set_targets. */
void imogen_set_character(imogen_t *im, float harmony_level, float humanize);

/* Beauty 0..1: the harmony voices land 8 / 10 / 12 ms late and +-4 ct off
 * (a choir, not a chord machine); the caller widens the pan spread too. */
void imogen_set_beauty(imogen_t *im, float beauty);

#ifdef __cplusplus
}
#endif
#endif

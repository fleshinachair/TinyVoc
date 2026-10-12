/*
 * Voice FX: pure formant processing of the voice, no vocoder bands and no
 * carrier. The input goes through a bank of four resonant band-pass formants
 * that sweep across the vowels A E I O U, one bank per side so the choir knob
 * can detune left from right. Fixed-size state, no allocation, flushes
 * denormals; nothing here knows about Schwung (dsp.c calls it).
 *
 * The eight Voice FX knobs (seven here, Mix/Drive is dsp.c's wet/dry plus the
 * `drive` argument):
 *   vowel    0..1  sweeps A E I O U (0 = A, 0.25 = E, 0.5 = I, 0.75 = O, 1 = U)
 *   tract    0..1  formant scale, 0.5 = natural, +-0.6 octave (gender / throat length)
 *   peak     0..1  formant Q: broad and natural at 0, narrow and intense at 1
 *   choir    0..1  left and right banks detune apart, up to +-7 % per formant
 *   breath   0..1  noise through the formants (whisper) plus the voice's own
 *                  consonants and air bled past them
 *   attack   0..1  onset swell: 0.5 ms (punchy) to 120 ms (a slow bloom)
 *   release  0..1  how long the vowel rings on after the voice stops (a tail
 *                  made from the formant output, not from the dry voice)
 */
#ifndef TINYVOICE_VFX_H
#define TINYVOICE_VFX_H

#include "verb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VFX_FORMANTS 4
/* Mix/Drive is one knob in two segments: 0 .. VFX_MIX_KNEE is the wet/dry
 * blend (all wet at the knee, with a touch of saturation), and from there to 1
 * the voice stays all wet and the saturation climbs from a warm clip to a hard one. */
#define VFX_MIX_KNEE 0.7f

typedef struct { float k, a1, a2, a3, s1, s2; } vfx_svf;

typedef struct {
    float sr;

    /* knobs, once per block */
    float vowel, tract, peak, choir, breath, attack, release;
    float drive_k, drive_n;           /* tanh drive and its level compensation */
    int   ov_on, ov_a, ov_b;          /* the tour's vowel: `ov_t` of the way from vowel ov_a to vowel ov_b (instead of the Vowel knob) */
    float ov_t;
    float lev, ms_in, ms_out;         /* the leveller: wet RMS held at the input's, so a knob changes tone, not loudness */
    float k_ms, k_lev;

    /* the bank: smoothed formant centres and weights, one set of filters per side */
    float f_hz[VFX_FORMANTS], f_w[VFX_FORMANTS];
    vfx_svf bank[2][VFX_FORMANTS];
    float k_f;                        /* per-block smoothing of the centres */

    /* the envelope shaper (attack) and breath */
    float env_fast, env_slow, env_breath;
    float k_att_fast, k_rel, k_att_slow;
    float hp_z[2][2];                 /* consonant high-pass, 2 x 1-pole */
    float hp_a;
    unsigned rng;

    verb_t tail;                      /* release: the formant output, left ringing */
} vfx_t;

void  vfx_init(vfx_t *v, float sample_rate);
/* Once per block. Every argument is 0..1 (`drive` is the Mix/Drive knob again:
 * the wet/dry part of it is dsp.c's, the saturation is read from it here). */
void  vfx_set(vfx_t *v, float vowel, float tract, float peak, float choir,
              float breath, float attack, float release, float drive);
/* Take the vowel from the tour instead of the Vowel knob: `t` (0..1) of the way from vowel `a` to
 * vowel `b` (0 A, 1 E, 2 I, 3 O, 4 U), a straight glide between the two. The knob's own
 * value is untouched: vfx_vowel_knob() goes back to it. */
void  vfx_vowel_pair(vfx_t *v, int a, int b, float t);
void  vfx_vowel_knob(vfx_t *v);
/* One sample in, stereo out. */
void  vfx_process(vfx_t *v, float in, float *out_l, float *out_r);

#ifdef __cplusplus
}
#endif
#endif

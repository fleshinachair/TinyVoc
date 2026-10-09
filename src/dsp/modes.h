/*
 * The PSOLA-based and effect modes: Snap, Morph, Prism, Double, Tape.
 * (Robot is daft.c; Chords is imogen.c.)
 *
 * One fx_t serves whichever of these modes is active, so its PSOLA voices,
 * Haas lines and filters are shared rather than duplicated per mode. All of
 * them read the Cher tracker (cher_t) for pitch and the input delay line.
 *
 * Macro knobs (char1, char2, mod are 0..1; 0.5 is neutral where bipolar):
 *   Snap    char1 formant -12..+12 st   char2 tracking sensitivity   mod vibrato depth
 *   Morph   char1 formant -12..+12 st   char2 drive / saturation     mod formant wobble
 *   Prism   char1 harmony spread        char2 tape warmth            mod humanize
 *   Double  char1 double detune         char2 stereo width           mod drift
 *   Tape    char1 pitch -12..0 st       char2 wow & flutter          mod saturation
 */
#ifndef TINYVOC_MODES_H
#define TINYVOC_MODES_H

#include "cher.h"
#include "psola.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FX_VOICES   4
#define FX_MT_LEN 1024        /* Beauty micro-timing lines: up to ~23 ms */
#define FX_HAAS_LEN 2048        /* power of two; Haas taps reach 25 ms + drift */

typedef struct {
    float sr;

    /* per-block settings */
    int   mode;
    float char1, char2, mod, spread;
    int   auto_on, root, scale;
    float glide_a;
    int   held[FX_VOICES];

    /* PSOLA voices: Snap/Morph use [0]; Double [0..2]; Prism [0..3] */
    psola_voice pv[FX_VOICES];
    int   note[FX_VOICES];       /* Prism targets */
    float glide_off[FX_VOICES];
    float amp[FX_VOICES];
    int   active;
    int   pad_oct;               /* octaves the held pad chord is folded by, toward the voice */
    int   pad_last[FX_VOICES];   /* the pads pad_oct was worked out for */
    float hp_a, hp_z[3], voiced_w;  /* Prism voice 0: unvoiced high-pass and its blend */
    int   snap_note;             /* Snap: last target note, to catch hard-tune jumps (-1 none) */

    /* LFOs and drift */
    double t;                    /* seconds */
    unsigned rng;
    float drift[2], drift_goal[2];
    int   drift_count;

    /* Double: Haas lines for the two doubles */
    float haas[2][FX_HAAS_LEN];
    int   haas_w;

    /* saturation / filters */
    float sat_k, sat_n;          /* per-block saturation drive and its RMS normalisation */
    float lp[2][2];              /* 2-pole lowpass per channel (Prism warmth, Tape) */
    /* Beauty (fx_set_beauty): per-mode polish */
    float beauty;
    float sh_x, sh_y, a12;                         /* Snap: 12 kHz air shelf */
    float hb[2], hb_env, hf_env, hk, k_env10;      /* Snap: anti-harshness (2.5-5 kHz dynamic cut) */
    float mt[FX_VOICES][FX_MT_LEN], mt_d[FX_VOICES];   /* Prism: micro-timing per harmony voice */
    int   mt_w;
    float tape_phi;              /* Tape: delay-line shifter phase */
} fx_t;

void  fx_init(fx_t *fx, float sample_rate);

/* Once per block, after the tracker's params are set. held[] are the MIDI slot
 * notes (-1 = free); Prism builds its chord from them or from c->note. */
/* Beauty 0..1, per block. Snap: +2 dB air shelf at 12 kHz, a dynamic cut of
 * up to 4 dB on 2.5-5 kHz harshness, a quiet Haas double. Prism: up to +40%
 * pan width, 8/10/12 ms micro-timing and +-4 ct detune on the harmonies. */
static inline void fx_set_beauty(fx_t *fx, float b) { fx->beauty = b < 0.0f ? 0.0f : (b > 1.0f ? 1.0f : b); }

void  fx_set(fx_t *fx, const cher_t *c, int mode, float char1, float char2, float mod,
             float spread, float glide_ms, int auto_on, int root, int scale,
             const int held[FX_VOICES]);

/* One sample: pushes `in` through the tracker and renders stereo. */
void  fx_process(fx_t *fx, cher_t *c, float in, float *out_l, float *out_r);

/* Wet-path latency in samples, for lining up the dry signal. */
float fx_latency(const fx_t *fx, const cher_t *c);

#ifdef __cplusplus
}
#endif
#endif

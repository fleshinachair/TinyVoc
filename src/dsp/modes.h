/*
 * The PSOLA-based and effect modes: Snap, Morph, Prism, Double, Tape,
 * Scream, HardTune. (Robot and Nightcall are daft.c; Chords is imogen.c;
 * Dalek, Harmony and Walkie are voice.c.)
 *
 * One fx_t serves whichever of these modes is active, so its PSOLA voices,
 * Haas lines and filters are shared rather than duplicated per mode. All of
 * them read the Cher tracker (cher_t) for pitch and the input delay line.
 *
 * Macro knobs (char1, char2, mod are 0..1; 0.5 is neutral where bipolar):
 *   Snap    char1 formant -12..+12 st   char2 tracking sensitivity   mod vibrato depth
 *           (+2.5 st fixed lift, telephone band + 2.5 kHz bite: SNAP_* below)
 *   Morph   char1 formant -12..+12 st   char2 drive / saturation     mod formant wobble
 *   Prism   char1 harmony spread        char2 tape warmth            mod humanize
 *   Double  char1 double detune         char2 stereo width           mod drift
 *   Tape    char1 pitch -12..0 st       char2 wow & flutter          mod saturation
 *           char4 Tone (alias, dsp.c)   char5 tape hiss (0 below 0.5)
 *   Scream  char1 pitch -12..-3 st (0.5 = -8)  char2 throat (formant scale, 0.5 = 0.84)
 *           mod grit (0 = clean)   char4 narrow (90 Hz..4.8 kHz at 0.5, a phone line at 1)
 *           char5 line hiss (0 below 0.5)
 *   HardTune char1 amount 0..200% (0.5 = full)  char2 formant -12..+12 st   mod vibrato
 *           (char4 glide and char5 sensitivity live in dsp.c)
 *   Prism   char4 refract (inharmonic glass: a frequency-shifted pair mixed in at
 *           0.8 char4^2, 0 = the plain harmonizer)   char5 shimmer (comb feedback)
 */
#ifndef TINYVOICE_MODES_H
#define TINYVOICE_MODES_H

#include "cher.h"
#include "psola.h"
#include "ssb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FX_VOICES   4
#define FX_MT_LEN 1024        /* Beauty micro-timing lines: up to ~23 ms */
#define FX_HAAS_LEN 2048        /* power of two; Haas taps reach 25 ms + drift */
#define FX_GLASS_LEN 512        /* power of two; Prism's comb lines reach ~7 ms */

/* Snap's "space-age" voicing, fixed: a telephone band (4th-order 350 Hz
 * high-pass, 4th-order 5 kHz low-pass), a +4 dB bite at 2.5 kHz, and the
 * formant lifted 2.5 st on top of char1. Snap only. */
#define SNAP_HPF_HZ        350.0f
#define SNAP_LPF_HZ        5000.0f
#define SNAP_BITE_HZ       2500.0f
#define SNAP_BITE_DB       4.0f
#define SNAP_FORMANT_LIFT  2.5f   /* semitones */
#define SNAP_MAKEUP        1.66f  /* +4.4 dB: what the band takes off the demo phrase, given back */

/* TPT state-variable filter section (fixed coefficients). */
typedef struct { float a1, a2, a3, k, s1, s2; } fx_svf_t;

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
    fx_svf_t tel[5];                               /* Snap: HP, HP, LP, LP, 2.5 kHz bite */
    float tel_bite, tel_makeup;
    float mt[FX_VOICES][FX_MT_LEN], mt_d[FX_VOICES];   /* Prism: micro-timing per harmony voice */
    int   mt_w;
    float tape_phi;              /* Tape: delay-line shifter phase */

    /* char4 / char5 (fx_set_extra) */
    float char4, char5;
    /* Scream: telephone band (4th-order high-pass + low-pass), grit, line hiss */
    fx_svf_t scr[4];
    float scr_hp, scr_lp;        /* the corner frequencies the sections are set to */
    /* Prism glass: the harmony bus through a Hilbert pair, shifted up on one
     * side and down on the other (non-integer partial ratios), then a slowly
     * modulated pair of combs */
    ssb_hilbert pg_hil;
    ssb_osc pg_l, pg_r;
    float pg_line[2][FX_GLASS_LEN];
    int   pg_w;
    float pg_amt, pg_amt_goal;   /* refract, smoothed */
    unsigned pg_rng;
} fx_t;

void  fx_init(fx_t *fx, float sample_rate);

/* Once per block, after the tracker's params are set. held[] are the MIDI slot
 * notes (-1 = free); Prism builds its chord from them or from c->note. */
/* Beauty 0..1, per block. Snap: +2 dB air shelf at 12 kHz, a dynamic cut of
 * up to 4 dB on 2.5-5 kHz harshness, a quiet Haas double. Prism: up to +40%
 * pan width, 8/10/12 ms micro-timing and +-4 ct detune on the harmonies. */
static inline void fx_set_beauty(fx_t *fx, float b) { fx->beauty = b < 0.0f ? 0.0f : (b > 1.0f ? 1.0f : b); }

/* char4 / char5 (0..1), once per block, after fx_set: Scream's narrow and line
 * hiss, Prism's refract and shimmer. */
void  fx_set_extra(fx_t *fx, float char4, float char5);

void  fx_set(fx_t *fx, const cher_t *c, int mode, float char1, float char2, float mod,
             float spread, float glide_ms, int auto_on, int root, int scale,
             const int held[FX_VOICES]);

/* One sample: pushes `in` through the tracker and renders stereo. */
void  fx_process(fx_t *fx, cher_t *c, float in, float *out_l, float *out_r);

/* Snap's telephone band + 2.5 kHz bite + makeup, one sample (stateful; used
 * by the Snap case, exposed for tests). */
float fx_snap_voicing(fx_t *fx, float x);

/* Wet-path latency in samples, for lining up the dry signal. */
float fx_latency(const fx_t *fx, const cher_t *c);

#ifdef __cplusplus
}
#endif
#endif

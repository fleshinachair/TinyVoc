/*
 * Audition groove: a two-bar funk bass loop from a bright dual-saw voice, so
 * Voice FX (and its presets) can be auditioned with no track routed in. Demo
 * On substitutes it for the input in Voice FX; the sung phrase (demo.c) stays
 * the vocoder's demo. Heap-free: a few floats and a const pattern.
 *
 *   tone     two band-limited saws, +-6 cents apart, through a pluck filter
 *            that opens on each note and closes as it decays; a 1.5 ms attack
 *            and a short decay VCA
 *   pattern  32 sixteenth-note steps (two bars of 4/4) of D minor around
 *            D1..D2 (MIDI 26..38), syncopated, at the host tempo; every note restarts both
 *            oscillators, so the loop repeats sample for sample
 */
#ifndef TINYVOICE_GROOVE_H
#define TINYVOICE_GROOVE_H

#ifdef __cplusplus
extern "C" {
#endif

#define GROOVE_STEPS 32          /* 16th notes in the loop: two bars */

typedef struct {
    float sr, step_len;          /* samples per sixteenth at the current tempo */
    double t;                    /* samples into the current step */
    int   step;                  /* 0 .. GROOVE_STEPS - 1 */
    int   gate;                  /* the note is held */
    int   att;                   /* the VCA is still ramping up */
    float k_att, k_dec, k_rel;   /* per-sample VCA steps: attack (linear), decay and release (multipliers) */
    float ph1, ph2, inc;         /* oscillator phases and the pitch's phase increment */
    float env, vel;              /* VCA and the step's velocity */
    float lp;                    /* pluck filter state */
} groove_t;

void  groove_init(groove_t *g, float sample_rate);
/* Back to the first step (Demo switched on). */
void  groove_reset(groove_t *g);
/* Once per block: bpm 20..400, 120 until set. */
void  groove_set_bpm(groove_t *g, float bpm);
/* One sample of the loop, about +-0.2 peak. */
float groove_process(groove_t *g);
/* The MIDI note on a step (0 = a rest), for tests and tools. */
int   groove_note(int step);

#ifdef __cplusplus
}
#endif
#endif

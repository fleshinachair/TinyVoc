/*
 * Space-page reverb: a small plate-style stereo reverb.
 *
 * Mono send -> two allpass diffusers -> 4-line feedback delay network
 * (Hadamard mix, per-line damping) -> decorrelated L/R taps. One knob,
 * `amount` 0..1, sets the return level and the decay together (RT60
 * ~0.8 s at the bottom of the range to ~3 s at the top). Fixed-size state,
 * no allocation, flushes denormals.
 */
#ifndef TINYVOICE_VERB_H
#define TINYVOICE_VERB_H

#ifdef __cplusplus
extern "C" {
#endif

#define VERB_LINES   4
#define VERB_LINE_N  4096          /* power of two; longest line ~3100 samples */
#define VERB_AP_N    512           /* power of two; diffuser lengths < 512 */

typedef struct {
    float line[VERB_LINES][VERB_LINE_N];
    int   len[VERB_LINES];
    float fb[VERB_LINES];          /* per-line feedback for the current RT60 */
    float damp[VERB_LINES];        /* in-loop lowpass state */
    float ap[2][VERB_AP_N];
    int   ap_len[2];
    int   w, ap_w;
    float damp_a;
    float ret, ret_goal;           /* smoothed return level */
    float k_ret;
    float amount;                  /* last amount the coefficients were built for (-1 = none) */
    float sr;
} verb_t;

void verb_init(verb_t *v, float sample_rate);
/* Once per block: amount 0..1 (0 = the return fades out; the tail still rings). */
void verb_set(verb_t *v, float amount);
/* Mono send in, stereo return added to *l and *r. */
void verb_process(verb_t *v, float in, float *l, float *r);

#ifdef __cplusplus
}
#endif
#endif

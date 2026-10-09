/*
 * TD-PSOLA voice: pitch and formant shifted independently.
 *
 * Reads the Cher tracker's delay line. Analysis epochs ("marks") step through
 * the input exactly one input period T0 apart, so consecutive grains are in
 * phase with each other. Each output grain is a Hann-windowed segment around
 * the current mark, emitted every T0 / pitch_ratio samples (that spacing IS
 * the new pitch) and read at formant_ratio speed (that resampling moves the
 * spectral envelope without moving the pitch).
 *
 * Unvoiced input uses a fixed 256-sample period at ratio 1, which is plain
 * overlap-add: the noise passes at the voice's latency.
 *
 * Fixed-size state; several voices can share one tracker (Prism, Double).
 */
#ifndef TINYVOC_PSOLA_H
#define TINYVOC_PSOLA_H

#include "cher.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PSOLA_MAX_GRAINS 8
#define PSOLA_UNVOICED_T0 256.0f

typedef struct {
    double start;       /* absolute input index of the grain's first sample */
    float  rate;        /* input samples per output sample (formant ratio) */
    float  t, len;      /* output position / output length */
    float  gain;
    float  cut_at, cut_len;  /* phase reset: fade out from t = cut_at - cut_len to cut_at (0 = none) */
    int    active;
} psola_grain;

typedef struct {
    psola_grain g[PSOLA_MAX_GRAINS];
    double mark;        /* absolute input index of the current analysis epoch */
    float  until_next;  /* output samples until the next grain starts */
    float  lat;         /* current latency, samples (smoothed) */
    int    started;
    int    have_mark;
    float  tight;       /* 0..1: grains shortened from 2 to 1.2 periods (buzzier, more pulse-like) */
    float  kick;        /* pending phase reset strength, applied at the next grain (0 = none) */
    unsigned resets;    /* phase resets applied (diagnostics / tests) */
} psola_voice;

void  psola_init(psola_voice *v);

/* Hard-tune character (Snap). tight 0 = plain TD-PSOLA. */
static inline void psola_set_tight(psola_voice *v, float tight) { v->tight = tight; }

/* Pitch-synchronous phase reset at the next grain start (the next output
 * pitch period): the epoch grid is re-anchored instead of continued and the
 * sounding grains fade out early (smooth taper, at least 5 ms), so old and
 * new phase overlap briefly: the gritty edge of a hard-tune note change,
 * without a click. strength 0..1 scales how early the old grains go. */
static inline void psola_kick(psola_voice *v, float strength) { if (strength > v->kick) v->kick = strength; }

/* One output sample. pitch_ratio and formant_ratio are clamped to [0.5, 2].
 * t0 is the input period in samples (pass 0 when unvoiced). min_lat lets the
 * caller line this voice up with another path (e.g. the plain/unvoiced one). */
float psola_process(psola_voice *v, const cher_t *c, float t0, float pitch_ratio,
                    float formant_ratio, float min_lat);

#ifdef __cplusplus
}
#endif
#endif

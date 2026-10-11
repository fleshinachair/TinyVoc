/*
 * Single-sideband frequency shifter: a 4th-order IIR Hilbert transform pair
 * (two cascades of four half-rate all-passes, Olli Niemitalo's coefficients)
 * giving I and Q 90 degrees apart (within 0.5 degree from 100 Hz to ~8 kHz at
 * 44.1 kHz), and a rotating phasor that moves the analytic signal.
 *
 * Header-only and allocation-free: used by Dalek (voice.c) and by Prism's
 * inharmonic refraction stage (modes.c). Group delay is a few samples above
 * a few hundred Hz and rises toward the low end; see ssb_delay().
 */
#ifndef TINYVOICE_SSB_H
#define TINYVOICE_SSB_H

#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { float x1, x2, y1, y2; } ssb_ap;     /* y = a2 (x + y[n-2]) - x[n-2] */

typedef struct {
    ssb_ap a[4], b[4];
    float  a_dly;              /* the A path is one sample late: that is the 90 degrees */
} ssb_hilbert;

static inline float ssb_flush(float x) { return fabsf(x) < 1e-15f ? 0.0f : x; }

static inline float ssb_ap_tick(ssb_ap *s, float a2, float x)
{
    const float y = ssb_flush(a2 * (x + s->y2) - s->x2);
    s->x2 = s->x1; s->x1 = x;
    s->y2 = s->y1; s->y1 = y;
    return y;
}

/* One sample in; i and q out, q = the Hilbert transform of i (q leads... see
 * ssb_shift for the sign that makes +shift go up in frequency). */
static inline void ssb_hilbert_tick(ssb_hilbert *h, float x, float *i, float *q)
{
    static const float A2[4] = { 0.6923878f * 0.6923878f, 0.9360654322959f * 0.9360654322959f,
                                 0.9882295226860f * 0.9882295226860f, 0.9987488452737f * 0.9987488452737f };
    static const float B2[4] = { 0.4021921162426f * 0.4021921162426f, 0.8561710882420f * 0.8561710882420f,
                                 0.9722909545651f * 0.9722909545651f, 0.9952884791278f * 0.9952884791278f };
    float pa = x, pb = x;
    for (int k = 0; k < 4; k++) {
        pa = ssb_ap_tick(&h->a[k], A2[k], pa);
        pb = ssb_ap_tick(&h->b[k], B2[k], pb);
    }
    *i = h->a_dly;
    h->a_dly = pa;
    *q = -pb;
}

/* A phasor, rotated by a fixed per-sample angle (set once per block). */
typedef struct { float c, s, cw, sw; } ssb_osc;

static inline void ssb_osc_init(ssb_osc *o) { o->c = 1.0f; o->s = 0.0f; o->cw = 1.0f; o->sw = 0.0f; }

static inline void ssb_osc_set(ssb_osc *o, float hz, float sr)
{
    const float w = 6.28318530718f * hz / sr;
    o->cw = cosf(w);
    o->sw = sinf(w);
}

static inline void ssb_osc_tick(ssb_osc *o)
{
    const float c = o->c * o->cw - o->s * o->sw;
    const float s = o->s * o->cw + o->c * o->sw;
    const float n = 1.5f - 0.5f * (c * c + s * s);       /* keeps |z| at 1 without a sqrt */
    o->c = c * n;
    o->s = s * n;
}

/* Upper sideband at +hz when the oscillator runs at +hz (a negative hz is the
 * lower sideband: the same formula with the phasor turning the other way). */
static inline float ssb_shift(const ssb_osc *o, float i, float q) { return i * o->c - q * o->s; }

#ifdef __cplusplus
}
#endif
#endif

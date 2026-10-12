#include "groove.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f

#define GR_AMP        0.20f      /* peak, about what Auto's input stage steers a voice to */
#define GR_DETUNE     0.0035f    /* +-6 cents between the two saws */
#define GR_GATE       0.62f      /* a note is held for this share of its step: punchy, not legato */
#define GR_ATTACK_SEC 0.0015f
#define GR_DECAY_SEC  0.14f      /* while held */
#define GR_REL_SEC    0.008f     /* after the gate: the step ends silent */

/* D minor around D1 (MIDI 26, 36.7 Hz; C4 = 60 as everywhere in this module) .. D2 (38): root and octave
 * hits, F, G, A and C as passing tones. Bar 1 states the riff; bar 2 answers and climbs into the turnaround. */
static const unsigned char k_notes[GROOVE_STEPS] = {
    /* bar 1 */ 26,  0,  0, 26,   0,  0, 38,  0,   26,  0, 26,  0,   0, 38,  0, 33,
    /* bar 2 */ 26,  0,  0, 26,   0, 29,  0, 31,   26,  0, 38,  0,  36,  0, 33,  0,
};

static inline float flush(float x) { return fabsf(x) < 1e-15f ? 0.0f : x; }

int groove_note(int step)
{
    return (step >= 0 && step < GROOVE_STEPS) ? (int)k_notes[step] : 0;
}

static inline float polyblep(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

void groove_set_bpm(groove_t *g, float bpm)
{
    if (!(bpm >= 20.0f && bpm <= 400.0f)) bpm = 120.0f;
    g->step_len = g->sr * 60.0f / bpm * 0.25f;
}

void groove_reset(groove_t *g)
{
    g->step = GROOVE_STEPS - 1;                 /* the first sample starts step 0, with the step clock at exactly 0 */
    g->t = (double)g->step_len - 1.0;
    g->gate = 0;
    g->att = 0;
    g->ph1 = g->ph2 = 0.0f;
    g->env = 0.0f;
    g->lp = 0.0f;
}

void groove_init(groove_t *g, float sr)
{
    memset(g, 0, sizeof(*g));
    g->sr = sr;
    g->k_att = 1.0f / (GR_ATTACK_SEC * sr);
    g->k_dec = expf(-1.0f / (GR_DECAY_SEC * sr));
    g->k_rel = expf(-1.0f / (GR_REL_SEC * sr));
    groove_set_bpm(g, 120.0f);
    groove_reset(g);
}

float groove_process(groove_t *g)
{
    /* the step clock: a double, so a loop of 32 steps is exact whenever step_len x 32 is */
    g->t += 1.0;
    if (g->t >= (double)g->step_len) {
        g->t -= (double)g->step_len;
        g->step = (g->step + 1) % GROOVE_STEPS;
        const int n = k_notes[g->step];
        if (n > 0) {
            g->inc = 440.0f * exp2f(((float)n - 69.0f) * (1.0f / 12.0f)) / g->sr;
            g->vel = (g->step % 4 == 0) ? 1.0f : (n >= 36 ? 0.85f : 0.7f);    /* downbeats and octaves lead */
            g->ph1 = g->ph2 = 0.0f;                                           /* every hit starts the same: the loop repeats exactly */
            g->env = 0.0f;
            g->lp = 0.0f;                                                     /* the pluck filter too: nothing of the last hit rides into this one */
            g->gate = 1;
            g->att = 1;
        }
    }
    if (g->gate && g->t >= (double)(GR_GATE * g->step_len)) g->gate = 0;

    /* VCA: a linear 1.5 ms attack to 1, then an exponential decay while held and a fast release after */
    if (g->gate && g->att) {
        g->env += g->k_att;
        if (g->env >= 1.0f) { g->env = 1.0f; g->att = 0; }
    } else if (g->gate) {
        g->env *= g->k_dec;
    } else {
        g->env = flush(g->env * g->k_rel);
    }
    if (g->env <= 0.0f) return 0.0f;

    /* two saws, a few cents apart */
    const float d1 = g->inc * (1.0f + GR_DETUNE), d2 = g->inc * (1.0f - GR_DETUNE);
    const float s1 = 2.0f * g->ph1 - 1.0f - polyblep(g->ph1, d1);
    const float s2 = 2.0f * g->ph2 - 1.0f - polyblep(g->ph2, d2);
    g->ph1 += d1; if (g->ph1 >= 1.0f) g->ph1 -= 1.0f;
    g->ph2 += d2; if (g->ph2 >= 1.0f) g->ph2 -= 1.0f;
    float x = 0.5f * (s1 + s2);

    /* the pluck: very bright on the hit, still open as it decays (cutoff 3.5 kHz + up to 10 kHz with the envelope) */
    const float fc = 3500.0f + 10000.0f * g->env * g->env;
    const float a = fminf(0.95f, 2.0f * PI_F * fc / g->sr);
    g->lp = flush(g->lp + (x - g->lp) * a);
    return g->lp * g->env * g->vel * GR_AMP;
}

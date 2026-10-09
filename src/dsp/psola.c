#include "psola.h"

#include <math.h>
#include <string.h>

#define HANN_N     512
#define MAX_SPAN   3000.0f      /* input samples one grain may cover (delay line is 4096) */
#define LAT_SLEW   0.002f       /* latency slews DOWN only; up it jumps (see below) */
#define MIN_GRAIN_SEC 0.005f    /* no grain shorter than 5 ms: wide, smooth overlaps */
#define CUT_MIN_SEC   0.005f    /* a phase reset fades old grains over at least 5 ms */
#define HEAD_MARGIN   2.0       /* samples kept between a grain's reads and the newest sample */

static float g_hann[HANN_N + 1];
static int   g_hann_ready;

static void hann_init(void)
{
    if (g_hann_ready) return;
    for (int i = 0; i <= HANN_N; i++)
        g_hann[i] = 0.5f - 0.5f * cosf(2.0f * 3.14159265358979f * (float)i / HANN_N);
    g_hann_ready = 1;
}

static inline float hann(float x)            /* x in [0, 1) */
{
    float f = x * HANN_N;
    int i = (int)f;
    if (i < 0) return 0.0f;
    if (i >= HANN_N) return 0.0f;
    float fr = f - (float)i;
    return g_hann[i] + fr * (g_hann[i + 1] - g_hann[i]);
}

void psola_init(psola_voice *v)
{
    hann_init();
    memset(v, 0, sizeof(*v));
}

static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

float psola_process(psola_voice *v, const cher_t *c, float t0, float pitch_ratio,
                    float formant_ratio, float min_lat)
{
    const double now = (double)c->n_written;            /* the newest sample is now - 1 */
    const float p = clampf(pitch_ratio, 0.5f, 2.0f);
    const float r = clampf(formant_ratio, 0.5f, 2.0f);
    const float T0 = t0 > 1.0f ? t0 : PSOLA_UNVOICED_T0;
    const float Tout = T0 / p;

    /* Grain: two input periods (standard TD-PSOLA, for every ratio: longer
     * grains would carry the old pitch with them when shifting down). */
    float len = 2.0f * T0 * (1.0f - 0.4f * clampf(v->tight, 0.0f, 1.0f));
    if (len < MIN_GRAIN_SEC * c->sr) len = MIN_GRAIN_SEC * c->sr;
    float span = len * r;
    if (span > MAX_SPAN) { len *= MAX_SPAN / span; span = MAX_SPAN; }

    /* Latency. A grain is read in real time from its start, at rate r, centred
     * (len/2 later) on its mark; that reading never runs ahead of the input as
     * long as lat >= |span - len| / 2, plus half a period because the mark is
     * rounded to the nearest grid point below. ~2.3 ms at 220 Hz, r = 1. */
    float lat_target = fabsf(span - len) * 0.5f + 0.5f * T0 + 2.0f;
    if (lat_target < min_lat) lat_target = min_lat;
    /* Up at once, down slowly: a latency lagging below its target (the pitch
     * just fell, so grains got longer) let grains read samples not written
     * yet - the ring's ~90 ms-old contents, i.e. a click. Measured before:
     * up to 195 samples ahead on 400 -> 120 Hz jumps. */
    if (!v->started) { v->lat = lat_target; v->started = 1; v->until_next = 0.0f; }
    else if (lat_target > v->lat) v->lat = lat_target;
    else v->lat += (lat_target - v->lat) * LAT_SLEW;

    /* Start a new grain every output period. Hann windows of length L spaced
     * Tout apart sum to L / (2 Tout); scale so the overlap-add is unity. */
    v->until_next -= 1.0f;
    if (v->until_next <= 0.0f) {
        /* The grain's centre is heard len/2 from now; for a latency of lat it
         * should be centred on input (now + len/2 - lat). Take the epoch grid
         * point nearest to that: grid steps are whole input periods, which is
         * what keeps successive grains in phase.
         *
         * (Snapping marks to waveform peaks was tried and made things worse:
         * on formant-dominated voices the "peak" hops between partials from
         * one period to the next. The MPM period is accurate enough that the
         * grid alone stays coherent; measured >= 0.999 autocorrelation at the
         * target period for ratios 0.5..2.) */
        const double ideal = now + 0.5 * len - (double)v->lat;
        const float kick = t0 > 1.0f ? v->kick : 0.0f;
        v->kick = 0.0f;
        if (!v->have_mark || t0 <= 1.0f || kick > 0.0f || fabs(ideal - v->mark) > 4.0 * T0) {
            v->mark = ideal;                                /* (re)start the grid */
        } else {
            double n = floor((ideal - v->mark) / T0 + 0.5);
            v->mark += n * T0;
        }
        v->have_mark = 1;

        /* Read-head protection: every read of this grain, start + t*r at output
         * time t, must stay HEAD_MARGIN behind the newest sample (now - 1 + t).
         * Worst at t = 0 for r <= 1, at t = len for r > 1. If the mark is too
         * late, step it back by whole periods (keeps the grid in phase). */
        {
            const double limit = now - 1.0 - HEAD_MARGIN - (r > 1.0f ? (double)(len * (r - 1.0f)) : 0.0);
            double start = v->mark - span * 0.5;
            if (start > limit) {
                if (t0 > 1.0f) v->mark -= ceil((start - limit) / T0) * T0;
                else v->mark = limit + span * 0.5;
            }
        }
        if (kick > 0.0f) {
            /* Phase reset: sounding grains fade out early, over a raised cosine
             * of at least 5 ms (it was half a period, ~1 ms: a click), and the
             * new grain starts at the bottom of its window as always. */
            float cut = T0 * (1.0f - 0.5f * kick);
            if (cut < CUT_MIN_SEC * c->sr) cut = CUT_MIN_SEC * c->sr;
            for (int k = 0; k < PSOLA_MAX_GRAINS; k++) {
                psola_grain *g = &v->g[k];
                if (!g->active || (g->cut_at > 0.0f && g->cut_at <= g->t + cut)) continue;
                g->cut_at = g->t + cut;
                g->cut_len = cut;
            }
            v->resets++;
        }

        for (int k = 0; k < PSOLA_MAX_GRAINS; k++) {
            psola_grain *g = &v->g[k];
            if (g->active) continue;
            g->start = v->mark - span * 0.5;
            g->rate = r;
            g->len = len;
            g->t = 0.0f;
            g->cut_at = 0.0f;
            /* Pulse-centred grains add in power: p grains per period at
             * gain g give p * g^2, so g = sqrt(Tout / T0) keeps the level. */
            g->gain = sqrtf(2.0f * Tout / len);
            g->active = 1;
            break;
        }
        v->until_next += Tout;
    }

    float out = 0.0f;
    for (int k = 0; k < PSOLA_MAX_GRAINS; k++) {
        psola_grain *g = &v->g[k];
        if (!g->active) continue;
        float w = hann(g->t / g->len);
        if (g->cut_at > 0.0f) {
            float f = clampf((g->cut_at - g->t) / g->cut_len, 0.0f, 1.0f);
            w *= f * f * (3.0f - 2.0f * f);                  /* smooth (C1) taper, no corner */
        }
        out += w * g->gain * cher_read_abs(c, g->start + (double)(g->t * g->rate));
        g->t += 1.0f;
        if (g->t >= g->len || (g->cut_at > 0.0f && g->t >= g->cut_at)) g->active = 0;
    }
    return out;
}

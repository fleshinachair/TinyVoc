#include "tour.h"

#include <math.h>

typedef struct { float bar, v; } tour_kf;

/* Vowel path: keyframes in bars, values are vowel indices. The engine glides straight from one
 * vowel to the next (not through the ones between on the A E I O U line). */
static const tour_kf k_vowel[] = {
    { 0.00f, 0 }, { 1.75f, 0 },                          /* A, the neutral baseline */
    { 2.00f, 4 },                                        /* u */
    { 2.50f, 3 },                                        /* o */
    { 3.00f, 0 },                                        /* a */
    { 3.50f, 1 },                                        /* e */
    { 4.00f, 2 },                                        /* i */
    { 4.25f, 0 }, { 8.00f, 0 },                          /* back to a for the drive, the chest and the loop */
};
static const tour_kf k_peak[] = {
    { 0.00f, 0.15f }, { 1.75f, 0.15f },                  /* low resonance */
    { 2.00f, 0.50f }, { 4.00f, 0.50f },                  /* the vowels talk */
    { 5.75f, 0.95f }, { 6.00f, 0.95f },                  /* squelch */
    { 6.50f, 0.50f }, { 7.50f, 0.50f },
    { 8.00f, 0.15f },
};
static const tour_kf k_mix[] = {
    { 0.00f, 0.30f }, { 1.75f, 0.30f },                  /* mostly the raw synth */
    { 2.00f, 0.70f }, { 4.00f, 0.70f },                  /* all formants, no drive */
    { 5.50f, 1.00f }, { 6.00f, 1.00f },                  /* into saturation */
    { 6.50f, 0.75f }, { 7.50f, 0.75f },
    { 8.00f, 0.30f },
};
static const tour_kf k_tract[] = {
    { 0.00f, 0.50f }, { 6.00f, 0.50f },
    { 6.70f, 0.05f },                                    /* deep chest */
    { 7.40f, 0.95f },                                    /* soprano */
    { 8.00f, 0.50f },
};
static const tour_kf k_choir[] = {
    { 0.00f, 0.00f }, { 6.00f, 0.00f },
    { 7.00f, 1.00f }, { 7.50f, 1.00f },                  /* wide open */
    { 8.00f, 0.00f },
};

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

static inline float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

/* The index of the keyframe segment [i-1, i] that holds `bar` (1 .. n - 1), or 0 outside the table. */
static int segment(const tour_kf *k, int n, float bar)
{
    if (bar <= k[0].bar) return 0;
    for (int i = 1; i < n; i++) if (bar < k[i].bar) return i;
    return 0;
}

static float kf_eval(const tour_kf *k, int n, float bar)
{
    const int i = segment(k, n, bar);
    if (i == 0) return bar <= k[0].bar ? k[0].v : k[n - 1].v;
    const float t = (bar - k[i - 1].bar) / (k[i].bar - k[i - 1].bar);
    return k[i - 1].v + (k[i].v - k[i - 1].v) * smooth(t);
}

void tour_eval(float bar, tour_t *o)
{
    if (!(bar >= 0.0f)) bar = 0.0f;
    bar = fmodf(bar, (float)TOUR_BARS);

    const int i = segment(k_vowel, N(k_vowel), bar);
    if (i == 0) {
        o->v0 = o->v1 = (int)k_vowel[bar <= k_vowel[0].bar ? 0 : N(k_vowel) - 1].v;
        o->vt = 0.0f;
    } else {
        o->v0 = (int)k_vowel[i - 1].v;
        o->v1 = (int)k_vowel[i].v;
        o->vt = o->v0 == o->v1 ? 0.0f : smooth((bar - k_vowel[i - 1].bar) / (k_vowel[i].bar - k_vowel[i - 1].bar));
    }
    o->tract = kf_eval(k_tract, N(k_tract), bar);
    o->peak = kf_eval(k_peak, N(k_peak), bar);
    o->choir = kf_eval(k_choir, N(k_choir), bar);
    o->mix = kf_eval(k_mix, N(k_mix), bar);
}

#include "verb.h"

#include <math.h>
#include <string.h>

#define DAMP_HZ   5500.0f       /* in-loop lowpass: the top end decays faster, like a plate */
#define AP_G      0.6f
#define RET_MAX   0.45f         /* return level at amount 1 */

static inline float flush(float x) { return fabsf(x) < 1e-15f ? 0.0f : x; }

void verb_init(verb_t *v, float sample_rate)
{
    memset(v, 0, sizeof(*v));
    v->sr = sample_rate > 0.0f ? sample_rate : 44100.0f;
    /* Mutually prime line lengths (44-70 ms at 44.1 kHz), scaled to the rate. */
    static const int base_len[VERB_LINES] = { 1931, 2213, 2647, 3089 };
    static const int base_ap[2] = { 223, 359 };
    const float k = v->sr / 44100.0f;
    for (int i = 0; i < VERB_LINES; i++) {
        int n = (int)(base_len[i] * k);
        v->len[i] = n < VERB_LINE_N - 1 ? n : VERB_LINE_N - 1;
    }
    for (int i = 0; i < 2; i++) {
        int n = (int)(base_ap[i] * k);
        v->ap_len[i] = n < VERB_AP_N - 1 ? n : VERB_AP_N - 1;
    }
    v->damp_a = 1.0f - expf(-2.0f * 3.14159265f * DAMP_HZ / v->sr);
    v->k_ret = 1.0f - expf(-1.0f / (0.05f * v->sr));
    v->amount = -1.0f;
    verb_set(v, 0.0f);
}

void verb_set(verb_t *v, float amount)
{
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    /* Return in equal-ish loudness steps; at 0 it fades out and the tail rings down. */
    v->ret_goal = RET_MAX * sqrtf(amount);
    if (amount == v->amount) return;
    v->amount = amount;
    /* Gain per line so every line falls 60 dB in rt60 seconds. */
    const float rt60 = 0.8f + 2.2f * amount;
    for (int i = 0; i < VERB_LINES; i++)
        v->fb[i] = powf(10.0f, -3.0f * (float)v->len[i] / (rt60 * v->sr));
}

void verb_process(verb_t *v, float in, float *l, float *r)
{
    /* Input diffusion: two Schroeder allpasses smear the onset into a wash. */
    float x = in;
    for (int k = 0; k < 2; k++) {
        int ri = (v->ap_w - v->ap_len[k]) & (VERB_AP_N - 1);
        float d = v->ap[k][ri];
        float y = d - AP_G * x;
        v->ap[k][v->ap_w] = flush(x + AP_G * y);
        x = y;
    }
    v->ap_w = (v->ap_w + 1) & (VERB_AP_N - 1);

    /* FDN: read, damp, Hadamard-mix (energy preserving: x0.5), feed back. */
    float o[VERB_LINES];
    for (int i = 0; i < VERB_LINES; i++) {
        float s = v->line[i][(v->w - v->len[i]) & (VERB_LINE_N - 1)];
        v->damp[i] = flush(v->damp[i] + (s - v->damp[i]) * v->damp_a);
        o[i] = v->damp[i] * v->fb[i];
    }
    const float h0 = 0.5f * (o[0] + o[1] + o[2] + o[3]);
    const float h1 = 0.5f * (o[0] - o[1] + o[2] - o[3]);
    const float h2 = 0.5f * (o[0] + o[1] - o[2] - o[3]);
    const float h3 = 0.5f * (o[0] - o[1] - o[2] + o[3]);
    v->line[0][v->w] = flush(h0 + x);
    v->line[1][v->w] = flush(h1 + x);
    v->line[2][v->w] = flush(h2 - x);
    v->line[3][v->w] = flush(h3 - x);
    v->w = (v->w + 1) & (VERB_LINE_N - 1);

    v->ret = flush(v->ret + (v->ret_goal - v->ret) * v->k_ret);
    if (v->ret > 0.0f) {
        /* Different line pairs per side: a decorrelated, wide return. */
        *l += v->ret * (o[0] + o[2]);
        *r += v->ret * (o[1] - o[3]);
    }
}

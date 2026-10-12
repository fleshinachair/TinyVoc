#include "vfx.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f

/* The wet leveller: holds the bank's output at WET_RMS x the input's RMS (a
 * 150 ms mean square, gain smoothed over 50 ms, 0.25 .. 5 x, held while the
 * input is silent). What a band-pass bank gives back depends on how well a
 * voice lines up with the formants (a 10 dB spread over vowels, Peak and Tract),
 * so no fixed makeup could keep the knobs level-neutral. */
#define VFX_WET_RMS  1.0f
#define VFX_LEV_MIN  0.25f
#define VFX_LEV_MAX  5.0f
#define VFX_AIR_HZ   3500.0f
#define VFX_NOISE    0.9f

static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline float flush(float x) { return fabsf(x) < 1e-15f ? 0.0f : x; }

/* Vowels A E I O U (adult male F1..F4 in Hz, and the weights that give them
 * the usual falling formant levels). */
static const float k_vowel_f[5][VFX_FORMANTS] = {
    { 730.0f, 1090.0f, 2440.0f, 3400.0f },    /* A  (ah) */
    { 530.0f, 1840.0f, 2480.0f, 3500.0f },    /* E  (eh) */
    { 270.0f, 2290.0f, 3010.0f, 3500.0f },    /* I  (ee) */
    { 570.0f,  840.0f, 2410.0f, 3400.0f },    /* O  (oh) */
    { 300.0f,  870.0f, 2240.0f, 3400.0f },    /* U  (oo) */
};
static const float k_vowel_w[5][VFX_FORMANTS] = {
    { 1.00f, 0.60f, 0.28f, 0.12f },
    { 1.00f, 0.50f, 0.28f, 0.12f },
    { 1.00f, 0.42f, 0.24f, 0.10f },
    { 1.00f, 0.70f, 0.26f, 0.12f },
    { 1.00f, 0.55f, 0.20f, 0.10f },
};
/* Q of each formant at Peak 0.5 (a vowel's higher formants are broader in absolute terms but sharper in Q). */
static const float k_q[VFX_FORMANTS] = { 6.0f, 9.0f, 12.0f, 14.0f };
/* Choir: how each formant is pulled apart between the sides (L up, R down). */
static const float k_choir_s[VFX_FORMANTS] = { 1.0f, 0.6f, -0.8f, 0.5f };

/* Gain that keeps tanh(k x) at the RMS of x for a 0.3-peak sine (as in modes.c). */
static float sat_norm(float k)
{
    double acc = 0.0;
    for (int i = 0; i < 32; i++) {
        const double y = tanh(k * 0.3 * sin((i + 0.5) * (3.14159265358979 / 32.0)));
        acc += y * y;
    }
    const double rms = sqrt(acc / 32.0);
    return rms > 1e-9 ? (float)(0.3 * 0.70710678 / rms) : 1.0f;
}

static void svf_design(vfx_svf *f, float hz, float q, float sr)
{
    const float g = tanf(PI_F * clampf(hz, 40.0f, 0.45f * sr) / sr);
    f->k = 1.0f / q;
    f->a1 = 1.0f / (1.0f + g * (g + f->k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
}

/* Band-pass, unity at the centre (k bp). */
static inline float svf_bp(vfx_svf *f, float x)
{
    const float v3 = x - f->s2;
    const float v1 = f->a1 * f->s1 + f->a2 * v3;
    const float v2 = f->s2 + f->a2 * f->s1 + f->a3 * v3;
    f->s1 = flush(2.0f * v1 - f->s1);
    f->s2 = flush(2.0f * v2 - f->s2);
    return f->k * v1;
}

static inline float rnd(vfx_t *v)                           /* white, -1..1 */
{
    v->rng = v->rng * 1664525u + 1013904223u;
    return (float)(int)(v->rng >> 8) * (1.0f / 8388608.0f) - 1.0f;
}

void vfx_init(vfx_t *v, float sr)
{
    memset(v, 0, sizeof(*v));
    v->sr = sr;
    v->rng = 0x7f3a91u;
    v->hp_a = expf(-2.0f * PI_F * VFX_AIR_HZ / sr);
    v->k_att_fast = 1.0f - expf(-1.0f / (0.001f * sr));
    v->k_rel = 1.0f - expf(-1.0f / (0.040f * sr));
    v->k_att_slow = v->k_att_fast;
    v->k_f = 1.0f - expf(-(128.0f / sr) / 0.02f);            /* ~20 ms at 128-sample blocks */
    v->drive_k = 1.0f;
    v->drive_n = sat_norm(1.0f);
    v->lev = 3.0f;
    v->k_ms = 1.0f - expf(-1.0f / (0.15f * sr));
    v->k_lev = 1.0f - expf(-1.0f / (0.05f * sr));
    for (int k = 0; k < VFX_FORMANTS; k++) { v->f_hz[k] = k_vowel_f[0][k]; v->f_w[k] = k_vowel_w[0][k]; }
    for (int c = 0; c < 2; c++)
        for (int k = 0; k < VFX_FORMANTS; k++) svf_design(&v->bank[c][k], v->f_hz[k], k_q[k], sr);
    verb_init(&v->tail, sr);
}

void vfx_set(vfx_t *v, float vowel, float tract, float peak, float choir,
             float breath, float attack, float release, float drive)
{
    v->vowel = clampf(vowel, 0.0f, 1.0f);
    v->tract = clampf(tract, 0.0f, 1.0f);
    v->peak = clampf(peak, 0.0f, 1.0f);
    v->choir = clampf(choir, 0.0f, 1.0f);
    v->breath = clampf(breath, 0.0f, 1.0f);
    v->attack = clampf(attack, 0.0f, 1.0f);
    v->release = clampf(release, 0.0f, 1.0f);

    /* the slow envelope's attack: 0.5 ms .. 120 ms (the fast one stays at 1 ms) */
    const float t_att = 0.0005f + 0.12f * v->attack * v->attack;
    v->k_att_slow = 1.0f - expf(-1.0f / (t_att * v->sr));

    const float dk = clampf(drive, 0.0f, 1.0f);
    const float lo = fminf(dk / VFX_MIX_KNEE, 1.0f);          /* the wet/dry segment: a touch of warmth ... */
    const float hi = fmaxf(dk - VFX_MIX_KNEE, 0.0f) / (1.0f - VFX_MIX_KNEE);   /* ... then the drive segment: x1.3 up to x5 */
    const float k = 1.0f + 0.3f * lo * lo + 3.7f * hi * sqrtf(hi);
    if (k != v->drive_k) { v->drive_k = k; v->drive_n = sat_norm(k); }

    /* Vowel: a position on the A E I O U line; centres interpolate in log
     * frequency, weights linearly. */
    const float pos = v->vowel * 4.0f;
    int i0 = (int)pos;
    if (i0 > 3) i0 = 3;
    const float fr = pos - (float)i0;
    const float scale = exp2f((v->tract - 0.5f) * 1.2f);      /* Tract: +-0.6 octave */
    const float qm = 0.4f * powf(6.25f, v->peak);             /* Peak: x0.4 .. x2.5, x1 at 0.5 */
    const float det = 0.07f * v->choir;
    for (int k2 = 0; k2 < VFX_FORMANTS; k2++) {
        const float f_goal = scale * k_vowel_f[i0][k2] * powf(k_vowel_f[i0 + 1][k2] / k_vowel_f[i0][k2], fr);
        const float w_goal = k_vowel_w[i0][k2] + (k_vowel_w[i0 + 1][k2] - k_vowel_w[i0][k2]) * fr;
        v->f_hz[k2] += (f_goal - v->f_hz[k2]) * v->k_f;       /* the sweep never steps */
        v->f_w[k2] += (w_goal - v->f_w[k2]) * v->k_f;
        const float q = clampf(k_q[k2] * qm, 1.0f, 40.0f);
        svf_design(&v->bank[0][k2], v->f_hz[k2] * (1.0f + det * k_choir_s[k2]), q, v->sr);
        svf_design(&v->bank[1][k2], v->f_hz[k2] * (1.0f - det * k_choir_s[k2]), q, v->sr);
    }
    verb_set(&v->tail, v->release);
}

void vfx_process(vfx_t *v, float in, float *out_l, float *out_r)
{
    /* Envelopes: a fast follower (1 ms up / 40 ms down) and a slow one that
     * chases it upward over the Attack time and drops with it at once. Their
     * ratio is exactly 1 on a held note and below 1 while the slow one is
     * still catching up, so the wet bank swells in over the Attack time
     * without costing a held note any level. */
    const float a = fabsf(in);
    v->env_fast += (a - v->env_fast) * (a > v->env_fast ? v->k_att_fast : v->k_rel);
    v->env_fast = flush(v->env_fast);
    if (v->env_fast > v->env_slow) v->env_slow += (v->env_fast - v->env_slow) * v->k_att_slow;
    else v->env_slow = v->env_fast;
    v->env_slow = flush(v->env_slow);
    v->env_breath += (a - v->env_breath) * v->k_rel;          /* the breath follows the voice, so silence stays silent */
    v->env_breath = flush(v->env_breath);
    const float swell = v->env_fast > 1e-6f ? fminf(1.0f, v->env_slow / v->env_fast) : 1.0f;

    /* Breath: noise follows the voice's level and is shaped by the formants
     * with it (a whispered vowel); the voice's own air above 3.5 kHz is bled
     * past the bank so consonants and sibilance stay. */
    const float noise = v->breath * VFX_NOISE * v->env_breath * rnd(v);
    const float ex = in + noise;
    float hp = in;
    for (int s = 0; s < 2; s++) {
        const float y = v->hp_a * (v->hp_z[s][0] + hp - v->hp_z[s][1]);
        v->hp_z[s][1] = hp;
        v->hp_z[s][0] = flush(y);
        hp = y;
    }
    const float air = v->breath * 1.4f * hp;

    float y[2];
    for (int c = 0; c < 2; c++) {
        y[c] = 0.0f;
        for (int k = 0; k < VFX_FORMANTS; k++) y[c] += v->f_w[k] * svf_bp(&v->bank[c][k], ex);
    }
    v->ms_in = flush(v->ms_in + (in * in - v->ms_in) * v->k_ms);
    v->ms_out = flush(v->ms_out + (0.5f * (y[0] * y[0] + y[1] * y[1]) - v->ms_out) * v->k_ms);
    if (v->ms_in > 1e-9f && v->ms_out > 1e-12f)                 /* silence holds the gain */
        v->lev += (clampf(VFX_WET_RMS * sqrtf(v->ms_in / v->ms_out), VFX_LEV_MIN, VFX_LEV_MAX) - v->lev) * v->k_lev;
    float side[2];
    for (int c = 0; c < 2; c++)
        side[c] = tanhf(v->drive_k * y[c] * v->lev * swell) * v->drive_n + air;
    float l = side[0], r = side[1];
    verb_process(&v->tail, 0.5f * (side[0] + side[1]), &l, &r);
    *out_l = l;
    *out_r = r;
}

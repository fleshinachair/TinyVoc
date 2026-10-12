#include "demo.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f

/* MIDI pitch per syllable: A3 +30 ct, C4 -25 ct, E4 +20 ct, D4 -35 ct. */
static const float k_pitch[DEMO_SYLLABLES] = { 57.30f, 59.75f, 64.20f, 61.65f };

/* Formants F1, F2, F3 (Hz) per vowel: ah, ee, oh, oo. */
static const float k_formant[DEMO_SYLLABLES][3] = {
    { 730.0f, 1090.0f, 2440.0f },
    { 270.0f, 2290.0f, 3010.0f },
    { 570.0f,  840.0f, 2410.0f },
    { 300.0f,  870.0f, 2240.0f },
};
static const float k_fgain[3] = { 1.0f, 0.55f, 0.25f };
static const float k_fq[3] = { 8.0f, 12.0f, 16.0f };

static const int k_chords[DEMO_CHORDS][4] = {
    { 57, 60, 64, 69 },   /* Am */
    { 53, 57, 60, 65 },   /* F  */
    { 48, 55, 60, 64 },   /* C  */
    { 55, 59, 62, 67 },   /* G  */
};

/* Steady mean |voice| before the syllable envelope: the level Auto steers a
 * real voice to, so the demo sounds the same with Auto On or Off (Auto applies
 * ~0 dB to it). Auto's follower rides the peaks, so that is ~6 dB under its
 * nominal 0.25 peak-equivalent target (x0.51, -5.8 dB, measured as Auto's mean
 * gain over a sung phrase). */
#define DEMO_LEVEL (0.51f * 0.25f / 1.5707963f / 0.9f)

void demo_chord(int bar, int out[4])
{
    int b = ((bar % DEMO_CHORDS) + DEMO_CHORDS) % DEMO_CHORDS;
    for (int i = 0; i < 4; i++) out[i] = k_chords[b][i];
}

void demo_init(demo_t *dm, float sr)
{
    memset(dm, 0, sizeof(*dm));
    dm->sr = sr;
    dm->rng = 0xD3A0u;
    dm->norm_env = DEMO_LEVEL;
    dm->k_norm = 1.0f - expf(-1.0f / (0.02f * sr));
}

void demo_reset(demo_t *dm)
{
    float sr = dm->sr;
    demo_init(dm, sr);
}

static inline float smooth(float x) { x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); return x * x * (3.0f - 2.0f * x); }

/* Pitch with a 60 ms glide into each syllable and 5 Hz vibrato after 200 ms. */
float demo_pitch_at(float t)
{
    const float syl = DEMO_PHRASE_SEC / DEMO_SYLLABLES;
    int s = (int)(t / syl) % DEMO_SYLLABLES;
    float ts = t - (float)s * syl;
    int prev = (s + DEMO_SYLLABLES - 1) % DEMO_SYLLABLES;
    float p = k_pitch[prev] + (k_pitch[s] - k_pitch[prev]) * smooth(ts / 0.06f);
    float vib = smooth((ts - 0.2f) / 0.15f) * 0.2f * sinf(2.0f * PI_F * 5.0f * ts);
    return p + vib;
}

static inline float polyblep(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

/* TPT state-variable bandpass, unity at the centre. */
static float resonate(float *s, float x, float f, float q, float sr)
{
    float g = tanf(PI_F * f / sr), k = 1.0f / q;
    float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    float v3 = x - s[1];
    float v1 = a1 * s[0] + a2 * v3;
    float v2 = s[1] + a2 * s[0] + a3 * v3;
    s[0] = 2.0f * v1 - s[0];
    s[1] = 2.0f * v2 - s[1];
    return k * v1;
}

float demo_process(demo_t *dm)
{
    const float syl = DEMO_PHRASE_SEC / DEMO_SYLLABLES;
    const float t = (float)dm->t;
    const int s = (int)(t / syl) % DEMO_SYLLABLES;
    const float ts = t - (float)s * syl;

    /* glottal buzz */
    float f0 = 440.0f * exp2f((demo_pitch_at(t) - 69.0f) / 12.0f);
    float dt = f0 / dm->sr;
    float saw = 2.0f * dm->ph - 1.0f - polyblep(dm->ph, dt);
    dm->ph += dt;
    if (dm->ph >= 1.0f) dm->ph -= 1.0f;
    dm->tilt += (saw - dm->tilt) * (1.0f - expf(-2.0f * PI_F * 2500.0f / dm->sr));

    dm->rng = dm->rng * 1664525u + 1013904223u;
    float noise = (float)(int)(dm->rng >> 8) * (1.0f / 8388608.0f) - 1.0f;
    float src = dm->tilt + 0.03f * noise;

    /* vowel: glide the formants from the previous vowel over 80 ms */
    int prev = (s + DEMO_SYLLABLES - 1) % DEMO_SYLLABLES;
    float mix = smooth(ts / 0.08f);
    float v = 0.0f;
    for (int k = 0; k < 3; k++) {
        float f = k_formant[prev][k] + (k_formant[s][k] - k_formant[prev][k]) * mix;
        v += k_fgain[k] * resonate(dm->res[k], src, f, k_fq[k], dm->sr);
    }

    /* Level: held at DEMO_LEVEL by a ~20 ms normaliser, so the narrow
     * formants catching and missing harmonics don't swing it. */
    dm->norm_env += (fabsf(v) - dm->norm_env) * dm->k_norm;
    float g = DEMO_LEVEL / (dm->norm_env + 1e-4f);
    if (g > 8.0f) g = 8.0f;
    if (g < 0.25f) g = 0.25f;
    v *= g;

    /* syllable envelope: 25 ms in, ease down over the last 70 ms */
    float env = smooth(ts / 0.025f) * (1.0f - 0.7f * smooth((ts - (syl - 0.07f)) / 0.07f));
    /* the last syllable is short and the phrase ends in a breath (~0.2 s of
     * silence), so the voice gate closes once a phrase as it does on a real
     * voice: Walkie's squelch tail, Robot's silent pauses */
    if (s == DEMO_SYLLABLES - 1) env *= 1.0f - smooth((ts - 0.22f) / 0.06f);

    /* "s" onset on syllables 2 and 4: 60 ms of high-passed noise before the vowel */
    float hiss = 0.0f;
    if ((s == 1 || s == 3) && ts < 0.06f) {
        float h = noise - 2.0f * dm->pn + dm->pn2;
        hiss = 0.12f * h * (1.0f - ts / 0.06f);
        env *= smooth((ts - 0.04f) / 0.03f);
    }
    dm->pn2 = dm->pn;
    dm->pn = noise;

    dm->t += 1.0 / dm->sr;
    if (dm->t >= DEMO_PHRASE_SEC) dm->t -= DEMO_PHRASE_SEC;
    return 0.9f * v * env + hiss;
}

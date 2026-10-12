#include "groove.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f

#define GR_BASS_AMP    0.15f     /* peaks: together about what Auto's input stage steers a voice to */
#define GR_CHORD_AMP   0.11f
#define GR_DETUNE      0.0035f   /* +-6 cents between a note's two saws */
#define GR_ATTACK_SEC  0.0012f
#define GR_B_GATE      0.62f     /* bass: held for this share of its step */
#define GR_B_DECAY_SEC 0.14f
#define GR_B_REL_SEC   0.008f
#define GR_C_GATE      0.30f     /* stabs: staccato */
#define GR_C_DECAY_SEC 0.07f
#define GR_C_REL_SEC   0.006f
#define R              (-99)     /* a rest in a bass pattern */

/* Bars: the bass root, which bass and stab pattern, and which chord. */
static const struct { unsigned char root, bass, stab, chord; } k_bars[GROOVE_BARS] = {
    { 38, 0, 0, 0 },    /* Dm7 */
    { 38, 1, 1, 0 },    /* Dm7 */
    { 46, 0, 0, 1 },    /* Bbmaj7 */
    { 48, 1, 1, 2 },    /* Cadd9 */
    { 38, 0, 0, 0 },    /* Dm7 */
    { 38, 1, 1, 0 },    /* Dm7 */
    { 43, 0, 0, 3 },    /* Gm7 */
    { 45, 2, 2, 4 },    /* A7: the turnaround */
};

/* Bass: semitones above the bar's root on each of the 16 steps. */
static const signed char k_bass[3][16] = {
    {  0, R, R,  0,   R, R, 12, R,    0, R,  0, R,    R, 12, R,  7 },    /* states the riff */
    {  0, R, R,  0,   R, 10, R, 12,   0, R, 12, R,    7,  R, 5, R },     /* answers it */
    {  0, R, R,  0,   R, R,  7, R,    0, R, 12, R,   10,  R, 12, R },    /* turns the loop around */
};

/* Stabs: a bit per step (bit 0 = the first sixteenth), all on the off-beats. */
static const unsigned short k_stab[3] = {
    (1u << 2) | (1u << 5) | (1u << 9) | (1u << 11) | (1u << 14),
    (1u << 2) | (1u << 6) | (1u << 9) | (1u << 13),
    (1u << 2) | (1u << 5) | (1u << 9) | (1u << 11) | (1u << 13) | (1u << 14) | (1u << 15),   /* a build into the loop */
};

static const unsigned char k_chords[5][GROOVE_CHORD_NOTES] = {
    { 62, 65, 69, 72 },    /* Dm7:     D F A C */
    { 58, 62, 65, 69 },    /* Bbmaj7:  Bb D F A */
    { 60, 64, 67, 74 },    /* Cadd9:   C E G D */
    { 58, 62, 65, 67 },    /* Gm7:     Bb D F G */
    { 61, 64, 67, 69 },    /* A7:      C# E G A */
};

static inline float flush(float x) { return fabsf(x) < 1e-15f ? 0.0f : x; }

int groove_bass_note(int step)
{
    if (step < 0 || step >= GROOVE_STEPS) return 0;
    const int off = k_bass[k_bars[step / 16].bass][step % 16];
    return off == R ? 0 : (int)k_bars[step / 16].root + off;
}

int groove_chord(int step, int notes[GROOVE_CHORD_NOTES])
{
    if (step < 0 || step >= GROOVE_STEPS) return 0;
    if (!((k_stab[k_bars[step / 16].stab] >> (step % 16)) & 1u)) return 0;
    for (int i = 0; i < GROOVE_CHORD_NOTES; i++) notes[i] = k_chords[k_bars[step / 16].chord][i];
    return GROOVE_CHORD_NOTES;
}

static inline float polyblep(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

/* One band-limited saw, advanced. */
static inline float saw(float *ph, float inc)
{
    const float s = 2.0f * *ph - 1.0f - polyblep(*ph, inc);
    *ph += inc;
    if (*ph >= 1.0f) *ph -= 1.0f;
    return s;
}

static inline float note_inc(int n, float sr) { return 440.0f * exp2f(((float)n - 69.0f) * (1.0f / 12.0f)) / sr; }

void groove_set_bpm(groove_t *g, float bpm)
{
    if (!(bpm >= 20.0f && bpm <= 400.0f)) bpm = 120.0f;
    g->step_len = g->sr * 60.0f / bpm * 0.25f;
}

void groove_reset(groove_t *g)
{
    g->step = GROOVE_STEPS - 1;                 /* the first sample starts step 0, with the step clock at exactly 0 */
    g->t = (double)g->step_len - 1.0;
    g->b_gate = g->b_att = g->c_gate = g->c_att = 0;
    g->b_ph1 = g->b_ph2 = 0.0f;
    memset(g->c_ph, 0, sizeof g->c_ph);
    g->b_env = g->c_env = 0.0f;
    g->b_lp = g->c_lp = 0.0f;
}

void groove_init(groove_t *g, float sr)
{
    memset(g, 0, sizeof(*g));
    g->sr = sr;
    g->k_att = 1.0f / (GR_ATTACK_SEC * sr);
    g->k_dec_b = expf(-1.0f / (GR_B_DECAY_SEC * sr));
    g->k_dec_c = expf(-1.0f / (GR_C_DECAY_SEC * sr));
    g->k_rel_b = expf(-1.0f / (GR_B_REL_SEC * sr));
    g->k_rel_c = expf(-1.0f / (GR_C_REL_SEC * sr));
    groove_set_bpm(g, 120.0f);
    groove_reset(g);
}

float groove_bar(const groove_t *g)
{
    float b = ((float)g->step + (float)(g->t / (double)g->step_len)) * (1.0f / 16.0f);
    if (b >= (float)GROOVE_BARS) b -= (float)GROOVE_BARS;       /* the instant before the first step */
    return b;
}

/* A VCA: a linear attack to 1, an exponential decay while the gate is held, a fast release after. */
static inline void vca(float *env, int *att, int gate, float k_att, float k_dec, float k_rel)
{
    if (gate && *att) {
        *env += k_att;
        if (*env >= 1.0f) { *env = 1.0f; *att = 0; }
    } else if (gate) {
        *env *= k_dec;
    } else {
        *env = flush(*env * k_rel);
    }
}

float groove_process(groove_t *g)
{
    /* the step clock: a double, so a loop of 128 steps is exact whenever step_len x 128 is */
    g->t += 1.0;
    if (g->t >= (double)g->step_len) {
        g->t -= (double)g->step_len;
        g->step = (g->step + 1) % GROOVE_STEPS;
        const int s = g->step;

        const int bn = groove_bass_note(s);
        if (bn > 0) {
            g->b_inc = note_inc(bn, g->sr);
            g->b_vel = (s % 4 == 0) ? 1.0f : (bn - k_bars[s / 16].root >= 12 ? 0.85f : 0.7f);   /* downbeats and octaves lead */
            g->b_ph1 = g->b_ph2 = 0.0f;                                  /* every hit starts the same: the loop repeats exactly */
            g->b_env = 0.0f;
            g->b_lp = 0.0f;
            g->b_gate = 1;
            g->b_att = 1;
        }
        int notes[GROOVE_CHORD_NOTES];
        const int cn = groove_chord(s, notes);
        if (cn > 0) {
            g->c_n = cn;
            for (int i = 0; i < cn; i++) { g->c_inc[i] = note_inc(notes[i], g->sr); g->c_ph[i][0] = g->c_ph[i][1] = 0.0f; }
            g->c_vel = (s % 16 == 2 || s % 16 == 9) ? 1.0f : 0.8f;
            g->c_env = 0.0f;
            g->c_lp = 0.0f;
            g->c_gate = 1;
            g->c_att = 1;
        }
    }
    if (g->b_gate && g->t >= (double)(GR_B_GATE * g->step_len)) g->b_gate = 0;
    if (g->c_gate && g->t >= (double)(GR_C_GATE * g->step_len)) g->c_gate = 0;
    vca(&g->b_env, &g->b_att, g->b_gate, g->k_att, g->k_dec_b, g->k_rel_b);
    vca(&g->c_env, &g->c_att, g->c_gate, g->k_att, g->k_dec_c, g->k_rel_c);

    float out = 0.0f;
    if (g->b_env > 0.0f) {
        /* the bass: two saws a few cents apart through a pluck filter that is very bright on the hit */
        const float d1 = g->b_inc * (1.0f + GR_DETUNE), d2 = g->b_inc * (1.0f - GR_DETUNE);
        const float x = 0.5f * (saw(&g->b_ph1, d1) + saw(&g->b_ph2, d2));
        const float fc = 3500.0f + 10000.0f * g->b_env * g->b_env;
        g->b_lp = flush(g->b_lp + (x - g->b_lp) * fminf(0.95f, 2.0f * PI_F * fc / g->sr));
        out += g->b_lp * g->b_env * g->b_vel * GR_BASS_AMP;
    }
    if (g->c_env > 0.0f) {
        /* the stab: the chord's saws summed, one filter and one envelope for all of them */
        float x = 0.0f;
        for (int i = 0; i < g->c_n; i++) {
            x += saw(&g->c_ph[i][0], g->c_inc[i] * (1.0f + GR_DETUNE));
            x += saw(&g->c_ph[i][1], g->c_inc[i] * (1.0f - GR_DETUNE));
        }
        x *= 1.0f / (float)(2 * GROOVE_CHORD_NOTES);
        const float fc = 4500.0f + 9000.0f * g->c_env * g->c_env;
        g->c_lp = flush(g->c_lp + (x - g->c_lp) * fminf(0.95f, 2.0f * PI_F * fc / g->sr));
        out += g->c_lp * g->c_env * g->c_vel * GR_CHORD_AMP * 2.0f;     /* x2: eight saws sum to far less than eight at their peaks */
    }
    return out;
}

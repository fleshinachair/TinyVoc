#include "modes.h"
#include "dsp.h"      /* VT_MODE_*, VT_SCALE_* */
#include "imogen.h"   /* vt_chord, voicings */

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f

static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline float flush(float x) { return fabsf(x) < 1e-15f ? 0.0f : x; }
static inline float onepole(float *z, float x, float a) { *z = flush(*z + (x - *z) * a); return *z; }

/* Gain that keeps tanh(k x) at the same RMS as x for a 0.3-peak sine. (Matching
 * peaks isn't enough: heavy drive squares the wave off and the RMS jumps up.)
 * Evaluated once per block, never per sample. */
static float sat_norm(float k)
{
    double acc = 0.0;
    for (int i = 0; i < 32; i++) {
        double y = tanh(k * 0.3 * sin((i + 0.5) * (3.14159265358979 / 32.0)));
        acc += y * y;
    }
    double rms = sqrt(acc / 32.0);
    return rms > 1e-9 ? (float)(0.3 * 0.70710678 / rms) : 1.0f;
}

/* Linear-interpolated read d samples behind w in a power-of-two line of n. */
static float haas_read_n(const float *line, int w, float d, int n)
{
    float pos = (float)w - d;
    float fl = floorf(pos);
    float fr = pos - fl;
    int i0 = (int)fl & (n - 1);
    int i1 = (i0 + 1) & (n - 1);
    return line[i0] + fr * (line[i1] - line[i0]);
}

static float haas_read(const float *line, int w, float d)
{
    float pos = (float)w - d;
    float fl = floorf(pos);
    float fr = pos - fl;
    int i0 = (int)fl & (FX_HAAS_LEN - 1);
    int i1 = (i0 + 1) & (FX_HAAS_LEN - 1);
    return line[i0] + fr * (line[i1] - line[i0]);
}

/* Bandpass (TPT state-variable), normalised to unity gain at the centre. */
static float svf_bp(float *s, float x, float f, float q, float sr)
{
    float g = tanf(PI_F * clampf(f, 20.0f, 0.45f * sr) / sr);
    float k = 1.0f / q;
    float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    float v3 = x - s[1];
    float v1 = a1 * s[0] + a2 * v3;
    float v2 = s[1] + a2 * s[0] + a3 * v3;
    s[0] = flush(2.0f * v1 - s[0]);
    s[1] = flush(2.0f * v2 - s[1]);
    return k * v1;
}

static void svf_design(fx_svf_t *f, float hz, float q, float sr)
{
    float g = tanf(PI_F * clampf(hz, 20.0f, 0.45f * sr) / sr);
    f->k = 1.0f / q;
    f->a1 = 1.0f / (1.0f + g * (g + f->k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
    f->s1 = f->s2 = 0.0f;
}

/* One sample; returns the low-pass, *bp the band-pass (high-pass = x - k bp - lp). */
static inline float svf_tick(fx_svf_t *f, float x, float *bp)
{
    float v3 = x - f->s2;
    float v1 = f->a1 * f->s1 + f->a2 * v3;
    float v2 = f->s2 + f->a2 * f->s1 + f->a3 * v3;
    f->s1 = flush(2.0f * v1 - f->s1);
    f->s2 = flush(2.0f * v2 - f->s2);
    *bp = v1;
    return v2;
}

/* Snap's telephone curve: 4th-order Butterworth high-pass and low-pass (two
 * sections each, Q 0.541 / 1.307), then a bell at 2.5 kHz, then makeup for
 * the chest and air it takes away. */
float fx_snap_voicing(fx_t *fx, float x)
{
    float bp, lp;
    for (int s = 0; s < 2; s++) {
        lp = svf_tick(&fx->tel[s], x, &bp);
        x = x - fx->tel[s].k * bp - lp;
    }
    for (int s = 2; s < 4; s++) x = svf_tick(&fx->tel[s], x, &bp);
    (void)svf_tick(&fx->tel[4], x, &bp);
    x += fx->tel_bite * fx->tel[4].k * bp;           /* k bp: unity at the centre */
    return x * fx->tel_makeup;
}

void fx_init(fx_t *fx, float sr)
{
    memset(fx, 0, sizeof(*fx));
    fx->sr = sr;
    fx->rng = 0x51f15e5u;
    fx->glide_a = 1.0f;
    fx->sat_k = 1.0f;
    fx->sat_n = sat_norm(1.0f);
    for (int i = 0; i < FX_VOICES; i++) {
        psola_init(&fx->pv[i]);
        fx->note[i] = -1;
        fx->held[i] = -1;
        fx->pad_last[i] = -1;
    }
    fx->snap_note = -1;
    fx->hp_a = vt_unvoiced_hp_coef(sr);
    fx->a12 = expf(-2.0f * PI_F * 12000.0f / sr);
    fx->k_env10 = 1.0f - expf(-1.0f / (0.01f * sr));
    svf_design(&fx->tel[0], SNAP_HPF_HZ, 0.5412f, sr);
    svf_design(&fx->tel[1], SNAP_HPF_HZ, 1.3066f, sr);
    svf_design(&fx->tel[2], SNAP_LPF_HZ, 0.5412f, sr);
    svf_design(&fx->tel[3], SNAP_LPF_HZ, 1.3066f, sr);
    svf_design(&fx->tel[4], SNAP_BITE_HZ, 1.2f, sr);
    fx->tel_bite = powf(10.0f, SNAP_BITE_DB / 20.0f) - 1.0f;
    fx->tel_makeup = SNAP_MAKEUP;
}

void fx_set(fx_t *fx, const cher_t *c, int mode, float char1, float char2, float mod,
            float spread, float glide_ms, int auto_on, int root, int scale,
            const int held[FX_VOICES])
{
    fx->mode = mode;
    fx->char1 = char1;
    fx->char2 = char2;
    fx->mod = mod;
    fx->spread = spread;
    fx->auto_on = auto_on;
    fx->root = root;
    fx->scale = scale;
    fx->glide_a = glide_ms <= 0.0f ? 1.0f : 1.0f - expf(-1000.0f / (glide_ms * fx->sr));
    for (int i = 0; i < FX_VOICES; i++) fx->held[i] = held[i];

    /* Saturation drive per mode (Morph: char2 drive, Prism: char2 warmth, Tape: mod). */
    float k = 1.0f;
    if (mode == VT_MODE_MORPH) k = 1.0f + 15.0f * char2;
    else if (mode == VT_MODE_PRISM) k = 1.0f + 6.0f * char2;
    else if (mode == VT_MODE_TAPE) k = 1.0f + 6.0f * mod;
    if (k != fx->sat_k) { fx->sat_k = k; fx->sat_n = sat_norm(k); }

    /* Snap's grain tightness (char1 above 0.5); every other mode plays plain PSOLA. */
    psola_set_tight(&fx->pv[0], mode == VT_MODE_SNAP ? clampf(2.0f * char1 - 1.0f, 0.0f, 1.0f) : 0.0f);
    if (mode != VT_MODE_SNAP) {
        fx->snap_note = -1;
        for (int s = 0; s < 5; s++) fx->tel[s].s1 = fx->tel[s].s2 = 0.0f;   /* no stale ring on return */
    }

    if (mode != VT_MODE_PRISM) return;

    /* Prism targets, like Chords: pads, else the auto chord (voicing from
     * char1, the harmony spread), else just the snapped lead. */
    int target[FX_VOICES] = { -1, -1, -1, -1 };
    int n_held = 0, pad = -1;
    for (int i = 0; i < FX_VOICES; i++) if (held[i] >= 0) { n_held++; pad = held[i]; }
    const int voicing = char1 < 0.34f ? VT_VOICING_CLOSE : (char1 < 0.67f ? VT_VOICING_STANDARD : VT_VOICING_WIDE);
    int same = 1;                                    /* the same pads as last block? */
    for (int i = 0; i < FX_VOICES; i++) { same &= held[i] == fx->pad_last[i]; fx->pad_last[i] = held[i]; }
    if (n_held == 1) {
        /* One pad = the chord root, as in Chords: the whole voicing moves. */
        fx->pad_oct = cher_fold_octave(c->f0_midi, (float)pad, fx->pad_oct, same);
        int r = pad + 12 * fx->pad_oct;
        while (r < 12) r += 12;
        vt_chord(r, root, scale, voicing, target);
    } else if (n_held > 1) {
        fx->pad_oct = cher_fold_chord(c->f0_midi, held, target, FX_VOICES, fx->pad_oct, same);
    } else if (c->voiced && c->note >= 0) {
        if (auto_on) {
            vt_chord(c->note, root, scale, voicing, target);
        } else {
            target[0] = c->note;
        }
    }
    vt_fit_register(target, FX_VOICES, c->voiced ? c->note : -1);   /* +11 / -12 st, as in Chords */
    fx->active = 0;
    for (int i = 0; i < FX_VOICES; i++) {
        if (target[i] >= 0) {
            fx->active++;
            if (fx->note[i] >= 0 && fx->amp[i] > 0.01f && target[i] != fx->note[i])
                fx->glide_off[i] += (float)(fx->note[i] - target[i]);
            else if (fx->note[i] < 0 || fx->amp[i] <= 0.01f)
                fx->glide_off[i] = 0.0f;
        }
        fx->note[i] = target[i];
    }
}

float fx_latency(const fx_t *fx, const cher_t *c)
{
    switch (fx->mode) {
    case VT_MODE_TAPE:    return cher_latency(c) - 1.0f;
    default:              return fx->pv[0].lat - 1.0f;   /* reads index now - lat; newest is now - 1 */
    }
}

/* Slow random drift in [-1, 1] (two independent walkers), for Double and Tape. */
static void drift_step(fx_t *fx)
{
    if (--fx->drift_count <= 0) {
        fx->drift_count = 4096;
        for (int k = 0; k < 2; k++) {
            fx->rng = fx->rng * 1664525u + 1013904223u;
            fx->drift_goal[k] = (float)(int)(fx->rng >> 8) * (1.0f / 8388608.0f) - 1.0f;
        }
    }
    for (int k = 0; k < 2; k++) fx->drift[k] += (fx->drift_goal[k] - fx->drift[k]) * 0.0002f;
}

void fx_process(fx_t *fx, cher_t *c, float in, float *out_l, float *out_r)
{
    cher_push(c, in);
    const float sr = fx->sr;
    fx->t += 1.0 / sr;
    if (fx->t > 3600.0) fx->t -= 3600.0;
    const float t = (float)fx->t;
    const float T0 = c->voiced ? sr / c->f0 : 0.0f;
    float l = 0.0f, r = 0.0f;

    switch (fx->mode) {
    case VT_MODE_SNAP: {
        /* Hard-tuned lead (Retune, Key/Scale, pads, Glide), the 1998 "Believe" way:
         *  - at Retune 0 the ratio changes at the next output pitch period with
         *    no slew (cher_lead_lr), and each note change also resets the grain
         *    phase (psola_kick) instead of crossfading over two periods. The
         *    reset fades out by Retune 50 ms, so slow retunes stay smooth.
         *  - the formant is not fully decoupled: it follows a quarter of the
         *    correction (char1 0.5), rising to all of it at char1 1, where the
         *    grains also tighten toward one period: bright, buzzy, metallic.
         *  - char1 still offsets the formant -12..+12 st, on top of a fixed
         *    +2.5 st lift (char1 0.5 = +2.5 st): processed, not natural.
         *  - the result goes through a telephone band with a 2.5 kHz bite
         *    (fx_snap_voicing): no chest, no air.
         *  - Beauty 0 makes the retune instant, Retune and Glide aside (dsp.c). */
        float lr = cher_lead_lr(c);
        const float rt = c->retune_ms;
        const float hard = rt <= 0.0f ? 1.0f : clampf(1.0f - rt * (1.0f / 50.0f), 0.0f, 1.0f);
        const int tn = c->override_note >= 0 ? c->override_note : c->note;            /* the pad moves at once */
        if (c->voiced && tn >= 0) {
            if (fx->snap_note >= 0 && tn != fx->snap_note && hard > 0.0f) psola_kick(&fx->pv[0], hard);
            fx->snap_note = tn;
        } else if (!c->voiced) {
            fx->snap_note = -1;                                                       /* an onset is not a jump */
        }
        const float couple = 0.25f + 0.75f * clampf(2.0f * fx->char1 - 1.0f, 0.0f, 1.0f);
        float fr = exp2f(2.0f * fx->char1 - 1.0f + SNAP_FORMANT_LIFT * (1.0f / 12.0f) + couple * lr);   /* + skew */
        if (c->voiced && fx->mod > 0.0f)
            lr += fx->mod * (0.5f / 12.0f) * sinf(2.0f * PI_F * 5.5f * t);            /* vibrato */
        float y = psola_process(&fx->pv[0], c, T0, exp2f(lr), fr, 0.0f);
        if (fx->beauty > 0.0f) {
            const float b = fx->beauty;
            /* anti-harshness: when 2.5-5 kHz gets loud against the whole, cut
             * it dynamically, up to ~4 dB */
            float band = svf_bp(fx->hb, y, 3500.0f, 0.9f, sr);
            fx->hb_env = flush(fx->hb_env + (fabsf(band) - fx->hb_env) * fx->k_env10);
            fx->hf_env = flush(fx->hf_env + (fabsf(y) - fx->hf_env) * fx->k_env10);
            float ratio = fx->hb_env / (fx->hf_env + 1e-9f);
            float goal = b * clampf((ratio - 0.35f) / 0.35f, 0.0f, 1.0f) * 0.37f;
            fx->hk = flush(fx->hk + (goal - fx->hk) * fx->k_env10);
            y -= fx->hk * band;
            /* air: a +2 dB (at full) shelf from 12 kHz */
            float hp = fx->a12 * (fx->sh_y + y - fx->sh_x);
            fx->sh_x = y;
            fx->sh_y = flush(hp);
            y += (exp2f(b * 2.0f / 6.0206f) - 1.0f) * hp;
            y = fx_snap_voicing(fx, y);
            /* micro-double: the snapped voice again, 11 / 16 ms late, drifting */
            fx->haas[0][fx->haas_w] = y;
            fx->haas_w = (fx->haas_w + 1) & (FX_HAAS_LEN - 1);
            float wob = 0.0002f * sr * sinf(2.0f * PI_F * 0.31f * t);
            float g = 0.22f * b;
            l = y + g * haas_read(fx->haas[0], fx->haas_w, 0.011f * sr + wob);
            r = y + g * haas_read(fx->haas[0], fx->haas_w, 0.016f * sr - wob);
        } else {
            l = r = fx_snap_voicing(fx, y);
        }
        break;
    }
    case VT_MODE_MORPH: {
        /* Formant-decoupled shifter into drive: pitch and timbre pulled apart. */
        float lr = cher_lead_lr(c);
        float fs = 2.0f * fx->char1 - 1.0f + fx->mod * (4.0f / 12.0f) * sinf(2.0f * PI_F * 0.25f * t);
        float y = psola_process(&fx->pv[0], c, T0, exp2f(lr), exp2f(fs), 0.0f);
        l = r = tanhf(fx->sat_k * y) * fx->sat_n;
        break;
    }
    case VT_MODE_DOUBLE: {
        /* Hard-tuned lead in the middle, two detuned doubles 15-25 ms late,
         * one each side, drifting slowly like a second take. */
        drift_step(fx);
        float lr = cher_lead_lr(c);
        float p = exp2f(lr);
        float lead = psola_process(&fx->pv[0], c, T0, p, 1.0f, 0.0f);
        float det = (5.0f + 25.0f * fx->char1) / 1200.0f;                             /* 5..30 cents */
        float dr = (0.3f + 0.7f * fx->mod) * (10.0f / 1200.0f);                       /* drift, cents */
        float d1 = psola_process(&fx->pv[1], c, T0, p * exp2f( det + dr * fx->drift[0]), 1.0f, 0.0f);
        float d2 = psola_process(&fx->pv[2], c, T0, p * exp2f(-det + dr * fx->drift[1]), 1.0f, 0.0f);
        fx->haas[0][fx->haas_w] = d1;
        fx->haas[1][fx->haas_w] = d2;
        fx->haas_w = (fx->haas_w + 1) & (FX_HAAS_LEN - 1);
        float swing = (0.3f + 0.7f * fx->mod) * 2.5f;                                 /* ms */
        float D1 = (17.5f + swing * fx->drift[0]) * 0.001f * sr;                      /* 15..20 ms */
        float D2 = (22.5f + swing * fx->drift[1]) * 0.001f * sr;                      /* 20..25 ms */
        float h1 = haas_read(fx->haas[0], fx->haas_w, D1);
        float h2 = haas_read(fx->haas[1], fx->haas_w, D2);
        float w = fx->char2;
        float a = 1.0f / (1.0f + 0.5f * w);
        l = (lead + 0.8f * w * h1) * a;
        r = (lead + 0.8f * w * h2) * a;
        break;
    }
    case VT_MODE_PRISM: {
        /* 0 ms hard snap into up to four formant-preserving harmony lines,
         * panned by the harmony spread, then warm tape saturation. Unvoiced
         * sounds pass on voice 0 at ratio 1, high-passed (consonants only, no
         * unshifted pitch). */
        static const float pos[FX_VOICES] = { -0.33f, -1.0f, 1.0f, 0.33f };
        static const float hum_pattern[FX_VOICES] = { 0.0f, 1.0f, -0.8f, 0.6f };
        const float norm = fx->active > 0 ? 1.0f / sqrtf((float)fx->active) : 1.0f;
        const float fade = 1.0f - expf(-1.0f / (0.01f * sr));
        for (int i = 0; i < FX_VOICES; i++) {
            int on = fx->note[i] >= 0 && c->voiced;
            float goal = (on || i == 0) ? 1.0f : 0.0f;
            fx->amp[i] += (goal - fx->amp[i]) * fade;
            if (fx->amp[i] < 1e-4f && goal == 0.0f) {
                fx->amp[i] = 0.0f;
                if (i > 0) fx->mt[i][fx->mt_w] = 0.0f;       /* a silent voice leaves silence in its line */
                continue;
            }
            float ratio = 1.0f;
            if (on) {
                fx->glide_off[i] *= 1.0f - fx->glide_a;
                if (fabsf(fx->glide_off[i]) < 1e-5f) fx->glide_off[i] = 0.0f;
                float hum = fx->mod * (0.15f * hum_pattern[i] +
                                       0.06f * sinf(2.0f * PI_F * (0.11f + 0.07f * (float)i) * t));
                static const float bdet[FX_VOICES] = { 0.0f, 0.04f, -0.04f, 0.025f };   /* Beauty: +-4 ct */
                hum += fx->beauty * bdet[i];
                float lr = ((float)fx->note[i] + fx->glide_off[i] + hum - c->f0_midi) * (1.0f / 12.0f);
                ratio = exp2f(clampf(lr, -1.0f, 1.0f));
            }
            float s = psola_process(&fx->pv[i], c, T0, ratio, 1.0f, 0.0f) * fx->amp[i] * (on ? norm : 1.0f);
            if (i > 0) {                                     /* Beauty: harmonies 8 / 10 / 12 ms late */
                static const float bms[FX_VOICES] = { 0.0f, 0.008f, 0.010f, 0.012f };
                fx->mt[i][fx->mt_w] = s;
                fx->mt_d[i] += (fx->beauty * bms[i] * sr - fx->mt_d[i]) * 0.001f;
                if (fx->mt_d[i] > 0.5f) s = haas_read_n(fx->mt[i], fx->mt_w + 1, fx->mt_d[i] + 1.0f, FX_MT_LEN);
            }
            if (i == 0) {
                fx->voiced_w += ((on ? 1.0f : 0.0f) - fx->voiced_w) * fade;
                float hp = vt_unvoiced_hp(fx->hp_z, s, fx->hp_a);
                s = fx->voiced_w * s + (1.0f - fx->voiced_w) * hp;
            }
            float pp = (pos[i] * fminf(1.0f, fx->char1 + 0.4f * fx->beauty) + 1.0f) * 0.25f * PI_F;   /* Beauty: wider */
            l += s * cosf(pp) * 1.41421356f;
            r += s * sinf(pp) * 1.41421356f;
        }
        fx->mt_w = (fx->mt_w + 1) & (FX_MT_LEN - 1);
        float k = fx->sat_k;                                                          /* warmth */
        float b = 0.2f * fx->char2;                                                   /* asymmetry */
        float n = fx->sat_n;
        l = (tanhf(k * l + b) - tanhf(b)) * n;
        r = (tanhf(k * r + b) - tanhf(b)) * n;
        float a = 1.0f - expf(-2.0f * PI_F * (12000.0f - 8000.0f * fx->char2) / sr);
        l = VT_HARMONY_TRIM * onepole(&fx->lp[0][1], onepole(&fx->lp[0][0], l, a), a);
        r = VT_HARMONY_TRIM * onepole(&fx->lp[1][1], onepole(&fx->lp[1][0], r, a), a);
        break;
    }
    case VT_MODE_TAPE: {
        /* Varispeed drop: formants fall with the pitch, like slowed tape, plus
         * wow, flutter, a darker top end the slower it runs, and saturation. */
        drift_step(fx);
        float lr = -(1.0f - fx->char1);                                               /* -12..0 st */
        float wf = fx->char2 * ((0.25f / 12.0f) * sinf(2.0f * PI_F * 0.55f * t)
                              + (0.06f / 12.0f) * sinf(2.0f * PI_F * 6.3f * t)
                              + (0.10f / 12.0f) * fx->drift[0]);
        float y = cher_tap(c, &fx->tape_phi, lr + wf);
        y = tanhf(fx->sat_k * y) * fx->sat_n;
        float fc = fmaxf(9000.0f * exp2f(lr), 2500.0f);
        float a = 1.0f - expf(-2.0f * PI_F * fc / sr);
        l = r = onepole(&fx->lp[0][1], onepole(&fx->lp[0][0], y, a), a);
        break;
    }
    default:
        l = r = cher_plain(c);
        break;
    }
    *out_l = l;
    *out_r = r;
}

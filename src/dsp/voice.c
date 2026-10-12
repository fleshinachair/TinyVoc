#include "voice.h"
#include "dsp.h"      /* VT_MODE_* */

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f

#define DK_LATENCY      25.0f    /* Hilbert pair's group delay near 500 Hz, samples (80 at 150 Hz, 7 at 2 kHz) */
#define DK_TRIM         1.2f     /* Dalek wet level: the ring-modulated shift loses ~9 dB; this gives it back */
#define HG_GUARD        2.0f     /* the shortest tap delay, samples (the read must stay behind the write) */
#define HG_TRIM         0.6f
#define WK_TRIM         0.40f
#define WK_PEAK_HZ      1800.0f
#define WK_HP_HZ        350.0f
#define WK_LP_HZ        3000.0f
#define WK_COMP_TARGET  0.12f    /* the mic compressor's mean |x|: every phrase arrives slammed */

static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline float flush(float x) { return fabsf(x) < 1e-15f ? 0.0f : x; }

/* ------------------------------------------------------------------ SVF */

static void svf_set(vox_svf *f, float hz, float q, float sr)
{
    f->g = tanf(PI_F * clampf(hz, 20.0f, 0.45f * sr) / sr);
    f->k = 1.0f / q;
    f->a1 = 1.0f / (1.0f + f->g * (f->g + f->k));
    f->a2 = f->g * f->a1;
    f->a3 = f->g * f->a2;
}

/* One sample; returns the low-pass, *bp the band-pass, *hp the high-pass. */
static inline float svf_tick(vox_svf *f, float x, float *bp, float *hp)
{
    const float v3 = x - f->s2;
    const float v1 = f->a1 * f->s1 + f->a2 * v3;
    const float v2 = f->s2 + f->a2 * f->s1 + f->a3 * v3;
    f->s1 = flush(2.0f * v1 - f->s1);
    f->s2 = flush(2.0f * v2 - f->s2);
    *bp = v1;
    *hp = x - f->k * v1 - v2;
    return v2;
}

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

static inline float rnd(vox_t *v)                   /* white, -1..1 */
{
    v->rng = v->rng * 1664525u + 1013904223u;
    return (float)(int)(v->rng >> 8) * (1.0f / 8388608.0f) - 1.0f;
}

void vox_init(vox_t *v, float sr)
{
    memset(v, 0, sizeof(*v));
    v->sr = sr;
    v->rng = 0x7a11e5u;

    ssb_osc_init(&v->sh_l);
    ssb_osc_init(&v->sh_r);
    ssb_osc_init(&v->ring);
    v->dk_k = 1.0f;
    v->dk_n = sat_norm(1.0f);
    v->k_dc = 1.0f - expf(-2.0f * PI_F * 20.0f / sr);

    v->hg_grain = v->hg_grain_goal = 0.013f * sr;
    v->hg_k_slew = 1.0f - expf(-1.0f / (0.008f * sr));
    v->hg_k_amp  = 1.0f - expf(-1.0f / (0.010f * sr));
    static const float def_st[VOX_HG_TAPS] = { -12.0f, 7.0f, 12.0f };
    for (int k = 0; k < VOX_HG_TAPS; k++) {
        v->hg_st[k] = v->hg_st_goal[k] = def_st[k];
        v->hg_amp[k] = v->hg_amp_goal[k] = 1.0f;
        v->hg_phi[k] = 0.31f * (float)k;
    }

    svf_set(&v->wk_hp[0], WK_HP_HZ, 0.5412f, sr);
    svf_set(&v->wk_hp[1], WK_HP_HZ, 1.3066f, sr);
    svf_set(&v->wk_lp[0], WK_LP_HZ, 0.5412f, sr);
    svf_set(&v->wk_lp[1], WK_LP_HZ, 1.3066f, sr);
    svf_set(&v->wk_peak, WK_PEAK_HZ, 2.75f, sr);
    svf_set(&v->wk_nz, 2200.0f, 0.8f, sr);
    v->wk_cg = 1.0f;
    v->k_wk_att = 1.0f - expf(-1.0f / (0.002f * sr));
    v->k_wk_rel = 1.0f - expf(-1.0f / (0.080f * sr));
    v->k_wk_g   = 1.0f - expf(-1.0f / (0.005f * sr));
    v->wk_rx = 1.0f;
    v->k_wk_rx  = 1.0f - expf(-1.0f / (0.004f * sr));
    v->wk_tau = 0.03f * sr;
    v->wk_since = 1e9f;          /* the first edge counts */
    v->wk_open = 1;              /* the voice gate starts open: no burst at load */
}

/* ------------------------------------------------------------------ Dalek */

void vox_set_dalek(vox_t *v, float c1, float c2, float mod, float c4, float c5)
{
    const float shift = (clampf(c1, 0.0f, 1.0f) - 0.5f) * 1000.0f;    /* -500..+500 Hz, linear */
    const float off = clampf(c5, 0.0f, 1.0f) * 12.0f;                 /* L and R apart by up to 2 x 12 Hz */
    ssb_osc_set(&v->sh_l, shift + off, v->sr);
    ssb_osc_set(&v->sh_r, shift - off, v->sr);
    const float ring = clampf(c2, 0.0f, 1.0f) * 60.0f;
    v->ring_on = ring >= 1.0f;
    ssb_osc_set(&v->ring, ring, v->sr);
    const float k = 1.0f + 4.0f * clampf(c4, 0.0f, 1.0f);
    if (k != v->dk_k) { v->dk_k = k; v->dk_n = sat_norm(k); }
    v->dk_fb = 0.7f * clampf(mod, 0.0f, 1.0f);
}

static void dalek(vox_t *v, float in, float *out_l, float *out_r)
{
    float i, q;
    ssb_hilbert_tick(&v->hil, in, &i, &q);
    float y[2] = { ssb_shift(&v->sh_l, i, q), ssb_shift(&v->sh_r, i, q) };
    ssb_osc_tick(&v->sh_l);
    ssb_osc_tick(&v->sh_r);
    if (v->ring_on) {                                   /* ring modulation: both sidebands */
        const float r = v->ring.s * 1.41421356f;
        ssb_osc_tick(&v->ring);
        y[0] *= r;
        y[1] *= r;
    }
    static const float comb_ms[2] = { 3.7f, 4.3f };
    const float fb = v->dk_fb, make = 1.0f - 0.6f * fb;
    float o[2];
    for (int c = 0; c < 2; c++) {
        float s = tanhf(v->dk_k * y[c]) * v->dk_n;
        if (fb > 0.0f) {
            const int d = (int)(comb_ms[c] * 0.001f * v->sr);
            s = flush(s + fb * v->dk_line[c][(v->dk_w - d) & (VOX_DK_LEN - 1)]);
            v->dk_line[c][v->dk_w] = s;
            s *= make;
        } else {
            v->dk_line[c][v->dk_w] = 0.0f;
        }
        v->dk_dc[c] += (s - v->dk_dc[c]) * v->k_dc;       /* the comb's gain at DC and the clip's asymmetry: block it */
        v->dk_dc[c] = flush(v->dk_dc[c]);
        o[c] = (s - v->dk_dc[c]) * DK_TRIM;
    }
    v->dk_w = (v->dk_w + 1) & (VOX_DK_LEN - 1);
    *out_l = o[0];
    *out_r = o[1];
}

/* --------------------------------------------------------------- Harmony */

/* Linear-interpolated read d samples behind the newest one (head). */
static inline float hg_read(const float *buf, int head, float d)
{
    const float pos = (float)head - d;
    const float fl = floorf(pos);
    const float fr = pos - fl;
    const int i0 = (int)fl & (VOX_HG_LEN - 1);
    const int i1 = (i0 + 1) & (VOX_HG_LEN - 1);
    return buf[i0] + fr * (buf[i1] - buf[i0]);
}

static int cmp_int(const int a, const int b) { return a < b ? -1 : (a > b); }

void vox_set_harmony(vox_t *v, float c1, float c2, float mod, float c4, float c5,
                     float spread, const int held[4], float f0_hz)
{
    v->hg_c1 = clampf(c1, 0.0f, 1.0f);
    v->hg_c2 = clampf(c2, 0.0f, 1.0f);
    v->hg_mod = clampf(mod, 0.0f, 1.0f);
    v->hg_spread = clampf(spread, 0.0f, 1.0f);
    float grain = (VOX_HG_GRAIN_MIN_MS + (VOX_HG_GRAIN_MAX_MS - VOX_HG_GRAIN_MIN_MS) * clampf(c4, 0.0f, 1.0f)) * 0.001f * v->sr;
    if (f0_hz >= 55.0f) {                                       /* whole periods of the voice */
        const float T0 = v->sr / f0_hz;
        float n = floorf(grain / T0 + 0.5f);
        if (n < 1.0f) n = 1.0f;
        grain = n * T0;
    }
    v->hg_grain_goal = grain;
    v->hg_micro = VOX_HG_MICRO_MAX_MS * clampf(c5, 0.0f, 1.0f) * 0.001f * v->sr;

    /* The taps: -12, +7, +12 semitones, or, with two or more pads held, the
     * intervals above the lowest one (a C major triad: +4, +7). The chord's
     * shape is what counts, not where it sits, so it follows the voice. */
    int n = 0, notes[4];
    for (int i = 0; i < 4; i++) if (held[i] >= 0) notes[n++] = held[i];
    for (int a = 1; a < n; a++)                                 /* insertion sort, ascending */
        for (int b = a; b > 0 && cmp_int(notes[b - 1], notes[b]) > 0; b--) { int t = notes[b]; notes[b] = notes[b - 1]; notes[b - 1] = t; }
    static const float def_st[VOX_HG_TAPS] = { -12.0f, 7.0f, 12.0f };
    int used = 0;
    if (n >= 2) {
        for (int i = 1; i < n && used < VOX_HG_TAPS; i++) {
            int s = notes[i] - notes[0];
            while (s > 12) s -= 12;
            if (s == 0) continue;                               /* a doubled root adds nothing */
            v->hg_st_goal[used] = (float)s;
            v->hg_amp_goal[used] = 1.0f;
            used++;
        }
    }
    if (used == 0) {
        for (int k = 0; k < VOX_HG_TAPS; k++) { v->hg_st_goal[k] = def_st[k]; v->hg_amp_goal[k] = 1.0f; }
        used = VOX_HG_TAPS;
    } else {
        for (int k = used; k < VOX_HG_TAPS; k++) v->hg_amp_goal[k] = 0.0f;
    }
    v->hg_active = used;
}

static void harmony(vox_t *v, float in, float *out_l, float *out_r)
{
    v->hg_buf[v->hg_w] = in;
    v->hg_w = (v->hg_w + 1) & (VOX_HG_LEN - 1);
    v->hg_t += 1.0 / (double)v->sr;
    if (v->hg_t > 3600.0) v->hg_t -= 3600.0;
    const float t = (float)v->hg_t;
    const int head = v->hg_w - 1;                                /* index of the newest sample */
    const float W = v->hg_grain += (v->hg_grain_goal - v->hg_grain) * v->hg_k_slew;

    static const float micro_w[VOX_HG_TAPS] = { 0.35f, 0.70f, 1.0f };
    static const float pan[VOX_HG_TAPS] = { -1.0f, 1.0f, 0.0f };
    static const float det_rate[VOX_HG_TAPS] = { 0.13f, 0.17f, 0.11f };
    const float width = 0.35f + 0.65f * v->hg_spread;
    const float norm = 1.0f / sqrtf((float)(v->hg_active > 0 ? v->hg_active : 1));
    float l = 0.0f, r = 0.0f;

    for (int k = 0; k < VOX_HG_TAPS; k++) {
        v->hg_st[k] += (v->hg_st_goal[k] - v->hg_st[k]) * v->hg_k_slew;
        v->hg_amp[k] += (v->hg_amp_goal[k] - v->hg_amp[k]) * v->hg_k_amp;
        if (v->hg_amp[k] < 1e-4f && v->hg_amp_goal[k] == 0.0f) { v->hg_amp[k] = 0.0f; continue; }
        const float st = v->hg_st[k] + v->hg_mod * 0.12f * sinf(2.0f * PI_F * det_rate[k] * t + (float)k);
        const float ratio = exp2f(st * (1.0f / 12.0f));
        float phi = v->hg_phi[k] + (1.0f - ratio) / W;           /* the delay sweeps 0..W: a tap's read speed is `ratio` */
        phi -= floorf(phi);
        v->hg_phi[k] = phi;
        float phb = phi + 0.5f;
        if (phb >= 1.0f) phb -= 1.0f;
        const float base = HG_GUARD + micro_w[k] * v->hg_micro;
        const float ya = hg_read(v->hg_buf, head, base + phi * W), yb = hg_read(v->hg_buf, head, base + phb * W);
        float w = 1.0f - fabsf(2.0f * phi - 1.0f);                /* triangle -> smoothstep: wa + wb = 1 */
        w = w * w * (3.0f - 2.0f * w);
        const float y = (w * ya + (1.0f - w) * yb) * v->hg_amp[k] * norm;
        const float p = (pan[k] * width + 1.0f) * 0.25f * PI_F;   /* equal power */
        l += y * cosf(p) * 1.41421356f;
        r += y * sinf(p) * 1.41421356f;
    }
    const float lead = hg_read(v->hg_buf, head, HG_GUARD + 0.5f * W + 0.68f * v->hg_micro);   /* the dry voice, on the taps' average delay */
    const float gl = 2.0f * v->hg_c1, gs = 2.0f * v->hg_c2;
    *out_l = (gl * lead + gs * l) * HG_TRIM;
    *out_r = (gl * lead + gs * r) * HG_TRIM;
}

/* ---------------------------------------------------------------- Walkie */

void vox_set_walkie(vox_t *v, float c1, float c2, float mod, float c4, float c5)
{
    c1 = clampf(c1, 0.0f, 1.0f); c2 = clampf(c2, 0.0f, 1.0f); c5 = clampf(c5, 0.0f, 1.0f);
    const float fc = WK_PEAK_HZ * exp2f((c5 - 0.5f) * 1.0f);       /* 1.27 .. 2.55 kHz */
    svf_set(&v->wk_peak, fc, 1.5f + 2.5f * c1, v->sr);
    v->wk_peak_g = 3.0f * c1;                                       /* presence: +8 dB at 0.5, +12 at 1 */
    v->wk_drive = 1.0f + 6.0f * c2;
    v->wk_static = 0.006f + 0.05f * clampf(mod, 0.0f, 1.0f);       /* a receiver always hisses a little */
    v->wk_squelch = 0.5f * clampf(c4, 0.0f, 1.0f);
}

static void walkie(vox_t *v, float in, int open, float *out)
{
    /* The receiver's squelch: a short noise burst as the channel opens and the
     * long "kssh" tail as it drops (edges closer than 120 ms to the last are
     * the gate chattering). */
    if (open != v->wk_open) {
        v->wk_open = open;
        if (v->wk_since > 0.12f * v->sr) {
            v->wk_env = open ? 0.6f : 1.0f;
            v->wk_tau = (open ? 0.025f : 0.12f) * v->sr;
            v->wk_since = 0.0f;
        }
    }
    if (v->wk_since < 1e9f) v->wk_since += 1.0f;
    /* ... and between phrases it mutes the channel: no carrier, no sound */
    v->wk_rx = flush(v->wk_rx + ((open ? 1.0f : 0.0f) - v->wk_rx) * v->k_wk_rx);

    float bp, hp, x = in;
    for (int s = 0; s < 2; s++) { svf_tick(&v->wk_hp[s], x, &bp, &hp); x = hp; }   /* 350 Hz high-pass */
    for (int s = 0; s < 2; s++) x = svf_tick(&v->wk_lp[s], x, &bp, &hp);          /* 3 kHz low-pass */
    float pk, d1;
    svf_tick(&v->wk_peak, x, &pk, &d1);
    x += v->wk_peak_g * v->wk_peak.k * pk;                         /* presence: k bp has unity gain at its centre */

    /* The handset's mic compressor: every phrase arrives at the same slammed
     * level (it holds its gain while the channel is shut, so pauses aren't lifted). */
    if (open) {
        const float a = fabsf(x);
        v->wk_cenv = flush(v->wk_cenv + (a - v->wk_cenv) * (a > v->wk_cenv ? v->k_wk_att : v->k_wk_rel));
        const float goal = clampf(WK_COMP_TARGET / (v->wk_cenv + 1e-6f), 0.25f, 8.0f);
        v->wk_cg += (goal - v->wk_cg) * v->k_wk_g;
    }
    x *= v->wk_cg;

    /* diode clipping: a pair with unequal forward drops (0.55 and 0.28), pushed hard */
    const float g = v->wk_drive * 1.6f;
    const float y = x * g;
    x = y >= 0.0f ? 0.55f * tanhf(y * (1.0f / 0.55f)) : -0.28f * tanhf(-y * (1.0f / 0.28f));
    v->wk_dc_y += (x - v->wk_dc_y) * v->k_dc;                       /* the asymmetry's DC */
    v->wk_dc_y = flush(v->wk_dc_y);
    x -= v->wk_dc_y;
    v->wk_lp1[0] += (x - v->wk_lp1[0]) * 0.35f;                     /* ~3.4 kHz again: takes the clip's fizz off */
    v->wk_lp1[1] += (v->wk_lp1[0] - v->wk_lp1[1]) * 0.35f;
    v->wk_lp1[0] = flush(v->wk_lp1[0]); v->wk_lp1[1] = flush(v->wk_lp1[1]);
    x = v->wk_lp1[1] * 1.4f;

    float noise = 0.0f;
    if (v->wk_env > 1e-4f) {
        v->wk_env *= expf(-1.0f / v->wk_tau);
        noise += v->wk_squelch * v->wk_env * rnd(v);
    } else {
        v->wk_env = 0.0f;
    }
    if (v->wk_rx > 1e-4f) noise += v->wk_rx * v->wk_static * rnd(v);
    float nb, nh;
    noise = svf_tick(&v->wk_nz, noise, &nb, &nh);
    noise = nb * v->wk_nz.k * 1.6f;                                 /* band-limited hiss, unity at 2.2 kHz */
    *out = (v->wk_rx * x + noise) * WK_TRIM;
}

/* ----------------------------------------------------------------- shared */

void vox_process(vox_t *v, int mode, float in, int voice_open, float *out_l, float *out_r)
{
    switch (mode) {
    case VT_MODE_DALEK:
        dalek(v, in, out_l, out_r);
        break;
    case VT_MODE_HARMONY:
        harmony(v, in, out_l, out_r);
        break;
    case VT_MODE_WALKIE: {
        float y;
        walkie(v, in, voice_open, &y);
        *out_l = *out_r = y;
        break;
    }
    default:
        *out_l = *out_r = 0.0f;
        break;
    }
}

void vox_listen(vox_t *v, float in)
{
    v->hg_buf[v->hg_w] = in;
    v->hg_w = (v->hg_w + 1) & (VOX_HG_LEN - 1);
}

float vox_latency(const vox_t *v, int mode)
{
    switch (mode) {
    case VT_MODE_DALEK:   return DK_LATENCY;
    case VT_MODE_HARMONY: return HG_GUARD + 0.5f * v->hg_grain + 0.68f * v->hg_micro;
    default:              return 0.0f;
    }
}

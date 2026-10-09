#include "cher.h"
#include "dsp.h"      /* vt_scale */

#include <math.h>
#include <string.h>

#define PI_F          3.14159265358979f
#define FMIN_HZ       70.0f
#define FMAX_HZ       1000.0f
#define MPM_K         0.9f      /* pick first peak >= K * highest peak */
#define CLARITY_MIN   0.65f     /* real mic speech is noisier than a test tone */
#define HOLD_HOPS     6         /* ~35 ms: keep snapping through voicing gaps in speech */
/* The tracker (not the audio you hear) sees the input boosted by this much, so
 * quiet mic levels still clear the Gate and the pitch snap engages. +12 dB. */
#define ANALYSIS_GAIN 4.0f
/* Highest auto-gate threshold (analysis domain, i.e. after ANALYSIS_GAIN):
 * -35 dB here is about -47 dBFS at the input. */
#define AUTO_GATE_CAP 0.0178f
#define HYST_SEMI     0.20f     /* quantizer stickiness: a note holds until another is 20 ct nearer (no hunting) */
#ifndef MPM_OCT_MARGIN
#define MPM_OCT_MARGIN 0.085f   /* fundamental bias: the lag twice the pick wins if its peak is this much higher.
                                 * Swept on synthetic voices: 0.085 removes octave-up errors on a vowel with F1 on
                                 * its 2nd harmonic (38 frames -> 0); above ~0.09 they come back; lower, strong creak
                                 * (alternate cycles 40% quieter) is read an octave down more often (9% -> 26% here) */
#endif
#define OCT_CONFIRM   3         /* hops a sudden octave jump must persist before it's believed */
#define RETUNE_MIN_MS 4.5f      /* Retune 0 = a 4.5 ms snap: hard-tune immediacy, but no ratio step */
#define LEN_MIN_SEC   0.008f    /* shortest grain: keeps crossfades smooth */
#define LEN_MAX       1500.0f

/* ------------------------------------------------------------- helpers */

float cher_hz_to_midi(float hz) { return 69.0f + 12.0f * log2f(hz / 440.0f); }
float cher_midi_to_hz(float m)  { return 440.0f * exp2f((m - 69.0f) / 12.0f); }

static const unsigned short k_scale_mask[] = {
    0xFFF,                                                   /* Chromatic */
    (1<<0)|(1<<2)|(1<<4)|(1<<5)|(1<<7)|(1<<9)|(1<<11),      /* Major */
    (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<10),      /* Natural minor */
    (1<<0)|(1<<2)|(1<<4)|(1<<7)|(1<<9),                      /* Major pentatonic */
    (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<9)|(1<<10),      /* Dorian */
};

static int note_allowed(int note, int root, int scale)
{
    int pc = ((note - root) % 12 + 12) % 12;
    return (k_scale_mask[scale] >> pc) & 1;
}

int cher_quantize(float midi, int root, int scale, int prev_note)
{
    if (scale < 0 || scale > VT_SCALE_DORIAN) scale = VT_SCALE_CHROMATIC;
    int centre = (int)lrintf(midi);
    int best = -1;
    float best_d = 1e9f;
    for (int n = centre - 4; n <= centre + 4; n++) {      /* widest scale gap is 3 */
        if (!note_allowed(n, root, scale)) continue;
        float d = fabsf(midi - (float)n);
        if (d < best_d) { best_d = d; best = n; }
    }
    if (best < 0) return centre;                          /* unreachable */
    if (prev_note >= 0 && prev_note != best && note_allowed(prev_note, root, scale) &&
        fabsf(midi - (float)prev_note) <= best_d + HYST_SEMI)
        return prev_note;
    return best;
}

/* ----------------------------------------------------------------- MPM */

int cher_mpm(const float *x, int n, float fs, float fmin, float fmax,
             float *nsdf, float *f0, float *clarity)
{
    int tau_min = (int)(fs / fmax);
    int tau_max = (int)(fs / fmin) + 1;
    if (tau_min < 2) tau_min = 2;
    if (tau_max > n / 2) tau_max = n / 2;
    if (tau_max > CHER_MAX_TAU) tau_max = CHER_MAX_TAU;
    if (tau_max <= tau_min + 2) return 0;

    /* Normalised square difference function. m(tau) updated recursively. */
    float m = 0.0f;
    for (int j = 0; j < n; j++) m += 2.0f * x[j] * x[j];
    for (int tau = 0; tau <= tau_max + 1; tau++) {
        if (tau > 0) m -= x[tau - 1] * x[tau - 1] + x[n - tau] * x[n - tau];
        float r = 0.0f;
        for (int j = 0; j < n - tau; j++) r += x[j] * x[j + tau];
        nsdf[tau] = m > 1e-9f ? 2.0f * r / m : 0.0f;
    }

    /* Skip the lobe around tau=0, then take the highest point of each
     * positive region as a candidate. */
    int i = 1;
    while (i <= tau_max && nsdf[i] > 0.0f) i++;
    if (i > tau_max) return 0;

    int cand[32], nc = 0;
    float vmax = 0.0f;
    while (i <= tau_max && nc < 32) {
        while (i <= tau_max && nsdf[i] <= 0.0f) i++;
        if (i > tau_max) break;
        int pk = i;
        while (i <= tau_max + 1 && nsdf[i] > 0.0f) { if (nsdf[i] > nsdf[pk]) pk = i; i++; }
        if (pk >= tau_min && pk <= tau_max) {
            cand[nc++] = pk;
            if (nsdf[pk] > vmax) vmax = nsdf[pk];
        }
    }
    if (nc == 0) return 0;

    int pick = cand[0];
    for (int k = 0; k < nc; k++)
        if (nsdf[cand[k]] >= MPM_K * vmax) { pick = cand[k]; break; }

    /* Fundamental bias: MPM takes the first strong peak, so a loud 2nd
     * harmonic (a vowel with F1 on it, chest voice) can win at half the true
     * period. Then the peak at twice the pick's lag is clearly the stronger
     * one: take it. "Nearly as strong" is not enough - on a clean note the
     * peak at twice the period is about as strong as the period's own, and
     * that rule (tried at 0.90-0.97) dropped clean notes an octave. One step
     * only: never two octaves. */
    for (int k = 0; k < nc; k++) {
        const int c2 = cand[k];
        if (c2 <= pick) continue;
        if (fabsf((float)c2 - 2.0f * (float)pick) <= 0.06f * 2.0f * (float)pick) {
            if (nsdf[c2] > nsdf[pick] + MPM_OCT_MARGIN && c2 + 1 <= tau_max + 1) pick = c2;
            break;
        }
        if ((float)c2 > 2.12f * (float)pick) break;
    }

    /* Parabolic interpolation around the peak for sub-sample lag. */
    float a = nsdf[pick - 1], b = nsdf[pick], c = nsdf[pick + 1];
    float den = a - 2.0f * b + c;
    float shift = fabsf(den) > 1e-12f ? 0.5f * (a - c) / den : 0.0f;
    float lag = (float)pick + shift;
    float peak = b - 0.25f * (a - c) * shift;
    if (lag <= 0.0f) return 0;

    *f0 = fs / lag;
    *clarity = peak;
    return 1;
}

/* ------------------------------------------------------------ engine */

void cher_init(cher_t *c, float sample_rate)
{
    memset(c, 0, sizeof(*c));
    c->sr = sample_rate;
    c->fs_dec = sample_rate * 0.5f;
    c->note = -1;
    c->len = c->len_target = LEN_MIN_SEC * sample_rate * 2.0f;
    c->bypass = 1.0f;
    c->hp_a = 1.0f / (1.0f + 2.0f * PI_F * 50.0f / sample_rate);
    c->scale = VT_SCALE_CHROMATIC;
    c->gate_db = -60.0f;
    c->override_note = -1;
    c->ov_pad = -1;
    c->glide_a = 1.0f;
    c->clarity_min = CLARITY_MIN;
    c->gate_lin = 0.001f;
    c->gate_offset = 1.0f;
    /* sub-windows of ~0.25 s worth of hops; start at -60 dB (analysis domain) */
    vt_floor_init(&c->floor, (int)(0.25f * sample_rate / CHER_HOP), 0.001f);
}

void cher_set_auto(cher_t *c, int on)
{
    c->auto_gate = on != 0;
}

void cher_set_clarity(cher_t *c, float clarity_min)
{
    c->clarity_min = clarity_min < 0.4f ? 0.4f : (clarity_min > 0.95f ? 0.95f : clarity_min);
}

float cher_read_abs(const cher_t *c, double index)
{
    double fl = floor(index);
    float frac = (float)(index - fl);
    long long i = (long long)fl;
    int i0 = (int)(i & (CHER_DELAY - 1));
    int i1 = (i0 + 1) & (CHER_DELAY - 1);
    return c->delay[i0] + frac * (c->delay[i1] - c->delay[i0]);
}

int cher_scale_step(int note, int steps, int root, int scale)
{
    if (scale < 0 || scale > VT_SCALE_DORIAN) scale = VT_SCALE_CHROMATIC;
    int dir = steps < 0 ? -1 : 1;
    for (int k = steps < 0 ? -steps : steps; k > 0; k--) {
        do note += dir; while (!note_allowed(note, root, scale));
    }
    return note;
}

/* Octaves to move a note d semitones from the voice by, folding only as far
 * as it takes to land within +-w. */
static int fold_into(float d, float w)
{
    int oct = 0;
    while (d + 12.0f * (float)oct > w) oct--;
    while (d + 12.0f * (float)oct < -w) oct++;
    return oct;
}

int cher_fold_octave(float ref, float centre, int prev_oct, int same)
{
    if (ref <= 0.0f) return same ? prev_oct : 0;
    /* From the voice's nearest semitone: a pad exactly an octave up stays an
     * octave up when the singer is a few cents flat. */
    const float d = centre - floorf(ref + 0.5f);
    /* Same pad still held: keep its octave while it stays valid for a window a
     * semitone either way, so a voice moving near the edge doesn't flip it. */
    if (same) {
        const int a = fold_into(d, CHER_FOLD_WINDOW + 1.0f), b = fold_into(d, CHER_FOLD_WINDOW - 1.0f);
        if (prev_oct >= (a < b ? a : b) && prev_oct <= (a > b ? a : b)) return prev_oct;
    }
    return fold_into(d, CHER_FOLD_WINDOW);
}

int cher_fold_chord(float ref, const int *in, int *out, int n, int prev_oct, int same)
{
    float sum = 0.0f;
    int k = 0;
    for (int i = 0; i < n; i++) if (in[i] >= 0) { sum += (float)in[i]; k++; }
    int oct = k > 0 ? cher_fold_octave(ref, sum / (float)k, prev_oct, same) : prev_oct;
    for (int i = 0; i < n; i++) {
        out[i] = in[i] < 0 ? -1 : in[i] + 12 * oct;
        while (in[i] >= 0 && out[i] < 0) out[i] += 12;
    }
    return oct;
}

void cher_set_params(cher_t *c, int root, int scale, float retune_ms, float gate_db,
                     int override_note, float glide_ms)
{
    const int pad = override_note;
    if (override_note >= 0) {
        c->ov_oct = cher_fold_octave(c->f0_midi, (float)override_note, c->ov_oct, override_note == c->ov_pad);
        override_note += 12 * c->ov_oct;
        while (override_note < 0) override_note += 12;     /* -1 means "no override" */
    }

    c->root = ((root % 12) + 12) % 12;
    c->scale = scale;
    c->retune_ms = retune_ms;
    if (gate_db != c->gate_db || c->gate_lin <= 0.0f) {
        c->gate_lin = powf(10.0f, gate_db / 20.0f);
        c->gate_offset = powf(10.0f, (gate_db + 60.0f) / 20.0f);
    }
    c->gate_db = gate_db;
    c->glide_a = glide_ms <= 0.0f ? 1.0f : 1.0f - expf(-1000.0f / (glide_ms * c->sr));

    /* The glide is kept as an offset from the target note, not an absolute
     * pitch: a float32 absolute slew stalls a few cents short of its goal. */
    if (override_note >= 0) {
        if (c->override_note < 0)                         /* override just started: glide from where we are */
            c->glide_off = c->voiced ? c->target_midi - (float)override_note : 0.0f;
        else if (override_note != c->override_note)       /* new note: keep the current pitch, retarget */
            c->glide_off += (float)(c->override_note - override_note);
    }
    c->override_note = override_note;
    c->ov_pad = pad;
    if (override_note >= 0 && !c->voiced) c->target_midi = (float)override_note;
}

float cher_latency(const cher_t *c)
{
    return 1.0f + 0.5f * c->len;
}

static float median3(float a, float b, float c)
{
    if (a > b) { float t = a; a = b; b = t; }
    if (b > c) b = c;
    return a > b ? a : b;
}

static void analyse(cher_t *c)
{
    /* Linearise newest CHER_WIN decimated samples. */
    int start = c->ring_pos - CHER_WIN;
    float energy = 0.0f;
    for (int i = 0; i < CHER_WIN; i++) {
        float v = c->ring[(start + i) & (CHER_RING - 1)];
        c->win[i] = v;
        energy += v * v;
    }

    /* Gate. Manual: the Gate knob is an RMS threshold. Auto: 8 dB over the
     * noise floor (capped so a long loud note can't close it), times the
     * Gate knob as an offset. Voicing itself is decided by MPM clarity, so
     * the auto gate only has to reject silence and room noise. */
    float rms = sqrtf(energy / CHER_WIN);
    float thr = c->gate_lin;
    if (c->auto_gate) {
        float fl = vt_floor_update(&c->floor, rms);
        thr = fminf(fl * 2.5f, AUTO_GATE_CAP) * c->gate_offset;
    }
    float f0, clarity;
    int ok = (rms > thr) &&
             cher_mpm(c->win, CHER_WIN, c->fs_dec, FMIN_HZ, FMAX_HZ, c->nsdf, &f0, &clarity) &&
             clarity >= c->clarity_min;

    if (ok) {
        float midi = cher_hz_to_midi(f0);

        /* Octave-glitch suppression: a jump of an octave (+-50 ct) from the
         * pitch being held, with no new onset (already voiced, level within
         * 6 dB of the last hop), is kept in the old octave until it has lasted
         * OCT_CONFIRM hops. Real octave leaps get through ~2 hops later;
         * one- and two-hop flips (a stray harmonic, a creaky cycle) never do. */
        if (c->voiced && c->hist_n >= 3 && c->last_rms > 0.0f) {
            const float d = midi - c->f0_midi;
            const int oct = fabsf(d - 12.0f) < 0.5f ? 1 : fabsf(d + 12.0f) < 0.5f ? -1 : 0;
            const float lv = rms / c->last_rms;
            if (oct != 0 && lv > 0.5f && lv < 2.0f) {
                if (++c->oct_run < OCT_CONFIRM) {
                    midi -= 12.0f * (float)oct;                                /* held in the old octave */
                } else {
                    /* It's real: take it, and move the median's history with
                     * it, or the two held hops outvote it straight back. */
                    for (int k = 0; k < c->hist_n; k++) c->midi_hist[k] += 12.0f * (float)oct;
                    c->oct_run = 0;
                }
            } else {
                c->oct_run = 0;
            }
        } else {
            c->oct_run = 0;
        }
        c->last_rms = rms;
        if (c->hist_n < 3) c->midi_hist[c->hist_n++] = midi;
        else { c->midi_hist[0] = c->midi_hist[1]; c->midi_hist[1] = c->midi_hist[2]; c->midi_hist[2] = midi; }
        float med = c->hist_n < 3 ? midi
                  : median3(c->midi_hist[0], c->midi_hist[1], c->midi_hist[2]);

        c->unvoiced_hops = 0;
        c->voiced = 1;
        c->f0_midi = med;
        c->f0 = cher_midi_to_hz(med);

        if (c->override_note >= 0) {
            c->note = c->override_note;                     /* target_midi is slewed per sample */
        } else {
            c->note = cher_quantize(med, c->root, c->scale, c->note);
            c->target_midi = (float)c->note;
        }
        c->target_hz = cher_midi_to_hz(c->target_midi);
        c->cents_err = (med - c->target_midi) * 100.0f;

        float t0 = c->sr / c->f0;                           /* period, input samples */
        float lmin = LEN_MIN_SEC * c->sr;
        float k = ceilf(lmin / (2.0f * t0));                /* grain = 2k periods */
        float len = 2.0f * k * t0;
        c->len_target = len > LEN_MAX ? LEN_MAX : len;
    } else if (++c->unvoiced_hops > HOLD_HOPS) {
        c->voiced = 0;
        c->f0 = 0.0f;
        c->hist_n = 0;
        c->note = -1;
        c->target_hz = 0.0f;
        c->cents_err = 0.0f;
    }
}

static float read_delay(const cher_t *c, float d)
{
    float pos = (float)c->wpos - d;
    float fl = floorf(pos);
    float frac = pos - fl;
    int i0 = (int)fl & (CHER_DELAY - 1);
    int i1 = (i0 + 1) & (CHER_DELAY - 1);
    return c->delay[i0] + frac * (c->delay[i1] - c->delay[i0]);
}

float cher_tap(const cher_t *c, float *phi, float lr)
{
    float ratio = exp2f(lr);
    *phi += (1.0f - ratio) / c->len;
    *phi -= floorf(*phi);
    float ph2 = *phi + 0.5f;
    ph2 -= floorf(ph2);
    float w1 = 0.5f - 0.5f * cosf(2.0f * PI_F * *phi);     /* w1 + w2 == 1 */
    return w1 * read_delay(c, 1.0f + *phi * c->len) + (1.0f - w1) * read_delay(c, 1.0f + ph2 * c->len);
}

float cher_plain(const cher_t *c)
{
    return read_delay(c, 1.0f + 0.5f * c->len);
}

static void feed(cher_t *c, float in, int analyse_on)
{
    /* --- analysis path only: high-pass away DC/rumble (the audible signal is untouched) --- */
    float an = in;
    for (int s = 0; s < 2; s++) {
        float y = c->hp_a * (c->hp_y[s] + an - c->hp_x[s]);
        c->hp_x[s] = an;
        c->hp_y[s] = fabsf(y) < 1e-15f ? 0.0f : y;
        an = c->hp_y[s];
    }

    /* --- decimate by 2 (binomial low-pass first) --- */
    float lp = (an + 4.0f * c->lp[0] + 6.0f * c->lp[1] + 4.0f * c->lp[2] + c->lp[3]) * (1.0f / 16.0f);
    c->lp[3] = c->lp[2]; c->lp[2] = c->lp[1]; c->lp[1] = c->lp[0]; c->lp[0] = an;
    if ((c->dec_phase ^= 1) == 0) {
        c->ring[c->ring_pos & (CHER_RING - 1)] = lp * ANALYSIS_GAIN;
        c->ring_pos++;
    }
    if (++c->hop_count >= CHER_HOP) { c->hop_count = 0; if (analyse_on) analyse(c); }

    /* --- shared delay line; grain length and voiced/unvoiced crossfade --- */
    c->delay[c->wpos & (CHER_DELAY - 1)] = in;
    c->wpos = (c->wpos + 1) & (CHER_DELAY - 1);
    c->n_written += 1.0;                                     /* wpos == n_written mod CHER_DELAY */
    c->len += (c->len_target - c->len) * 0.0005f;           /* slow: grain size changes bend pitch */
    c->bypass += ((c->voiced ? 0.0f : 1.0f) - c->bypass) * 0.004f;
    if (c->bypass < 1e-9f) c->bypass = 0.0f;
}

void cher_push(cher_t *c, float in)   { feed(c, in, 1); }
void cher_listen(cher_t *c, float in) { feed(c, in, 0); }

float cher_process(cher_t *c, float in)
{
    cher_push(c, in);
    float lr = cher_lead_lr(c);
    float shifted = cher_tap(c, &c->phi, lr);
    if (c->bypass < 0.001f) return shifted;
    return shifted + c->bypass * (cher_plain(c) - shifted);
}

float cher_lead_lr(cher_t *c)
{
    /* Retune: one-pole on log2 ratio, never faster than RETUNE_MIN_MS (Retune
     * 0 still sounds like a hard snap, without an instant ratio jump). */
    const float rt = c->retune_ms > RETUNE_MIN_MS ? c->retune_ms : RETUNE_MIN_MS;
    float a = c->retune_ms < 0.0f ? 1.0f : 1.0f - expf(-1000.0f / (rt * c->sr));   /* < 0: instant (Snap, Beauty 0) */
    if (c->override_note >= 0) {
        c->glide_off *= 1.0f - c->glide_a;
        if (fabsf(c->glide_off) < 1e-5f) c->glide_off = 0.0f;
        c->target_midi = (float)c->override_note + c->glide_off;
    }
    float lr_goal = 0.0f;
    if (c->voiced) {
        lr_goal = (c->target_midi - c->f0_midi) * (1.0f / 12.0f);   /* semitones -> log2 ratio */
        if (lr_goal > 2.0f) lr_goal = 2.0f;                         /* +-2 octaves max */
        if (lr_goal < -2.0f) lr_goal = -2.0f;
    }
    c->lr += (lr_goal - c->lr) * (c->voiced ? a : 0.01f);
    if (fabsf(c->lr) < 1e-9f) c->lr = 0.0f;                 /* no denormals in the decay */
    return c->lr;
}

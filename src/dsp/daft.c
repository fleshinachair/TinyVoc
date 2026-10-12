#include "daft.h"

#include <math.h>
#include <string.h>

#define PI_F          3.14159265358979f
#define TINY          1e-20f      /* DC bias that keeps filter state out of denormal range */
#define PULSE_WIDTH   0.3f
#define MAKEUP        5.66f       /* band-splitting costs ~18-29 dB; was 8 (calibrated on tone/noise/vowel), -3 dB for headroom */
#define CARRIER_GAIN  0.60f       /* +6 dB on the original 0.30: drive the bank harder */

/* Output leveller: a vocoder's crest factor is huge (measured ~17 dB on a sung
 * phrase), so a fixed makeup gain just drives its peaks into the limiter. This
 * lifts the AVERAGE toward LEV_TARGET (up to +15 dB) and pulls the gain back
 * fast whenever a peak would pass LEV_CEIL. */
#define LEV_TARGET    0.1416f     /* RMS, about -17 dBFS (-3 dB for headroom) */
#define LEV_CEIL      0.89f       /* peaks, about -1 dBFS */
#define LEV_MAX_GAIN  5.62f       /* +15 dB */
#define NOISE_GAIN    1.8f        /* +6 dB with the carrier, so consonants keep their balance against it */
#define HP_FC         2500.0f     /* consonant detector: low enough for a 'k' burst (1.5-4 kHz), above a vowel's F2 */

/* time constants, seconds */
#define ENV_ATTACK    0.0015f
#define ENV_RELEASE   0.015f      /* band envelopes: syllables at 8/s keep ~21 dB gaps (25 ms gave ~13) */
#define HF_ATTACK     0.001f
#define HF_RELEASE    0.02f
#define LVL_ATTACK    0.002f
#define LVL_RELEASE   0.04f
#define AMP_ATTACK    0.003f
#define AMP_RELEASE   0.08f
#define GATE_SLEW     0.003f
#define AGC_ATTACK    0.005f
#define AGC_RELEASE   1.5f
#define AGC_WARM_SEC  0.020f    /* a cold follower first runs 1 ms both ways for 20 ms */
#define AGC_WARM_TAU  0.001f
#define SPEECH_ATTACK 0.02f
#define SPEECH_RELEASE 0.4f

/* Input AGC: the modulator is scaled so its level sits near AGC_TARGET
 * (peak-equivalent, what MAKEUP was calibrated at), within these limits. */
#define AGC_TARGET    0.25f
#define AGC_MAX_GAIN  31.6f       /* +30 dB for soft speech */
#define AGC_MIN_GAIN  0.25f       /* -12 dB for shouting */
/* A hiss counts as a consonant once its high-band energy is this fraction of
 * the running speech energy (relative, so it holds at any input level). */
#define SIB_REL       0.15f
/* ...and once the hiss is this share of hiss + low bands (smoothstep up to
 * 0.8). Sustained vowels, bright and breathy "ee" included, stay well under. */
#define SIB_RATIO_LO  0.40f
#define AUTO_GATE_CAP 0.003f      /* -50 dBFS: highest threshold the adaptive gate will use */

/* The analysis floor gate: when the raw input level (peak-equivalent, as the Gate knob reads it) drops below
 * AN_FLOOR_CLOSE every band envelope is clamped to exactly 0, and it opens again only above AN_FLOOR_OPEN (3 dB of
 * hysteresis), so a noise floor hovering at the line cannot flutter the followers. A manual Gate set lower than the
 * floor wins (its owner asked to hear quieter than that). */
#define AN_FLOOR_CLOSE 0.00398f   /* -48 dBFS */
#define AN_FLOOR_OPEN  0.00562f   /* -45 dBFS */
/* Bands whose synthesis centre is above this release faster (the follower's tail was what rang on after speech). */
#define HI_BAND_HZ     3500.0f
#define HI_ENV_RELEASE 0.005f
#define DC_BLOCK_HZ    15.0f      /* the carrier's DC blocker */
#define RETRIGGER_SEC  0.001f     /* a re-triggered voice: 0.5 ms fade out, 0.5 ms fade in */

/* Flush tiny values so decaying smoothers never enter the denormal range. */
static inline float flush(float x) { return fabsf(x) < 1e-15f ? 0.0f : x; }

static float coef_1pole(float tau_s, float sr) { return 1.0f - expf(-1.0f / (tau_s * sr)); }

static float daft_midi_hz(int note) { return 440.0f * exp2f(((float)note - 69.0f) / 12.0f); }

/* ----------------------------------------------------------- biquads */

static inline float bq_bandpass(const daft_bq *c, daft_bqs *s, float x)
{
    float y = c->b0 * x + s->z1;                  /* transposed direct form II, b1 = 0 */
    s->z1 = s->z2 - c->a1 * y;
    s->z2 = -c->b0 * x - c->a2 * y;               /* b2 = -b0 */
    return y;
}

static inline float bq_highpass(const daft_bq *c, daft_bqs *s, float x)
{
    float y = c->b0 * x + s->z1;                  /* b1 = -2*b0, b2 = b0 */
    s->z1 = -2.0f * c->b0 * x + s->z2 - c->a1 * y;
    s->z2 = c->b0 * x - c->a2 * y;
    return y;
}

/* RBJ constant-peak-gain bandpass. */
static daft_bq design_bandpass(float fc, float q, float sr)
{
    float w = 2.0f * PI_F * fc / sr;
    float alpha = sinf(w) / (2.0f * q);
    float a0 = 1.0f + alpha;
    daft_bq c = { alpha / a0, 0.0f, -2.0f * cosf(w) / a0, (1.0f - alpha) / a0 };
    c.b2 = -c.b0;
    return c;
}

static daft_bq design_highpass(float fc, float q, float sr)
{
    float w = 2.0f * PI_F * fc / sr;
    float alpha = sinf(w) / (2.0f * q);
    float a0 = 1.0f + alpha;
    daft_bq c = { (1.0f + cosf(w)) * 0.5f / a0, 0.0f, -2.0f * cosf(w) / a0, (1.0f - alpha) / a0 };
    c.b2 = c.b0;
    return c;
}

static void design_bank(daft_t *d)
{
    /* Log-spaced edges; Q chosen so neighbouring bands cross at about -3 dB
     * (before the width scaling). Two cascaded sections narrow the total
     * bandwidth by 0.644, so each section is designed 1/0.644 wider. */
    float r = powf(DAFT_FMAX / DAFT_FMIN, 1.0f / DAFT_BANDS);
    float q0 = sqrtf(r) / (r - 1.0f);
    float qscale = exp2f(1.0f - 2.0f * d->width);             /* width 0 -> x2, 0.5 -> x1, 1 -> x0.5 */
    /* Formant: the synthesis bank's centres move by +-12 semitones while the
     * analysis bank stays put, so band i's envelope now shapes the carrier at
     * a higher/lower frequency - a smaller (child) or bigger (monster) tract. */
    float fratio = exp2f(d->formant / 12.0f);
    for (int i = 0; i < DAFT_BANDS; i++) {
        float lo = DAFT_FMIN * powf(r, (float)i);
        d->centre[i] = lo * sqrtf(r);
        d->bq[i] = design_bandpass(d->centre[i], 0.644f * q0 * qscale, d->sr);
        float fs = d->centre[i] * fratio;
        if (fs > 0.45f * d->sr) fs = 0.45f * d->sr;
        d->bq_sy[i] = design_bandpass(fs, 0.644f * q0 * qscale, d->sr);
        d->k_rel[i] = fs > HI_BAND_HZ ? d->k_env_r_hi : d->k_env_r;       /* the high synthesis bands let go faster */
    }
    d->last_width = d->width;
    d->last_formant = d->formant;
}

/* -------------------------------------------------------------- init */

void daft_init(daft_t *d, float sr)
{
    memset(d, 0, sizeof(*d));
    d->sr = sr;
    d->width = 0.5f;
    d->detune_ratio = 1.0f;
    d->glide_a = 1.0f;
    d->gate_thr = 0.001f;
    d->gate_gain = 1.0f;
    d->voice_present = 1;
    d->k_car_lp = 1.0f - expf(-2.0f * PI_F * 11000.0f / sr);
    d->air_a = expf(-2.0f * PI_F * 5500.0f / sr);
    d->gate_open = 1;
    d->rng = 0x1234567u;
    d->k_env_a = coef_1pole(ENV_ATTACK, sr);   d->k_env_r = coef_1pole(ENV_RELEASE, sr);
    d->k_env_r_hi = coef_1pole(HI_ENV_RELEASE, sr);
    d->k_rt = 1.0f / (0.5f * RETRIGGER_SEC * sr);          /* each half of a re-trigger takes 0.5 ms */
    d->dc_r = 1.0f - 2.0f * PI_F * DC_BLOCK_HZ / sr;
    d->an_open = 1;
    d->k_hf_a  = coef_1pole(HF_ATTACK, sr);    d->k_hf_r  = coef_1pole(HF_RELEASE, sr);
    d->k_lvl_a = coef_1pole(LVL_ATTACK, sr);   d->k_lvl_r = coef_1pole(LVL_RELEASE, sr);
    d->k_amp_a = coef_1pole(AMP_ATTACK, sr);   d->k_amp_r = coef_1pole(AMP_RELEASE, sr);
    d->k_gate  = coef_1pole(GATE_SLEW, sr);    d->k_sib   = coef_1pole(0.002f, sr);
    d->k_agc_a = coef_1pole(AGC_ATTACK, sr);   d->k_agc_r = coef_1pole(AGC_RELEASE, sr);
    d->k_sp_a  = coef_1pole(SPEECH_ATTACK, sr); d->k_sp_r = coef_1pole(SPEECH_RELEASE, sr);
    d->k_agc_warm = coef_1pole(AGC_WARM_TAU, sr);
    d->agc_warm = (int)(AGC_WARM_SEC * sr);
    d->agc_env = AGC_TARGET;        /* cold: unity gain, not +30 dB, until it has heard a voice */
    d->agc_gain = 1.0f;
    d->gate_offset = 1.0f;
    vt_floor_init(&d->floor, (int)(0.25f * sr), 0.001f);     /* 0.25 s sub-windows, start at -60 dB */
    d->auto_mode = 1;
    d->k_ens_lp = coef_1pole(1.0f / (2.0f * PI_F * 7000.0f), sr);
    d->k_lev_rms = coef_1pole(0.30f, sr);       /* program level, 300 ms */
    d->k_lev_pk_r = coef_1pole(0.15f, sr);      /* peak memory release */
    d->k_lev_up = coef_1pole(0.20f, sr);        /* gain rises over 200 ms */
    d->k_lev_down = coef_1pole(0.001f, sr);     /* and drops within ~1 ms */
    d->lev_gain = 1.0f;
    d->lev_target = LEV_TARGET;
    d->lev_ceil = LEV_CEIL;
    d->duty = PULSE_WIDTH;
    design_bank(d);
    d->hp[0] = design_highpass(HP_FC, 0.7071f, sr);
    d->hp[1] = d->hp[0];
    for (int i = 0; i < DAFT_BANDS; i++) d->tilt_gain[i] = 1.0f;
    for (int i = 0; i < DAFT_VOICES; i++) d->v[i].note = -1;

    d->drone_enabled = DAFT_DRONE_DEFAULT;
    static const int drone_notes[DAFT_DRONE_VOICES] = { 48, 55, 60 };   /* C3, G3, C4 */
    for (int i = 0; i < DAFT_DRONE_VOICES; i++) {
        daft_voice *v = &d->v[DAFT_VOICES + i];
        v->note = drone_notes[i];
        v->lf = v->target_lf = log2f(daft_midi_hz(drone_notes[i]));
    }
}

void daft_set_params(daft_t *d, int pulse, float detune_cents, float glide_ms,
                     float width, float tilt, float sibilance, float formant_st,
                     float gate_db)
{
    d->pulse = pulse;
    d->detune_ratio = exp2f(0.5f * detune_cents / 1200.0f);
    d->glide_a = glide_ms <= 0.0f ? 1.0f : 1.0f - expf(-1000.0f / (glide_ms * d->sr));
    d->sibilance = sibilance;
    d->formant = formant_st;
    d->gate_thr = powf(10.0f, gate_db / 20.0f);
    d->gate_offset = d->gate_thr * 1000.0f;                   /* 1.0 at the -60 dB default */

    if (formant_st != d->last_formant && width == d->width) design_bank(d);
    if (width != d->width || tilt != d->tilt) {
        d->width = width;
        d->tilt = tilt;
        if (width != d->last_width || d->formant != d->last_formant) design_bank(d);
        for (int i = 0; i < DAFT_BANDS; i++)                  /* +-9 dB across the range at |tilt| = 1 */
            d->tilt_gain[i] = powf(10.0f, tilt * 9.0f * ((float)i - 7.5f) / 7.5f / 20.0f);
    }
}

void daft_set_beauty(daft_t *d, float beauty)
{
    d->beauty = beauty < 0.0f ? 0.0f : (beauty > 1.0f ? 1.0f : beauty);
}

void daft_set_voice_present(daft_t *d, int present)
{
    d->voice_present = present != 0;
}

void daft_set_auto(daft_t *d, int on)
{
    d->auto_mode = on != 0;
    /* The drone carries the voice whenever no pad is held, Auto or not: a
     * vocoder with no carrier is silence, never what anyone wants. */
    d->drone_enabled = DAFT_DRONE_DEFAULT;
}

void daft_set_drone_root(daft_t *d, int root)
{
    /* Root, fifth and octave, rooted between F#2 and F3 so the drone stays in
     * the C3 drone's register whatever the key (every scale TinyVoice offers
     * has the perfect fifth, so the scale doesn't change the chord). */
    root = ((root % 12) + 12) % 12;
    const int base = 48 + (root >= 6 ? root - 12 : root);
    const int notes[DAFT_DRONE_VOICES] = { base, base + 7, base + 12 };
    for (int i = 0; i < DAFT_DRONE_VOICES; i++) {
        daft_voice *v = &d->v[DAFT_VOICES + i];
        if (v->note == notes[i]) continue;
        v->note = notes[i];
        v->target_lf = log2f(daft_midi_hz(notes[i]));
        if (v->amp < 1e-3f) v->lf = v->target_lf;            /* silent: no glide from the old key */
    }
}

void daft_set_voice(daft_t *d, int slot, int note, int velocity)
{
    if (slot < 0 || slot >= DAFT_VOICES) return;
    daft_voice *v = &d->v[slot];
    if (note >= 0) {
        float tlf = log2f(daft_midi_hz(note));
        int retrigger = 0;
        if (!v->gate && v->amp < 1e-3f) {                     /* fresh voice: no glide from a stale pitch */
            v->lf = tlf;                                      /* the oscillator phases are NOT reset: they free-run across note-ons */
            v->rt = 0;
        } else if (note != v->note && d->glide_a >= 1.0f) {
            retrigger = 1;                                    /* a sounding voice given another note, with no glide to soften it */
        }
        if (retrigger) {
            /* Not a pitch jump: fade out (0.5 ms), take the new note at the bottom, fade in (0.5 ms). A new note
             * that lands mid-fade starts from where the gain is now. */
            v->pend_note = note;
            v->pend_lf = tlf;
            if (v->rt == 0) { v->rt = 1; v->rt_pos = 0.0f; }
            else if (v->rt == 2 && v->note != note) { v->rt = 1; v->rt_pos = 1.0f - v->rt_pos; }
        } else if (v->rt == 0) {
            v->note = note;
            v->target_lf = tlf;
        }
        v->amp_target = (float)velocity * (1.0f / 127.0f);
        v->gate = 1;
    } else {
        v->gate = 0;
        v->amp_target = 0.0f;
    }
}

/* ---------------------------------------------------------- carrier */

static inline float polyblep(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

static inline float osc(float *ph, float dt, int pulse, float width)
{
    float t = *ph;
    float y;
    if (!pulse) {
        y = 2.0f * t - 1.0f - polyblep(t, dt);
    } else {
        float t2 = t + 1.0f - width;
        t2 -= floorf(t2);
        y = (t < width ? 1.0f : -1.0f) + polyblep(t, dt) - polyblep(t2, dt)
            - (2.0f * width - 1.0f);
    }
    t += dt;
    t -= floorf(t);
    *ph = t;
    return y;
}

static float carrier(daft_t *d)
{
    float sum = 0.0f, amp_sum = 0.0f;
    const float width = d->talker ? d->duty : PULSE_WIDTH;
    const float a_att = d->k_amp_a, a_rel = d->k_amp_r;

    /* The drone is on only while no MIDI key is held. */
    int held = 0;
    for (int i = 0; i < DAFT_VOICES; i++) held |= d->v[i].gate;
    const int drone_on = d->drone_enabled && !held;
    for (int i = DAFT_VOICES; i < DAFT_VOICES + DAFT_DRONE_VOICES; i++) {
        d->v[i].gate = drone_on;
        d->v[i].amp_target = drone_on ? 0.6f : 0.0f;
    }

    for (int i = 0; i < DAFT_VOICES + DAFT_DRONE_VOICES; i++) {
        daft_voice *v = &d->v[i];
        if (!v->gate && v->amp < 1e-4f) { v->amp = 0.0f; continue; }

        v->lf += (v->target_lf - v->lf) * d->glide_a;
        v->amp += (v->amp_target - v->amp) * (v->amp_target > v->amp ? a_att : a_rel);
        float va = v->amp;
        if (v->rt) {                                          /* a re-trigger's dip: raised-cosine out, new note, raised-cosine in */
            v->rt_pos += d->k_rt;
            if (v->rt == 1) {
                if (v->rt_pos >= 1.0f) {
                    v->note = v->pend_note;
                    v->lf = v->target_lf = v->pend_lf;
                    v->rt = 2;
                    v->rt_pos = 0.0f;
                    va = 0.0f;
                } else {
                    va *= 0.5f * (1.0f + cosf(PI_F * v->rt_pos));
                }
            } else if (v->rt_pos >= 1.0f) {
                v->rt = 0;
            } else {
                va *= 0.5f * (1.0f - cosf(PI_F * v->rt_pos));
            }
        }

        float f = exp2f(v->lf);
        float dt1 = f * d->detune_ratio / d->sr, dt2 = f / d->detune_ratio / d->sr;
        if (dt1 > 0.45f) dt1 = 0.45f;
        if (dt2 > 0.45f) dt2 = 0.45f;
        sum += va * (osc(&v->ph[0], dt1, d->pulse, width) + osc(&v->ph[1], dt2, d->pulse, width));
        amp_sum += va;
        if (d->beauty > 0.0f) {                              /* Beauty: sub-octave warmth, -10 dB at full */
            sum += va * 0.37f * d->beauty * sinf(2.0f * PI_F * v->ph_sub);
            v->ph_sub += 0.5f * f / d->sr;
            v->ph_sub -= floorf(v->ph_sub);
        }
    }
    /* Talker: white noise at `rasp` of the carrier's level. A 0.18-duty pulse
     * pair has an RMS of about 1.5 per unit amplitude, uniform noise 0.577. */
    if (d->talker && d->rasp > 0.0f) {
        d->rng = d->rng * 1664525u + 1013904223u;
        const float n = (float)(int)(d->rng >> 8) * (1.0f / 8388608.0f) - 1.0f;
        sum += n * d->rasp * 2.67f * amp_sum;
    }
    /* Beauty: blend toward an 11 kHz low-pass (the saws' fizz) */
    d->car_lp = flush(d->car_lp + (sum - d->car_lp) * d->k_car_lp);
    sum += d->beauty * (d->car_lp - sum);
    /* A 15 Hz DC blocker on the carrier (y = x - x1 + R y1): a pulse's duty, the noise and the sub-octave leave a
     * little offset, and a note that starts or stops would step it. */
    const float dc = sum - d->dc_x + d->dc_r * d->dc_y;
    d->dc_x = sum;
    d->dc_y = flush(dc);
    return dc * CARRIER_GAIN;
}

/* ---------------------------------------------------------- talker EQ */

static inline float bqf_tick(const daft_bqf *c, daft_bqs *s, float x)
{
    const float y = c->b0 * x + s->z1;                 /* transposed direct form II */
    s->z1 = flush(c->b1 * x - c->a1 * y + s->z2);
    s->z2 = flush(c->b2 * x - c->a2 * y);
    return y;
}

static daft_bqf bqf_highpass(float fc, float q, float sr)
{
    const float w = 2.0f * PI_F * fc / sr, cs = cosf(w), al = sinf(w) / (2.0f * q), a0 = 1.0f + al;
    daft_bqf c = { (1.0f + cs) * 0.5f / a0, -(1.0f + cs) / a0, (1.0f + cs) * 0.5f / a0, -2.0f * cs / a0, (1.0f - al) / a0 };
    return c;
}

static daft_bqf bqf_peak(float fc, float q, float db, float sr)
{
    const float A = powf(10.0f, db / 40.0f), w = 2.0f * PI_F * fc / sr, cs = cosf(w), al = sinf(w) / (2.0f * q), a0 = 1.0f + al / A;
    daft_bqf c = { (1.0f + al * A) / a0, -2.0f * cs / a0, (1.0f - al * A) / a0, -2.0f * cs / a0, (1.0f - al / A) / a0 };
    return c;
}

static daft_bqf bqf_highshelf(float fc, float db, float sr)      /* shelf slope 1 */
{
    const float A = powf(10.0f, db / 40.0f), w = 2.0f * PI_F * fc / sr, cs = cosf(w);
    const float al = sinf(w) * 0.5f * sqrtf(2.0f), sa = 2.0f * sqrtf(A) * al;
    const float a0 = (A + 1.0f) - (A - 1.0f) * cs + sa;
    daft_bqf c = { A * ((A + 1.0f) + (A - 1.0f) * cs + sa) / a0, -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cs) / a0,
                   A * ((A + 1.0f) + (A - 1.0f) * cs - sa) / a0, 2.0f * ((A - 1.0f) - (A + 1.0f) * cs) / a0,
                   ((A + 1.0f) - (A - 1.0f) * cs - sa) / a0 };
    return c;
}

void daft_set_talker(daft_t *d, int on, float duty, float rasp, float bleed, float cavity_db)
{
    const int was = d->talker;
    d->talker = on != 0;
    if (!d->talker) {
        if (was) { d->lev_target = LEV_TARGET; d->lev_ceil = LEV_CEIL; }
        return;
    }
    d->duty = duty < 0.10f ? 0.10f : (duty > 0.30f ? 0.30f : duty);
    d->rasp = rasp < 0.0f ? 0.0f : rasp;
    d->bleed = bleed < 0.0f ? 0.0f : bleed;
    d->lev_target = LEV_TARGET * 0.4f;                 /* -8 dB: the headroom stage keeps -6.1 dBFS */
    d->lev_ceil = LEV_CEIL * 0.45f;
    d->tk_air_a = expf(-2.0f * PI_F * 3500.0f / d->sr);
    if (!was) {
        d->tk_eq[0] = bqf_highpass(250.0f, 0.5412f, d->sr);      /* steep low-cut: 4th-order Butterworth */
        d->tk_eq[1] = bqf_highpass(250.0f, 1.3066f, d->sr);
        d->tk_eq[3] = bqf_highshelf(3500.0f, 6.0f, d->sr);       /* crisp consonants */
        d->last_cav_db = -1000.0f;
        memset(d->tk_z, 0, sizeof d->tk_z);
    }
    if (cavity_db != d->last_cav_db) {                 /* the oral cavity: a peak near 1.2 kHz */
        d->cav_db = cavity_db;
        d->last_cav_db = cavity_db;
        d->tk_eq[2] = bqf_peak(1200.0f, 1.4f, cavity_db, d->sr);
    }
}

float daft_carrier_sample(daft_t *d) { return carrier(d); }

/* ---------------------------------------------------------- process */

void daft_process_lr(daft_t *d, float mod, float *out_l, float *out_r)
{
    const float a_att = d->k_env_a;
    const float raw = mod + TINY;

    /* Gate on the RAW input level, 3 dB hysteresis. It runs before the AGC so
     * the Gate knob means input level and the AGC can never boost the noise
     * floor into opening it. */
    {
        float a = fabsf(raw) * (PI_F * 0.5f);
        d->lvl_env = flush(d->lvl_env + (a - d->lvl_env) * (a > d->lvl_env ? d->k_lvl_a : d->k_lvl_r));
        float thr = d->gate_thr;
        if (d->auto_mode) {
            /* 15 dB over the noise floor (the floor is the envelope's minimum,
             * and a noise envelope wanders well above it), capped at -50 dBFS
             * so a long steady note can't drag the floor up and close the gate
             * on itself. */
            float fl = vt_floor_update(&d->floor, d->lvl_env);
            thr = fminf(fl * 5.6f, AUTO_GATE_CAP) * d->gate_offset;
        }
        if (d->gate_open && d->lvl_env < thr * 0.707f) d->gate_open = 0;
        else if (!d->gate_open && d->lvl_env > thr && d->voice_present) d->gate_open = 1;
        if (!d->voice_present) d->gate_open = 0;                   /* the caller's gate (Auto) */
        /* The analysis floor: below -48 dBFS the band envelopes are held at exactly 0 until the level comes back
         * above -45 dBFS. (A manual Gate lower than that moves the floor down with it.) */
        {
            const float f_close = d->auto_mode ? AN_FLOOR_CLOSE : fminf(AN_FLOOR_CLOSE, d->gate_thr);
            const float f_open  = d->auto_mode ? AN_FLOOR_OPEN  : fminf(AN_FLOOR_OPEN, d->gate_thr * 1.41f);
            if (d->an_open && d->lvl_env < f_close) d->an_open = 0;
            else if (!d->an_open && d->lvl_env > f_open) d->an_open = 1;
        }
        d->gate_gain = flush(d->gate_gain + ((d->gate_open ? 1.0f : 0.0f) - d->gate_gain) * d->k_gate);
        if (!d->gate_open && d->gate_gain < 1e-4f) d->gate_gain = 0.0f;   /* fully shut: exact zero */
    }

    /* Input AGC: fast-attack / slow-release level follower, then scale the
     * modulator toward AGC_TARGET. Soft speech gets up to +30 dB, loud speech
     * is pulled down, so the band envelopes (and the output) stay articulate. */
    /* The follower only listens while the gate is open: a pause leaves it on
     * the last phrase's level (decaying through the pause, the next phrase
     * started at +30 dB for its first milliseconds). A cold follower starts
     * at unity gain and, for the first 20 ms of voice it ever hears, settles
     * at 1 ms both ways (creeping up from zero gave +30 dB: a burst into the
     * limiter on the first switch to Robot). */
    if (d->gate_open) {
        float a = fabsf(raw) * (PI_F * 0.5f);
        float k = a > d->agc_env ? d->k_agc_a : d->k_agc_r;
        if (d->agc_warm > 0) { d->agc_warm--; k = d->k_agc_warm; }
        d->agc_env = flush(d->agc_env + (a - d->agc_env) * k);
        float g = AGC_TARGET / fmaxf(d->agc_env, AGC_TARGET / AGC_MAX_GAIN);
        d->agc_gain = !d->auto_mode ? 1.0f : g < AGC_MIN_GAIN ? AGC_MIN_GAIN : g;
    } else if (!d->auto_mode) {
        d->agc_gain = 1.0f;
    }
    const float x = raw * d->agc_gain;

    /* Analysis: band envelopes. |bandpass| averages 2/pi of its peak, so *pi/2
     * gives a peak-equivalent level. */
    float total = 0.0f;
    for (int i = 0; i < DAFT_BANDS; i++) {
        float y = bq_bandpass(&d->bq[i], &d->an[i][0], x);
        y = bq_bandpass(&d->bq[i], &d->an[i][1], y);
        float a = fabsf(y) * (PI_F * 0.5f);
        d->env[i] = d->an_open ? flush(d->env[i] + (a - d->env[i]) * (a > d->env[i] ? a_att : d->k_rel[i])) : 0.0f;
        total += d->env[i];
    }

    /* Consonant detector, adaptive: the hiss must dominate the spectrum
     * (high-band vs low-band ratio) AND be a real share of the running speech
     * energy, rather than clear a fixed absolute level. */
    {
        float h = bq_highpass(&d->hp[0], &d->hps[0], x);
        h = bq_highpass(&d->hp[1], &d->hps[1], h);
        float a = fabsf(h) * (PI_F * 0.5f);
        d->hf_env = d->an_open ? flush(d->hf_env + (a - d->hf_env) * (a > d->hf_env ? d->k_hf_a : d->k_hf_r)) : 0.0f;

        float e = total + d->hf_env;
        d->speech_env = flush(d->speech_env + (e - d->speech_env) * (e > d->speech_env ? d->k_sp_a : d->k_sp_r));

        float low = 0.0f;
        for (int i = 0; i < 10; i++) low += d->env[i];
        float ratio = d->hf_env / (d->hf_env + low + 1e-6f);
        float t = (ratio - SIB_RATIO_LO) / (0.8f - SIB_RATIO_LO);
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        float amount = t * t * (3.0f - 2.0f * t);                 /* smoothstep */
        float level = d->hf_env / (SIB_REL * d->speech_env + 1e-6f);
        if (level > 1.0f) level = 1.0f;
        d->sib = flush(d->sib + (amount * level - d->sib) * d->k_sib);
        if (!d->gate_open || !d->an_open) d->sib = 0.0f;           /* the consonant noise is muted the instant the gate shuts */
    }

    /* Carrier + consonant noise. */
    d->rng = d->rng * 1664525u + 1013904223u;
    float noise = (float)(int)(d->rng >> 8) * (1.0f / 8388608.0f) - 1.0f;
    float cin = carrier(d) + noise * d->sib * d->sibilance * NOISE_GAIN + TINY;

    /* Synthesis: band i's envelope shapes the carrier through the (formant-
     * shifted) synthesis filter i. Odd bands lean left and even bands right by
     * pan_spread: at 0 both channels carry every band (mono). */
    float l = 0.0f, r = 0.0f;
    const float other = 1.0f - d->pan_spread;
    for (int i = 0; i < DAFT_BANDS; i++) {
        float g = d->env[i] * d->tilt_gain[i];
        float y = bq_bandpass(&d->bq_sy[i], &d->sy[i][0], cin);
        y = bq_bandpass(&d->bq_sy[i], &d->sy[i][1], y) * g;
        if (i & 1) { l += y; r += y * other; }               /* bands 1, 3, 5... (odd) */
        else       { r += y; l += y * other; }               /* bands 0, 2, 4... (even) */
    }
    l *= MAKEUP * d->gate_gain;
    r *= MAKEUP * d->gate_gain;

    /* Beauty: the voice's own air (> 5.5 kHz, two one-pole high-passes) bled
     * in under the vocoder, through the same gate, so pauses stay silent */
    {
        const float aa = d->talker ? d->tk_air_a : d->air_a;
        float y1 = aa * (d->air_z[1] + x - d->air_z[0]);
        float y2 = aa * (d->air_z[2] + y1 - d->air_z[1]);
        d->air_z[0] = x;
        d->air_z[1] = flush(y1);
        d->air_z[2] = flush(y2);
        float air = 0.5f * d->beauty * y2 * d->gate_gain;
        if (d->talker) air = d->bleed * d->sib * y2 * d->gate_gain;   /* the unvoiced breath, not the whole top */
        l += air;
        r += air;
    }
    /* Talker: low-cut, cavity peak, high shelf, per channel. */
    if (d->talker) {
        for (int k = 0; k < 4; k++) {
            l = bqf_tick(&d->tk_eq[k], &d->tk_z[0][k], l);
            r = bqf_tick(&d->tk_eq[k], &d->tk_z[1][k], r);
        }
    }

    /* Ensemble (VP-330 style): three BBD-like taps on the mono vocoder,
     * modulated by a slow and a fast LFO with the taps 120 degrees apart, mixed
     * in stereo. Dark feed (7 kHz) like the bucket brigades it imitates. */
    if (d->ens_depth > 0.0f) {
        float mono = 0.5f * (l + r);
        d->ens_lp = flush(d->ens_lp + (mono - d->ens_lp) * d->k_ens_lp);
        d->ens_buf[d->ens_w] = d->ens_lp;
        d->ens_w = (d->ens_w + 1) & (DAFT_ENS_LEN - 1);
        d->ens_ph[0] += 0.62f / d->sr; if (d->ens_ph[0] >= 1.0f) d->ens_ph[0] -= 1.0f;
        d->ens_ph[1] += 6.1f / d->sr;  if (d->ens_ph[1] >= 1.0f) d->ens_ph[1] -= 1.0f;
        float tap[3];
        for (int k = 0; k < 3; k++) {
            float off = (float)k * (1.0f / 3.0f);
            float ms = 7.0f + 2.0f * sinf(2.0f * PI_F * (d->ens_ph[0] + off))
                            + 0.3f * sinf(2.0f * PI_F * (d->ens_ph[1] + off));
            float pos = (float)d->ens_w - ms * 0.001f * d->sr;
            float fl = floorf(pos), fr = pos - fl;
            int i0 = (int)fl & (DAFT_ENS_LEN - 1), i1 = (i0 + 1) & (DAFT_ENS_LEN - 1);
            tap[k] = d->ens_buf[i0] + fr * (d->ens_buf[i1] - d->ens_buf[i0]);
        }
        float m = d->ens_depth;
        l = l * (1.0f - 0.5f * m) + m * 0.67f * (tap[0] + 0.5f * tap[1]);
        r = r * (1.0f - 0.5f * m) + m * 0.67f * (tap[2] + 0.5f * tap[1]);
    }

    /* Output leveller (see LEV_*): makeup toward the target level, with the
     * gain pulled down at once if the current peak would pass the ceiling.
     * Held while the gate is shut: in a pause the output is silent, so the
     * leveller climbed to full makeup and every phrase opened ~+7 dB hot into
     * the limiter. */
    if (d->gate_open) {
        float m = 0.5f * (l + r), am = fmaxf(fabsf(l), fabsf(r));
        d->lev_ms = flush(d->lev_ms + (m * m - d->lev_ms) * d->k_lev_rms);
        d->lev_pk = am > d->lev_pk ? am : flush(d->lev_pk + (am - d->lev_pk) * d->k_lev_pk_r);
        float rms = sqrtf(d->lev_ms);
        float goal = rms > 1e-5f ? d->lev_target / rms : LEV_MAX_GAIN;
        if (goal > LEV_MAX_GAIN) goal = LEV_MAX_GAIN;
        if (goal < 1.0f) goal = 1.0f;                       /* a leveller here, never a cut */
        if (d->lev_pk > 1e-6f && goal * d->lev_pk > d->lev_ceil) goal = fmaxf(d->lev_ceil / d->lev_pk, 0.25f);
        d->lev_gain += (goal - d->lev_gain) * (goal < d->lev_gain ? d->k_lev_down : d->k_lev_up);
    }
    l *= d->lev_gain;
    r *= d->lev_gain;

    /* Gentle limiter so chords + hot input can't run away. */
    float al = fabsf(l), ar = fabsf(r);
    if (al > 0.8f) l = copysignf(0.8f + 0.2f * tanhf((al - 0.8f) * 5.0f), l);
    if (ar > 0.8f) r = copysignf(0.8f + 0.2f * tanhf((ar - 0.8f) * 5.0f), r);

    /* Gate fully shut: absolute digital silence, not a faint sputter of
     * carrier or consonant noise through the leveller. The band envelopes,
     * consonant amount and ensemble line are cleared too, so nothing stale
     * plays when it opens again. */
    if (d->gate_gain == 0.0f) {
        if (!d->silent) {
            for (int i = 0; i < DAFT_BANDS; i++) d->env[i] = 0.0f;
            d->sib = 0.0f;
            d->ens_lp = 0.0f;
            memset(d->ens_buf, 0, sizeof d->ens_buf);
            d->silent = 1;
        }
        l = r = 0.0f;
    } else {
        d->silent = 0;
    }
    *out_l = l;
    *out_r = r;
}

float daft_process(daft_t *d, float mod)
{
    float l, r;
    daft_process_lr(d, mod, &l, &r);
    return 0.5f * (l + r);
}

void daft_set_stereo(daft_t *d, float spread, float ensemble)
{
    d->pan_spread = spread < 0.0f ? 0.0f : (spread > 1.0f ? 1.0f : spread);
    d->ens_depth = ensemble < 0.0f ? 0.0f : (ensemble > 1.0f ? 1.0f : ensemble);
}

float daft_band_level(const daft_t *d, int band)
{
    if (band < 0 || band >= DAFT_BANDS) return 0.0f;
    float db = 20.0f * log10f(d->env[band] + 1e-6f);
    float v = (db + 60.0f) * (1.0f / 60.0f);
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

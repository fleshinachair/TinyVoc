/*
 * Daft engine: 16-band channel vocoder.
 *
 *   modulator (voice) -> 16 bandpass analysis filters -> envelope followers
 *   carrier (4-voice polyBLEP saw/pulse synth + sibilance noise)
 *       -> 16 bandpass synthesis filters x envelopes -> sum
 *
 * Bands are log-spaced across DAFT_FMIN..DAFT_FMAX. Each band is two cascaded
 * biquad bandpasses (4th order) for steep skirts. Fixed-size state, no
 * allocation; embedded in vt_t.
 */
#ifndef TINYVOICE_DAFT_H
#define TINYVOICE_DAFT_H

#include "vt_floor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DAFT_BANDS      16
#define DAFT_VOICES     4           /* MIDI-driven carrier voices (slots 0..3) */
#define DAFT_DRONE_VOICES 3         /* internal fallback chord C3 + G3 + C4, see drone_enabled */
#ifndef DAFT_DRONE_DEFAULT
#define DAFT_DRONE_DEFAULT 1        /* build with -DDAFT_DRONE_DEFAULT=0 to ship without it */
#endif
#define DAFT_ENS_LEN    1024        /* power of two; ensemble taps reach ~9.3 ms */
#define DAFT_FMIN       100.0f
#define DAFT_FMAX       8000.0f

typedef struct { float b0, b2, a1, a2; } daft_bq;      /* bandpass: b1 == 0 */
typedef struct { float z1, z2; } daft_bqs;
typedef struct { float b0, b1, b2, a1, a2; } daft_bqf;     /* general biquad (Nightcall's EQ) */

typedef struct {
    int   note;                 /* last assigned note (for freq) */
    int   gate;                 /* key held */
    float amp, amp_target;      /* amplitude envelope */
    float lf, target_lf;        /* log2(frequency), glided */
    float ph[2];                /* two detuned oscillators */
    float ph_sub;               /* Beauty's sub-octave sine */
} daft_voice;

typedef struct {
    float sr;

    /* params */
    int   pulse;                /* 0 saw, 1 pulse */
    float detune_ratio;         /* per-oscillator frequency ratio (>=1) */
    float glide_a;
    float width, tilt, sibilance, formant;
    float gate_thr;             /* linear */

    /* one-pole coefficients, computed in init */
    float k_env_a, k_env_r, k_hf_a, k_hf_r, k_lvl_a, k_lvl_r, k_amp_a, k_amp_r, k_gate, k_sib;
    float k_agc_a, k_agc_r, k_sp_a, k_sp_r;

    /* Auto mode (daft_set_auto): AGC + adaptive gate (the drone is on either way) */
    int   auto_mode;
    float gate_offset;          /* Auto: Gate knob as an offset from the floor (1.0 at -60 dB) */
    vt_floor_t floor;           /* raw input level floor, for the adaptive gate */

    /* input AGC: normalizes the modulator so soft and loud speech articulate alike */
    float agc_env;              /* raw input level, fast attack / slow release */
    float k_agc_warm;           /* 1 ms both ways, for the follower's first moments */
    int   agc_warm;             /* open-gate samples left before the normal follower */
    float agc_gain;             /* last applied gain (for tests/inspection) */
    float speech_env;           /* running speech energy, the sibilance reference */

    /* filterbank */
    float     centre[DAFT_BANDS];
    daft_bq   bq[DAFT_BANDS];       /* analysis bank */
    daft_bq   bq_sy[DAFT_BANDS];    /* synthesis bank: centres moved by the formant shift */
    float     last_formant;
    daft_bqs  an[DAFT_BANDS][2];
    daft_bqs  sy[DAFT_BANDS][2];
    float     tilt_gain[DAFT_BANDS];
    float     env[DAFT_BANDS];  /* analysis envelopes (peak-equivalent) */
    float     last_width;

    /* sibilance detector */
    daft_bq   hp[2];            /* high-pass sections (b1 = -2*b0 implied) */
    daft_bqs  hps[2];
    float     hf_env;
    float     sib;              /* 0..1 smoothed consonant amount */

    /* gate */
    float lvl_env, gate_gain;
    int   gate_open;
    int   voice_present;        /* the caller's say (Auto's silence gate): 0 shuts the gate */
    float beauty;               /* 0..1: sub-octave, carrier low-pass, air bleed (daft_set_beauty) */
    float car_lp, k_car_lp, air_a, air_z[3];
    int   silent;               /* fully shut and already cleared */

    /* Slots 0..DAFT_VOICES-1 follow MIDI. The last DAFT_DRONE_VOICES are an
     * internal C3+G3 drone that sounds only while no key is held, so Daft is
     * playable (and testable) before any MIDI arrives, and yields to the keys. */
    daft_voice v[DAFT_VOICES + DAFT_DRONE_VOICES];

    /* stereo out: odd/even band panning and the VP-330 style ensemble */
    float pan_spread, ens_depth;
    float ens_buf[DAFT_ENS_LEN];
    int   ens_w;
    float ens_ph[2], ens_lp, k_ens_lp;

    /* Talker (Nightcall, daft_set_talker): a DigiTech-Talker style voicing of
     * the same vocoder. Off (0) for Robot: none of this is touched. */
    int       talker;
    float     duty;             /* carrier pulse width, 0.10..0.30 */
    float     rasp;             /* white noise in the carrier, as a share of its level */
    float     bleed;            /* unvoiced high-frequency modulator bled into the output */
    float     cav_db, last_cav_db, tk_air_a;
    daft_bqf  tk_eq[4];         /* 250 Hz low-cut x2 (4th order), 1.2 kHz cavity peak, high shelf */
    daft_bqs  tk_z[2][4];       /* per channel */

    /* output leveller */
    float lev_target, lev_ceil;
    float lev_ms, lev_pk, lev_gain;
    float k_lev_rms, k_lev_pk_r, k_lev_up, k_lev_down;
    int        drone_enabled;
    unsigned   rng;
} daft_t;

void  daft_init(daft_t *d, float sample_rate);
void  daft_set_params(daft_t *d, int pulse, float detune_cents, float glide_ms,
                      float width, float tilt, float sibilance, float formant_st,
                      float gate_db);
/* Auto mode on: the input AGC normalizes the voice and the gate follows the
 * noise floor (the Gate knob becomes an offset). Off: raw input level, manual
 * Gate. Either way the C3+G3+C4 drone plays whenever no key is held. */
void  daft_set_auto(daft_t *d, int on);
/* The caller's gate (Auto's silence gate): 0 shuts the vocoder's gate and
 * holds its AGC; once shut it outputs exact zeros. 1 (default) leaves it to
 * the vocoder's own gate. Set before each sample (or block). */
void  daft_set_voice_present(daft_t *d, int present);
/* Beauty 0..1: a sub-octave sine under each carrier voice (up to -10 dB), the
 * carrier blended toward an 11 kHz low-pass, and the voice's own air above
 * 5.5 kHz bled into the output (gated with it). Detune and band spread are set
 * by the caller through daft_set_params / daft_set_stereo. */
void  daft_set_beauty(daft_t *d, float beauty);
/* Nightcall's talkbox voicing. on 0 = plain vocoder (Robot). On: the carrier
 * is a narrow pulse (duty 0.10..0.30) with `rasp` (~0.10) of white noise
 * on top, the output goes through a steep 250 Hz low-cut, a vocal-cavity peak
 * of cavity_db at 1.2 kHz and a crisp high shelf, and `bleed` (0.15..0.20) of the
 * modulator's own > 3.5 kHz breath (weighted by the consonant detector) is mixed
 * in; the output leveller aims 6 dB lower, so the peaks stay under the output
 * ceiling. */
void  daft_set_talker(daft_t *d, int on, float duty, float rasp, float bleed, float cavity_db);
/* One carrier sample (voices + noise, before the bank): for tests. */
float daft_carrier_sample(daft_t *d);
/* Key the drone follows: root + fifth + octave (root 0 = C: C3 G3 C4, the default). */
void  daft_set_drone_root(daft_t *d, int root);
/* Assign slot `slot` to a MIDI note (note < 0 releases it). Call once per block. */
void  daft_set_voice(daft_t *d, int slot, int note, int velocity);
float daft_process(daft_t *d, float modulator);          /* mono: (L + R) / 2 */
void  daft_process_lr(daft_t *d, float modulator, float *out_l, float *out_r);
/* spread 0..1: odd bands left, even bands right (0 = mono). ensemble 0..1:
 * VP-330 style 3-tap BBD chorus depth on the output. */
void  daft_set_stereo(daft_t *d, float spread, float ensemble);
/* Analysis band level, 0..1 on a 60 dB scale (for the visualizer). */
float daft_band_level(const daft_t *d, int band);

#ifdef __cplusplus
}
#endif
#endif

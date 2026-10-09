/*
 * Cher engine: MPM pitch tracker -> hard scale quantizer -> pitch-synchronous
 * dual-tap delay-line pitch shifter.
 *
 * Fixed-size state, no allocation anywhere; the struct is embedded in vt_t.
 * Detection runs on a 2x-decimated copy of the input (cheap, <=1 kHz is all a
 * voice needs) once every CHER_HOP input samples.
 */
#ifndef TINYVOC_CHER_H
#define TINYVOC_CHER_H

#include "vt_floor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CHER_HOP      256     /* input samples between pitch analyses */
#define CHER_WIN      768     /* analysis window, decimated samples */
#define CHER_RING     1024    /* decimated history (power of two) */
#define CHER_MAX_TAU  400     /* >= fs_dec / fmin for any sample rate we expect */
#define CHER_DELAY    4096    /* shifter delay line (power of two) */

typedef struct {
    float sr;                 /* input sample rate */
    float fs_dec;             /* decimated rate = sr / 2 */

    /* params (set per block via cher_set_params) */
    int   root, scale;
    float retune_ms;
    float gate_db;
    float gate_lin;           /* manual threshold, RMS */
    float gate_offset;        /* Auto: Gate knob as an offset from the floor (1.0 at -60 dB) */
    int   auto_gate;          /* Auto mode: threshold follows the noise floor */
    vt_floor_t floor;         /* per-hop RMS floor (analysis domain) */
    int   override_note;      /* held MIDI note after the octave fold, -1 = use the scale quantizer */
    int   ov_oct;             /* octaves the held note is folded by, toward the voice */
    int   ov_pad;             /* the pad note ov_oct was worked out for, -1 none */
    float glide_a;           /* per-sample slew coefficient for the override target */
    float glide_off;          /* semitones the override target is still gliding from (decays to 0) */

    /* analysis high-pass (2 x 1st order, 50 Hz): DC offset or rumble keeps the
     * NSDF from crossing zero, and then MPM finds no pitch at all */
    float hp_a, hp_x[2], hp_y[2];

    /* decimator + history */
    float lp[4];              /* input low-pass taps */
    int   dec_phase;
    float ring[CHER_RING];
    int   ring_pos;
    int   hop_count;
    float win[CHER_WIN];      /* linearised analysis window (scratch) */
    float nsdf[CHER_MAX_TAU + 2];

    /* tracker state */
    float midi_hist[3];       /* last voiced estimates, for median-of-3 */
    int   oct_run;            /* hops in a row an octave away from the held pitch */
    float last_rms;           /* the last voiced hop's level (an onset isn't a glitch) */
    int   hist_n;
    int   unvoiced_hops;
    int   voiced;             /* currently holding a pitch */
    float f0;                 /* median-filtered f0 in Hz (0 when unvoiced) */
    float f0_midi;            /* same, as fractional MIDI */
    float target_midi;        /* where the shifter is steering to */
    int   note;               /* quantized MIDI note, -1 none (hysteresis memory) */

    /* viz */
    float target_hz, cents_err;

    /* shifter */
    float delay[CHER_DELAY];
    int   wpos;
    double n_written;         /* samples ever pushed: absolute index of the next write */
    float clarity_min;        /* MPM voicing threshold (tracking sensitivity) */
    float phi;                /* sweep phase 0..1 */
    float len;                /* smoothed grain length in samples */
    float len_target;
    float lr;                 /* log2 pitch ratio (smoothed) */
    float bypass;             /* 0 = shifted, 1 = plain delayed dry (unvoiced) */
} cher_t;

void  cher_init(cher_t *c, float sample_rate);
/* Octave fold for pad notes (`centre`: a note, or a chord's mean) against the
 * sung pitch `ref`. Within +-12 st of the voice a pad plays at its own octave,
 * so octave jumps on the pads are heard; further away it folds by octaves only
 * until it is back inside +-12 (C1 and C2 both land an octave under C4, not on
 * it). Returns the octave shift. Distance is from the voice's nearest
 * semitone. `same`: the pad is the one prev_oct was worked out for, so near
 * the +-12 edge prev_oct is kept within a semitone either way (a moving voice
 * doesn't flip it); a new pad always gets the plain rule, so an octave jump
 * is never swallowed. ref <= 0 (no pitch heard yet): prev_oct if same, else 0. */
#define CHER_FOLD_WINDOW 12.0f
int   cher_fold_octave(float ref, float centre, int prev_oct, int same);
/* The same for a held chord (in[i] < 0 = empty): one octave shift for every
 * note, by the rule above applied to the chord's mean, keeping its voicing.
 * Writes out[0..n-1] and returns the new octave shift. */
int   cher_fold_chord(float ref, const int *in, int *out, int n, int prev_oct, int same);

/* override_note: held MIDI note to force the target to (-1 = scale quantizer).
 * The note's name is kept and its octave folded toward the voice (pads are
 * often octaves above it, outside the shifter's +-1 octave).
 * glide_ms slews the target between override notes (0 = jump). */
void  cher_set_params(cher_t *c, int root, int scale, float retune_ms, float gate_db,
                      int override_note, float glide_ms);
float cher_process(cher_t *c, float in);           /* one sample in, one sample out */

/* Auto mode: the gate threshold tracks the input's noise floor instead of the
 * Gate knob, which then acts as an offset (default -60 dB = none). */
void  cher_set_auto(cher_t *c, int on);

/* Voicing threshold for MPM clarity, 0.4..0.95 (lower = tracks more readily). */
void  cher_set_clarity(cher_t *c, float clarity_min);

/* Linear-interpolated read of the input at an absolute sample index (must be
 * within the last CHER_DELAY samples; see n_written). */
float cher_read_abs(const cher_t *c, double index);

/* Building blocks shared with the Imogen harmonizer, which runs extra voices
 * off this tracker and delay line:
 *   cher_push  - feed one input sample (analysis, delay line, crossfades)
 *   cher_tap   - one pitch-synchronous shifter voice: advances *phi for the
 *                log2 ratio lr and returns its output sample
 *   cher_plain - the unshifted signal at the same latency (for unvoiced input) */
void  cher_push(cher_t *c, float in);
/* cher_listen - feed the analysis window and the delay line but skip the
 * pitch analysis: keeps them current while Robot runs, at a fraction of the
 * cost, so a switch to a pitch mode reads the live voice. */
void  cher_listen(cher_t *c, float in);
/* Advance the lead's retune/glide smoothing one sample and return its log2
 * pitch ratio (0 when unvoiced). cher_process = push + lead_lr + tap. */
float cher_lead_lr(cher_t *c);
float cher_tap(const cher_t *c, float *phi, float lr);
float cher_plain(const cher_t *c);

/* Move `steps` notes along the scale from an in-scale note (negative = down).
 * Chromatic moves in semitones. */
int   cher_scale_step(int note, int steps, int root, int scale);

/* Current latency of the wet path in samples (average tap delay). Delaying the
 * dry signal by this much keeps wet/dry blends phase-coherent. */
float cher_latency(const cher_t *c);

/* Building blocks, exposed for tests. */

/* McLeod pitch method on x[0..n). Returns 1 and sets *f0 (Hz) / *clarity if a
 * pitch in [fmin,fmax] is found, else 0. nsdf must hold max_tau+2 floats. */
int   cher_mpm(const float *x, int n, float fs, float fmin, float fmax,
               float *nsdf, float *f0, float *clarity);

/* Nearest allowed MIDI note to a fractional MIDI pitch. prev_note (>=0) is
 * kept if it is nearly as close, which stops flicker at note boundaries. */
int   cher_quantize(float midi, int root, int scale, int prev_note);

float cher_hz_to_midi(float hz);
float cher_midi_to_hz(float midi);

#ifdef __cplusplus
}
#endif
#endif

/*
 * Voice-transformation engines that run straight on the input (no tracker, no
 * carrier synth): Dalek, Harmony, Walkie. (Scream and HardTune are PSOLA modes
 * in modes.c; Nightcall is daft.c's vocoder.)
 *
 * One vox_t serves whichever of the three is active, like fx_t in modes.c.
 * Everything is a fixed-size member: no allocation, nothing but the input
 * sample and a handful of per-block settings go in.
 *
 *   Dalek    c1 Shift  -500..+500 Hz single-sideband shift (0.5 = 0 Hz)
 *            c2 Ring   ring-mod frequency 0..60 Hz (0.5 = the classic 30 Hz; 0 = off)
 *            mod Metal comb resonance (3.7 / 4.3 ms), c4 Drive, c5 Width
 *            (L and R shifted apart by up to +-12 Hz)
 *   Harmony  c1 Lead   the dry-aligned lead's level (0.5 = unity)
 *            c2 Stack  the three harmony taps' level (0.5 = unity)
 *            mod Detune slow +-12 ct drift per tap, c4 Grain 8..18 ms (rounded to whole
 *            periods of the voice), c5 Micro 0..4.5 ms
 *            taps: -12, +7, +12 st; held MIDI notes replace them with the intervals
 *            above the lowest held note (a C major triad gives +4, +7)
 *   Walkie   c1 Peak  presence peak (0..+12 dB, 0.5 = +8 dB), c2 Drive (diode
 *            clip after the mic compressor), mod Static (hiss under the voice:
 *            a faint floor at 0), c4 Squelch (the receiver's noise burst on gate
 *            open and its longer tail on close), c5 Band (peak 1.2..2.6 kHz,
 *            0.5 = 1.8 kHz). Between phrases the receiver mutes the channel.
 */
#ifndef TINYVOICE_VOICE_H
#define TINYVOICE_VOICE_H

#include "ssb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VOX_HG_LEN     4096      /* Harmony's input ring (power of two): ~93 ms */
#define VOX_HG_TAPS    3
#define VOX_DK_LEN     512       /* Dalek's comb lines (power of two): 11.6 ms */
#define VOX_HG_GRAIN_MIN_MS  8.0f
#define VOX_HG_GRAIN_MAX_MS  18.0f
#define VOX_HG_MICRO_MAX_MS  4.5f

typedef struct { float g, k, a1, a2, a3, s1, s2; } vox_svf;

typedef struct {
    float sr;

    /* ---- Dalek ---- */
    ssb_hilbert hil;
    ssb_osc sh_l, sh_r, ring;
    int   ring_on;
    float dk_k, dk_n;                 /* drive and its level compensation */
    float dk_fb;                      /* comb feedback */
    float dk_line[2][VOX_DK_LEN];
    int   dk_w;
    float dk_dc[2];                   /* DC blocker state */

    /* ---- Harmony ---- */
    float hg_buf[VOX_HG_LEN];
    int   hg_w;
    float hg_c1, hg_c2, hg_mod, hg_spread;
    float hg_grain;                   /* smoothed grain length, samples */
    float hg_grain_goal, hg_micro;    /* micro: milliseconds * sr / 1000 */
    float hg_phi[VOX_HG_TAPS];        /* sweep phase 0..1 */
    float hg_st[VOX_HG_TAPS];         /* smoothed interval, semitones */
    float hg_st_goal[VOX_HG_TAPS];
    float hg_amp[VOX_HG_TAPS], hg_amp_goal[VOX_HG_TAPS];
    float hg_k_slew, hg_k_amp;
    double hg_t;
    int   hg_active;

    /* ---- Walkie ---- */
    vox_svf wk_hp[2], wk_lp[2], wk_peak, wk_nz;   /* the band: 4th order each side */
    float wk_peak_g, wk_drive, wk_static, wk_squelch;
    float wk_cenv, wk_cg;             /* mic compressor: level follower and its gain */
    float k_wk_att, k_wk_rel, k_wk_g;
    float wk_rx, k_wk_rx;             /* receiver: channel gain (0 between phrases) */
    float wk_dc_x, wk_dc_y, wk_lp1[2];
    float wk_env, wk_tau;             /* squelch burst envelope and its decay time, samples */
    float wk_since;                   /* samples since the last edge */
    int   wk_open;
    unsigned rng;
    float k_dc;
} vox_t;

void  vox_init(vox_t *v, float sample_rate);

/* Once per block. c1, c2, mod, c4, c5 are the five 0..1 knobs. */
void  vox_set_dalek(vox_t *v, float c1, float c2, float mod, float c4, float c5);
/* held[]: the MIDI slot notes (-1 = free). spread 0..1 is the stack's pan width.
 * f0_hz is the tracked pitch (0 = none): the grain is set to a whole number of
 * its periods (about the Grain knob's length), so every grain wrap lands on a
 * whole cycle of the voice and its partials stay in phase through the
 * cross-fade (a fixed grain length drops a held tone's phase at each wrap and
 * the shifted line collapses into sidebands: ~30 dB down on a pure tone). */
void  vox_set_harmony(vox_t *v, float c1, float c2, float mod, float c4, float c5,
                      float spread, const int held[4], float f0_hz);
void  vox_set_walkie(vox_t *v, float c1, float c2, float mod, float c4, float c5);

/* One sample of the engine for `mode` (a VT_MODE_ DALEK, HARMONY or WALKIE).
 * voice_open is the voice gate's state (Walkie's squelch follows its edges). */
void  vox_process(vox_t *v, int mode, float in, int voice_open, float *out_l, float *out_r);

/* Keep Harmony's input ring current while another mode runs (a sample in, no
 * output), so a switch to Harmony reads the live voice rather than whatever
 * the ring held when it was last used. */
void  vox_listen(vox_t *v, float in);

/* Wet-path latency in samples, for lining up the dry signal. */
float vox_latency(const vox_t *v, int mode);

#ifdef __cplusplus
}
#endif
#endif

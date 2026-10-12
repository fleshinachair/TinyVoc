/*
 * TinyVoice engine: parameter table, MIDI voice tracking, mode dispatch,
 * delay-compensated dry/wet mix, Tone, Spread chorus, state snapshot,
 * visualizer feed.
 * Engines: daft.c (Robot, Nightcall), imogen.c (Chords), modes.c (Snap, Morph,
 * Prism, Double, Tape, Scream, HardTune), voice.c (Dalek, Harmony, Walkie); the
 * pitch modes run on the cher.c tracker.
 */
#include "dsp.h"
#include "cher.h"
#include "daft.h"
#include "imogen.h"
#include "modes.h"
#include "voice.h"
#include "freeze.h"
#include "demo.h"
#include "vfx.h"
#include "presets.h"
#include "vt_floor.h"
#include "module_meta.h"   /* generated: chain_params / ui_hierarchy JSON */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VT_DRY_DELAY 2048   /* power of two; cher latency tops out near 760 samples */
#define VT_CHORUS_LEN 2048  /* power of two; chorus taps reach ~12 ms */
#define CHORUS_BASE_MS 9.0f
#define CHORUS_DEPTH_MS 2.5f
#define CHORUS_RATE_HZ 0.4f
/* Chords with Auto Off: +1 dB on top of the harmony bus's +4.5 dB, +5.5 dB
 * net over the input, so the cluster stays present at a raw mic level (Auto
 * On levels it anyway). +9 dB net drove a hot voice into the output limiter. */
#define CHORDS_MANUAL_TRIM 1.1220f
#define LABEL_PULSE_SEC 0.6f /* is_loading "1" after a mode change: > the UI's ~8-tick poll */
#define ROBOT_REG_LO    48     /* Robot carrier register: held pads fold, as a block, */
#define ROBOT_REG_HI    72     /* so their mean sits in C3..B4 */
#define GATE_DEFAULT_DB (-39)  /* the manual level until one is chosen: above room noise */
#define MODE_FADE_SEC   0.005f /* a mode switch fades the old engine out and the new one in */
#define SNAP_INSTANT_BEAUTY 0.005f /* Beauty at (or within half a step of) 0: Snap retunes instantly */

/* Auto's input AGC (every mode): soft voices are lifted towards a working
 * level, loud ones eased down. It holds its gain in silence rather than
 * chasing the noise floor up. */
#define IN_AGC_TARGET  0.25f    /* peak-equivalent, about -12 dBFS */
#define IN_AGC_MAX     3.981f   /* +12 dB: never enough to turn a room into a rush */
#define IN_AGC_MIN     0.5f     /* -6 dB */
/* The voice gate: true RMS of the raw input (10 ms, before any gain) against
 * a fixed threshold, Auto's -42 dBFS or the Gate knob with Auto Off. One
 * decision for everything: below it (after a 50 ms hold) Robot outputs
 * exact zeros, and with Auto the AGC freezes at once and a downward
 * expander takes the input 40 dB down. (A peak-riding level read noise ~3 dB
 * hot and let -45 dBFS RMS mic hiss open it, playing a pad's carrier; an
 * adaptive room-noise threshold missed pauses on the device.) */
#define SG_THR         0.00794f /* -42 dBFS RMS */
#define SG_DEPTH       0.01f    /* closed: -40 dB */
#define SG_HOLD_SEC    0.05f    /* stays open this long below the threshold */
#define SG_ATTACK_SEC  0.002f
#define SG_RELEASE_SEC 0.06f

#ifdef VT_TEST_ALLOC
/* Tests substitute counting allocators to prove nothing leaks. */
void *vt_test_calloc(size_t n, size_t sz);
void  vt_test_free(void *p);
#define VT_CALLOC vt_test_calloc
#define VT_FREE   vt_test_free
#else
#define VT_CALLOC calloc
#define VT_FREE   free
#endif

/* ------------------------------------------------------------------ params */

static const char *const k_mode_opts[]  = {"Robot", "Snap", "Chords", "Prism", "Tape", "Nightcall", "Scream", "Dalek",
                                           "HardTune", "Harmony", "Walkie", "Morph", "Double"};
/* Gate: Auto, then the manual threshold in 3 dB steps (option i = -60 + 3 (i - 1) dB). */
static const char *const k_gate_opts[VT_GATE_OPTS] = {
    "Auto", "-60 dB", "-57 dB", "-54 dB", "-51 dB", "-48 dB", "-45 dB", "-42 dB", "-39 dB", "-36 dB", "-33 dB",
    "-30 dB", "-27 dB", "-24 dB", "-21 dB", "-18 dB", "-15 dB", "-12 dB", "-9 dB", "-6 dB", "-3 dB", "0 dB",
};
static const char *const k_onoff_opts[] = {"Off", "On"};
static const char *const k_engine_opts[] = {"Vocoder", "VoiceFX"};
static const char *const k_freeze_opts[] = {"Off", "Rec", "Loop"};   /* = FREEZE_OFF / _REC / _LOOP */
static const char *const k_freeze_len_opts[] = {"Free", "1 Bar", "2 Bars", "4 Bars"};
static const int k_freeze_bars[] = { 0, 1, 2, 4 };
static const char *const k_root_opts[]  = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const k_scale_opts[] = {"Chromatic", "Major", "Minor", "Pentatonic", "Dorian"};

#define ENUM_DEF(key, opts, def) {key, VT_PTYPE_ENUM, 0, (float)(sizeof(opts) / sizeof(opts[0]) - 1), def, opts, (int)(sizeof(opts) / sizeof(opts[0])), 0}
#define TOGGLE_DEF(key) {key, VT_PTYPE_ENUM, 0, 1, 0, k_onoff_opts, 2, 1}
#define TRANSIENT_ENUM_DEF(key, opts) {key, VT_PTYPE_ENUM, 0, (float)(sizeof(opts) / sizeof(opts[0]) - 1), 0, opts, (int)(sizeof(opts) / sizeof(opts[0])), 1}
#define FLOAT_DEF(key, lo, hi, def) {key, VT_PTYPE_FLOAT, lo, hi, def, NULL, 0, 0}

/* Must stay in sync with vt_param_id and module.json (see tests). */
static const vt_param_def k_params[VT_P_COUNT] = {
    [VT_P_MODE]     = ENUM_DEF("mode",  k_mode_opts,  VT_MODE_SNAP),
    [VT_P_KEY]       = ENUM_DEF("key",   k_root_opts,  0),             /* C */
    [VT_P_SCALE]     = ENUM_DEF("scale", k_scale_opts, VT_SCALE_CHROMATIC),
    [VT_P_RETUNE]    = FLOAT_DEF("retune",    0.0f,   500.0f, 0.0f),
    [VT_P_CHAR1]     = FLOAT_DEF("char1",     0.0f,   1.0f,   0.5f),
    [VT_P_CHAR2]     = FLOAT_DEF("char2",     0.0f,   1.0f,   0.5f),
    [VT_P_GATE]      = ENUM_DEF("gate",   k_gate_opts, 0),       /* Auto */
    [VT_P_MIX]      = FLOAT_DEF("mix",       0.0f,   1.0f,   1.0f),
    [VT_P_GAIN]      = FLOAT_DEF("gain",    -12.0f,  12.0f,   0.0f),
    [VT_P_SPREAD]    = FLOAT_DEF("spread",    0.0f,   1.0f,   0.25f),
    [VT_P_TONE]      = FLOAT_DEF("tone",      0.0f,   1.0f,   1.0f),
    [VT_P_GLIDE]     = FLOAT_DEF("glide",     0.0f,   500.0f, 0.0f),
    [VT_P_MOD]       = FLOAT_DEF("mod",       0.0f,   1.0f,   0.0f),
    [VT_P_FREEZE_LEN] = ENUM_DEF("freeze_len", k_freeze_len_opts, 2),  /* 2 Bars */
    [VT_P_BEAUTY]    = FLOAT_DEF("beauty",    0.0f,   1.0f,   0.5f),
    [VT_P_LATCH]     = ENUM_DEF("latch",      k_onoff_opts, 0),
    [VT_P_CHAR4]     = FLOAT_DEF("char4",     0.0f,   1.0f,   0.5f),
    [VT_P_CHAR5]     = FLOAT_DEF("char5",     0.0f,   1.0f,   0.5f),
    [VT_P_ENGINE]    = ENUM_DEF("engine",     k_engine_opts, VT_ENGINE_VOCODER),
    /* defaults = the "Vowel Morph Solo" preset (the test checks it) */
    [VT_P_VF_VOWEL]  = FLOAT_DEF("vf_vowel",   0.0f,  1.0f,   0.0f),
    [VT_P_VF_TRACT]  = FLOAT_DEF("vf_tract",   0.0f,  1.0f,   0.5f),
    [VT_P_VF_PEAK]   = FLOAT_DEF("vf_peak",    0.0f,  1.0f,   0.5f),
    [VT_P_VF_CHOIR]  = FLOAT_DEF("vf_choir",   0.0f,  1.0f,   0.35f),
    [VT_P_VF_BREATH] = FLOAT_DEF("vf_breath",  0.0f,  1.0f,   0.15f),
    [VT_P_VF_ATTACK] = FLOAT_DEF("vf_attack",  0.0f,  1.0f,   0.12f),
    [VT_P_VF_RELEASE] = FLOAT_DEF("vf_release", 0.0f, 1.0f,   0.3f),
    [VT_P_VF_MIX]    = FLOAT_DEF("vf_mix",     0.0f,  1.0f,   0.75f),
    [VT_P_VF_PRESET] = {"vf_preset", VT_PTYPE_FLOAT, 0, 5, 5, NULL, 0, 1},    /* 0..5 = the bank; transient; 5 = Vowel Morph Solo */
    /* performance toggles: never saved in "state" */
    [VT_P_FREEZE]   = TRANSIENT_ENUM_DEF("freeze", k_freeze_opts),
    [VT_P_DEMO]      = TOGGLE_DEF("demo"),
};

/* Gate option <-> manual level in dB (option 0 is Auto). */
static float gate_db_of(int opt) { return (float)(VT_GATE_MIN_DB + VT_GATE_STEP_DB * (opt - 1)); }
static int gate_opt_of(float db)                                    /* nearest 3 dB step */
{
    if (db < (float)VT_GATE_MIN_DB) db = (float)VT_GATE_MIN_DB;
    if (db > 0.0f) db = 0.0f;
    return 1 + (int)lroundf((db - (float)VT_GATE_MIN_DB) / (float)VT_GATE_STEP_DB);
}

const vt_param_def *dsp_param_def(int id)
{
    return (id >= 0 && id < VT_P_COUNT) ? &k_params[id] : NULL;
}

int dsp_param_index(const char *key)
{
    if (!key) return -1;
    for (int i = 0; i < VT_P_COUNT; i++)
        if (strcmp(k_params[i].key, key) == 0) return i;
    return -1;
}

/* -------------------------------------------------------------- instance */

typedef struct {
    int note;       /* -1 = free */
    int velocity;
    uint32_t age;   /* for oldest-voice stealing */
} vt_voice;

struct vt {
    int sample_rate;
    float p[VT_P_COUNT];

    vt_voice voices[VT_MAX_VOICES];
    uint32_t voice_clock;

    /* Viz state, written by the DSP and read by get_param("viz"). */
    float viz_f0, viz_target, viz_cents;
    float viz_bands[VT_NUM_BANDS];

    cher_t cher;                /* Cher engine, and the tracker/delay line Imogen runs on */
    daft_t daft;                /* Robot */
    daft_t nc;                  /* Nightcall: the same vocoder with the talker voicing */
    imogen_t imogen;
    fx_t fx;                    /* Snap, Morph, Prism, Double, Tape, Scream, HardTune */
    vox_t vox;                  /* Dalek, Harmony, Walkie */
    freeze_t freeze;            /* 16 s punch-in looper on the input */
    demo_t demo;                /* built-in sung phrase */
    vfx_t vfx;                  /* Voice FX: the formant engine of the second personality */
    double demo_time;           /* seconds since Demo was switched on */
    float in_env, agc_goal, agc_gain;   /* Auto input AGC */
    float k_agc_att, k_agc_rel, k_agc_gain;
    /* Auto silence gate (freezes the AGC and expands pauses down) */
    float sg_ms, sg_gain, k_sg_env_a, k_sg_env_r, k_sg_att, k_sg_rel;   /* sg_ms: mean square, 10 ms */
    int   sg_open, sg_hold, sg_hold_n;
    /* `diag` readout (get_param only): input and output levels, held over
     * ~1 s, and how often the voice gate was open. Updated per block. */
    float dg_in_pk, dg_in_min, dg_out_pk, dg_open, dg_ropen, dg_out_ms;
    unsigned midi_note_ons;     /* note-ons received, for the screen / diagnostics */
    int      midi_ch;           /* the slot's receive channel 0-15, -1 = omni */
    unsigned midi_foreign;      /* channel messages dropped for another channel (diag) */
    int      midi_ch_raw;       /* what the host answered for the slot's channel (diag) */
    int      midi_src;          /* source of the message being handled (diag) */
    int      midi_log[6][3];    /* last note-ons: status, note, source (diag) */
    int      midi_log_n;
    int   demo_active, demo_saved_mode;
    int   demo_pinned;            /* Mode turned during Demo: the tour stops on it */
    int   restoring;              /* inside apply_state: Beauty is set whatever the mode */
    float bpm;
    double beat;                /* host transport, beats at the next block's start; < 0 = stopped */

    /* Per-mode knob names: the served chain_params names char1/char2/mod for
     * the current mode. When the mode changes, is_loading reads "1" for a
     * moment; its fall back to "0" is what makes the knob pages re-read
     * chain_params (shadow_ui_param_pages.mjs, reloadIfChanged). */
    int label_mode;             /* mode the served names describe, -1 = not yet */
    int loading_blocks;         /* blocks left reading is_loading = "1" */
    int warm_samples;           /* samples processed, saturating at 1 s */

    /* Mode switch: the running engine's wet fades out, the new engine takes
     * over at the next block boundary and fades in, so switching (knob, state
     * or the Demo tour) never cuts one engine's waveform into another's. */
    int   run_mode;             /* engine actually running, -1 = none yet */
    int   robot_fold;           /* Robot carrier: octaves (x12 st) the held pads are moved by */
    float hp_a, hp_x[2], hp_y[2];   /* mic pre-conditioning: 1-pole high-pass, 120 Hz, per channel */
    /* Latch: the chord the poly modes (Robot, Chords, Prism) play while Latch
     * is On, independent of the pads' note-offs. */
    int   latch_note[VT_MAX_VOICES], latch_vel[VT_MAX_VOICES];
    int   latch_was;            /* Latch at the last block, to catch it being switched */
    int   gate_mem;             /* Gate option of the last manual level (what "auto Off" returns to) */
    float sw_g, k_sw;           /* wet switch gain 0..1, its per-sample step */

    /* Tone (2-pole lowpass on the wet signal) and Spread (stereo chorus). */
    float tone_z[2][2];
    float chorus[VT_CHORUS_LEN];
    int   ch_w;
    float ch_phase;
    float ch_amt_s;             /* the chorus amount, smoothed: it is 0 while a vocoder runs, and
                                 * switching on at once replayed the old engine's tail 9 ms late */

    /* Dry path delay line (per channel) so wet/dry blends stay phase-aligned
     * with the shifter's latency (Cher and Imogen). */
    float dry[2][VT_DRY_DELAY];
    int   dry_w;
    float dry_d;                /* current dry delay in samples */
};

vt_t *dsp_init(int sample_rate)
{
    vt_t *vt = (vt_t *)VT_CALLOC(1, sizeof(*vt));
    if (!vt) return NULL;

    vt->sample_rate = sample_rate > 0 ? sample_rate : VT_SAMPLE_RATE;
    for (int i = 0; i < VT_P_COUNT; i++) vt->p[i] = k_params[i].def;
    for (int i = 0; i < VT_MAX_VOICES; i++) vt->voices[i].note = -1;
    cher_init(&vt->cher, (float)vt->sample_rate);
    daft_init(&vt->daft, (float)vt->sample_rate);
    daft_init(&vt->nc, (float)vt->sample_rate);
    vox_init(&vt->vox, (float)vt->sample_rate);
    imogen_init(&vt->imogen, (float)vt->sample_rate);
    fx_init(&vt->fx, (float)vt->sample_rate);
    freeze_init(&vt->freeze, (float)vt->sample_rate);
    demo_init(&vt->demo, (float)vt->sample_rate);
    vfx_init(&vt->vfx, (float)vt->sample_rate);
    vt->label_mode = -1;
    vt->run_mode = -1;
    vt->midi_ch = -1;                       /* omni until the host names the slot's channel */
    vt->midi_ch_raw = -99;                  /* never asked */
    vt->midi_src = -1;
    vt->gate_mem = gate_opt_of(GATE_DEFAULT_DB);
    vt->hp_a = expf(-2.0f * 3.14159265f * VT_INPUT_HPF_HZ / (float)vt->sample_rate);
    for (int i = 0; i < VT_MAX_VOICES; i++) vt->latch_note[i] = -1;
    vt->sw_g = 1.0f;
    vt->k_sw = 1.0f / (MODE_FADE_SEC * (float)vt->sample_rate);
    vt->bpm = 120.0f;
    vt->beat = -1.0;
    vt->agc_goal = vt->agc_gain = 1.0f;
    vt->k_agc_att  = 1.0f - expf(-1.0f / (0.010f * (float)vt->sample_rate));
    vt->k_agc_rel  = 1.0f - expf(-1.0f / (1.000f * (float)vt->sample_rate));
    vt->k_agc_gain = 1.0f - expf(-1.0f / (0.050f * (float)vt->sample_rate));
    /* the gate's level: a plain 10 ms average (a fast-attack follower rides
     * noise peaks ~6 dB over the noise's level and keeps reopening) */
    vt->k_sg_env_a = vt->k_sg_env_r = 1.0f - expf(-1.0f / (0.010f * (float)vt->sample_rate));
    vt->k_sg_att   = 1.0f - expf(-1.0f / (SG_ATTACK_SEC * (float)vt->sample_rate));
    vt->k_sg_rel   = 1.0f - expf(-1.0f / (SG_RELEASE_SEC * (float)vt->sample_rate));
    vt->sg_hold_n  = (int)(SG_HOLD_SEC * (float)vt->sample_rate);
    vt->sg_gain = 1.0f;
    vt->sg_open = 1;
    return vt;
}

void dsp_cleanup(vt_t *vt)
{
    if (!vt) return;
    VT_FREE(vt);
}

/* ---------------------------------------------------------------- params */

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int apply_state(vt_t *vt, const char *json);

/* Case-insensitive equality: Schwung's Mode page hands back a mode's name in the
 * spelling it matched (level names are lower case), which must still select it. */
static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        const int x = (unsigned char)*a, y = (unsigned char)*b;
        if ((x >= 'A' && x <= 'Z' ? x + 32 : x) != (y >= 'A' && y <= 'Z' ? y + 32 : y)) return 0;
    }
    return *a == *b;
}

/*
 * The OLED widget's feed (canvas.js reads it as an extra_key, ~5 Hz):
 *   "S,<mode>,<freeze>,<loop phase 0..1>,<loop s>,<f0 Hz>,<target Hz>,<cents>,<b0>,...,<b15>,
 *    (freeze: 0 live, 1 looping, 2 recording, 3 armed for the downbeat)
 *    <auto gain dB>,<held pad note or -1>,<note-on count mod 1000>,<transport beat or -1>"
 * Pitch modes fill f0/target/cents (Tape: target = the dropped output pitch);
 * Robot fills the 16 analysis bands (0..255 each). Built only when asked: nothing extra runs
 * per sample for it.
 */
static int newest_held_note(const vt_t *vt);
static int shown_note(const vt_t *vt);

/* Which engine family a mode runs on. */
static int has_bands(int mode) { return mode == VT_MODE_ROBOT || mode == VT_MODE_NIGHTCALL; }   /* the vocoders */
static int is_vox(int mode) { return mode == VT_MODE_DALEK || mode == VT_MODE_HARMONY || mode == VT_MODE_WALKIE; }

static int format_scope(vt_t *vt, char *buf, int buf_len)
{
    const int mode = (int)vt->p[VT_P_MODE];
    const float sr = (float)vt->sample_rate;
    float f0 = 0.0f, tgt = 0.0f, cents = 0.0f;
    int bands[VT_NUM_BANDS] = {0};

    if (has_bands(mode)) {
        for (int i = 0; i < VT_NUM_BANDS; i++) bands[i] = (int)clampf(vt->viz_bands[i] * 255.0f, 0.0f, 255.0f);
    } else if (is_vox(mode) && mode != VT_MODE_HARMONY) {
        /* no tracker running: nothing pitched to show */
    } else {
        f0 = vt->cher.f0;
        tgt = vt->cher.target_hz;
        cents = vt->cher.cents_err;
        if (mode == VT_MODE_TAPE) {
            float drop = -(1.0f - vt->p[VT_P_CHAR1]) * 12.0f;
            tgt = f0 > 0.0f ? f0 * exp2f(drop / 12.0f) : 0.0f;
            cents = drop * 100.0f;
        }
    }

    int n = snprintf(buf, (size_t)buf_len, "S,%d,%d,%.3f,%.3f,%.1f,%.1f,%.0f", mode,
                     freeze_looping(&vt->freeze) ? 1 : vt->freeze.state == FREEZE_REC ? 2 :
                     freeze_armed(&vt->freeze) ? 3 : 0,
                     freeze_phase(&vt->freeze),
                     freeze_loop_seconds(&vt->freeze, sr), f0, tgt, cents);
    for (int i = 0; i < VT_NUM_BANDS && n > 0 && n < buf_len; i++)
        n += snprintf(buf + n, (size_t)(buf_len - n), ",%d", bands[i]);
    /* Auto's input boost in dB, the newest held pad (-1 none) and how many
     * note-ons have arrived: tells you on screen whether pads reach the FX. */
    if (n > 0 && n < buf_len)
        n += snprintf(buf + n, (size_t)(buf_len - n), ",%.1f,%d,%u",
                      20.0f * log10f(vt->agc_gain), shown_note(vt), vt->midi_note_ons % 1000u);
    /* the host transport, beats (-1 = stopped / no clock): Freeze's banner
     * counts down to the downbeat with it, and shows when there is no clock */
    if (n > 0 && n < buf_len)
        n += snprintf(buf + n, (size_t)(buf_len - n), ",%.2f", vt->beat >= 0.0 ? vt->beat : -1.0);
    return n;
}


/* What char1 / char2 / mod / char4 / char5 do in each mode: the knob names the
 * UI shows (8 characters or fewer, to fit the OLED cell). */
static const char *const k_macro_names[VT_MODE_COUNT][VT_MACROS] = {
    [VT_MODE_ROBOT]     = { "Shift",   "Noise",    "Ensemble", "Beauty",  "Glide"   },
    [VT_MODE_SNAP]      = { "Formant", "Sens",     "Vibrato",  "Retune",  "Beauty"  },
    [VT_MODE_CHORDS]    = { "Level",   "Voicing",  "Humanize", "Retune",  "Beauty"  },
    [VT_MODE_PRISM]     = { "Voicing", "Warmth",   "Humanize", "Refract", "Shimmer" },
    [VT_MODE_TAPE]      = { "Drop",    "Flutter",  "Saturate", "Tone",    "Hiss"    },
    [VT_MODE_NIGHTCALL] = { "Formant", "Breath",   "Rasp",     "Duty",    "Cavity"  },
    [VT_MODE_SCREAM]    = { "Pitch",   "Throat",   "Grit",     "Narrow",  "Line"    },
    [VT_MODE_DALEK]     = { "Shift",   "Ring",     "Metal",    "Drive",   "Width"   },
    [VT_MODE_HARDTUNE]  = { "Amount",  "Formant",  "Vibrato",  "Glide",   "Sens"    },
    [VT_MODE_HARMONY]   = { "Lead",    "Stack",    "Detune",   "Grain",   "Micro"   },
    [VT_MODE_WALKIE]    = { "Peak",    "Drive",    "Static",   "Squelch", "Band"    },
    [VT_MODE_MORPH]     = { "Throat",  "Drive",    "Wobble",   "Retune",  "Glide"   },
    [VT_MODE_DOUBLE]    = { "Detune",  "Width",    "Drift",    "Retune",  "Glide"   },
};

/* char4 / char5 in the older modes are not new controls: they are the param of
 * the same name, scaled to 0..1 (the knob on the VOICE page and the one on its
 * own page move together: one value). -1 = the macro's own storage. The
 * aliased param always acts in that mode (vt_param_acts), checked by a test. */
#define SLOT_OWN (-1)
static const int k_slot_alias[VT_MODE_COUNT][2] = {
    [VT_MODE_ROBOT]     = { VT_P_BEAUTY, VT_P_GLIDE },
    [VT_MODE_SNAP]      = { VT_P_RETUNE, VT_P_BEAUTY },
    [VT_MODE_CHORDS]    = { VT_P_RETUNE, VT_P_BEAUTY },
    [VT_MODE_PRISM]     = { SLOT_OWN,    SLOT_OWN },
    [VT_MODE_TAPE]      = { VT_P_TONE,   SLOT_OWN },
    [VT_MODE_NIGHTCALL] = { SLOT_OWN,    SLOT_OWN },
    [VT_MODE_SCREAM]    = { SLOT_OWN,    SLOT_OWN },
    [VT_MODE_DALEK]     = { SLOT_OWN,    SLOT_OWN },
    [VT_MODE_HARDTUNE]  = { VT_P_GLIDE,  SLOT_OWN },
    [VT_MODE_HARMONY]   = { SLOT_OWN,    SLOT_OWN },
    [VT_MODE_WALKIE]    = { SLOT_OWN,    SLOT_OWN },
    [VT_MODE_MORPH]     = { VT_P_RETUNE, VT_P_GLIDE },
    [VT_MODE_DOUBLE]    = { VT_P_RETUNE, VT_P_GLIDE },
};

/* The param char4 (slot 0) / char5 (slot 1) is an alias of in `mode`, or SLOT_OWN. */
static int slot_alias(int mode, int id)
{
    if (mode < 0 || mode >= VT_MODE_COUNT) return SLOT_OWN;
    return k_slot_alias[mode][id == VT_P_CHAR4 ? 0 : 1];
}

static float slot_norm(const vt_t *vt, int alias)
{
    const vt_param_def *d = &k_params[alias];
    return (vt->p[alias] - d->min) / (d->max - d->min);
}

/* The effective 0..1 value of char4 / char5 for the engines. */
static float slot_value(const vt_t *vt, int id)
{
    const int a = slot_alias((int)vt->p[VT_P_MODE], id);
    return a >= 0 ? slot_norm(vt, a) : vt->p[id];
}

const char *dsp_macro_name(int mode, int which)
{
    if (mode < 0 || mode >= VT_MODE_COUNT || which < 0 || which >= VT_MACROS) return NULL;
    return k_macro_names[mode][which];
}

/* chain_params for the current mode: the generated JSON with its @1@..@5@
 * name placeholders (char1, char2, mod, char4, char5) filled in. Returns bytes written, or
 * -1 if it doesn't fit. */
static int format_chain_params(const vt_t *vt, char *buf, int buf_len)
{
    const int mode = (int)vt->p[VT_P_MODE];
    int n = 0;
    for (size_t part = 0; part < sizeof VT_CHAIN_PARAMS_PARTS / sizeof VT_CHAIN_PARAMS_PARTS[0]; part++) {
        for (const char *t = VT_CHAIN_PARAMS_PARTS[part]; *t; t++) {
            const char *sub = NULL;
            if (t[0] == '@' && t[1] >= '1' && t[1] <= '5' && t[2] == '@') {
                sub = k_macro_names[mode][t[1] - '1'];
                t += 2;
            }
            if (sub) {
                int k = (int)strlen(sub);
                if (n + k >= buf_len) return -1;
                memcpy(buf + n, sub, (size_t)k);
                n += k;
            } else {
                if (n + 1 >= buf_len) return -1;
                buf[n++] = *t;
            }
        }
    }
    buf[n] = '\0';
    return n;
}

int dsp_set_param(vt_t *vt, const char *key, const char *val)
{
    if (!vt || !key || !val) return -1;
    if (strcmp(key, "state") == 0) return apply_state(vt, val);
    if (strcmp(key, "auto") == 0) {
        /* The old Auto switch (sets saved before Gate took it in, tests):
         * On = Gate Auto, Off = back to the last manual level. */
        int on;
        if (strcmp(val, "On") == 0 || strcmp(val, "1") == 0) on = 1;
        else if (strcmp(val, "Off") == 0 || strcmp(val, "0") == 0) on = 0;
        else return -1;
        const int cur = (int)vt->p[VT_P_GATE];
        if (on && cur) { vt->gate_mem = cur; vt->p[VT_P_GATE] = 0.0f; }
        if (!on && !cur) vt->p[VT_P_GATE] = (float)vt->gate_mem;
        return 0;
    }
    if (strcmp(key, "gate") == 0) {
        /* Option names ("Auto", "-38 dB") and option indices (0 = Auto) as
         * any enum; a negative or fractional number is a level in dB (sets
         * saved when Gate was a dB knob, and tests). */
        int opt = -1;
        for (int i = 0; i < VT_GATE_OPTS; i++) if (strcmp(k_gate_opts[i], val) == 0) opt = i;
        if (opt < 0) {
            char *end;
            float f = strtof(val, &end);
            if (end == val || !isfinite(f)) return -1;
            while (*end == ' ') end++;
            if (*end != '\0' && strcmp(end, "dB") != 0) return -1;
            const int is_db = f < 0.0f || strchr(val, '.') != NULL || strstr(val, "dB") != NULL;
            opt = is_db ? gate_opt_of(f) : (int)clampf(f, 0.0f, (float)(VT_GATE_OPTS - 1));
        }
        vt->p[VT_P_GATE] = (float)opt;
        if (opt) vt->gate_mem = opt;
        return 0;
    }
    int id = dsp_param_index(key);
    if (id < 0) return -1;
    const vt_param_def *d = &k_params[id];

    /* A knob that does nothing in the current mode (vt_param_acts: Key, Scale,
     * Retune, Glide, Beauty) ignores turns, so its stored value waits unchanged
     * for the next mode that uses it. A restored state or config still sets it
     * (it may come before "mode" in the object), and so does any write in the
     * first second after load, in case the host restores a set key by key. */
    if (!vt->restoring && vt->warm_samples >= vt->sample_rate && !vt_param_acts(id, (int)vt->p[VT_P_MODE])) return 0;

    if (d->type == VT_PTYPE_ENUM) {
        for (int i = 0; i < d->num_options; i++) {
            if (strcmp(d->options[i], val) == 0) {
                vt->p[id] = (float)i;
                if (id == VT_P_MODE && vt->demo_active) vt->demo_pinned = 1;   /* stop the tour here */
                return 0;
            }
        }
        for (int i = 0; i < d->num_options; i++) {                    /* the host's own spelling of an option (case aside) */
            if (ieq(d->options[i], val)) {
                vt->p[id] = (float)i;
                if (id == VT_P_MODE && vt->demo_active) vt->demo_pinned = 1;
                return 0;
            }
        }
        char *end;
        long idx = strtol(val, &end, 10);
        if (end == val || *end != '\0') return -1;
        if (idx < 0) idx = 0;
        if (idx >= d->num_options) idx = d->num_options - 1;
        vt->p[id] = (float)idx;
        if (id == VT_P_MODE && vt->demo_active) vt->demo_pinned = 1;
        return 0;
    }

    char *end;
    float f = strtof(val, &end);
    if (end == val || !isfinite(f)) return -1;
    if (id == VT_P_VF_PRESET) {                                   /* the browser writes an index: load that preset into the eight knobs */
        const vt_preset *ps = vt_preset_get(VT_ENGINE_VOICEFX, (int)lroundf(clampf(f, d->min, d->max)));
        if (!ps) return -1;
        vt->p[VT_P_VF_PRESET] = (float)(int)lroundf(clampf(f, d->min, d->max));
        for (int k = 0; k < VT_PRESET_VALUES; k++) vt->p[VT_P_VF_VOWEL + k] = ps->v[k];
        return 0;
    }
    vt->p[id] = clampf(f, d->min, d->max);
    if ((id == VT_P_CHAR4 || id == VT_P_CHAR5) && !vt->restoring) {     /* an alias moves the param it stands for */
        const int a = slot_alias((int)vt->p[VT_P_MODE], id);
        if (a >= 0) vt->p[a] = k_params[a].min + vt->p[id] * (k_params[a].max - k_params[a].min);
    }
    return 0;
}

int dsp_get_param(vt_t *vt, const char *key, char *buf, int buf_len)
{
    if (!vt || !key || !buf || buf_len <= 0) return -1;
    int n;

    if (strcmp(key, "state") == 0) {
        n = snprintf(buf, (size_t)buf_len, "{");
        for (int i = 0; i < VT_P_COUNT && n >= 0 && n < buf_len; i++) {
            const vt_param_def *d = &k_params[i];
            if (d->transient) continue;                         /* performance toggles aren't saved */
            int w = d->type == VT_PTYPE_ENUM
                  ? snprintf(buf + n, (size_t)(buf_len - n), "%s\"%s\":\"%s\"", i ? "," : "", d->key, d->options[(int)vt->p[i]])
                  : snprintf(buf + n, (size_t)(buf_len - n), "%s\"%s\":%g", i ? "," : "", d->key, (double)vt->p[i]);
            if (w < 0) return -1;
            n += w;
        }
        if (n >= 0 && n < buf_len) n += snprintf(buf + n, (size_t)(buf_len - n), "}");
    } else if (strcmp(key, "diag") == 0) {
        /* For checking a real mic on the device: input RMS (10 ms, max over
         * ~1 s), voice gate open share, Robot's own gate / AGC / leveller,
         * output RMS (max over ~1 s), held notes, the MIDI channel listened
         * to (1-16, 0 = omni) and how many other-channel messages were dropped. */
        const daft_t *d = &vt->daft;
        n = snprintf(buf, (size_t)buf_len,
                     "in %.1f / floor %.1f dBFS, vgate %.0f%%, robot gate %.0f%%, agc %+.1f dB, lev %.2f, out %.1f dBFS, notes %d %d %d %d, latch %d %d %d %d, mode %d, midi ch %d, foreign %u",
                     10.0 * log10(vt->dg_in_pk + 1e-12), 10.0 * log10(vt->dg_in_min + 1e-12), 100.0 * vt->dg_open, 100.0 * vt->dg_ropen,
                     20.0 * log10(d->agc_gain + 1e-9), d->lev_gain, 10.0 * log10(vt->dg_out_pk + 1e-12),
                     vt->voices[0].note, vt->voices[1].note, vt->voices[2].note, vt->voices[3].note,
                     vt->latch_note[0], vt->latch_note[1], vt->latch_note[2], vt->latch_note[3], vt->run_mode,
                     vt->midi_ch + 1, vt->midi_foreign);
        /* the host's raw channel answer and the last note-ons (status/note/source) */
        if (n >= 0 && n < buf_len) n += snprintf(buf + n, (size_t)(buf_len - n), ", recv %d, last", vt->midi_ch_raw);
        for (int k = 0; k < 6 && k < vt->midi_log_n && n >= 0 && n < buf_len; k++) {
            const int *e = vt->midi_log[(vt->midi_log_n - 1 - k) % 6];
            n += snprintf(buf + n, (size_t)(buf_len - n), " %02x/%d/s%d", e[0], e[1], e[2]);
        }
    } else if (strcmp(key, "auto") == 0) {                        /* the old switch, read-only view of Gate */
        n = snprintf(buf, (size_t)buf_len, "%s", (int)vt->p[VT_P_GATE] == 0 ? "On" : "Off");
    } else if (strcmp(key, "vf_preset_count") == 0) {              /* the Voice FX level's preset browser (list_param / count_param / name_param) */
        n = snprintf(buf, (size_t)buf_len, "%d", vt_preset_count(VT_ENGINE_VOICEFX));
    } else if (strcmp(key, "vf_preset_name") == 0) {
        const vt_preset *ps = vt_preset_get(VT_ENGINE_VOICEFX, (int)vt->p[VT_P_VF_PRESET]);
        n = snprintf(buf, (size_t)buf_len, "%s", ps ? ps->name : "");
    } else if (strcmp(key, "voices") == 0) {
        n = snprintf(buf, (size_t)buf_len, "%d", dsp_active_voices(vt));
    } else if (strcmp(key, "name") == 0) {
        /* The chain editor polls this every frame; unanswered it logged ~70 read errors a second. */
        n = snprintf(buf, (size_t)buf_len, "TinyVoice");
    } else if (strcmp(key, "chain_params") == 0) {
        /* Answering this ourselves is what carries `viz` to the UI (the chain
         * host's fallback drops it), so the canvas.js widget can mount. */
        n = format_chain_params(vt, buf, buf_len);
    } else if (strcmp(key, "ui_hierarchy") == 0) {
        n = snprintf(buf, (size_t)buf_len, "%s", VT_UI_HIERARCHY_JSON);
    } else if (strcmp(key, "is_loading") == 0) {
        n = snprintf(buf, (size_t)buf_len, "%s", vt->loading_blocks > 0 ? "1" : "0");
    } else if (strcmp(key, "pitch") == 0) {                       /* read-only readouts under the scope */
        n = snprintf(buf, (size_t)buf_len, "%.1f", (double)vt->viz_f0);
    } else if (strcmp(key, "target") == 0) {
        n = snprintf(buf, (size_t)buf_len, "%.1f", (double)vt->viz_target);
    } else if (strcmp(key, "cents") == 0) {
        n = snprintf(buf, (size_t)buf_len, "%.0f", (double)vt->viz_cents);
    } else if (strcmp(key, "scope") == 0) {
        n = format_scope(vt, buf, buf_len);
    } else if (strcmp(key, "viz") == 0) {
        if (!has_bands((int)vt->p[VT_P_MODE])) {                  /* pitch modes: tuning view */
            n = snprintf(buf, (size_t)buf_len, "c,%.2f,%.2f,%.1f",
                         vt->viz_f0, vt->viz_target, vt->viz_cents);
        } else {
            int off = snprintf(buf, (size_t)buf_len, "d");
            for (int i = 0; i < VT_NUM_BANDS && off > 0 && off < buf_len; i++) {
                int v = (int)clampf(vt->viz_bands[i] * 255.0f, 0.0f, 255.0f);
                off += snprintf(buf + off, (size_t)(buf_len - off), ",%d", v);
            }
            n = off;
        }
    } else {
        int id = dsp_param_index(key);
        if (id < 0) return -1;
        const vt_param_def *d = &k_params[id];
        if (d->type == VT_PTYPE_ENUM)
            n = snprintf(buf, (size_t)buf_len, "%s", d->options[(int)vt->p[id]]);
        else if (id == VT_P_CHAR4 || id == VT_P_CHAR5)
            n = snprintf(buf, (size_t)buf_len, "%g", (double)slot_value(vt, id));
        else
            n = snprintf(buf, (size_t)buf_len, "%g", (double)vt->p[id]);
    }
    return (n < 0 || n >= buf_len) ? -1 : n;
}

/* Flat JSON object -> dsp_set_param per member. Strings and bare numbers are
 * applied; nested objects/arrays are skipped. Returns 0 if the text was an
 * object, -1 otherwise. No allocation. */
static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static const char *skip_string(const char *p)    /* p at opening quote */
{
    for (p++; *p && *p != '"'; p++) if (*p == '\\' && p[1]) p++;
    return *p ? p + 1 : p;
}

static int is_transient(const char *key)
{
    int id = dsp_param_index(key);
    return id >= 0 && k_params[id].transient;
}

/* A set saved before Gate took in Auto has "auto" and a dB "gate". Its Auto
 * is applied after everything else, so the manual level is remembered either
 * way and Auto On wins. */
static int apply_members(vt_t *vt, const char *json);

static int apply_state(vt_t *vt, const char *json)
{
    vt->restoring = 1;
    const int r = apply_members(vt, json);
    vt->restoring = 0;
    return r;
}

static int apply_members(vt_t *vt, const char *json)
{
    const char *p = skip_ws(json);
    if (*p != '{') return -1;
    p++;
    char legacy_auto[8] = "";
    for (;;) {
        char key[32], val[32];
        p = skip_ws(p);
        if (*p == ',') { p++; continue; }
        if (*p == '}' || *p == '\0') {
            if (legacy_auto[0]) dsp_set_param(vt, "auto", legacy_auto);
            return 0;
        }
        if (*p != '"') return -1;
        const char *ks = p + 1;
        p = skip_string(p);
        size_t kn = (size_t)(p - ks) - 1;
        if (kn >= sizeof key) kn = sizeof key - 1;
        memcpy(key, ks, kn); key[kn] = '\0';
        p = skip_ws(p);
        if (*p != ':') return -1;
        p = skip_ws(p + 1);

        if (*p == '"') {
            const char *vs = p + 1;
            p = skip_string(p);
            size_t vn = (size_t)(p - vs) - 1;
            if (vn >= sizeof val) vn = sizeof val - 1;
            memcpy(val, vs, vn); val[vn] = '\0';
            if (strcmp(key, "auto") == 0) snprintf(legacy_auto, sizeof legacy_auto, "%s", val);
            else if (!is_transient(key)) dsp_set_param(vt, key, val);
        } else if (*p == '{' || *p == '[') {
            int depth = 0;
            do {
                if (*p == '"') { p = skip_string(p); continue; }
                if (*p == '{' || *p == '[') depth++;
                else if (*p == '}' || *p == ']') depth--;
                p++;
            } while (*p && depth > 0);
        } else {
            size_t vn = 0;
            while (*p && *p != ',' && *p != '}' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') {
                if (vn < sizeof val - 1) val[vn++] = *p;
                p++;
            }
            val[vn] = '\0';
            if (strcmp(key, "auto") == 0) snprintf(legacy_auto, sizeof legacy_auto, "%s", val);
            else if (!is_transient(key)) dsp_set_param(vt, key, val);
        }
    }
}

/* ------------------------------------------------------------------ MIDI */

int dsp_active_voices(const vt_t *vt)
{
    int n = 0;
    if (!vt) return 0;
    for (int i = 0; i < VT_MAX_VOICES; i++) n += vt->voices[i].note >= 0;
    return n;
}

/* Latch: the chord buffer, oldest first. A press with no pad physically
 * held starts a new chord (the old one is replaced at once); presses while
 * others are held add to it, the oldest dropping out past four notes. */
static void latch_clear(vt_t *vt)
{
    for (int i = 0; i < VT_MAX_VOICES; i++) vt->latch_note[i] = -1;
}

static void latch_add(vt_t *vt, int note, int vel)
{
    for (int i = 0; i < VT_MAX_VOICES; i++)
        if (vt->latch_note[i] == note) { vt->latch_vel[i] = vel; return; }
    int n = 0;
    while (n < VT_MAX_VOICES && vt->latch_note[n] >= 0) n++;
    if (n == VT_MAX_VOICES) {                                      /* full: drop the oldest */
        for (int i = 1; i < VT_MAX_VOICES; i++) { vt->latch_note[i - 1] = vt->latch_note[i]; vt->latch_vel[i - 1] = vt->latch_vel[i]; }
        n = VT_MAX_VOICES - 1;
    }
    vt->latch_note[n] = note;
    vt->latch_vel[n] = vel;
}

static int latch_on(const vt_t *vt) { return vt->p[VT_P_LATCH] >= 0.5f; }

/* The modes Latch applies to: the ones the pads play as chords. */
static int latch_mode(int mode)
{
    return mode == VT_MODE_ROBOT || mode == VT_MODE_CHORDS || mode == VT_MODE_PRISM ||
           mode == VT_MODE_NIGHTCALL || mode == VT_MODE_HARMONY;
}

static void note_on(vt_t *vt, int note, int vel)
{
    if (latch_on(vt)) {
        if (dsp_active_voices(vt) == 0) latch_clear(vt);           /* a new chord */
        latch_add(vt, note, vel);
    }
    int slot = 0;
    uint32_t oldest = UINT32_MAX;
    for (int i = 0; i < VT_MAX_VOICES; i++) {
        if (vt->voices[i].note == note) { slot = i; goto assign; }   /* retrigger */
    }
    for (int i = 0; i < VT_MAX_VOICES; i++) {
        if (vt->voices[i].note < 0) { slot = i; goto assign; }       /* free slot */
        if (vt->voices[i].age < oldest) { oldest = vt->voices[i].age; slot = i; }
    }
assign:
    vt->voices[slot].note = note;
    vt->voices[slot].velocity = vel;
    vt->voices[slot].age = ++vt->voice_clock;
}

static void note_off(vt_t *vt, int note)
{
    for (int i = 0; i < VT_MAX_VOICES; i++)
        if (vt->voices[i].note == note) vt->voices[i].note = -1;
}

void dsp_handle_midi(vt_t *vt, const uint8_t *msg, int len)
{
    if (!vt || !msg || len < 1) return;
    /* The chain hands over 3-byte messages; a 4-byte USB-MIDI packet
     * ([cable|CIN], status, d1, d2) is accepted too, by skipping its header. */
    if (len >= 4 && !(msg[0] & 0x80) && (msg[1] & 0x80)) { msg++; len--; }
    int status = msg[0] & 0xF0;
    int d1 = len > 1 ? msg[1] & 0x7F : 0;
    int d2 = len > 2 ? msg[2] & 0x7F : 0;

    const int src = vt->midi_src;
    vt->midi_src = -1;                                               /* this message only */
    if (status == 0x90 && d2 > 0) {                                  /* diag: who sent it */
        int *e = vt->midi_log[vt->midi_log_n++ % 6];
        e[0] = msg[0]; e[1] = d1; e[2] = src;
    }

    /* Only the slot's own MIDI. The chain host also broadcasts every track's
     * MIDI to every audio FX (Move's sequencer sends notes on muted tracks
     * too, with MIDI Out on): taken, those played Robot's carrier and
     * Chords/Prism voices. The slot's own notes come on their own path as
     * well, so broadcasts are dropped whole; and where the host names the
     * slot's channel (slot_recv_channel; it answers -2 for an FX on the
     * device), other channels are too. Notes, CCs (panic included), pitch
     * bend: all filtered. */
    if (status >= 0x80 && status <= 0xE0 &&
        (src == VT_MIDI_SOURCE_BROADCAST || (vt->midi_ch >= 0 && (msg[0] & 0x0F) != vt->midi_ch))) {
        vt->midi_foreign++;
        return;
    }

    if (status == 0x90 && d2 > 0) { note_on(vt, d1, d2); vt->midi_note_ons++; }
    else if (status == 0x80 || status == 0x90) note_off(vt, d1);
    else if (status == 0xB0 && (d1 == 120 || d1 == 123)) {            /* all sound/notes off */
        for (int i = 0; i < VT_MAX_VOICES; i++) vt->voices[i].note = -1;
        latch_clear(vt);                                              /* a panic releases a latch too */
    }
}

void dsp_set_midi_channel(vt_t *vt, int channel)
{
    if (!vt) return;
    vt->midi_ch_raw = channel;
    vt->midi_ch = channel >= 0 && channel <= 15 ? channel : -1;
}

void dsp_set_midi_source(vt_t *vt, int source)
{
    if (vt) vt->midi_src = source;
}

void dsp_set_bpm(vt_t *vt, float bpm)
{
    if (vt && bpm >= 20.0f && bpm <= 400.0f) vt->bpm = bpm;
}

void dsp_set_transport(vt_t *vt, double beat)
{
    if (vt) vt->beat = beat;
}

/* Most recently pressed note that is still held, or -1. */
static int newest_held_note(const vt_t *vt)
{
    int best = -1;
    uint32_t age = 0;
    for (int i = 0; i < VT_MAX_VOICES; i++)
        if (vt->voices[i].note >= 0 && vt->voices[i].age >= age) { age = vt->voices[i].age; best = vt->voices[i].note; }
    return best;
}

/* The pad the screen shows: the newest latched note while Latch holds the
 * chord in this mode, else the newest pad held. */
static int shown_note(const vt_t *vt)
{
    if (latch_on(vt) && latch_mode(vt->run_mode)) {
        for (int i = VT_MAX_VOICES - 1; i >= 0; i--) if (vt->latch_note[i] >= 0) return vt->latch_note[i];
        return -1;
    }
    return newest_held_note(vt);
}

/* ----------------------------------------------------------------- audio */

/* Transparent below 0.9; above, eases into a 1.0 ceiling (Gain goes to +12 dB). */
static inline float soft_limit(float x)
{
    float a = fabsf(x);
    if (a <= 0.9f) return x;
    return copysignf(0.9f + 0.1f * tanhf((a - 0.9f) * 10.0f), x);
}

/* Linear-interpolated read d samples behind write index w in a power-of-two ring. */
static float ring_read(const float *line, int mask, int w, float d)
{
    float pos = (float)w - d;
    float fl = floorf(pos);
    float frac = pos - fl;
    int i0 = (int)fl & mask;
    int i1 = (i0 + 1) & mask;
    return line[i0] + frac * (line[i1] - line[i0]);
}

/* The dry delay tracks the engine's latency exactly, except across a jump
 * (the shifter's latency steps up when the pitch falls): there it glides over
 * ~3 ms, so the dry signal doesn't step (a click whenever Mix < 100%). */
static inline void dry_follow(vt_t *vt, float target)
{
    float d = target - vt->dry_d;
    if (d > 1.0f || d < -1.0f) vt->dry_d += d * (1.0f - expf(-1.0f / (0.003f * (float)vt->sample_rate)));
    else vt->dry_d = target;
}

/* Held pads -> a vocoder's carrier voices. Move's pads sit around E5 and up,
 * where a carrier is above most of the voice's formants (no words come
 * through, just a high tone). Held pads keep their names and voicing but move
 * by whole octaves, together, until their mean is in C3..B4; the last fold is
 * kept while the mean stays within 2 st of that range. */
static void assign_carriers(vt_t *vt, daft_t *d, const int held[VT_MAX_VOICES], const int vel[VT_MAX_VOICES])
{
    int nh = 0, sum = 0;
    for (int s = 0; s < VT_MAX_VOICES; s++) if (held[s] >= 0) { sum += held[s]; nh++; }
    if (nh) {
        const float mean = (float)sum / (float)nh;
        const float m = mean + (float)vt->robot_fold;
        if (m < (float)(ROBOT_REG_LO - 2) || m >= (float)(ROBOT_REG_HI + 2)) {
            int f = 0;
            while (mean + (float)f >= (float)ROBOT_REG_HI) f -= 12;
            while (mean + (float)f < (float)ROBOT_REG_LO) f += 12;
            vt->robot_fold = f;
        }
    }
    for (int s = 0; s < VT_MAX_VOICES; s++)
        daft_set_voice(d, s, held[s] >= 0 ? held[s] + vt->robot_fold : -1, vel[s]);
}

/* The headroom stage of the new modes: linear up to 0.8 of the ceiling, then
 * eased in so the output never reaches it. */
static inline float headroom(float x)
{
    const float knee = 0.8f * TINYVOICE_OUTPUT_HEADROOM, span = TINYVOICE_OUTPUT_HEADROOM - knee;
    const float a = fabsf(x);
    if (a <= knee) return x;
    return copysignf(knee + span * tanhf((a - knee) / span), x);
}

void dsp_process_block_f(vt_t *vt, float *lr, int frames)
{
    if (!vt || !lr || frames <= 0) return;

    const float sr = (float)vt->sample_rate;

    /* Demo: on switch-on remember the user's mode; while on, step through
     * every mode, 4 bars each, unless the Mode knob was turned, which pins the
     * tour on that mode; on switch-off put the user's mode back (or keep the
     * pinned one: that was a choice). */
    const int demo_on = (int)vt->p[VT_P_DEMO];
    if (demo_on && !vt->demo_active) {
        vt->demo_active = 1;
        vt->demo_pinned = 0;
        vt->demo_saved_mode = (int)vt->p[VT_P_MODE];
        vt->demo_time = 0.0;
        demo_reset(&vt->demo);
    } else if (!demo_on && vt->demo_active) {
        vt->demo_active = 0;
        if (!vt->demo_pinned) vt->p[VT_P_MODE] = (float)vt->demo_saved_mode;
        vt->demo_pinned = 0;
    }
    int demo_chord_on = 0, demo_notes[VT_MAX_VOICES];
    if (vt->demo_active) {
        const double bar = 240.0 / (double)vt->bpm;                 /* 4 beats */
        if (!vt->demo_pinned && (int)vt->p[VT_P_ENGINE] == VT_ENGINE_VOCODER)
            vt->p[VT_P_MODE] = (float)((int)(vt->demo_time / (4.0 * bar)) % VT_MODE_COUNT);
        int m = (int)vt->p[VT_P_MODE];
        if (latch_mode(m)) {
            demo_chord((int)(vt->demo_time / bar), demo_notes);      /* virtual pads, one chord a bar */
            demo_chord_on = 1;
        }
        vt->demo_time += (double)frames / sr;
    }

    /* A mode change (knob, state or Demo) renames char1/char2/mod: pulse
     * is_loading so the UI re-reads them. Not in the first second, when a set
     * is being restored (the host's restore waits while is_loading says "1"). */
    const int want_mode = (int)vt->p[VT_P_ENGINE] == VT_ENGINE_VOICEFX ? VT_MODE_VOICEFX : (int)vt->p[VT_P_MODE];
    if (want_mode != vt->label_mode) {
        if (vt->label_mode >= 0 && vt->warm_samples >= vt->sample_rate)
            vt->loading_blocks = (int)(LABEL_PULSE_SEC * sr / (float)frames);
        vt->label_mode = want_mode;
    } else if (vt->loading_blocks > 0) {
        vt->loading_blocks--;
    }
    if (vt->warm_samples < vt->sample_rate) vt->warm_samples += frames;

    /* The engine that runs this block: the old one until its fade-out has
     * reached silence, then the chosen one (the very first block takes it at
     * once). A knob swept across several modes only lands on the last. */
    if (vt->run_mode < 0 || (vt->run_mode != want_mode && vt->sw_g <= 0.0f)) vt->run_mode = want_mode;
    const int mode = vt->run_mode;
    /* Voice FX has its own Mix/Drive knob, a fixed Level, and runs with the
     * automatic front end (Auto's input AGC and voice gate), Tone off and its
     * own Choir for the stereo chorus: nothing on the pages it hides can leave
     * it in a state its one page cannot show. */
    const int vf = mode == VT_MODE_VOICEFX;
    const float mix  = vf ? clampf(vt->p[VT_P_VF_MIX] / VFX_MIX_KNEE, 0.0f, 1.0f) : vt->p[VT_P_MIX];   /* Voice FX: wet/dry up to the knee, drive above */
    const float dry_g = mix >= 0.999f ? 0.0f : 1.0f - mix;          /* 100% wet: no dry at all */
    const float gain = vf ? 1.0f : powf(10.0f, vt->p[VT_P_GAIN] / 20.0f);
    const float sw_goal = mode == want_mode ? 1.0f : 0.0f;
    const int freeze_want = (int)vt->p[VT_P_FREEZE];   /* FREEZE_OFF / _REC / _LOOP */
    {   /* Freeze Len: bars at the host tempo (4 beats a bar), 0 = free punch-out */
        const int bars = k_freeze_bars[(int)vt->p[VT_P_FREEZE_LEN]];
        freeze_set_target(&vt->freeze, bars ? (int)((float)bars * (240.0f / vt->bpm) * sr) : 0);
        /* samples to the next downbeat (bar = 4 beats), for an armed take */
        int to_db = -1;
        if (vt->beat >= 0.0) {
            double next = ceil(vt->beat / 4.0 - 1e-9) * 4.0;
            to_db = (int)lround((next - vt->beat) * 60.0 / (double)vt->bpm * (double)sr);
        }
        freeze_set_clock(&vt->freeze, to_db);
    }
    const int auto_on = (int)vt->p[VT_P_GATE] == 0 || vf;           /* Gate = Auto (always, in Voice FX) */
    const float gate_manual_db = gate_db_of((int)vt->p[VT_P_GATE] ? (int)vt->p[VT_P_GATE] : vt->gate_mem);
    const int root = (int)vt->p[VT_P_KEY];        /* 0 = C */
    const float c1 = vt->p[VT_P_CHAR1], c2 = vt->p[VT_P_CHAR2], md = vt->p[VT_P_MOD];
    const float beauty = vt->p[VT_P_BEAUTY];
    const int scale = (int)vt->p[VT_P_SCALE];
    /* Auto runs its own gate; the manual Gate knob is hidden then and ignored. */
    const float gate_db = auto_on ? -60.0f : gate_manual_db;

    /* Tone: 2 x 1-pole lowpass, 400 Hz (0) .. 20 kHz (1); fully open = bypassed. */
    const int tone_on = vt->p[VT_P_TONE] < 0.999f && !vf;
    const float tone_a = 1.0f - expf(-2.0f * 3.14159265f * 400.0f * powf(50.0f, vt->p[VT_P_TONE]) / sr);

    /* Spread: two modulated taps (quadrature LFO) widen the wet signal. In
     * Robot, Spread pans the vocoder's odd/even bands instead. */
    const float spread = vt->p[VT_P_SPREAD];
    const float ch_amt = has_bands(mode) ? 0.0f : (vf ? vt->p[VT_P_VF_CHOIR] : spread) * 0.7f;
    const float ch_base = CHORUS_BASE_MS * 0.001f * sr, ch_depth = CHORUS_DEPTH_MS * 0.001f * sr;
    const float ch_inc = CHORUS_RATE_HZ / sr;
    const float k_ch = 1.0f - expf(-1.0f / (0.010f * sr));      /* 10 ms: the amount never steps */

    /* Latch switched: On takes the pads held right now as the chord; Off
     * empties it, which releases the latched voices at once. */
    if (latch_on(vt) && !vt->latch_was) {
        latch_clear(vt);
        for (uint32_t age = 0;;) {                                 /* oldest first */
            int pick = -1;
            for (int i = 0; i < VT_MAX_VOICES; i++)
                if (vt->voices[i].note >= 0 && vt->voices[i].age > age && (pick < 0 || vt->voices[i].age < vt->voices[pick].age)) pick = i;
            if (pick < 0) break;
            latch_add(vt, vt->voices[pick].note, vt->voices[pick].velocity);
            age = vt->voices[pick].age;
        }
    } else if (!latch_on(vt) && vt->latch_was) {
        latch_clear(vt);
    }
    vt->latch_was = latch_on(vt);
    const int latched = !demo_chord_on && latch_on(vt) && latch_mode(mode);
    int held[VT_MAX_VOICES], vel[VT_MAX_VOICES];
    for (int s = 0; s < VT_MAX_VOICES; s++) {
        held[s] = demo_chord_on ? demo_notes[s] : latched ? vt->latch_note[s] : vt->voices[s].note;
        vel[s] = demo_chord_on ? 100 : latched ? vt->latch_vel[s] : vt->voices[s].velocity;
    }

    const float c4 = slot_value(vt, VT_P_CHAR4), c5 = slot_value(vt, VT_P_CHAR5);
    if (mode == VT_MODE_ROBOT) {
        /* char1 formant (+-12 st, moves the synthesis bank), char2 sibilance,
         * mod VP-330 ensemble, spread odd/even band panning. Saw carrier with
         * a fixed 8-cent detune; band width at its middle setting. */
        /* Beauty: carrier detune 0..+-7.5 ct (0.5 ~ the old fixed +-4), plus
         * the vocoder's own polish (daft_set_beauty) and up to +35% band spread */
        daft_set_params(&vt->daft, 0, 15.0f * beauty, vt->p[VT_P_GLIDE],
                        0.5f, 0.0f, c2, (c1 - 0.5f) * 24.0f, gate_db);
        daft_set_beauty(&vt->daft, beauty);
        daft_set_auto(&vt->daft, auto_on);
        daft_set_drone_root(&vt->daft, root);                 /* the drone plays in the Key */
        daft_set_stereo(&vt->daft, fminf(1.0f, spread + 0.35f * beauty), md);
        assign_carriers(vt, &vt->daft, held, vel);
    } else if (mode == VT_MODE_NIGHTCALL) {
        /* DigiTech Talker style: one narrow pulse (duty 12..28%, 20% at 0.5) with
         * ~10% white noise, the pads or the drone as the carrier, the vocal
         * cavity EQ, and some of the unvoiced breath bled in. char1 formant,
         * char2 breath bleed (0..35%, 17.5% at 0.5), mod rasp (10..30%), char4
         * duty, char5 cavity peak (0..+12 dB at 1.2 kHz). Spread pans odd/even bands. */
        daft_set_params(&vt->nc, 1, 0.0f, vt->p[VT_P_GLIDE], 0.5f, 0.0f, 0.4f, (c1 - 0.5f) * 24.0f, gate_db);
        daft_set_beauty(&vt->nc, 0.0f);
        daft_set_auto(&vt->nc, auto_on);
        daft_set_drone_root(&vt->nc, root);
        daft_set_stereo(&vt->nc, spread, 0.0f);
        daft_set_talker(&vt->nc, 1, 0.12f + 0.16f * c4, 0.10f + 0.20f * md, 0.35f * c2, 12.0f * c5);
        assign_carriers(vt, &vt->nc, held, vel);
    } else if (vf) {
        vfx_set(&vt->vfx, vt->p[VT_P_VF_VOWEL], vt->p[VT_P_VF_TRACT], vt->p[VT_P_VF_PEAK], vt->p[VT_P_VF_CHOIR],
                vt->p[VT_P_VF_BREATH], vt->p[VT_P_VF_ATTACK], vt->p[VT_P_VF_RELEASE], vt->p[VT_P_VF_MIX]);
    } else if (is_vox(mode)) {
        if (mode == VT_MODE_DALEK) vox_set_dalek(&vt->vox, c1, c2, md, c4, c5);
        else if (mode == VT_MODE_HARMONY) {
            /* the tracker runs for Harmony (cher_push below): the grain is set to whole periods of the voice */
            cher_set_params(&vt->cher, root, scale, 0.0f, gate_db, -1, 0.0f);
            cher_set_auto(&vt->cher, auto_on);
            cher_set_clarity(&vt->cher, 0.65f);
            vox_set_harmony(&vt->vox, c1, c2, md, c4, c5, spread, held, vt->cher.voiced ? vt->cher.f0 : 0.0f);
        }
        else vox_set_walkie(&vt->vox, c1, c2, md, c4, c5);
    } else {
        /* Every other mode runs on the tracker. The lead modes take a held pad
         * as the target note; Chords/Prism use the pads as harmony instead.
         * Prism is a 0 ms hard snap whatever Retune says. Snap at Beauty 0
         * is instant (no slew at all, Retune and Glide aside): a sliding
         * voice steps from semitone to semitone. HardTune is always instant. */
        const int lead_override = (mode == VT_MODE_SNAP || mode == VT_MODE_MORPH || mode == VT_MODE_DOUBLE || mode == VT_MODE_HARDTUNE);
        const int snap_instant = mode == VT_MODE_SNAP && beauty <= SNAP_INSTANT_BEAUTY;
        const float retune = (snap_instant || mode == VT_MODE_HARDTUNE) ? -1.0f : mode == VT_MODE_PRISM ? 0.0f : vt->p[VT_P_RETUNE];
        cher_set_params(&vt->cher, root, scale, retune, gate_db,
                        lead_override ? newest_held_note(vt) : -1, snap_instant ? 0.0f : vt->p[VT_P_GLIDE]);
        cher_set_auto(&vt->cher, auto_on);
        /* Snap's char2 / HardTune's char5 is tracking sensitivity: 0.5 = the default 0.65 clarity. */
        cher_set_clarity(&vt->cher, mode == VT_MODE_SNAP ? 0.9f - 0.5f * c2 : mode == VT_MODE_HARDTUNE ? 0.9f - 0.5f * c5 : 0.65f);
        if (mode == VT_MODE_CHORDS) {
            imogen_set_beauty(&vt->imogen, beauty);
            imogen_set_targets(&vt->imogen, &vt->cher, held, auto_on, root, scale,
                               retune, vt->p[VT_P_GLIDE], fminf(1.0f, spread + 0.4f * beauty),   /* Beauty: choir spread */
                               c2 < 0.5f ? VT_VOICING_STANDARD : VT_VOICING_WIDE);
            imogen_set_character(&vt->imogen, 2.0f * c1, md);
        }
    }
    if (!has_bands(mode) && !is_vox(mode) && !vf && mode != VT_MODE_CHORDS) {
        fx_set(&vt->fx, &vt->cher, mode, c1, c2, md, spread, vt->p[VT_P_GLIDE], auto_on, root, scale, held);
        fx_set_extra(&vt->fx, c4, c5);
        fx_set_beauty(&vt->fx, beauty);
    }
    const int head = vt_mode_headroom(mode);
    /* the voice gate's threshold (RMS): Auto's, or the Gate knob */
    const float vg_thr = auto_on ? SG_THR : powf(10.0f, gate_manual_db / 20.0f);

    for (int i = 0; i < frames; i++) {
        float in_l = lr[2 * i], in_r = lr[2 * i + 1];

        /* Input stage: the demo phrase replaces the mic; Freeze then records
         * or loops whichever it is. Both the engine and the dry path see it. */
        if (vt->demo_active) in_l = in_r = demo_process(&vt->demo);   /* every mode, Snap included: Key/Scale act on it */
        /* Mic pre-conditioning: a 1-pole high-pass at 120 Hz on everything
         * (engines, gates, dry path, Freeze takes), so pad taps and chassis
         * thumps on the built-in mic don't open the voice gate, ride the AGC,
         * fill the vocoder's low analysis bands, or sit in the dry blend. */
        {
            const float yl = vt->hp_a * (vt->hp_y[0] + in_l - vt->hp_x[0]);
            const float yr = vt->hp_a * (vt->hp_y[1] + in_r - vt->hp_x[1]);
            vt->hp_x[0] = in_l; vt->hp_x[1] = in_r;
            vt->hp_y[0] = fabsf(yl) < 1e-15f ? 0.0f : yl;
            vt->hp_y[1] = fabsf(yr) < 1e-15f ? 0.0f : yr;
            in_l = vt->hp_y[0];
            in_r = vt->hp_y[1];
        }
        float fmix;
        float loop = freeze_process(&vt->freeze, 0.5f * (in_l + in_r), freeze_want, &fmix);
        if (fmix > 0.0f) {
            in_l += (loop - in_l) * fmix;
            in_r += (loop - in_r) * fmix;
        }
        const float mhp = 0.5f * (in_l + in_r);
        /* Auto: input AGC, so soft singing lands in the engines' sweet spot,
         * and the silence gate, so pauses stay quiet instead of the AGC
         * lifting the room into hiss. */
        {
            float a = fabsf(mhp) * 1.5707963f;
            vt->in_env += (a - vt->in_env) * (a > vt->in_env ? vt->k_agc_att : vt->k_agc_rel);
            if (vt->in_env < 1e-12f) vt->in_env = 0.0f;
            vt->sg_ms += (mhp * mhp - vt->sg_ms) * vt->k_sg_env_a;
            if (vt->sg_ms < 1e-20f) vt->sg_ms = 0.0f;
            const int voice = vt->sg_ms >= vg_thr * vg_thr;              /* RMS >= threshold */
            if (voice) {
                vt->sg_open = 1;
                vt->sg_hold = vt->sg_hold_n;
            } else if (vt->sg_hold > 0) {
                vt->sg_hold--;
            } else {
                vt->sg_open = 0;
            }
            if (auto_on) {
                if (voice)                                              /* frozen the moment it drops */
                    vt->agc_goal = clampf(IN_AGC_TARGET / fmaxf(vt->in_env, 1e-6f), IN_AGC_MIN, IN_AGC_MAX);
            } else {
                vt->agc_goal = 1.0f;
            }
            vt->agc_gain += (vt->agc_goal - vt->agc_gain) * vt->k_agc_gain;
            const float sg_goal = (!auto_on || vt->sg_open) ? 1.0f : SG_DEPTH;   /* the expander: Auto only */
            vt->sg_gain += (sg_goal - vt->sg_gain) * (sg_goal > vt->sg_gain ? vt->k_sg_att : vt->k_sg_rel);
            in_l *= vt->agc_gain * vt->sg_gain;
            in_r *= vt->agc_gain * vt->sg_gain;
        }
        const float mono = 0.5f * (in_l + in_r);

        float wet_l, wet_r;
        if (mode != VT_MODE_HARMONY) vox_listen(&vt->vox, mono);   /* Harmony's ring stays current (its own process writes it while it runs) */
        if (has_bands(mode)) {
            daft_t *dd = mode == VT_MODE_NIGHTCALL ? &vt->nc : &vt->daft;
            daft_set_voice_present(dd, vt->sg_open);         /* the voice gate shuts the vocoder */
            daft_process_lr(dd, mono, &wet_l, &wet_r);
            /* keep the tracker's delay line and analysis window current
             * (no pitch analysis: that doubled Robot's CPU), so a switch to a
             * pitch mode reads the live voice, not audio from before Robot */
            cher_listen(&vt->cher, mono);
            vt->dry_d *= 0.998f;                             /* vocoder is ~zero latency */
            if (vt->dry_d < 1e-6f) vt->dry_d = 0.0f;
        } else if (vf) {
            vfx_process(&vt->vfx, mono, &wet_l, &wet_r);
            cher_listen(&vt->cher, mono);                    /* a switch back to a pitch mode finds the live voice */
            dry_follow(vt, 0.0f);
        } else if (is_vox(mode)) {
            vox_process(&vt->vox, mode, mono, vt->sg_open, &wet_l, &wet_r);
            if (mode == VT_MODE_HARMONY) cher_push(&vt->cher, mono);   /* its grain follows the voice's pitch */
            else cher_listen(&vt->cher, mono);               /* as for the vocoders: a switch to a pitch mode finds the live voice */
            dry_follow(vt, vox_latency(&vt->vox, mode));
        } else if (mode == VT_MODE_CHORDS) {
            imogen_process(&vt->imogen, &vt->cher, mono, &wet_l, &wet_r);
            if (!auto_on) { wet_l *= CHORDS_MANUAL_TRIM; wet_r *= CHORDS_MANUAL_TRIM; }
            dry_follow(vt, imogen_latency(&vt->imogen));      /* the voices' PSOLA latency */
        } else {
            fx_process(&vt->fx, &vt->cher, mono, &wet_l, &wet_r);
            dry_follow(vt, fx_latency(&vt->fx, &vt->cher));
        }
        if (vt->sw_g != sw_goal)                             /* mode switch fade */
            vt->sw_g = sw_goal > vt->sw_g ? fminf(1.0f, vt->sw_g + vt->k_sw) : fmaxf(0.0f, vt->sw_g - vt->k_sw);
        wet_l *= vt->sw_g;
        wet_r *= vt->sw_g;

        if (tone_on) {
            vt->tone_z[0][0] += (wet_l - vt->tone_z[0][0]) * tone_a;
            vt->tone_z[0][1] += (vt->tone_z[0][0] - vt->tone_z[0][1]) * tone_a;
            vt->tone_z[1][0] += (wet_r - vt->tone_z[1][0]) * tone_a;
            vt->tone_z[1][1] += (vt->tone_z[1][0] - vt->tone_z[1][1]) * tone_a;
            for (int c = 0; c < 2; c++)
                for (int k = 0; k < 2; k++)
                    if (fabsf(vt->tone_z[c][k]) < 1e-15f) vt->tone_z[c][k] = 0.0f;
            wet_l = vt->tone_z[0][1];
            wet_r = vt->tone_z[1][1];
        }

        vt->chorus[vt->ch_w] = 0.5f * (wet_l + wet_r);
        vt->ch_w = (vt->ch_w + 1) & (VT_CHORUS_LEN - 1);
        vt->ch_amt_s += (ch_amt - vt->ch_amt_s) * k_ch;
        if (vt->ch_amt_s < 1e-5f && ch_amt == 0.0f) vt->ch_amt_s = 0.0f;
        if (vt->ch_amt_s > 0.0f) {
            vt->ch_phase += ch_inc;
            if (vt->ch_phase >= 1.0f) vt->ch_phase -= 1.0f;
            float ph = 2.0f * 3.14159265f * vt->ch_phase;
            wet_l += vt->ch_amt_s * ring_read(vt->chorus, VT_CHORUS_LEN - 1, vt->ch_w, ch_base + ch_depth * sinf(ph));
            wet_r += vt->ch_amt_s * ring_read(vt->chorus, VT_CHORUS_LEN - 1, vt->ch_w, ch_base + ch_depth * cosf(ph));
        }

        vt->dry[0][vt->dry_w] = in_l;
        vt->dry[1][vt->dry_w] = in_r;
        vt->dry_w = (vt->dry_w + 1) & (VT_DRY_DELAY - 1);
        float dry_l = ring_read(vt->dry[0], VT_DRY_DELAY - 1, vt->dry_w, vt->dry_d + 1.0f);
        float dry_r = ring_read(vt->dry[1], VT_DRY_DELAY - 1, vt->dry_w, vt->dry_d + 1.0f);

        float out_l = dry_l * dry_g + wet_l * mix;
        float out_r = dry_r * dry_g + wet_r * mix;
        if (head) { out_l = headroom(out_l); out_r = headroom(out_r); }   /* new modes: nothing past -6.1 dBFS at Level 0 */

        vt->dg_out_ms += (out_l * out_l - vt->dg_out_ms) * vt->k_sg_env_a;
        if (vt->dg_out_ms < 1e-20f) vt->dg_out_ms = 0.0f;
        if (vt->dg_out_ms > vt->dg_out_pk) vt->dg_out_pk = vt->dg_out_ms;
        if (vt->sg_ms > vt->dg_in_pk) vt->dg_in_pk = vt->sg_ms;
        if (vt->sg_ms < vt->dg_in_min) vt->dg_in_min = vt->sg_ms;     /* ... falls at once */
        lr[2 * i]     = soft_limit(out_l * gain);
        lr[2 * i + 1] = soft_limit(out_r * gain);
    }
    {   /* diag: holds decay over ~1 s, gate-open share averaged over ~1 s */
        const float k = 1.0f - expf(-(float)frames / sr);
        vt->dg_in_pk -= vt->dg_in_pk * k;
        vt->dg_out_pk -= vt->dg_out_pk * k;
        vt->dg_open += ((vt->sg_open ? 1.0f : 0.0f) - vt->dg_open) * k;
        vt->dg_ropen += ((vt->daft.gate_open ? 1.0f : 0.0f) - vt->dg_ropen) * k;
        vt->dg_in_min += (vt->sg_ms - vt->dg_in_min) * k;            /* rises slowly ... */
        if (vt->dg_in_pk < 1e-20f) vt->dg_in_pk = 0.0f;
        if (vt->dg_out_pk < 1e-20f) vt->dg_out_pk = 0.0f;
    }
    if (freeze_take_full(&vt->freeze)) vt->p[VT_P_FREEZE] = (float)FREEZE_LOOP;   /* the knob follows: Rec -> Loop */

    if (has_bands(mode)) {
        for (int b = 0; b < VT_NUM_BANDS; b++) vt->viz_bands[b] = daft_band_level(mode == VT_MODE_NIGHTCALL ? &vt->nc : &vt->daft, b);
    } else if (is_vox(mode) && mode != VT_MODE_HARMONY) {
        vt->viz_f0 = vt->viz_target = vt->viz_cents = 0.0f;           /* nothing is tracked */
    } else {
        vt->viz_f0 = vt->cher.f0;
        vt->viz_target = vt->cher.target_hz;
        vt->viz_cents = vt->cher.cents_err;
    }
}

void dsp_process_block(vt_t *vt, int16_t *audio, int frames)
{
    if (!vt || !audio || frames <= 0) return;

    float tmp[2 * VT_MAX_BLOCK];            /* stack: no allocation on the audio thread */
    while (frames > 0) {
        int n = frames > VT_MAX_BLOCK ? VT_MAX_BLOCK : frames;
        for (int i = 0; i < 2 * n; i++) tmp[i] = (float)audio[i] * (1.0f / 32768.0f);
        dsp_process_block_f(vt, tmp, n);
        for (int i = 0; i < 2 * n; i++) {
            float s = clampf(tmp[i], -1.0f, 32767.0f / 32768.0f) * 32768.0f;
            audio[i] = (int16_t)lrintf(s);
        }
        audio += 2 * n;
        frames -= n;
    }
}

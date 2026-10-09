/*
 * TinyVoc engine interface (host-independent).
 *
 * This header knows nothing about Schwung. src/dsp/plugin.c adapts it to the
 * audio_fx_api_v2 ABI, and tests/ links it directly.
 *
 * Realtime contract (Schwung runs every entry point on the SPI audio callback,
 * ~2.3 ms per 128-frame block): after dsp_init() nothing here may allocate,
 * lock, or do I/O. All buffers are sized in dsp_init().
 */
#ifndef TINYVOC_DSP_H
#define TINYVOC_DSP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VT_SAMPLE_RATE   44100
#define VT_MAX_VOICES    4
#define VT_NUM_BANDS     16
#define VT_MAX_BLOCK     512     /* upper bound on frames per call; Move uses 128 */
#define VT_INPUT_HPF_HZ  120.0f  /* mic pre-conditioning: 1-pole high-pass on the whole input */
#define VT_GATE_MIN_DB   (-60)   /* Gate: option 0 is Auto, option i (1..21) is -60 + 3 (i - 1) dB */
#define VT_GATE_STEP_DB  3
#define VT_GATE_OPTS     22

/* Parameter ids. Order matches module.json chain_params (which then adds three
 * read-only readouts). The order is not the page layout: that is TUNE, SPACE
 * and MORE in scripts/gen_module_json.py. char1/char2/mod are 0..1 macros
 * whose meaning (and served knob name) depends on the mode. */
typedef enum {
    VT_P_MODE = 0,    /* enum  Snap, Robot, Chords, Morph, Prism, Double, Tape */
    VT_P_KEY,           /* enum  C..B: the key, set by hand (Move does not share its project key) */
    VT_P_SCALE,         /* enum  Chromatic, Major, Minor, Pentatonic, Dorian */
    VT_P_RETUNE,        /* ms 0..500, 0 = instant hard snap */
    VT_P_CHAR1,         /* macro 1 */
    VT_P_CHAR2,         /* macro 2 */
    VT_P_GATE,          /* enum  Auto, -60 dB .. 0 dB: Auto is the automatic front end (input
                         *       AGC + its own gate, Robot drone/AGC, scale chords with no pads in
                         *       Chords/Prism); a dB value is Auto Off with that manual threshold.
                         *       Steps of 3 dB. The old "auto" key is still accepted (dsp.c). */
    VT_P_MIX,          /* 0..1 wet/dry */
    VT_P_GAIN,          /* dB -12..+12 output level (soft-limited) */
    VT_P_DELAY,         /* ms 0..1000; < 1 ms = delay off */
    VT_P_FEEDBACK,      /* 0..0.95, 3.5 kHz damped repeats */
    VT_P_SPREAD,        /* 0..1 stereo width (ADT chorus; Chords also pans its voices) */
    VT_P_TONE,          /* 0..1 lowpass on the wet signal, 1 = open */
    VT_P_GLIDE,         /* ms 0..500 portamento between pad notes / carrier notes */
    VT_P_MOD,           /* macro 3 (contextual modulation) */
    VT_P_REVERB,        /* 0..1 plate reverb: return level and decay together, 0 = off */
    VT_P_FREEZE_LEN,    /* enum  Free, 1 Bar, 2 Bars, 4 Bars: Rec stops itself after that many bars */
    VT_P_BEAUTY,        /* 0..1 per-mode polish (stereo, detune, air, warmth); 0 = the dry engine */
    VT_P_LATCH,         /* enum  Off, On: Robot/Chords/Prism keep the last pad chord after release */
    /* Performance toggles (knobs on SPACE), not saved in "state" */
    VT_P_FREEZE,       /* enum  Off, On: loop the last 4 s of input into the engine */
    VT_P_DEMO,          /* enum  Off, On: built-in sung phrase, modes advance every 4 bars */
    VT_P_COUNT
} vt_param_id;

/*
 * Macro map for Robot and Chords (the others are in modes.h):
 *   Robot   char1 formant -12..+12 st (synthesis bank centres)   char2 sibilance noise
 *           mod VP-330 ensemble chorus      spread odd/even band panning (instead of ADT)
 *   Chords  char1 harmony level (0..2x)    char2 voicing (<0.5 standard, else wide)   mod humanize
 */
typedef enum {
    VT_MODE_SNAP = 0, VT_MODE_ROBOT, VT_MODE_CHORDS, VT_MODE_MORPH,
    VT_MODE_PRISM, VT_MODE_DOUBLE, VT_MODE_TAPE, VT_MODE_COUNT   /* Talkbox removed 2026-10-09 */
} vt_mode;
/* Beauty polishes Snap, Robot, Chords and Prism; Morph, Double and Tape never read it. */
static inline int vt_beauty_acts(int mode)
{
    return mode == VT_MODE_SNAP || mode == VT_MODE_ROBOT || mode == VT_MODE_CHORDS || mode == VT_MODE_PRISM;
}
typedef enum { VT_SCALE_CHROMATIC = 0, VT_SCALE_MAJOR, VT_SCALE_MINOR, VT_SCALE_PENTATONIC, VT_SCALE_DORIAN } vt_scale;
typedef enum { VT_PTYPE_FLOAT = 0, VT_PTYPE_ENUM } vt_ptype;

typedef struct {
    const char *key;
    vt_ptype type;
    float min, max, def;
    const char *const *options;   /* enum only */
    int num_options;
    int transient;                /* performance toggle: left out of "state" */
} vt_param_def;

/* Opaque engine instance. One per FX slot; Schwung may create several. */
typedef struct vt vt_t;

/* Lifecycle. dsp_init returns NULL on failure. */
vt_t *dsp_init(int sample_rate);
void  dsp_cleanup(vt_t *vt);

/*
 * Audio. dsp_process_block is the Schwung format: stereo interleaved int16,
 * processed in place. dsp_process_block_f is the float core (same layout,
 * +-1.0 nominal) used by tests. The vocal is read as (L+R)/2.
 */
void dsp_process_block(vt_t *vt, int16_t *audio_inout, int frames);
void dsp_process_block_f(vt_t *vt, float *lr_inout, int frames);

/* Params by string key/value, as the host delivers them. Enums accept an
 * option name or an index. Out-of-range floats are clamped.
 * set returns 0 on success, -1 on unknown key/value.
 * get returns bytes written (excluding NUL) or -1 on unknown key / short buffer.
 * Besides the params, get accepts "viz" (see below), "voices", "scope",
 * "chain_params", "ui_hierarchy", "is_loading" and the readouts "pitch",
 * "target", "cents".
 * "state" (get and set) is a flat JSON object holding every saved param, e.g.
 *   {"mode":"Snap","auto":"On","key":"C",...,"retune":0,"mix":1}
 * set applies every recognised member and ignores the rest (unknown keys,
 * nested values), so older/newer snapshots load cleanly. */
int dsp_set_param(vt_t *vt, const char *key, const char *val);
int dsp_get_param(vt_t *vt, const char *key, char *buf, int buf_len);

/* Raw 3-byte-or-less MIDI (status, d1, d2). Drives the carrier synth. */
void dsp_handle_midi(vt_t *vt, const uint8_t *msg, int len);

/* Host tempo (BPM) for Demo's 4-bar mode changes; 120 until set. */
void dsp_set_bpm(vt_t *vt, float bpm);
/* Host transport: beats since transport start at the start of the next block
 * (host->get_beat_position), < 0 when stopped. Bars are 4 beats. Freeze uses
 * it to start a bar take on the next downbeat. */
void dsp_set_transport(vt_t *vt, double beat);

/* Introspection (tests, UI plumbing). */
const vt_param_def *dsp_param_def(int id);          /* NULL if out of range */
int   dsp_param_index(const char *key);             /* -1 if unknown */
/* Knob name of macro `which` (0 char1, 1 char2, 2 mod) in `mode`; NULL if out of range. */
const char *dsp_macro_name(int mode, int which);
int   dsp_active_voices(const vt_t *vt);

/*
 * Visualizer feed, via get_param("viz"). Text, comma separated, mode dependent:
 *   Cher: "c,<f0_hz>,<target_hz>,<cents_error>"    (f0 = 0 when unvoiced)
 *   Daft: "d,<b0>,<b1>,...,<b15>"                   (envelope 0..255 per band)
 * Text keeps the host/JS boundary trivial; at 60 fps the cost is negligible.
 */

#ifdef __cplusplus
}
#endif
#endif /* TINYVOC_DSP_H */

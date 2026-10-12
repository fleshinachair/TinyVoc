/*
 * TinyVoice engine interface (host-independent).
 *
 * This header knows nothing about Schwung. src/dsp/plugin.c adapts it to the
 * audio_fx_api_v2 ABI, and tests/ links it directly.
 *
 * Realtime contract (Schwung runs every entry point on the SPI audio callback,
 * ~2.3 ms per 128-frame block): after dsp_init() nothing here may allocate,
 * lock, or do I/O. All buffers are sized in dsp_init().
 */
#ifndef TINYVOICE_DSP_H
#define TINYVOICE_DSP_H

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
 * read-only readouts). The order is not the page layout: that is VOICE, PITCH,
 * and MORE in scripts/gen_module_json.py. (Delay, Feedback and Reverb
 * were removed on 2026-10-11: a set saved with them ignores those keys.) char1/char2/mod are 0..1 macros
 * whose meaning (and served knob name) depends on the mode. */
typedef enum {
    VT_P_MODE = 0,    /* enum  the 11 modes below, then Morph and Double */
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
    VT_P_SPREAD,        /* 0..1 stereo width (ADT chorus; Chords also pans its voices) */
    VT_P_TONE,          /* 0..1 lowpass on the wet signal, 1 = open */
    VT_P_GLIDE,         /* ms 0..500 portamento between pad notes / carrier notes */
    VT_P_MOD,           /* macro 3 (contextual modulation) */
    VT_P_FREEZE_LEN,    /* enum  Free, 1 Bar, 2 Bars, 4 Bars: Rec stops itself after that many bars */
    VT_P_BEAUTY,        /* 0..1 per-mode polish (stereo, detune, air, warmth); 0 = the dry engine */
    VT_P_LATCH,         /* enum  Off, On: Robot/Chords/Prism keep the last pad chord after release */
    VT_P_CHAR4,         /* macro 4 (the VOICE page's knob 5): per mode, a new control or an alias of an old param */
    VT_P_CHAR5,         /* macro 5 (knob 6) */
    /* Engine switch and the Voice FX page (its own params, so the vocoder side's
     * settings are never touched by it). Hidden from the Vocoder layout and
     * the other way round by the ui_hierarchy's `modes` (mode_param = engine). */
    VT_P_ENGINE,        /* enum  Vocoder, VoiceFX: the module's two personalities */
    VT_P_VF_VOWEL,      /* 0..1  A E I O U sweep */
    VT_P_VF_TRACT,      /* 0..1  formant scale (gender / throat), 0.5 = natural */
    VT_P_VF_PEAK,       /* 0..1  formant Q */
    VT_P_VF_CHOIR,      /* 0..1  L/R formant detune and stereo spread */
    VT_P_VF_BREATH,     /* 0..1  noise through the formants + consonant air */
    VT_P_VF_ATTACK,     /* 0..1  onset swell */
    VT_P_VF_RELEASE,    /* 0..1  how long the vowel rings on */
    VT_P_VF_MIX,        /* 0..1  Mix/Drive: wet/dry up to 0.7, then all wet with the drive climbing */
    VT_P_VF_PRESET,     /* index into the Voice FX bank (presets.c); writing it loads that preset into the
                         * eight knobs above. Transient: a saved state holds the knob values, not the preset. */
    /* Performance toggles (knobs on MORE), not saved in "state" */
    VT_P_FREEZE,       /* enum  Off, On: loop the last 4 s of input into the engine */
    VT_P_DEMO,          /* enum  Off, On: built-in sung phrase, modes advance every 4 bars */
    VT_P_COUNT
} vt_param_id;

/*
 * Modes, in the order of the Mode knob: the vocoder and carrier modes first
 * (Robot, Snap, Chords, Prism, Tape, Nightcall), then the voice transformers
 * (Scream, Dalek, HardTune, Harmony, Walkie). Morph and Double are the two
 * older PSOLA modes: still here, after the roster.
 *
 * The VOICE page is Mode, five contextual knobs (char1 char2 mod char4 char5),
 * Mix, Level (gain). The five are named per mode (k_macro_names in dsp.c):
 *   Robot     Shift Noise Ensemble          Beauty* Glide*
 *   Snap      Formant Sensitivity Vibrato   Retune* Beauty*
 *   Chords    Level Voicing Humanize        Retune* Beauty*
 *   Prism     Voicing Warmth Humanize       Refract Shimmer
 *   Tape      Drop Flutter Saturate         Tone* Hiss
 *   Nightcall Formant Breath Rasp           Duty Cavity
 *   Scream    Pitch Throat Grit             Narrow Line
 *   Dalek     Shift Ring Metal              Drive Width
 *   HardTune  Amount Formant Vibrato        Glide* Sens
 *   Harmony   Lead Stack Detune             Grain Micro
 *   Walkie    Peak Drive Static             Squelch Band
 *   Morph     Throat Drive Wobble           Retune* Glide*
 *   Double    Detune Width Drift            Retune* Glide*
 * (* an alias of the param of that name: the same value, read and written
 * from either knob; see slot_alias in dsp.c.)
 */
typedef enum {
    VT_MODE_ROBOT = 0, VT_MODE_SNAP, VT_MODE_CHORDS, VT_MODE_PRISM, VT_MODE_TAPE, VT_MODE_NIGHTCALL,
    VT_MODE_SCREAM, VT_MODE_DALEK, VT_MODE_HARDTUNE, VT_MODE_HARMONY, VT_MODE_WALKIE,
    VT_MODE_MORPH, VT_MODE_DOUBLE, VT_MODE_COUNT   /* Talkbox removed 2026-10-09 */
} vt_mode;
#define VT_MACROS 5       /* char1, char2, mod, char4, char5 */
/* The engine the Voice FX personality runs as: an internal id one past the
 * Mode knob's 13 (the Mode knob is the Vocoder personality's; VT_MODE_COUNT
 * stays 13 for every table indexed by it). */
#define VT_MODE_VOICEFX VT_MODE_COUNT
typedef enum { VT_ENGINE_VOCODER = 0, VT_ENGINE_VOICEFX } vt_engine;

/* Modes whose output stage keeps its peaks under TINYVOICE_OUTPUT_HEADROOM. */
static inline int vt_mode_headroom(int mode)
{
    return mode == VT_MODE_NIGHTCALL || (mode >= VT_MODE_SCREAM && mode <= VT_MODE_WALKIE) || mode == VT_MODE_VOICEFX;
}
/* The ceiling those modes keep into the master buffer: -6.1 dBFS (10^(-6.1/20)).
 * Applied after Spread and before Level, so at Level 0 dB no
 * peak passes it. */
#define TINYVOICE_OUTPUT_HEADROOM 0.4955f

/* Does this knob do anything in this mode? Where not, the screen shows "N/A"
 * (canvas.js carries the same table) and the DSP ignores turns of it.
 *   Key     Robot (the drone), Snap, Chords, Prism, Nightcall (the drone), HardTune, Morph, Double
 *   Scale   the pitch-snapping modes: Snap, Chords, Prism, HardTune, Morph, Double
 *   Retune  Snap, Chords, Morph, Double (Prism is a fixed 0 ms snap, HardTune is instant)
 *   Glide   Robot, Snap, Chords, Prism, Nightcall (carrier), HardTune, Morph, Double
 *   Beauty  Snap, Robot, Chords, Prism (the other engines never read it) */
static inline int vt_param_acts(int id, int mode)
{
    const int pad_lead = mode == VT_MODE_SNAP || mode == VT_MODE_CHORDS || mode == VT_MODE_PRISM ||
                         mode == VT_MODE_MORPH || mode == VT_MODE_DOUBLE || mode == VT_MODE_HARDTUNE;
    switch (id) {
    case VT_P_KEY:    return pad_lead || mode == VT_MODE_ROBOT || mode == VT_MODE_NIGHTCALL;
    case VT_P_SCALE:  return pad_lead;
    case VT_P_RETUNE: return mode == VT_MODE_SNAP || mode == VT_MODE_CHORDS || mode == VT_MODE_MORPH || mode == VT_MODE_DOUBLE;
    case VT_P_GLIDE:  return pad_lead || mode == VT_MODE_ROBOT || mode == VT_MODE_NIGHTCALL;
    case VT_P_BEAUTY: return mode == VT_MODE_SNAP || mode == VT_MODE_ROBOT || mode == VT_MODE_CHORDS || mode == VT_MODE_PRISM;
    default:          return 1;
    }
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

/* Raw 3-byte-or-less MIDI (status, d1, d2). Drives the carrier synth.
 * Channel messages on any channel but the one set by dsp_set_midi_channel
 * are ignored (counted for "diag"). */
void dsp_handle_midi(vt_t *vt, const uint8_t *msg, int len);
/* The slot's receive channel, 0-15 (host->slot_recv_channel); -1 = omni (the
 * slot receives All, or there is no slot context). Omni until set. */
void dsp_set_midi_channel(vt_t *vt, int channel);
/* The host's MIDI source for the next dsp_handle_midi call only (kept with
 * the last note-ons for "diag"). VT_MIDI_SOURCE_BROADCAST (the host's
 * MOVE_MIDI_SOURCE_FX_BROADCAST) is every track's MIDI sent to every audio
 * FX: its channel messages are ignored. The slot's own MIDI comes on its own
 * path (seen on the device: every own-channel note arrives as source 2 and
 * again as a broadcast; other tracks' notes only as broadcasts). */
#define VT_MIDI_SOURCE_BROADCAST 4
void dsp_set_midi_source(vt_t *vt, int source);

/* Host tempo (BPM) for Demo's 4-bar mode changes; 120 until set. */
void dsp_set_bpm(vt_t *vt, float bpm);
/* Host transport: beats since transport start at the start of the next block
 * (host->get_beat_position), < 0 when stopped. Bars are 4 beats. Freeze uses
 * it to start a bar take on the next downbeat. */
void dsp_set_transport(vt_t *vt, double beat);

/* Introspection (tests, UI plumbing). */
const vt_param_def *dsp_param_def(int id);          /* NULL if out of range */
int   dsp_param_index(const char *key);             /* -1 if unknown */
/* Knob name of macro `which` (0 char1, 1 char2, 2 mod, 3 char4, 4 char5) in `mode`; NULL if out of range. */
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
#endif /* TINYVOICE_DSP_H */

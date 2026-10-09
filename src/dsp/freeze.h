/*
 * Freeze: a punch-in looper on the input, up to 16 seconds.
 *
 *   Off  -> Rec    a fresh take: the write position goes to 0 and the input
 *                  is recorded (and still heard live) until you stop
 *   Rec  -> Loop   the take is locked at exactly the length recorded and loops
 *                  from its start into the engine instead of the live input
 *   Rec  -> Off    the take is locked and kept; you hear live input
 *   Off  -> Loop   loops the kept take (nothing recorded: stays live)
 *   Loop -> Rec    a new take replaces the old one
 *   a full buffer  locks and starts looping by itself (freeze_take_full)
 *
 * Bar mode with the transport running (freeze_set_clock >= 0): Rec arms
 * instead of recording, and the take starts on the exact sample of the next
 * downbeat, so it lines up with Move's bar grid. Off (or Loop) while armed
 * cancels. Transport stopped, or Free: Rec records at once, as before.
 *
 * Bar mode (freeze_set_target > 0, from Freeze Len and the host tempo): Rec
 * stops by itself once the take is exactly that long and starts looping
 * (freeze_take_full, as for a full buffer), so a 2-bar take is a 2-bar loop
 * without having to stop in time. It records 5 ms past the target so the
 * wrap crossfade comes out of that extra: the loop period is exactly the
 * target, and the loop stays in time with the tempo. Stopping early (Loop or
 * Off) still locks what was recorded.
 *
 * The loop wraps with a 5 ms equal-power crossfade (the take's last 5 ms fade
 * into its first), and live <-> loop switches fade over 10 ms: no clicks.
 * Whatever the engine does on top (pads, Robot carriers, knobs) runs on the
 * loop exactly as on a live voice.
 *
 * Fixed-size 16-bit buffer (sized for 48 kHz, ~1.5 MB); embedded in vt_t, no
 * allocation after init.
 */
#ifndef TINYVOC_FREEZE_H
#define TINYVOC_FREEZE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FREEZE_SEC 16.0f
#define FREEZE_MAX (16 * 48000)
#define FREEZE_XF_SEC 0.005f         /* loop-wrap crossfade */

enum { FREEZE_OFF = 0, FREEZE_REC, FREEZE_LOOP };   /* the param's values */
#define FREEZE_ARMED 3                               /* internal: Rec asked, waiting for the downbeat */

typedef struct {
    int16_t buf[FREEZE_MAX];
    int   len;          /* capacity: FREEZE_SEC at this sample rate */
    int   state;        /* FREEZE_OFF / _REC / _LOOP: what it is doing */
    int   asked;        /* the param's last value (transitions fire on its changes) */
    int   rec_len;      /* samples in the take being recorded */
    int   loop_len;     /* locked take length (0 = nothing to loop) */
    int   pos;          /* playback position, 0 .. loop_len - xf */
    int   xf;           /* wrap crossfade length */
    int   full;         /* the take ended itself (bar count reached or buffer full) */
    int   target;       /* bar mode: loop period in samples, 0 = free */
    int   to_downbeat;  /* samples until the next downbeat, -1 = transport stopped */
    float mix, k_mix;   /* 0 = live, 1 = loop, and its slew */
} freeze_t;

void  freeze_init(freeze_t *f, float sample_rate);

/* One sample. want: FREEZE_OFF / _REC / _LOOP (the param). Records `in` while
 * recording; returns the loop sample and sets *mix (0 = use the live input,
 * 1 = use the returned loop, in between = fading). */
float freeze_process(freeze_t *f, float in, int want, float *mix);

/* Bar mode: the loop period in samples (0 = free punch-out). Clamped so the
 * take, 5 ms crossfade included, fits the buffer. Set it every block. */
void  freeze_set_target(freeze_t *f, int samples);

/* Transport: samples from the start of this block to the next downbeat (0 =
 * the block starts on one), or -1 when the transport is stopped / unknown.
 * Set it every block, before processing it. */
void  freeze_set_clock(freeze_t *f, int samples_to_downbeat);

/* After a block: the take ended itself (bar count reached, or the buffer
 * filled) and is now looping. Returns 1 once (so the caller can move the
 * param to Loop), then 0. */
int   freeze_take_full(freeze_t *f);

/* For the display: while looping, the playhead 0..1 and the loop length in
 * seconds; while recording, how full the buffer is 0..1 and the seconds so
 * far (bar mode: how far through the take, 0..1); otherwise 0. */
float freeze_phase(const freeze_t *f);
float freeze_loop_seconds(const freeze_t *f, float sample_rate);
int   freeze_looping(const freeze_t *f);       /* a take is playing */
int   freeze_armed(const freeze_t *f);         /* Rec asked, waiting for the downbeat */

#ifdef __cplusplus
}
#endif
#endif

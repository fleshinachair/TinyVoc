/*
 * The Showcase Tour's "ghost hands": a timeline over the audition phrase's
 * eight bars that moves Voice FX's knobs for you. It computes *effective*
 * values (dsp.c hands them to the engine each block); the stored knob values
 * are never written, so the tour cannot overwrite what you set. Breath,
 * Attack and Release are not moved: they stay yours. Pure functions, no state,
 * no allocation.
 *
 *   bars 1-2   neutral, low resonance: Peak 0.15, mostly the raw synth (Mix/Drv 0.30), vowel A
 *   bars 3-4   Vowel Morph: glides u -> o -> a -> e -> i, half a bar each, so the synth talks;
 *              Peak 0.5, wet with no drive (Mix/Drv 0.7)
 *   bars 5-6   Drive & Peak: the drive climbs into saturation (Mix/Drv 0.7 -> 1.0) while Peak
 *              ramps 0.5 -> 0.95: squelchy talkbox grit, on an open "ah"
 *   bars 7-8   Tract & Choir: Tract dives to a deep chest voice, swings up to soprano and
 *              returns to natural, while Choir opens to full stereo width and closes again
 *
 * Every track starts and ends the loop on the same value (bar 0 = bar 8), and
 * moves between keyframes on a smoothstep, so the loop is seamless.
 */
#ifndef TINYVOICE_TOUR_H
#define TINYVOICE_TOUR_H

#ifdef __cplusplus
extern "C" {
#endif

#define TOUR_BARS 8

typedef struct {
    int   v0, v1;                /* the vowel is `t` of the way from vowel v0 to vowel v1 (0 A, 1 E, 2 I, 3 O, 4 U) */
    float vt;
    float tract, peak, choir;    /* the effective Tract, Peak and Choir knobs, 0..1 */
    float mix;                   /* the effective Mix/Drv knob, 0..1 */
} tour_t;

/* The tour at `bar` (0 .. TOUR_BARS, in bars; wraps). */
void tour_eval(float bar, tour_t *out);

#ifdef __cplusplus
}
#endif
#endif

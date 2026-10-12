/*
 * Audition phrase: an eight-bar electro-funk loop (a syncopated bass line under
 * staccato chord stabs) from bright dual-saw voices, so Voice FX can be
 * auditioned with no track routed in. Demo On substitutes it for the input in
 * Voice FX; the sung phrase (demo.c) stays the vocoder's demo. tour.c rides
 * its bar position. Heap-free: a few floats and const tables.
 *
 *   key      D minor: Dm7 | Dm7 | Bbmaj7 | Cadd9 | Dm7 | Dm7 | Gm7 | A7
 *            (the turnaround, and the loop restarts on the Dm7)
 *   bass     one dual-saw voice, D2..C4: roots, octaves, a fifth and a seventh,
 *            three rhythmic patterns (the second answers the first, the third
 *            turns the loop around)
 *   stabs    four notes at once, short (about a third of a sixteenth), on the
 *            off-beats; two saws a few cents apart per note
 *   tone     both layers go through a pluck filter that opens on each hit and
 *            closes as it decays, behind a 1-1.5 ms attack and a short decay VCA
 *   loop     128 sixteenth steps (eight bars of 4/4) at the host tempo; every
 *            hit restarts its oscillators and filter, so the loop repeats
 *            sample for sample (16 s at 120 bpm)
 */
#ifndef TINYVOICE_GROOVE_H
#define TINYVOICE_GROOVE_H

#ifdef __cplusplus
extern "C" {
#endif

#define GROOVE_BARS        8
#define GROOVE_STEPS       (GROOVE_BARS * 16)   /* sixteenths in the loop */
#define GROOVE_CHORD_NOTES 4

typedef struct {
    float sr, step_len;          /* samples per sixteenth at the current tempo */
    double t;                    /* samples into the current step */
    int   step;                  /* 0 .. GROOVE_STEPS - 1 */
    float k_att;                 /* per-sample linear attack step (shared) */
    float k_dec_b, k_dec_c, k_rel_b, k_rel_c;   /* per-sample decay and release multipliers, bass and stabs */

    /* bass: two saws */
    int   b_gate, b_att;
    float b_ph1, b_ph2, b_inc, b_env, b_vel, b_lp;

    /* stabs: four notes, two saws each, one envelope and one filter */
    int   c_gate, c_att, c_n;
    float c_ph[GROOVE_CHORD_NOTES][2], c_inc[GROOVE_CHORD_NOTES], c_env, c_vel, c_lp;
} groove_t;

void  groove_init(groove_t *g, float sample_rate);
/* Back to the first step (Demo switched on). */
void  groove_reset(groove_t *g);
/* Once per block: bpm 20..400, 120 until set. */
void  groove_set_bpm(groove_t *g, float bpm);
/* One sample of the loop, about +-0.25 peak. */
float groove_process(groove_t *g);
/* Where the loop is, in bars (4 beats a bar): 0 .. GROOVE_BARS. The tour reads it. */
float groove_bar(const groove_t *g);
/* The bass note on a step (MIDI, C4 = 60; 0 = a rest), for tests and tools. */
int   groove_bass_note(int step);
/* The stab on a step: its notes in `notes` and their count (0 = no stab). */
int   groove_chord(int step, int notes[GROOVE_CHORD_NOTES]);

#ifdef __cplusplus
}
#endif
#endif

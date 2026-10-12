/*
 * TinyVoice test runner. No framework; exits non-zero on any failure.
 * dsp.c is compiled with -DVT_TEST_ALLOC so every engine allocation is
 * counted here and checked for leaks at the end.
 */
#include "dsp.h"
#include "cher.h"
#include "daft.h"
#include "imogen.h"
#include "modes.h"
#include "demo.h"
#include "psola.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------- leak accounting */

static long g_live_allocs;

void *vt_test_calloc(size_t n, size_t sz)
{
    void *p = calloc(n, sz);
    if (p) g_live_allocs++;
    return p;
}

void vt_test_free(void *p)
{
    if (p) g_live_allocs--;
    free(p);
}

/* ---------------------------------------------------------------- harness */

static int g_failures, g_checks;

#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; \
    printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define RUN(fn) do { printf("%s\n", #fn); fn(); } while (0)

/* ------------------------------------------------------------------ tests */

static void test_lifecycle(void)
{
    long before = g_live_allocs;
    for (int i = 0; i < 100; i++) {
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        CHECK(vt != NULL);
        dsp_cleanup(vt);
    }
    CHECK(g_live_allocs == before);
    dsp_cleanup(NULL);   /* must be a no-op */
}

static void test_param_defaults_and_roundtrip(void)
{
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    char buf[64];

    for (int i = 0; i < VT_P_COUNT; i++) {
        const vt_param_def *d = dsp_param_def(i);
        CHECK(d && dsp_param_index(d->key) == i);
        CHECK(dsp_get_param(vt, d->key, buf, sizeof buf) > 0);
    }
    CHECK(dsp_param_def(VT_P_COUNT) == NULL);

    CHECK(dsp_set_param(vt, "mode", "Robot") == 0);
    CHECK(dsp_get_param(vt, "mode", buf, sizeof buf) > 0 && strcmp(buf, "Robot") == 0);
    CHECK(dsp_set_param(vt, "scale", "2") == 0);
    CHECK(dsp_get_param(vt, "scale", buf, sizeof buf) > 0 && strcmp(buf, "Minor") == 0);
    CHECK(dsp_set_param(vt, "scale", "99") == 0);               /* clamps to last option */
    CHECK(dsp_get_param(vt, "scale", buf, sizeof buf) > 0 && strcmp(buf, "Dorian") == 0);

    CHECK(dsp_set_param(vt, "retune", "9999") == 0);            /* clamps to max */
    CHECK(dsp_get_param(vt, "retune", buf, sizeof buf) > 0 && atof(buf) == 500.0);
    CHECK(dsp_set_param(vt, "mix", "0.25") == 0);
    CHECK(dsp_get_param(vt, "mix", buf, sizeof buf) > 0 && fabs(atof(buf) - 0.25) < 1e-6);

    CHECK(dsp_set_param(vt, "nope", "1") == -1);
    CHECK(dsp_set_param(vt, "mix", "abc") == -1);
    CHECK(dsp_get_param(vt, "nope", buf, sizeof buf) == -1);
    CHECK(dsp_get_param(vt, "mode", buf, 2) == -1);             /* short buffer */

    dsp_cleanup(vt);
}

static void test_midi_voices(void)
{
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    for (int n = 60; n < 66; n++) {                              /* 6 notes, 4 voices */
        uint8_t on[3] = {0x90, (uint8_t)n, 100};
        dsp_handle_midi(vt, on, 3);
    }
    CHECK(dsp_active_voices(vt) == VT_MAX_VOICES);

    uint8_t off[3] = {0x80, 65, 0};
    dsp_handle_midi(vt, off, 3);
    CHECK(dsp_active_voices(vt) == VT_MAX_VOICES - 1);

    uint8_t on0[3] = {0x90, 64, 0};                              /* note-on vel 0 == off */
    dsp_handle_midi(vt, on0, 3);
    CHECK(dsp_active_voices(vt) == VT_MAX_VOICES - 2);

    uint8_t panic[3] = {0xB0, 123, 0};
    dsp_handle_midi(vt, panic, 3);
    CHECK(dsp_active_voices(vt) == 0);

    dsp_handle_midi(vt, NULL, 0);                                /* must not crash */
    dsp_cleanup(vt);
}

static void test_process_sane(void)
{
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    enum { N = 128 * 64 };
    static int16_t buf[2 * N];

    uint8_t hold[3] = {0x90, 57, 100};
    dsp_handle_midi(vt, hold, 3);                                  /* Robot needs a carrier note (or the drone) */
    static const char *const modes[] = {"Robot", "Snap", "Chords", "Prism", "Tape", "Nightcall", "Scream", "Dalek", "HardTune", "Harmony", "Walkie", "Morph", "Double"};
    CHECK(sizeof modes / sizeof modes[0] == VT_MODE_COUNT);
    for (int mode = 0; mode < VT_MODE_COUNT; mode++) {
        CHECK(dsp_set_param(vt, "mode", modes[mode]) == 0);
        char got[32];
        CHECK(dsp_get_param(vt, "mode", got, sizeof got) > 0 && strcmp(got, modes[mode]) == 0);
        CHECK(dsp_set_param(vt, "mode", "0") == 0 && dsp_set_param(vt, "mode", modes[mode]) == 0);
        for (int i = 0; i < N; i++) {
            int16_t s = (int16_t)(20000.0 * sin(2.0 * M_PI * 220.0 * i / VT_SAMPLE_RATE));
            buf[2 * i] = buf[2 * i + 1] = s;
        }
        for (int off = 0; off < N; off += 128) dsp_process_block(vt, buf + 2 * off, 128);

        int nonzero = 0;
        for (int i = 0; i < 2 * N; i++) nonzero |= buf[i] != 0;   /* int16 can't be NaN; check not silenced */
        if (!nonzero) printf("  mode %s produced silence\n", modes[mode]);
        CHECK(nonzero);
    }

    /* oversized block is chunked, not overrun */
    static int16_t big[2 * 2000];
    memset(big, 0, sizeof big);
    dsp_process_block(vt, big, 2000);
    dsp_process_block(vt, big, 0);
    dsp_process_block(vt, NULL, 128);
    dsp_cleanup(vt);
}

static void test_viz_feed(void)
{
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    char buf[256];
    dsp_set_param(vt, "mode", "Snap");
    CHECK(dsp_get_param(vt, "viz", buf, sizeof buf) > 0 && buf[0] == 'c');
    dsp_set_param(vt, "mode", "Robot");
    CHECK(dsp_get_param(vt, "viz", buf, sizeof buf) > 0 && buf[0] == 'd');
    int commas = 0;
    for (char *p = buf; *p; p++) commas += *p == ',';
    CHECK(commas == VT_NUM_BANDS);
    dsp_cleanup(vt);
}

/* ------------------------------------------------------------ Cher tests */

static double cents(double f, double ref) { return 1200.0 * log2(f / ref); }

/* Harmonic tone: partials 1..nh with 1/h amplitude (saw-ish). */
static float tone(double f, int i, int nh, double sr)
{
    double s = 0.0, norm = 0.0;
    for (int h = 1; h <= nh; h++) { s += sin(2.0 * M_PI * f * h * i / sr) / h; norm += 1.0 / h; }
    return (float)(0.6 * s / norm);
}

static void test_pitch_tracking_accuracy(void)
{
    static const double freqs[] = {82.41, 110.0, 146.83, 196.0, 261.63, 329.63, 440.0, 587.33, 783.99, 987.77};
    static cher_t c;

    for (size_t k = 0; k < sizeof freqs / sizeof freqs[0]; k++) {
        for (int nh = 1; nh <= 6; nh += 5) {                      /* pure sine, then 6 partials */
            cher_init(&c, VT_SAMPLE_RATE);
            for (int i = 0; i < VT_SAMPLE_RATE / 2; i++)
                cher_process(&c, tone(freqs[k], i, nh, VT_SAMPLE_RATE));
            double err = c.f0 > 0 ? cents(c.f0, freqs[k]) : 1e9;
            if (fabs(err) >= 5.0)
                printf("  %.2f Hz (%d partials): got %.2f Hz, %.2f cents\n", freqs[k], nh, c.f0, err);
            CHECK(fabs(err) < 5.0);
        }
    }

    /* Silence and noise must not report a pitch. */
    cher_init(&c, VT_SAMPLE_RATE);
    for (int i = 0; i < 20000; i++) cher_process(&c, 0.0f);
    CHECK(c.f0 == 0.0f);

    unsigned seed = 12345;
    cher_init(&c, VT_SAMPLE_RATE);
    for (int i = 0; i < 20000; i++) {
        seed = seed * 1664525u + 1013904223u;
        cher_process(&c, ((int)(seed >> 8) / 8388608.0f - 1.0f) * 0.3f);
    }
    CHECK(c.f0 == 0.0f);
}

static void test_quantizer(void)
{
    /* root, scale, midi in, prev -> expected */
    CHECK(cher_quantize(61.4f, 0, VT_SCALE_CHROMATIC, -1) == 61);
    CHECK(cher_quantize(61.4f, 0, VT_SCALE_MAJOR, -1) == 62);          /* C# -> D */
    CHECK(cher_quantize(65.4f, 0, VT_SCALE_PENTATONIC, -1) == 64);     /* F -> E */
    CHECK(cher_quantize(61.2f, 9, VT_SCALE_MINOR, -1) == 62);          /* A minor: C# -> D */
    CHECK(cher_quantize(66.0f, 6, VT_SCALE_MAJOR, -1) == 66);          /* F# major root, F# kept */
    CHECK(cher_quantize(59.9f, 0, VT_SCALE_MAJOR, -1) == 60);          /* just under C */
    CHECK(cher_quantize(60.52f, 0, VT_SCALE_CHROMATIC, 60) == 60);     /* hysteresis holds */
    CHECK(cher_quantize(60.70f, 0, VT_SCALE_CHROMATIC, 60) == 61);     /* ...until clearly past */
    CHECK(cher_quantize(61.0f, 0, VT_SCALE_MAJOR, 61) != 61);          /* prev outside scale is ignored */
    CHECK(fabs(cher_hz_to_midi(440.0f) - 69.0) < 1e-4);
    CHECK(fabs(cher_midi_to_hz(60.0f) - 261.6256) < 1e-2);
}

/* Measure the pitch of the last half second of a processed tone. */
static double snapped_pitch(double f_in, int root, int scale, double retune_ms, int nh)
{
    static cher_t c;
    static float out[VT_SAMPLE_RATE];
    int total = VT_SAMPLE_RATE + VT_SAMPLE_RATE / 2;

    cher_init(&c, VT_SAMPLE_RATE);
    cher_set_params(&c, root, scale, (float)retune_ms, -60.0f, -1, 0.0f);
    for (int i = 0; i < total; i++) {
        float y = cher_process(&c, tone(f_in, i, nh, VT_SAMPLE_RATE));
        if (i >= total - VT_SAMPLE_RATE / 2) out[i - (total - VT_SAMPLE_RATE / 2)] = y;
    }
    static float nsdf[CHER_MAX_TAU + 2];
    float f0 = 0, clar = 0;
    if (!cher_mpm(out + 4096, 4096, VT_SAMPLE_RATE, 120.0f, 1000.0f, nsdf, &f0, &clar)) return 0.0;
    return f0;
}

static void test_snap_to_scale(void)
{
    const double D4 = 293.665, Csharp4 = 277.183, A4 = 440.0;

    /* 281 Hz = MIDI 61.2: chromatic snaps to C#4, C major (nearest allowed) to D4. */
    double f = snapped_pitch(281.0, 0, VT_SCALE_CHROMATIC, 0, 1);
    CHECK(f > 0 && fabs(cents(f, Csharp4)) < 15.0);
    f = snapped_pitch(281.0, 0, VT_SCALE_MAJOR, 0, 1);
    CHECK(f > 0 && fabs(cents(f, D4)) < 15.0);

    /* Flat A (-35 cents) and sharp A (+35 cents), saw-ish: both land on A4. */
    f = snapped_pitch(440.0 * pow(2.0, -35.0 / 1200.0), 0, VT_SCALE_CHROMATIC, 0, 6);
    CHECK(f > 0 && fabs(cents(f, A4)) < 15.0);
    f = snapped_pitch(440.0 * pow(2.0, 35.0 / 1200.0), 0, VT_SCALE_CHROMATIC, 0, 6);
    CHECK(f > 0 && fabs(cents(f, A4)) < 15.0);

    /* Output energy stays in family (no gain blow-up/dropout). */
    static cher_t c;
    cher_init(&c, VT_SAMPLE_RATE);
    double ein = 0, eout = 0;
    for (int i = 0; i < VT_SAMPLE_RATE; i++) {
        float x = tone(220.0 * pow(2.0, 30.0 / 1200.0), i, 6, VT_SAMPLE_RATE);
        float y = cher_process(&c, x);
        CHECK(isfinite(y));
        if (i > VT_SAMPLE_RATE / 2) { ein += x * x; eout += y * y; }
    }
    CHECK(eout > 0.5 * ein && eout < 1.5 * ein);
}

static void test_cher_through_engine(void)
{
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Snap");
    dsp_set_param(vt, "mix", "1");
    dsp_set_param(vt, "gain", "0");

    static float blk[2 * 128];
    for (int b = 0; b < 400; b++) {                              /* ~1.16 s of 128-frame blocks */
        for (int i = 0; i < 128; i++)
            blk[2 * i] = blk[2 * i + 1] = tone(445.0, b * 128 + i, 1, VT_SAMPLE_RATE);
        dsp_process_block_f(vt, blk, 128);
    }
    char buf[96];
    CHECK(dsp_get_param(vt, "viz", buf, sizeof buf) > 0);
    float f0 = 0, target = 0, cents_err = 0;
    CHECK(sscanf(buf, "c,%f,%f,%f", &f0, &target, &cents_err) == 3);
    CHECK(fabs(cents(f0, 445.0)) < 5.0);
    CHECK(fabs(target - 440.0) < 0.5);
    CHECK(cents_err > 15.0 && cents_err < 25.0);                  /* 445 Hz = +19.6 cents */
    dsp_cleanup(vt);
}


/* ------------------------------------------------------ shared helpers */

static float lcg_noise(unsigned *s)
{
    *s = *s * 1664525u + 1013904223u;
    return (float)((int)(*s >> 8)) / 8388608.0f - 1.0f;          /* [-1, 1) */
}

static double rms_of(const float *x, int n)
{
    double s = 0;
    for (int i = 0; i < n; i++) s += (double)x[i] * x[i];
    return sqrt(s / n);
}

/* Power near f via Goertzel. */
static double goertzel(const float *x, int n, double f)
{
    double w = 2.0 * M_PI * f / VT_SAMPLE_RATE, c = 2.0 * cos(w), s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) { double s = x[i] + c * s1 - s2; s2 = s1; s1 = s; }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / ((double)n * n);
}

static double band_energy(const float *x, int n, double lo, double hi)
{
    double e = 0;
    for (double f = lo; f < hi; f += lo * 0.02) e += goertzel(x, n, f);
    return e;
}

static void note_on(vt_t *vt, int n)  { uint8_t m[3] = {0x90, (uint8_t)n, 100}; dsp_handle_midi(vt, m, 3); }
static void note_off(vt_t *vt, int n) { uint8_t m[3] = {0x80, (uint8_t)n, 0};   dsp_handle_midi(vt, m, 3); }

/* Run `blocks` x 128 frames of a sine through the engine, advancing *pos (sample
 * counter, keeps the sine phase continuous). The first cap_n output samples (left
 * channel) go to `capture` if given. */
static void run_sine(vt_t *vt, double f, float amp, int blocks, long *pos, float *capture, int cap_n)
{
    static float blk[2 * 128];
    int cap_i = 0;
    for (int b = 0; b < blocks; b++) {
        for (int i = 0; i < 128; i++, (*pos)++)
            blk[2 * i] = blk[2 * i + 1] = amp * (float)sin(2.0 * M_PI * f * (double)(*pos) / VT_SAMPLE_RATE);
        dsp_process_block_f(vt, blk, 128);
        if (capture)
            for (int i = 0; i < 128 && cap_i < cap_n; i++) capture[cap_i++] = blk[2 * i];
    }
}

static float viz_target(vt_t *vt)
{
    char buf[96];
    float f0 = 0, tgt = 0, ce = 0;
    dsp_get_param(vt, "viz", buf, sizeof buf);
    sscanf(buf, "c,%f,%f,%f", &f0, &tgt, &ce);
    return tgt;
}

/* --------------------------------------------- Cher: dry alignment, MIDI */

static void test_dry_wet_alignment(void)
{
    /* With a pitch the tracker cannot lock (gate above any input level), the wet
     * path is the shifter's plain delayed copy. The dry path must be delayed by
     * the same amount, so a pure-dry run (mix 0) and a pure-wet run (mix 1) over
     * the same noise are the same signal. Uncompensated they would be ~350
     * samples apart and uncorrelated, which is what makes a blend comb.
     * Noise is never pitched (unvoiced: the plain copy). Snap's fixed formant
     * lift is cancelled with char1, and its telephone band is applied to the
     * dry run too, since it is a linear filter on the wet path only. */
    static float dry_out[VT_SAMPLE_RATE / 2], wet_out[VT_SAMPLE_RATE / 2];
    for (int pass = 0; pass < 2; pass++) {
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "mode", "Snap");
        dsp_set_param(vt, "mix", pass ? "1" : "0");
        dsp_set_param(vt, "gain", "0");
        dsp_set_param(vt, "auto", "Off");
        dsp_set_param(vt, "gate", "-60 dB");                       /* the input gate stays open */
        dsp_set_param(vt, "spread", "0");                          /* no chorus: compare the shifter path alone */
        dsp_set_param(vt, "beauty", "0");                          /* nor Beauty's double / shelf */
        char c1[32];
        snprintf(c1, sizeof c1, "%.9f", 0.5 - SNAP_FORMANT_LIFT / 24.0);
        dsp_set_param(vt, "char1", c1);                            /* formant ratio 1 */
        unsigned seed = 99;
        static float blk[2 * 128];
        float *dst = pass ? wet_out : dry_out;
        int n = 0;
        for (int b = 0; b < 172; b++) {                            /* ~0.5 s */
            for (int i = 0; i < 128; i++) blk[2 * i] = blk[2 * i + 1] = 0.3f * lcg_noise(&seed);
            dsp_process_block_f(vt, blk, 128);
            for (int i = 0; i < 128 && n < VT_SAMPLE_RATE / 2; i++) dst[n++] = blk[2 * i];
        }
        dsp_cleanup(vt);
    }
    static fx_t band;
    fx_init(&band, VT_SAMPLE_RATE);
    for (int i = 0; i < VT_SAMPLE_RATE / 2; i++) dry_out[i] = fx_snap_voicing(&band, dry_out[i]);
    double sxy = 0, sxx = 0, syy = 0;
    for (int i = 8192; i < VT_SAMPLE_RATE / 2; i++) {
        sxy += (double)dry_out[i] * wet_out[i];
        sxx += (double)dry_out[i] * dry_out[i];
        syy += (double)wet_out[i] * wet_out[i];
    }
    double corr = sxy / sqrt(sxx * syy);
    if (corr < 0.999) printf("  dry/wet correlation %.4f\n", corr);
    CHECK(corr > 0.999);
}

static void test_midi_target_override(void)
{
    const double E4 = 329.628, G4 = 391.995, C4 = 261.626, A4 = 440.0;
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Snap");
    dsp_set_param(vt, "key", "C");
    dsp_set_param(vt, "scale", "Major");                           /* A is in C major: quantizer target = 440 */
    dsp_set_param(vt, "mix", "1");
    dsp_set_param(vt, "gain", "0");
    long pos = 0;

    /* no notes: quantizer decides. Sing 430 Hz (-40 cents) -> A4 */
    run_sine(vt, 430.0, 0.5f, 200, &pos, NULL, 0);
    CHECK(fabs(viz_target(vt) - A4) < 0.5);

    /* hold E4: override beats the quantizer, and the audio really moves there */
    static float cap[4096 + 512];
    note_on(vt, 64);
    run_sine(vt, 430.0, 0.5f, 200, &pos, NULL, 0);
    CHECK(fabs(viz_target(vt) - E4) < 0.5);
    run_sine(vt, 430.0, 0.5f, 40, &pos, cap, 4096 + 512);
    static float nsdf[CHER_MAX_TAU + 2];
    float f0 = 0, clar = 0;
    int ok = cher_mpm(cap + 512, 4096, VT_SAMPLE_RATE, 120.0f, 1000.0f, nsdf, &f0, &clar);
    if (!ok || fabs(cents(f0, E4)) >= 15.0) printf("  override output pitch: ok=%d f0=%.2f\n", ok, f0);
    CHECK(ok && fabs(cents(f0, E4)) < 15.0);

    /* a note outside the scale works too: this is an override, not a different quantizer */
    note_off(vt, 64);
    note_on(vt, 63);                                               /* Eb4: not in C major */
    run_sine(vt, 430.0, 0.5f, 200, &pos, NULL, 0);
    CHECK(fabs(viz_target(vt) - 311.127) < 0.5);
    note_off(vt, 63);

    /* last-note priority with fall-back to still-held notes */
    note_on(vt, 64);
    note_on(vt, 60);
    note_on(vt, 67);
    run_sine(vt, 430.0, 0.5f, 100, &pos, NULL, 0);
    CHECK(fabs(viz_target(vt) - G4) < 0.5);
    note_off(vt, 67);
    run_sine(vt, 430.0, 0.5f, 100, &pos, NULL, 0);
    CHECK(fabs(viz_target(vt) - C4) < 0.5);                        /* newest remaining = 60 */
    note_off(vt, 60);
    run_sine(vt, 430.0, 0.5f, 100, &pos, NULL, 0);
    CHECK(fabs(viz_target(vt) - E4) < 0.5);
    note_off(vt, 64);
    run_sine(vt, 430.0, 0.5f, 100, &pos, NULL, 0);
    CHECK(fabs(viz_target(vt) - A4) < 0.5);                        /* back to the quantizer */
    dsp_cleanup(vt);
}

static void test_midi_override_glide(void)
{
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Snap");
    dsp_set_param(vt, "mix", "1");
    long pos = 0;

    note_on(vt, 64);
    run_sine(vt, 330.0, 0.5f, 200, &pos, NULL, 0);                 /* settle on E4, glide off */
    CHECK(fabs(viz_target(vt) - 329.628) < 0.5);

    dsp_set_param(vt, "glide", "300");
    note_on(vt, 72);                                               /* C5 = 523.25 Hz (+8: within the fold) */
    float prev = viz_target(vt), t_early = 0;
    int monotonic = 1;
    for (int step = 0; step < 100; step++) {                       /* 100 x 8 blocks ~ 2.3 s */
        run_sine(vt, 330.0, 0.5f, 8, &pos, NULL, 0);
        float t = viz_target(vt);
        if (t + 0.01f < prev) monotonic = 0;
        prev = t;
        if (step == 3) t_early = t;                                /* ~70 ms in */
    }
    CHECK(monotonic);
    if (!(t_early > 340.0f && t_early < 640.0f)) printf("  early glide target %.1f\n", t_early);
    CHECK(t_early > 340.0f && t_early < 510.0f);                   /* mid-glide, not jumped */
    CHECK(fabs(prev - 523.251) < 1.5);                             /* arrived */
    dsp_cleanup(vt);
}

/* ---------------------------------------------------------------- Daft */

static void daft_prime(daft_t *d, int pulse, float sib, float formant, float width, float tilt)
{
    daft_init(d, VT_SAMPLE_RATE);
    daft_set_auto(d, 0);                                           /* manual: no drone, raw level, fixed gate */
    daft_set_params(d, pulse, 8.0f, 0.0f, width, tilt, sib, formant, -60.0f);
    daft_set_voice(d, 0, 57, 100);                                 /* A major triad */
    daft_set_voice(d, 1, 61, 100);
    daft_set_voice(d, 2, 64, 100);
}

static void test_daft_follows_modulator(void)
{
    static daft_t d;
    static float out[VT_SAMPLE_RATE];
    int half = VT_SAMPLE_RATE / 2;

    for (int pulse = 0; pulse < 2; pulse++) {
        daft_prime(&d, pulse, 0.5f, 0.0f, 0.5f, 0.0f);
        for (int i = 0; i < VT_SAMPLE_RATE; i++)
            out[i] = daft_process(&d, 0.3f * (float)sin(2.0 * M_PI * 1000.0 * i / VT_SAMPLE_RATE));
        double at_1k = band_energy(out + half, half, 800, 1250);
        double low   = band_energy(out + half, half, 200, 400);
        double high  = band_energy(out + half, half, 3000, 6000);
        CHECK(rms_of(out + half, half) > 0.05);                     /* audible */
        if (at_1k < 1.5 * low) printf("  pulse=%d: 1k/low energy ratio %.2f\n", pulse, at_1k / low);
        CHECK(at_1k > 1.5 * low);                                   /* spectrum follows the modulator... */
        CHECK(at_1k > 100.0 * high);

        /* ...and moves with it: the same carrier driven by a 250 Hz tone has its
         * energy in the low band instead (ratio swings by orders of magnitude). */
        static float out2[VT_SAMPLE_RATE];
        daft_prime(&d, pulse, 0.5f, 0.0f, 0.5f, 0.0f);
        for (int i = 0; i < VT_SAMPLE_RATE; i++)
            out2[i] = daft_process(&d, 0.3f * (float)sin(2.0 * M_PI * 250.0 * i / VT_SAMPLE_RATE));
        double r_low_mod = band_energy(out2 + half, half, 800, 1250) / band_energy(out2 + half, half, 200, 400);
        CHECK(at_1k / low > 100.0 * r_low_mod);
        daft_prime(&d, pulse, 0.5f, 0.0f, 0.5f, 0.0f);              /* restore state for the band check below */
        for (int i = 0; i < VT_SAMPLE_RATE; i++)
            out[i] = daft_process(&d, 0.3f * (float)sin(2.0 * M_PI * 1000.0 * i / VT_SAMPLE_RATE));

        int peak = 0;
        for (int b = 1; b < DAFT_BANDS; b++)
            if (daft_band_level(&d, b) > daft_band_level(&d, peak)) peak = b;
        CHECK(peak == 7 || peak == 8);                              /* centres ~935 and ~1230 Hz */
    }

    /* +9 semitones of formant shift moves the energy up ~2 bands */
    daft_prime(&d, 0, 0.5f, 9.0f, 0.5f, 0.0f);
    for (int i = 0; i < VT_SAMPLE_RATE; i++)
        out[i] = daft_process(&d, 0.3f * (float)sin(2.0 * M_PI * 1000.0 * i / VT_SAMPLE_RATE));
    CHECK(band_energy(out + half, half, 1400, 2200) > 5.0 * band_energy(out + half, half, 800, 1250));
}

static void test_daft_silent_without_carrier(void)
{
    static daft_t d;
    daft_init(&d, VT_SAMPLE_RATE);
    d.drone_enabled = 0;
    daft_set_params(&d, 0, 8.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, -60.0f);   /* sibilance off */
    unsigned seed = 5;
    float peak = 0;
    for (int i = 0; i < VT_SAMPLE_RATE / 2; i++) {
        float y = fabsf(daft_process(&d, 0.3f * lcg_noise(&seed)));
        if (y > peak) peak = y;
    }
    CHECK(peak < 1e-6f);

    /* notes released -> the carrier decays to nothing (drone off: it would
     * otherwise take over the moment the keys go up) */
    daft_prime(&d, 0, 0.0f, 0.0f, 0.5f, 0.0f);
    d.drone_enabled = 0;
    for (int i = 0; i < 4000; i++) daft_process(&d, 0.2f * lcg_noise(&seed));
    for (int s = 0; s < 3; s++) daft_set_voice(&d, s, -1, 0);
    for (int i = 0; i < VT_SAMPLE_RATE; i++) daft_process(&d, 0.2f * lcg_noise(&seed));
    peak = 0;
    for (int i = 0; i < 4000; i++) { float y = fabsf(daft_process(&d, 0.2f * lcg_noise(&seed))); if (y > peak) peak = y; }
    CHECK(peak < 1e-4f);
}

static void test_daft_drone(void)
{
    /* With no key held the C3+G3 drone carries the voice; pressing a key
     * replaces it, and releasing brings it back. Broadband modulator so the
     * output spectrum shows the carrier's harmonics. */
    static daft_t d;
    static float out[VT_SAMPLE_RATE / 2];
    const int n = VT_SAMPLE_RATE / 2;
    const double C3 = 130.81, E3 = 164.81;
    unsigned seed = 11;

    daft_init(&d, VT_SAMPLE_RATE);
    CHECK(d.drone_enabled == DAFT_DRONE_DEFAULT);
    d.drone_enabled = 1;
    daft_set_params(&d, 0, 8.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, -60.0f);   /* sibilance off */

    for (int i = 0; i < n; i++) out[i] = daft_process(&d, 0.2f * lcg_noise(&seed));
    double drone_c3 = band_energy(out + n / 2, n / 2, C3 * 0.99, C3 * 1.01);
    double drone_e3 = band_energy(out + n / 2, n / 2, E3 * 0.99, E3 * 1.01);
    CHECK(rms_of(out + n / 2, n / 2) > 0.01);                      /* audible with no MIDI at all */
    CHECK(drone_c3 > 5.0 * drone_e3);

    daft_set_voice(&d, 0, 52, 100);                                /* hold E3 */
    for (int i = 0; i < n; i++) out[i] = daft_process(&d, 0.2f * lcg_noise(&seed));
    double key_c3 = band_energy(out + n / 2, n / 2, C3 * 0.99, C3 * 1.01);
    double key_e3 = band_energy(out + n / 2, n / 2, E3 * 0.99, E3 * 1.01);
    if (!(key_e3 > 5.0 * key_c3)) printf("  held E3: E3/C3 energy %.2f\n", key_e3 / key_c3);
    CHECK(key_e3 > 5.0 * key_c3);                                  /* the key replaced the drone */

    daft_set_voice(&d, 0, -1, 0);                                  /* release: drone returns */
    for (int i = 0; i < n; i++) out[i] = daft_process(&d, 0.2f * lcg_noise(&seed));
    CHECK(band_energy(out + n / 2, n / 2, C3 * 0.99, C3 * 1.01) >
          5.0 * band_energy(out + n / 2, n / 2, E3 * 0.99, E3 * 1.01));

    d.drone_enabled = 0;                                           /* and it can be switched off */
    for (int i = 0; i < VT_SAMPLE_RATE; i++) daft_process(&d, 0.2f * lcg_noise(&seed));   /* 80 ms release: ~0.7 s to -80 dB */
    for (int i = 0; i < n; i++) out[i] = daft_process(&d, 0.2f * lcg_noise(&seed));
    float peak = 0;
    for (int i = n / 2; i < n; i++) if (fabsf(out[i]) > peak) peak = fabsf(out[i]);
    CHECK(peak < 1e-4f);
}

static void test_daft_auto_front_end(void)
{
    static daft_t d;
    static float out[VT_SAMPLE_RATE];
    const int half = VT_SAMPLE_RATE / 2;
    unsigned seed = 31;

    /* AGC: a vowel-like modulator at -40 dB and at -6 dB relative to full
     * scale. Without the AGC the outputs would differ by ~34 dB. */
    double lvl[2];
    const float amps[2] = { 0.01f, 0.5f };
    for (int k = 0; k < 2; k++) {
        daft_prime(&d, 0, 0.0f, 0.0f, 0.5f, 0.0f);
        daft_set_auto(&d, 1);                                      /* AGC is part of Auto */
        d.drone_enabled = 0;                                       /* keys are held anyway */
        for (int i = 0; i < VT_SAMPLE_RATE; i++)
            out[i] = daft_process(&d, amps[k] * tone(140.0, i, 12, VT_SAMPLE_RATE) / 0.6f);
        lvl[k] = rms_of(out + half, half);
    }
    double spread_db = 20.0 * log10(lvl[1] / lvl[0]);
    if (fabs(spread_db) > 6.0) printf("  AGC: soft %.4f loud %.4f (%.1f dB apart)\n", lvl[0], lvl[1], spread_db);
    CHECK(lvl[0] > 0.02);                                          /* soft speech is clearly audible */
    CHECK(fabs(spread_db) < 6.0);

    /* Adaptive sibilance: quiet hiss (-40 dB) is a consonant too. The old fixed
     * threshold (hf level x 30) would have scored it ~0.3. */
    daft_prime(&d, 0, 1.0f, 0.0f, 0.5f, 0.0f);
    float p1 = 0, p2 = 0;
    for (int i = 0; i < VT_SAMPLE_RATE; i++) {
        float n = lcg_noise(&seed), h = n - 2.0f * p1 + p2;
        p2 = p1; p1 = n;
        daft_process(&d, 0.0025f * h);
    }
    if (d.sib < 0.9f) printf("  quiet hiss: sib=%.2f\n", d.sib);
    CHECK(d.sib > 0.9f);

    /* The AGC never opens the gate: in Auto, once the adaptive gate has learnt
     * the room (its noise-floor window is ~3 s, and the silence before the
     * noise starts counts as the floor until it ages out), noise sits under the
     * threshold and stays silent even with the AGC at full +30 dB. */
    daft_prime(&d, 0, 1.0f, 0.0f, 0.5f, 0.0f);
    daft_set_auto(&d, 1);
    d.drone_enabled = 0;
    float peak = 0;
    for (int i = 0; i < 4 * VT_SAMPLE_RATE; i++) {
        float y = fabsf(daft_process(&d, 0.0002f * lcg_noise(&seed)));   /* ~ -77 dB */
        if (i > 4 * VT_SAMPLE_RATE - half && y > peak) peak = y;
    }
    CHECK(d.agc_gain > 30.0f);
    CHECK(peak < 1e-4f);
}

static void test_cher_quiet_input(void)
{
    /* Mic-level input at -66 dB RMS (below the default -60 dB Gate) still
     * engages the snap thanks to the +12 dB analysis gain. tone(..., 6) has an
     * RMS of 0.2115, so scale it to 0.0005. */
    static cher_t c;
    unsigned seed = 21;
    cher_init(&c, VT_SAMPLE_RATE);
    cher_set_params(&c, 0, VT_SCALE_CHROMATIC, 0.0f, -60.0f, -1, 0.0f);
    for (int i = 0; i < VT_SAMPLE_RATE / 2; i++) {
        float x = tone(196.0, i, 6, VT_SAMPLE_RATE) * (0.0005f / 0.2115f) + 0.00005f * lcg_noise(&seed);
        cher_process(&c, x);
    }
    double err = c.f0 > 0 ? cents(c.f0, 196.0) : 1e9;
    if (fabs(err) >= 5.0) printf("  quiet input: f0=%.2f (%.1f cents)\n", c.f0, err);
    CHECK(fabs(err) < 5.0);
    CHECK(c.note == 55);                                           /* G3: snap target set */

    /* DC offset and low rumble. Without the analysis high-pass, the NSDF never
     * crosses zero and MPM reports no pitch at all, so the voice passes dry. */
    const float offsets[] = { 0.2f, -0.3f };
    for (int k = 0; k < 2; k++) {
        cher_init(&c, VT_SAMPLE_RATE);
        for (int i = 0; i < VT_SAMPLE_RATE / 2; i++)
            cher_process(&c, offsets[k] + 0.3f * (float)sin(2.0 * M_PI * 25.0 * i / VT_SAMPLE_RATE)
                             + tone(220.0, i, 6, VT_SAMPLE_RATE) * 0.5f);
        err = c.f0 > 0 ? cents(c.f0, 220.0) : 1e9;
        if (fabs(err) >= 5.0) printf("  DC %.1f + rumble: f0=%.2f (%.1f cents)\n", offsets[k], c.f0, err);
        CHECK(fabs(err) < 5.0);
    }

    /* Noisy voice: 9 dB SNR white noise still tracks. */
    cher_init(&c, VT_SAMPLE_RATE);
    for (int i = 0; i < VT_SAMPLE_RATE / 2; i++)
        cher_process(&c, tone(220.0, i, 6, VT_SAMPLE_RATE) + 0.130f * lcg_noise(&seed));
    err = c.f0 > 0 ? cents(c.f0, 220.0) : 1e9;
    if (fabs(err) >= 10.0) printf("  noisy input: f0=%.2f (%.1f cents)\n", c.f0, err);
    CHECK(fabs(err) < 10.0);
}

static void test_daft_sibilance(void)
{
    static daft_t d;
    static float out[VT_SAMPLE_RATE];
    int half = VT_SAMPLE_RATE / 2;
    unsigned seed = 7;
    double hiss[2];

    /* Hissy modulator (2nd difference of noise, mostly above 5 kHz): the noise
     * injection adds high-band energy. */
    for (int sb = 0; sb < 2; sb++) {
        daft_prime(&d, 0, sb ? 1.0f : 0.0f, 0.0f, 0.5f, 0.0f);
        float p1 = 0, p2 = 0;
        for (int i = 0; i < VT_SAMPLE_RATE; i++) {
            float n = lcg_noise(&seed), h = n - 2.0f * p1 + p2;
            p2 = p1; p1 = n;
            out[i] = daft_process(&d, 0.15f * h);
        }
        hiss[sb] = band_energy(out + half, half, 4000, 8000) / (band_energy(out + half, half, 200, 2000) + 1e-20);   /* spectral balance: the output leveller evens out overall level */
        if (sb) CHECK(d.sib > 0.9f);
    }
    if (!(hiss[1] > 4.0 * hiss[0])) printf("  hiss balance %.3e -> %.3e\n", hiss[0], hiss[1]);
    CHECK(hiss[1] > 4.0 * hiss[0]);

    /* Voiced vowel-like modulator: the detector stays quiet, so the knob changes nothing. */
    double lvl[2];
    for (int sb = 0; sb < 2; sb++) {
        daft_prime(&d, 0, sb ? 1.0f : 0.0f, 0.0f, 0.5f, 0.0f);
        for (int i = 0; i < VT_SAMPLE_RATE; i++) {
            double v = 0;
            for (int h = 1; h <= 30; h++) v += sin(2.0 * M_PI * 120.0 * h * i / VT_SAMPLE_RATE) / h * (h < 8 ? 1.0 : 0.3);
            out[i] = daft_process(&d, 0.2f * (float)v);
        }
        if (sb) CHECK(d.sib < 0.05f);
        lvl[sb] = rms_of(out + half, half);
    }
    CHECK(lvl[0] > 0.02);
    CHECK(fabs(lvl[1] - lvl[0]) < 0.05 * lvl[0]);
}

static void test_filterbank_stability(void)
{
    static daft_t d;
    unsigned seed = 3;
    struct { float width, tilt, formant; int pulse; } cfg[] = {
        {0.5f, 0.0f, 0.0f, 0}, {0.0f, 1.0f, 12.0f, 1}, {1.0f, -1.0f, -12.0f, 0}, {0.0f, -1.0f, 12.0f, 1},
    };

    for (size_t c = 0; c < sizeof cfg / sizeof cfg[0]; c++) {
        daft_prime(&d, cfg[c].pulse, 1.0f, cfg[c].formant, cfg[c].width, cfg[c].tilt);
        daft_set_voice(&d, 3, 100, 127);                           /* very high note: aliasing guard */
        float peak = 0;
        int bad = 0, subnormal = 0;
        for (int phase = 0; phase < 5; phase++) {
            for (int i = 0; i < 20000; i++) {
                float x = 0.0f;
                switch (phase) {
                case 0: x = i == 0 ? 1.0f : 0.0f; break;                          /* impulse */
                case 1: x = 0.5f; break;                                           /* DC */
                case 2: x = ((i / 220) & 1) ? 1.0f : -1.0f; break;                 /* full-scale square */
                case 3: x = lcg_noise(&seed); break;                               /* full-scale noise */
                default: x = 0.0f; break;                                          /* silence */
                }
                float y = daft_process(&d, x);
                if (!isfinite(y)) bad++;
                if (fpclassify(y) == FP_SUBNORMAL) subnormal++;
                if (fabsf(y) > peak) peak = fabsf(y);
            }
        }
        for (int s = 0; s < 4; s++) daft_set_voice(&d, s, -1, 0);
        for (int i = 0; i < VT_SAMPLE_RATE; i++) daft_process(&d, 0.0f);           /* let tails die */
        float tail = 0;
        for (int i = 0; i < 4410; i++) { float y = fabsf(daft_process(&d, 0.0f)); if (y > tail) tail = y; }

        if (bad || subnormal || peak > 1.0001f || tail > 1e-6f)
            printf("  cfg %zu: nonfinite=%d subnormal=%d peak=%.3f tail=%.2e\n", c, bad, subnormal, peak, tail);
        CHECK(bad == 0);
        CHECK(subnormal == 0);
        CHECK(peak <= 1.0001f);                                     /* limiter ceiling */
        CHECK(tail < 1e-6f);
        for (int b = 0; b < DAFT_BANDS; b++) CHECK(isfinite(d.env[b]));
    }
}

static void test_daft_through_engine(void)
{
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Robot");
    dsp_set_param(vt, "mix", "1");
    dsp_set_param(vt, "gain", "0");
    note_on(vt, 57); note_on(vt, 61); note_on(vt, 64);
    long pos = 0;
    static float cap[4096];
    run_sine(vt, 1000.0, 0.3f, 300, &pos, NULL, 0);
    run_sine(vt, 1000.0, 0.3f, 32, &pos, cap, 4096);
    CHECK(rms_of(cap, 4096) > 0.05);

    char buf[256];
    CHECK(dsp_get_param(vt, "viz", buf, sizeof buf) > 0 && buf[0] == 'd');
    int v[DAFT_BANDS], n = 0, peak = 0;
    char *p = buf + 1;
    while (*p == ',' && n < DAFT_BANDS) { v[n++] = (int)strtol(p + 1, &p, 10); }
    CHECK(n == DAFT_BANDS);
    for (int b = 1; b < n; b++) if (v[b] > v[peak]) peak = b;
    CHECK(peak == 7 || peak == 8);
    CHECK(v[peak] > 128);

    /* switching back to Cher with a partial blend stays finite */
    dsp_set_param(vt, "mix", "0.5");
    dsp_set_param(vt, "mode", "Snap");
    run_sine(vt, 220.0, 0.4f, 50, &pos, cap, 4096);
    for (int i = 0; i < 4096; i++) CHECK(isfinite(cap[i]));
    dsp_cleanup(vt);
}


/* -------------------------------------------------------- dry path */

static float g_imp[VT_SAMPLE_RATE];

/* Impulse (0.5) at frame 0 through Daft with no notes and mix 0, i.e. a pure
 * dry signal: the output is the dry path alone. The input's
 * 120 Hz high-pass would smear an impulse (and take its DC), so the test feeds
 * the filter's inverse: 0.5/a, then 0.5(1/a - 1) held, which the high-pass
 * turns back into exactly 0.5 at frame 0 and zeros after it. */
static void delay_impulse(vt_t *vt, int frames)
{
    static float blk[2 * 128];
    const double a = exp(-2.0 * M_PI * VT_INPUT_HPF_HZ / VT_SAMPLE_RATE);
    const float first = (float)(0.5 / a), held = (float)(0.5 / a - 0.5);
    int n = 0;
    while (n < frames) {
        for (int i = 0; i < 128; i++) blk[2 * i] = blk[2 * i + 1] = (n == 0 && i == 0) ? first : held;
        dsp_process_block_f(vt, blk, 128);
        for (int i = 0; i < 128 && n < frames; i++) g_imp[n++] = blk[2 * i];
    }
}

static void test_dry_passthrough(void)
{
    /* Mix 0: the dry vocal passes untouched (the engine's latency is 0 in Robot), and nothing follows it */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Robot");
    dsp_set_param(vt, "mix", "0");
    dsp_set_param(vt, "gain", "0");
    dsp_set_param(vt, "auto", "Off");
    delay_impulse(vt, VT_SAMPLE_RATE);
    double tail = 0;
    for (int i = 1; i < VT_SAMPLE_RATE; i++) tail += fabs(g_imp[i]);
    CHECK(fabs(g_imp[0] - 0.5) < 1e-6 && tail < 1e-9);
    dsp_cleanup(vt);
}

/* ------------------------------------------------------------- state */

static void test_state_roundtrip(void)
{
    vt_t *a = dsp_init(VT_SAMPLE_RATE), *b = dsp_init(VT_SAMPLE_RATE);
    const char *set[][2] = {
        {"mode", "Prism"}, {"key", "F#"}, {"scale", "Dorian"},
        {"retune", "37"}, {"char1", "0.83"}, {"char2", "0.12"}, {"gate", "-42 dB"},
        {"mix", "0.44"}, {"gain", "-7.5"},
        {"spread", "0.71"}, {"tone", "0.4"}, {"glide", "120"}, {"mod", "0.33"},
        {"freeze_len", "4 Bars"}, {"beauty", "0.83"}, {"latch", "On"},
        {"char4", "0.77"}, {"char5", "0.21"},
        {"engine", "VoiceFX"}, {"vf_vowel", "0.62"}, {"vf_tract", "0.31"}, {"vf_peak", "0.88"}, {"vf_choir", "0.73"},
        {"vf_breath", "0.44"}, {"vf_attack", "0.57"}, {"vf_release", "0.91"}, {"vf_mix", "0.38"},
    };
    CHECK(sizeof set / sizeof set[0] == VT_P_COUNT - 3);            /* every saved param (not freeze, demo or the preset index) off its default */
    for (size_t i = 0; i < sizeof set / sizeof set[0]; i++) CHECK(dsp_set_param(a, set[i][0], set[i][1]) == 0);

    char state[1024];
    int n = dsp_get_param(a, "state", state, sizeof state);
    CHECK(n > 0 && state[0] == '{' && state[n - 1] == '}');
    char small[40];
    CHECK(dsp_get_param(a, "state", small, sizeof small) == -1);    /* too small: refuses, no overrun */

    CHECK(dsp_set_param(b, "state", state) == 0);
    for (int i = 0; i < VT_P_COUNT; i++) {
        char va[64], vb[64];
        const char *key = dsp_param_def(i)->key;
        CHECK(dsp_get_param(a, key, va, sizeof va) > 0 && dsp_get_param(b, key, vb, sizeof vb) > 0);
        if (strcmp(va, vb)) printf("  %s: %s vs %s\n", key, va, vb);
        CHECK(strcmp(va, vb) == 0);
    }
    char s2[1024];
    CHECK(dsp_get_param(b, "state", s2, sizeof s2) == n && strcmp(state, s2) == 0);   /* stable fixed point */

    /* tolerant loader: whitespace, unknown keys, nested values, numeric enums, clamping, junk */
    vt_t *c = dsp_init(VT_SAMPLE_RATE);
    const char *messy = " { \"mode\" : \"Robot\" , \"bogus\" : { \"mix\" : 0.1 , \"x\" : [1, \"}\", {\"mode\":\"Snap\"}] },\n"
                        "  \"mix\":0.25, \"key\": 6, \"root\": \"B\", \"spread\": 7, \"reverb\": 0.5, \"future_param\": true, \"scale\": \"Nope\" } ";
    CHECK(dsp_set_param(c, "state", messy) == 0);
    char v[64];
    dsp_get_param(c, "mode", v, sizeof v);     CHECK(strcmp(v, "Robot") == 0);  /* not clobbered by the nested "Snap" */
    dsp_get_param(c, "mix", v, sizeof v);      CHECK(strcmp(v, "0.25") == 0);   /* top-level, not the nested 0.1 */
    dsp_get_param(c, "key", v, sizeof v);      CHECK(strcmp(v, "F#") == 0);     /* index 6: C, C#, D, D#, E, F, F#; old "root" ignored */
    dsp_get_param(c, "spread", v, sizeof v);   CHECK(strcmp(v, "1") == 0);      /* clamped; the old "reverb" key is ignored */
    dsp_get_param(c, "scale", v, sizeof v);    CHECK(strcmp(v, "Chromatic") == 0);  /* bad value ignored */

    CHECK(dsp_set_param(c, "state", "not json") == -1);
    CHECK(dsp_set_param(c, "state", "") == -1);
    CHECK(dsp_set_param(c, "state", "{\"mode\":\"Snap\"") == 0);    /* truncated: applies what it can */
    CHECK(dsp_set_param(c, "state", "{\"mode\"") == -1);
    CHECK(dsp_set_param(c, "state", "{\"unterminated\":\"abc") == 0);
    dsp_cleanup(a); dsp_cleanup(b); dsp_cleanup(c);
}

/* ------------------------------------------------ module.json consistency */

static const char *js_ws(const char *p) { while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++; return p; }
static const char *js_skip_str(const char *p) { for (p++; *p && *p != '"'; p++) if (*p == '\\') p++; return *p ? p + 1 : p; }
static const char *js_skip_val(const char *p)
{
    p = js_ws(p);
    if (*p == '"') return js_skip_str(p);
    if (*p == '{' || *p == '[') {
        int depth = 0;
        do {
            if (*p == '"') { p = js_skip_str(p); continue; }
            if (*p == '{' || *p == '[') depth++;
            else if (*p == '}' || *p == ']') depth--;
            p++;
        } while (*p && depth > 0);
        return p;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']' && !isspace((unsigned char)*p)) p++;
    return p;
}
/* Value start of member `key` of object `obj`, or NULL. */
static const char *js_get(const char *obj, const char *key)
{
    const char *p = js_ws(obj);
    if (*p != '{') return NULL;
    for (p++;;) {
        p = js_ws(p);
        if (*p == ',') { p++; continue; }
        if (*p != '"') return NULL;
        const char *ks = p + 1;
        p = js_skip_str(p);
        size_t kn = (size_t)(p - ks) - 1;
        p = js_ws(p);
        if (*p != ':') return NULL;
        p = js_ws(p + 1);
        if (strlen(key) == kn && strncmp(ks, key, kn) == 0) return p;
        p = js_skip_val(p);
    }
}
/* idx-th member of an object: copies the key, returns value start. */
static const char *js_nth_member(const char *obj, int idx, char *key, size_t cap)
{
    const char *p = js_ws(obj);
    if (*p != '{') return NULL;
    for (p++;;) {
        p = js_ws(p);
        if (*p == ',') { p++; continue; }
        if (*p != '"') return NULL;
        const char *ks = p + 1;
        p = js_skip_str(p);
        size_t kn = (size_t)(p - ks) - 1;
        p = js_ws(p);
        if (*p != ':') return NULL;
        p = js_ws(p + 1);
        if (idx-- == 0) { if (kn >= cap) kn = cap - 1; memcpy(key, ks, kn); key[kn] = '\0'; return p; }
        p = js_skip_val(p);
    }
}
static const char *js_elem(const char *arr, int idx)
{
    const char *p = js_ws(arr);
    if (*p != '[') return NULL;
    for (p++;; idx--) {
        p = js_ws(p);
        if (*p == ']' || !*p) return NULL;
        if (idx == 0) return p;
        p = js_ws(js_skip_val(p));
        if (*p == ',') p++;
    }
}
static int js_str(const char *v, char *out, size_t cap)
{
    if (!v || *v != '"') return 0;
    size_t n = (size_t)(js_skip_str(v) - v) - 2;
    if (n >= cap) n = cap - 1;
    memcpy(out, v + 1, n); out[n] = '\0';
    return 1;
}
static int js_arr_len(const char *arr) { int n = 0; while (js_elem(arr, n)) n++; return n; }
/* case-insensitive equality (the host matches mode names to option names that way) */
static int vt_ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

static char g_module_json[32768];

static int load_module_json(void)
{
    const char *paths[] = {
#ifdef VT_MODULE_JSON
        VT_MODULE_JSON,
#endif
        "module.json", "../module.json", "../../module.json"
    };
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        FILE *f = fopen(paths[i], "rb");
        if (!f) continue;
        size_t n = fread(g_module_json, 1, sizeof g_module_json - 1, f);
        fclose(f);
        g_module_json[n] = '\0';
        return n > 0;
    }
    return 0;
}

/* A page lists a param as "key" or as {"key": "...", "visible_if": {...}}. */
static int entry_key(const char *pe, char *out, size_t cap)
{
    if (!pe) return 0;
    if (*pe == '"') return js_str(pe, out, cap);
    if (*pe == '{') return js_str(js_get(pe, "key"), out, cap);
    return 0;
}

static void test_module_json_matches_params(void)
{
    if (!load_module_json()) { CHECK(!"module.json not found (run from the repo root)"); return; }
    const char *root = g_module_json;
    char s[64], t[64];

    /* Identity. Schwung's chain loads audio FX from <id>/<id>.so, so "dsp" must
     * name exactly that file or the module lists but never loads. */
    CHECK(js_str(js_get(root, "id"), s, sizeof s) && strcmp(s, "tinyvoice") == 0);
    CHECK(js_str(js_get(root, "dsp"), t, sizeof t) && strcmp(t, "tinyvoice.so") == 0);
    CHECK(js_str(js_get(root, "component_type"), s, sizeof s) && strcmp(s, "audio_fx") == 0);
    CHECK(js_get(root, "api_version") && atoi(js_get(root, "api_version")) == 2);

    /* Everything the host reads lives under "capabilities". */
    const char *caps = js_get(root, "capabilities");
    CHECK(caps != NULL);
    if (!caps) return;
    CHECK(js_str(js_get(caps, "component_type"), s, sizeof s) && strcmp(s, "audio_fx") == 0);
    CHECK(strncmp(js_get(caps, "chainable"), "true", 4) == 0);
    CHECK(js_get(caps, "midi_in") && strncmp(js_get(caps, "midi_in"), "true", 4) == 0);   /* Daft carrier is MIDI-driven */
    CHECK(js_get(root, "chain_params") == NULL && js_get(root, "ui_hierarchy") == NULL);   /* not at top level */

    /* chain_params: same keys, same order, same types, ranges, options and defaults as the C table */
    const char *cp = js_get(caps, "chain_params");
    CHECK(cp && js_arr_len(cp) == VT_P_COUNT + 3);                /* the params, then 3 readouts */
    for (int i = 0; i < VT_P_COUNT; i++) {
        const vt_param_def *d = dsp_param_def(i);
        const char *o = js_elem(cp, i);
        if (!o) { CHECK(!"missing chain_params entry"); continue; }

        CHECK(js_str(js_get(o, "key"), s, sizeof s) && strcmp(s, d->key) == 0);
        CHECK(js_str(js_get(o, "name"), s, sizeof s) && s[0]);
        CHECK(js_str(js_get(o, "type"), s, sizeof s) &&
              strcmp(s, d->type == VT_PTYPE_ENUM ? "enum" : "float") == 0);

        const char *def = js_get(o, "default");
        CHECK(def != NULL);
        if (def && d->type == VT_PTYPE_ENUM) {
            const char *opts = js_get(o, "options");
            CHECK(opts && js_arr_len(opts) == d->num_options);
            for (int k = 0; opts && k < d->num_options; k++)
                CHECK(js_str(js_elem(opts, k), s, sizeof s) && strcmp(s, d->options[k]) == 0);
            CHECK((int)atof(def) == (int)d->def && atof(def) == (double)(int)atof(def));   /* default is an option index */
        } else if (def) {
            const char *mn = js_get(o, "min"), *mx = js_get(o, "max"), *st = js_get(o, "step");
            CHECK(mn && mx && st);
            if (mn && mx && st) {
                CHECK(fabs(atof(mn) - d->min) < 1e-6);
                CHECK(fabs(atof(mx) - d->max) < 1e-6);
                CHECK(atof(st) > 0 && atof(st) <= (d->max - d->min));
            }
            CHECK(fabs(atof(def) - d->def) < 1e-6);
        }
    }

    /* The OLED widget: three read-only readouts after the params (pitch,
     * target, cents) declare one custom group fed by the "scope" key. They
     * are not params (never set, never saved) but the DSP serves them, and
     * they sit on TUNE knobs 5-7, so turning those knobs changes nothing. */
    {
        const char *rkeys[3] = { "pitch", "target", "cents" };
        char seen_roles[3][8];
        vt_t *w = dsp_init(VT_SAMPLE_RATE);
        for (int k = 0; k < 3; k++) {
            const char *o = js_elem(cp, VT_P_COUNT + k);
            if (!o) { CHECK(!"missing readout"); continue; }
            CHECK(js_str(js_get(o, "key"), s, sizeof s) && strcmp(s, rkeys[k]) == 0);
            CHECK(js_str(js_get(o, "access"), t, sizeof t) && strcmp(t, "read") == 0);
            CHECK(dsp_param_index(rkeys[k]) < 0);
            char got[64];
            CHECK(dsp_get_param(w, rkeys[k], got, sizeof got) > 0);
            CHECK(dsp_set_param(w, rkeys[k], "1") == -1);
            const char *viz = js_get(o, "viz");
            CHECK(viz != NULL);
            if (!viz) continue;
            CHECK(js_str(js_get(viz, "kind"), s, sizeof s) && strcmp(s, "custom:tinyvoice_scope") == 0);
            CHECK(js_str(js_get(viz, "group"), s, sizeof s) && strcmp(s, "scope") == 0);
            CHECK(js_str(js_get(viz, "role"), seen_roles[k], sizeof seen_roles[k]));
            for (int j = 0; j < k; j++) CHECK(strcmp(seen_roles[j], seen_roles[k]) != 0);   /* a role per cell */
            const char *ek = js_get(viz, "extra_keys");
            CHECK(ek && js_str(js_elem(ek, 0), s, sizeof s) && strcmp(s, "scope") == 0);
        }
        int declared = 0;                                        /* and nothing else declares it */
        for (int i = 0; i < js_arr_len(cp); i++) {
            const char *o = js_elem(cp, i);
            if (o && js_get(o, "viz")) declared++;
        }
        CHECK(declared == 11);                                   /* the scope's 3, Freeze banner's 2, Latch's 1, Key/Scale/Retune/Glide/Beauty 5 */
        /* Nothing disappears: no param is conditionally hidden. Gate holds
         * Auto as its lowest setting, then -60..0 dB. */
        for (int i = 0; i < js_arr_len(cp); i++) CHECK(js_get(js_elem(cp, i), "visible_if") == NULL);
        const char *g = js_elem(cp, VT_P_GATE);
        const char *go = g ? js_get(g, "options") : NULL;
        CHECK(go && js_arr_len(go) == VT_GATE_OPTS);
        CHECK(go && js_str(js_elem(go, 0), s, sizeof s) && strcmp(s, "Auto") == 0);
        CHECK(go && js_str(js_elem(go, 1), s, sizeof s) && strcmp(s, "-60 dB") == 0);
        CHECK(go && js_str(js_elem(go, VT_GATE_OPTS - 1), s, sizeof s) && strcmp(s, "0 dB") == 0);
        const char *lt = js_elem(cp, VT_P_LATCH), *lv = lt ? js_get(lt, "viz") : NULL;
        CHECK(lv && js_str(js_get(lv, "kind"), s, sizeof s) && strcmp(s, "custom:tinyvoice_latch") == 0);
        CHECK(lv && js_get(lv, "extra_keys") && js_str(js_elem(js_get(lv, "extra_keys"), 0), s, sizeof s) && strcmp(s, "mode") == 0);
        static const int gated_ids[5] = { VT_P_KEY, VT_P_SCALE, VT_P_RETUNE, VT_P_GLIDE, VT_P_BEAUTY };
        for (int g = 0; g < 5; g++) {                                /* Key, Scale, Retune, Glide, Beauty: N/A widgets */
            const char *gp = js_elem(cp, gated_ids[g]), *gv = gp ? js_get(gp, "viz") : NULL;
            char want_kind[48];
            snprintf(want_kind, sizeof want_kind, "custom:tinyvoice_%s", dsp_param_def(gated_ids[g])->key);
            CHECK(gv && js_str(js_get(gv, "kind"), s, sizeof s) && strcmp(s, want_kind) == 0);
            CHECK(gv && js_get(gv, "extra_keys") && js_str(js_elem(js_get(gv, "extra_keys"), 0), s, sizeof s) && strcmp(s, "mode") == 0);
        }
        char dummy[256];
        CHECK(dsp_get_param(w, "scope", dummy, sizeof dummy) > 0); /* the extra_key exists */
        dsp_cleanup(w);
    }

    /* a fresh engine: char4 / char5 are Snap's Retune (default 0 ms) and Beauty (default 0.5), scaled to 0..1 */
    {
        vt_t *f = dsp_init(VT_SAMPLE_RATE);
        char got[64];
        CHECK(dsp_get_param(f, "char4", got, sizeof got) > 0 && fabs(atof(got) - 0.0) < 1e-6);
        CHECK(dsp_get_param(f, "char5", got, sizeof got) > 0 && fabs(atof(got) - 0.5) < 1e-6);
        dsp_cleanup(f);
    }
    /* declared defaults are what a fresh engine actually reports */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    for (int i = 0; i < VT_P_COUNT; i++) {
        const vt_param_def *d = dsp_param_def(i);
        char got[64];
        CHECK(dsp_get_param(vt, d->key, got, sizeof got) > 0);
        if (d->type == VT_PTYPE_ENUM) CHECK(strcmp(got, d->options[(int)d->def]) == 0);
        else if (i == VT_P_CHAR4 || i == VT_P_CHAR5) { /* aliases of Retune / Beauty in Snap, whatever the sweeps left them at: checked below */ }
        else CHECK(fabs(atof(got) - d->def) < 1e-6);

        /* setter/getter tables: the full declared range is reachable, nothing beyond it */
        if (d->type == VT_PTYPE_ENUM) {
            for (int k = 0; k < d->num_options; k++) {
                CHECK(dsp_set_param(vt, d->key, d->options[k]) == 0);
                CHECK(dsp_get_param(vt, d->key, got, sizeof got) > 0 && strcmp(got, d->options[k]) == 0);
            }
        } else {
            char in[64];
            if (i == VT_P_BEAUTY) dsp_set_param(vt, "mode", "Snap");   /* the sweep left Mode on Tape, where Beauty is N/A */
            snprintf(in, sizeof in, "%g", (double)d->min - 1000.0);
            dsp_set_param(vt, d->key, in);
            CHECK(dsp_get_param(vt, d->key, got, sizeof got) > 0 && fabs(atof(got) - d->min) < 1e-6);
            snprintf(in, sizeof in, "%g", (double)d->max + 1000.0);
            dsp_set_param(vt, d->key, in);
            CHECK(dsp_get_param(vt, d->key, got, sizeof got) > 0 && fabs(atof(got) - d->max) < 1e-6);
        }
    }
    dsp_cleanup(vt);

    /* ui_hierarchy: TUNE, SPACE, MORE, in the shape other Schwung modules use.
     * The root links to them with {"level", "label"} and repeats page 1's
     * knobs; each page lists its params as key strings (or {"key": ...}).
     * Knobs are exactly the agreed layout, every knob is listed on its page,
     * and every param is on exactly one page. */
    static const char *const want[3][8] = {
        { "mode", "char1", "char2", "mod", "char4", "char5", "mix", "gain" },       /* VOICE: Mode, five contextual knobs, Mix, Level */
        { "key", "scale", "retune", "glide", "pitch", "target", "cents" },           /* PITCH: scope on 5-7 */
        { "spread", "tone", "freeze", "freeze_len", "gate", "latch", "beauty", "demo" }, /* MORE: Freeze + its length side by side (top row), Gate (with Auto), Latch, Beauty, Demo bottom right */
    };
    static const char *const page_names[3] = { "VOICE", "PITCH", "MORE" };
    const char *levels = js_get(js_get(caps, "ui_hierarchy"), "levels");
    CHECK(levels != NULL);
    if (!levels) return;
    /* the two personalities: `modes` names the root levels, `mode_param` is the param the host's Mode page writes */
    const char *uih = js_get(caps, "ui_hierarchy");
    const char *modes_arr = js_get(uih, "modes");
    CHECK(modes_arr && js_arr_len(modes_arr) == 2);
    CHECK(js_str(js_get(uih, "mode_param"), s, sizeof s) && strcmp(s, "engine") == 0);
    CHECK(modes_arr && js_str(js_elem(modes_arr, 0), s, sizeof s) && strcmp(s, "vocoder") == 0);
    CHECK(modes_arr && js_str(js_elem(modes_arr, 1), s, sizeof s) && strcmp(s, "voicefx") == 0);
    {   /* the engine param's options are those mode names in the host's own spelling (case aside), so its Mode page commits a value the DSP takes */
        const vt_param_def *ed = dsp_param_def(VT_P_ENGINE);
        CHECK(ed && ed->type == VT_PTYPE_ENUM && ed->num_options == 2);
        CHECK(ed && vt_ieq(ed->options[0], "vocoder") && vt_ieq(ed->options[1], "voicefx"));
    }
    const char *rootlv = js_get(levels, "vocoder");
    CHECK(rootlv != NULL);
    if (!rootlv) return;

    int nlevels = 0;
    char names[16][32];
    while (js_nth_member(levels, nlevels, names[nlevels], sizeof names[0]) && nlevels < 16) nlevels++;
    CHECK(nlevels == 6);
    CHECK(strcmp(names[0], "vocoder") == 0);
    for (int l = 1; l < nlevels && l < 4; l++) CHECK(strcmp(names[l], page_names[l - 1]) == 0);
    CHECK(strcmp(names[4], "voicefx") == 0);
    CHECK(strcmp(names[5], "audition") == 0);

    int listed_on[VT_P_COUNT] = {0}, referenced[16] = {0};
    for (int l = 0; l < nlevels && l < 4; l++) {
        const char *lv = js_get(levels, names[l]);
        const int is_root = l == 0;
        const int pg = is_root ? 0 : l - 1;
        const char *knobs = js_get(lv, "knobs"), *params = js_get(lv, "params");
        CHECK(knobs && params);
        CHECK(js_get(lv, is_root ? "name" : "label") != NULL);
        if (!knobs || !params) continue;
        int nk = 0;
        while (nk < 8 && want[pg][nk]) nk++;
        CHECK(js_arr_len(knobs) == nk);                               /* Move has 8 encoders per page */

        for (int k = 0; k < js_arr_len(knobs) && k < 8; k++) {
            CHECK(js_str(js_elem(knobs, k), s, sizeof s) && want[pg][k] && strcmp(s, want[pg][k]) == 0);
            if (is_root) continue;
            int listed = 0;                                           /* every knob is a listed param */
            for (int q = 0; q < js_arr_len(params); q++)
                if (entry_key(js_elem(params, q), t, sizeof t) && strcmp(s, t) == 0) listed = 1;
            CHECK(listed);
        }
        for (int q = 0; q < js_arr_len(params); q++) {
            const char *pe = js_elem(params, q);
            if (is_root) {
                CHECK(*pe == '{');
                CHECK(js_str(js_get(pe, "level"), t, sizeof t));
                CHECK(js_get(pe, "label") != NULL);
                int found = -1;
                for (int m = 0; m < nlevels; m++) if (strcmp(names[m], t) == 0) found = m;
                CHECK(found > 0);                                     /* a real child, not root */
                if (found >= 0) referenced[found] = 1;
            } else {
                CHECK(entry_key(pe, t, sizeof t));                    /* key string, or {"key": ...} */
                int id = dsp_param_index(t);
                int readout = strcmp(t, "pitch") == 0 || strcmp(t, "target") == 0 || strcmp(t, "cents") == 0;
                CHECK(id >= 0 || (readout && pg == 1));               /* readouts only on PITCH */
                if (id >= 0) listed_on[id]++;
            }
        }
    }
    /* Voice FX: one page of eight, in the agreed order, nothing else */
    {
        static const char *const vf[8] = { "vf_vowel", "vf_tract", "vf_peak", "vf_choir", "vf_breath", "vf_attack", "vf_release", "vf_mix" };
        const char *lv = js_get(levels, "voicefx");
        const char *knobs = js_get(lv, "knobs"), *params = js_get(lv, "params");
        CHECK(lv && knobs && params && js_get(lv, "name") != NULL);
        CHECK(knobs && js_arr_len(knobs) == 8 && params && js_arr_len(params) == 9);      /* eight knobs, then the nav to the Audition page */
        CHECK(params && js_get(js_elem(params, 8), "level") != NULL && js_str(js_get(js_elem(params, 8), "level"), s, sizeof s) && strcmp(s, "audition") == 0);
        /* the preset browser's triple lives on this level and no other: the Vocoder has no browser */
        CHECK(js_str(js_get(lv, "list_param"), s, sizeof s) && strcmp(s, "vf_preset") == 0);
        CHECK(js_str(js_get(lv, "count_param"), s, sizeof s) && strcmp(s, "vf_preset_count") == 0);
        CHECK(js_str(js_get(lv, "name_param"), s, sizeof s) && strcmp(s, "vf_preset_name") == 0);
        for (int q = 0; q < nlevels; q++)
            if (strcmp(names[q], "voicefx") != 0) CHECK(js_get(js_get(levels, names[q]), "list_param") == NULL);
        for (int k = 0; k < 8 && knobs; k++) {
            CHECK(js_str(js_elem(knobs, k), s, sizeof s) && strcmp(s, vf[k]) == 0);
            const int id = dsp_param_index(vf[k]);
            CHECK(id >= 0 && dsp_param_def(id)->type == VT_PTYPE_FLOAT);
            if (id >= 0) listed_on[id]++;
        }
    }
    /* Audition: Demo alone on a page after the eight knobs, so those stay untouched (Mode and the preset browser are drawn by the host) */
    {
        const char *lv = js_get(levels, "audition");
        const char *knobs = js_get(lv, "knobs");
        CHECK(lv && knobs && js_arr_len(knobs) == 1 && js_str(js_elem(knobs, 0), s, sizeof s) && strcmp(s, "demo") == 0);
        listed_on[VT_P_DEMO]++;
    }
    for (int i = 0; i < VT_P_COUNT; i++) {
        const int want_pages = (i == VT_P_ENGINE || i == VT_P_VF_PRESET) ? 0 : (i == VT_P_DEMO ? 2 : 1);   /* Demo: the vocoder's More page and Voice FX's Audition page */   /* `engine` is the host's Mode page, `vf_preset` its preset browser: not knobs */
        if (listed_on[i] != want_pages) printf("  param %s listed on %d pages\n", dsp_param_def(i)->key, listed_on[i]);
        CHECK(listed_on[i] == want_pages);
    }
    for (int l = 1; l < 4; l++) CHECK(referenced[l]);                 /* every vocoder page reachable from its root (Voice FX is reached by the Mode page) */
}


/* ------------------------------------------------------------- Imogen */

/* Energy within +-1.5% of f, probed every 0.1%: fine enough that a pure tone
 * can't fall between probes (Goertzel's main lobe here is only a few Hz wide). */
static double band_at(const float *x, int n, double f)
{
    double e = 0;
    for (double g = f * 0.985; g < f * 1.015; g += f * 0.001) e += goertzel(x, n, g);
    return e;
}

/* Sing a harmonic tone into Imogen directly (tracker + voices), stereo out. */
static void imogen_run(imogen_t *im, cher_t *c, const int held[IMOGEN_VOICES], int auto_on,
                       int root, int scale, float spread, double f_in, int n, float *out_l, float *out_r)
{
    cher_set_params(c, root, scale, 0.0f, -60.0f, -1, 0.0f);
    for (int i = 0; i < n; i++) {
        if (i % 128 == 0) imogen_set_targets(im, c, held, auto_on, root, scale, 0.0f, 0.0f, spread, VT_VOICING_STANDARD);
        imogen_process(im, c, tone(f_in, i, 6, VT_SAMPLE_RATE), &out_l[i], &out_r[i]);
    }
}

static void test_imogen_auto_chord(void)
{
    static cher_t c;
    static imogen_t im;
    static float l[3 * VT_SAMPLE_RATE / 2], r[3 * VT_SAMPLE_RATE / 2];
    const int n = 3 * VT_SAMPLE_RATE / 2, tail = VT_SAMPLE_RATE / 2;
    const int none[IMOGEN_VOICES] = { -1, -1, -1, -1 };

    /* Chord shapes: in C major, A3 gets A3 / C4 / E4 / A2 (a minor triad, in key);
     * chromatic falls back to a major triad (C#4 instead of C4). */
    int ch[IMOGEN_VOICES];
    imogen_auto_chord(57, 0, VT_SCALE_MAJOR, ch);
    CHECK(ch[0] == 57 && ch[1] == 60 && ch[2] == 64 && ch[3] == 45);
    imogen_auto_chord(57, 0, VT_SCALE_CHROMATIC, ch);
    CHECK(ch[0] == 57 && ch[1] == 61 && ch[2] == 64 && ch[3] == 45);
    imogen_auto_chord(62, 0, VT_SCALE_PENTATONIC, ch);            /* D in C pentatonic: D E G A C */
    CHECK(ch[1] == 67 && ch[2] == 72);
    CHECK(cher_scale_step(64, -1, 0, VT_SCALE_MAJOR) == 62 && cher_scale_step(60, 7, 0, VT_SCALE_MAJOR) == 72);

    /* Audio: sing A3, C major, Auto on, no pads -> energy at all four chord
     * fundamentals, and the in-key 3rd (C4) rather than the out-of-key C#4. */
    cher_init(&c, VT_SAMPLE_RATE);
    imogen_init(&im, VT_SAMPLE_RATE);
    imogen_run(&im, &c, none, 1, 0, VT_SCALE_MAJOR, 0.0f, 220.0, n, l, r);
    const float *t = l + n - tail;
    double a2 = band_at(t, tail, 110.0), a3 = band_at(t, tail, 220.0);
    double c4 = band_at(t, tail, 261.63), e4 = band_at(t, tail, 329.63), cs4 = band_at(t, tail, 277.18);
    double weakest = fmin(fmin(a2, a3), fmin(c4, e4));
    if (!(weakest > 0.05 * fmax(fmax(a2, a3), fmax(c4, e4)) && c4 > 10.0 * cs4))
        printf("  auto chord energies A2 %.2e A3 %.2e C4 %.2e E4 %.2e (C#4 %.2e)\n", a2, a3, c4, e4, cs4);
    CHECK(weakest > 0.05 * fmax(fmax(a2, a3), fmax(c4, e4)));     /* all four voices sound */
    CHECK(c4 > 10.0 * cs4);                                        /* the 3rd is diatonic */
    for (int i = 0; i < n; i++) CHECK(isfinite(l[i]) && isfinite(r[i]));
}

static void test_imogen_midi_chord_and_lead(void)
{
    static cher_t c;
    static imogen_t im;
    static float l[3 * VT_SAMPLE_RATE / 2], r[3 * VT_SAMPLE_RATE / 2];
    const int n = 3 * VT_SAMPLE_RATE / 2, tail = VT_SAMPLE_RATE / 2;

    /* Pads C4 E4 G4 held while singing A3: the voices sing the pad notes and
     * the sung pitch itself is gone (it's an override, not a doubling). */
    const int held[IMOGEN_VOICES] = { 60, 64, 67, -1 };
    cher_init(&c, VT_SAMPLE_RATE);
    imogen_init(&im, VT_SAMPLE_RATE);
    imogen_run(&im, &c, held, 1, 0, VT_SCALE_MAJOR, 0.0f, 220.0, n, l, r);
    const float *t = l + n - tail;
    double c4 = band_at(t, tail, 261.63), e4 = band_at(t, tail, 329.63), g4 = band_at(t, tail, 392.0);
    double a3 = band_at(t, tail, 220.0);
    double weakest = fmin(c4, fmin(e4, g4));
    if (!(weakest > 20.0 * a3)) printf("  pad chord: C4 %.2e E4 %.2e G4 %.2e vs sung A3 %.2e\n", c4, e4, g4, a3);
    CHECK(weakest > 20.0 * a3);
    CHECK(im.active == 3);

    /* Auto off, no pads: just the lead, snapped (225 Hz is A3 +39 cents). */
    const int none[IMOGEN_VOICES] = { -1, -1, -1, -1 };
    cher_init(&c, VT_SAMPLE_RATE);
    imogen_init(&im, VT_SAMPLE_RATE);
    imogen_run(&im, &c, none, 0, 0, VT_SCALE_CHROMATIC, 0.0f, 225.0, n, l, r);
    static float nsdf[CHER_MAX_TAU + 2];
    float f0 = 0, clar = 0;
    int ok = cher_mpm(l + n - 4096, 4096, VT_SAMPLE_RATE, 120.0f, 1000.0f, nsdf, &f0, &clar);
    if (!ok || fabs(cents(f0, 220.0)) >= 15.0) printf("  lead only: ok=%d f0=%.2f\n", ok, f0);
    CHECK(ok && fabs(cents(f0, 220.0)) < 15.0);
    CHECK(im.active == 1);
}

static void test_imogen_spread(void)
{
    static cher_t c;
    static imogen_t im;
    static float l[VT_SAMPLE_RATE], r[VT_SAMPLE_RATE];
    const int none[IMOGEN_VOICES] = { -1, -1, -1, -1 };
    const int n = VT_SAMPLE_RATE, tail = VT_SAMPLE_RATE / 2;

    cher_init(&c, VT_SAMPLE_RATE);
    imogen_init(&im, VT_SAMPLE_RATE);
    imogen_run(&im, &c, none, 1, 0, VT_SCALE_MAJOR, 0.0f, 220.0, n, l, r);
    int same = 1;
    for (int i = 0; i < n; i++) if (l[i] != r[i]) same = 0;
    CHECK(same);                                                   /* spread 0: mono */

    cher_init(&c, VT_SAMPLE_RATE);
    imogen_init(&im, VT_SAMPLE_RATE);
    imogen_run(&im, &c, none, 1, 0, VT_SCALE_MAJOR, 1.0f, 220.0, n, l, r);
    /* the 3rd is panned hard left, the 5th hard right */
    double c4l = band_at(l + n - tail, tail, 261.63), c4r = band_at(r + n - tail, tail, 261.63);
    double e4l = band_at(l + n - tail, tail, 329.63), e4r = band_at(r + n - tail, tail, 329.63);
    if (!(c4l > 10.0 * c4r && e4r > 10.0 * e4l)) printf("  spread: C4 L/R %.2e/%.2e  E4 L/R %.2e/%.2e\n", c4l, c4r, e4l, e4r);
    CHECK(c4l > 10.0 * c4r);
    CHECK(e4r > 10.0 * e4l);
}

static void test_imogen_through_engine(void)
{
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    CHECK(dsp_set_param(vt, "mode", "Chords") == 0);
    dsp_set_param(vt, "gain", "0");
    long pos = 0;
    static float cap[4096];
    run_sine(vt, 220.0, 0.4f, 300, &pos, NULL, 0);                 /* auto chord on a sung A3 */
    run_sine(vt, 220.0, 0.4f, 32, &pos, cap, 4096);
    CHECK(rms_of(cap, 4096) > 0.05);
    char buf[96];
    CHECK(dsp_get_param(vt, "viz", buf, sizeof buf) > 0 && buf[0] == 'c');

    note_on(vt, 60); note_on(vt, 64); note_on(vt, 67);            /* pads take over */
    run_sine(vt, 220.0, 0.4f, 300, &pos, NULL, 0);
    run_sine(vt, 220.0, 0.4f, 32, &pos, cap, 4096);
    /* Mix is 100%: nothing of the sung A3 is left (the dry path is exactly 0).
     * (A pure sine has no spectrum above itself for a formant-preserving
     * shift to keep, so the +10 st G4 is weak here; C4 and E4 are not.) */
    double a3 = band_at(cap, 4096, 220.0);
    if (!(band_at(cap, 4096, 261.63) > 100.0 * a3 && band_at(cap, 4096, 329.63) > 100.0 * a3))
        printf("  chords pads: C4 %.2e E4 %.2e vs A3 %.2e\n", band_at(cap, 4096, 261.63), band_at(cap, 4096, 329.63), a3);
    CHECK(band_at(cap, 4096, 261.63) > 100.0 * a3);
    CHECK(band_at(cap, 4096, 329.63) > 100.0 * a3);
    for (int i = 0; i < 4096; i++) CHECK(isfinite(cap[i]));
    dsp_cleanup(vt);
}

/* ---------------------------------------------------- Auto, Key, Tone, Spread */

static void test_cher_auto_gate_rejects_hum(void)
{
    /* Steady 120 Hz mains hum at -50 dBFS. The manual -60 dB gate lets it
     * through and the tracker "sings" it; the Auto gate learns it as the noise
     * floor (within its ~3 s window) and ignores it, yet still tracks a voice
     * over the top. */
    static cher_t c;
    for (int auto_on = 0; auto_on < 2; auto_on++) {
        cher_init(&c, VT_SAMPLE_RATE);
        cher_set_params(&c, 0, VT_SCALE_CHROMATIC, 0.0f, -60.0f, -1, 0.0f);
        cher_set_auto(&c, auto_on);
        for (int i = 0; i < 4 * VT_SAMPLE_RATE; i++)
            cher_process(&c, 0.003f * (float)sin(2.0 * M_PI * 120.0 * i / VT_SAMPLE_RATE));
        if (auto_on) CHECK(c.f0 == 0.0f);
        else CHECK(fabs(cents(c.f0, 120.0)) < 10.0);
        if (auto_on) {
            for (int i = 0; i < VT_SAMPLE_RATE / 2; i++)
                cher_process(&c, 0.003f * (float)sin(2.0 * M_PI * 120.0 * i / VT_SAMPLE_RATE)
                                 + 0.2f * tone(220.0, i, 6, VT_SAMPLE_RATE));
            if (fabs(cents(c.f0, 220.0)) >= 5.0) printf("  voice over hum: f0=%.2f\n", c.f0);
            CHECK(fabs(cents(c.f0, 220.0)) < 5.0);
        }
    }
}

static void test_root_follow(void)
{
    /* Key is set by hand: the 12 roots, C by default. "Follow" is gone (Move
     * never shares its project key), and an old set that saved it loads as C,
     * which is what Follow always played. */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    char v[32];
    CHECK(dsp_get_param(vt, "key", v, sizeof v) > 0 && strcmp(v, "C") == 0);        /* default */
    CHECK(dsp_param_def(VT_P_KEY)->num_options == 12);
    CHECK(dsp_set_param(vt, "key", "Follow") == -1);
    CHECK(dsp_set_param(vt, "state", "{\"key\":\"Follow\",\"scale\":\"Major\"}") == 0);
    CHECK(dsp_get_param(vt, "key", v, sizeof v) > 0 && strcmp(v, "C") == 0);        /* old set: C */
    CHECK(dsp_set_param(vt, "host_root", "D") == -1);                               /* no hook any more */
    CHECK(dsp_set_param(vt, "key", "B") == 0 && dsp_get_param(vt, "key", v, sizeof v) > 0 && strcmp(v, "B") == 0);
    dsp_set_param(vt, "key", "C");

    /* And it changes what the snap does: 281 Hz (MIDI 61.2) in C major -> D4,
     * in D major (C# is in key) -> C#4. */
    dsp_set_param(vt, "mode", "Snap");
    dsp_set_param(vt, "scale", "Major");
    long pos = 0;
    run_sine(vt, 281.0, 0.4f, 200, &pos, NULL, 0);
    CHECK(fabs(viz_target(vt) - 293.665) < 0.5);
    dsp_set_param(vt, "key", "D");
    run_sine(vt, 281.0, 0.4f, 200, &pos, NULL, 0);
    CHECK(fabs(viz_target(vt) - 277.183) < 0.5);
    dsp_cleanup(vt);
}

static void test_tone_and_spread(void)
{
    static float out[2][2 * 128 * 200];
    double hf[2];
    for (int k = 0; k < 2; k++) {                                  /* Tone open vs closed on noise */
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "mode", "Snap");
        dsp_set_param(vt, "spread", "0");
        dsp_set_param(vt, "tone", k ? "0" : "1");
        unsigned seed = 5;
        static float blk[2 * 128], mono[128 * 200];
        for (int b = 0; b < 200; b++) {
            for (int i = 0; i < 128; i++) blk[2 * i] = blk[2 * i + 1] = 0.3f * lcg_noise(&seed);
            dsp_process_block_f(vt, blk, 128);
            for (int i = 0; i < 128; i++) mono[b * 128 + i] = blk[2 * i];
        }
        hf[k] = band_energy(mono + 128 * 100, 128 * 100, 6000, 9000);
        dsp_cleanup(vt);
    }
    if (!(hf[1] < 0.01 * hf[0])) printf("  tone: HF open %.2e closed %.2e\n", hf[0], hf[1]);
    CHECK(hf[1] < 0.01 * hf[0]);

    for (int k = 0; k < 2; k++) {                                  /* Spread 0 = identical channels */
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "mode", "Snap");
        dsp_set_param(vt, "beauty", "0");                          /* (Beauty adds its own stereo) */
        dsp_set_param(vt, "spread", k ? "1" : "0");
        unsigned seed = 9;
        for (int b = 0; b < 200; b++) {
            float *blk = out[k] + b * 256;
            for (int i = 0; i < 128; i++) blk[2 * i] = blk[2 * i + 1] = 0.3f * lcg_noise(&seed);
            dsp_process_block_f(vt, blk, 128);
        }
        dsp_cleanup(vt);
    }
    double sxy = 0, sxx = 0, syy = 0;
    int same = 1;
    for (int i = 128 * 100; i < 128 * 200; i++) {
        if (out[0][2 * i] != out[0][2 * i + 1]) same = 0;
        double x = out[1][2 * i], y = out[1][2 * i + 1];
        sxy += x * y; sxx += x * x; syy += y * y;
    }
    CHECK(same);
    double corr = sxy / sqrt(sxx * syy);
    if (!(corr < 0.95)) printf("  spread 1: L/R correlation %.3f\n", corr);
    CHECK(corr < 0.95);                                            /* spread 1: genuinely stereo */
}

static void test_daft_auto_off_through_engine(void)
{
    /* Auto Off with no pad held: the drone still carries the voice. (It used
     * to go silent here, which on the device read as "Robot is too quiet".)
     * A held pad takes over from it, Auto or not, and Auto On is no louder. */
    double lv[2], padded;
    for (int a = 0; a < 2; a++) {
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "mode", "Robot");
        dsp_set_param(vt, "auto", a ? "On" : "Off");
        long pos = 0;
        static float cap[4096];
        run_sine(vt, 1000.0, 0.3f, 200, &pos, NULL, 0);
        run_sine(vt, 1000.0, 0.3f, 32, &pos, cap, 4096);
        lv[a] = rms_of(cap, 4096);
        if (a == 0) {
            note_on(vt, 64);
            run_sine(vt, 1000.0, 0.3f, 100, &pos, NULL, 0);
            run_sine(vt, 1000.0, 0.3f, 32, &pos, cap, 4096);
            padded = rms_of(cap, 4096);
        }
        dsp_cleanup(vt);
    }
    if (!(lv[0] > 0.02 && padded > 0.02)) printf("  robot auto off: drone %.4f, pad %.4f, auto on %.4f\n", lv[0], padded, lv[1]);
    CHECK(lv[0] > 0.02);                                           /* drone, Auto Off */
    CHECK(padded > 0.02);                                          /* pad, Auto Off */
    CHECK(lv[1] > 0.02);                                           /* drone, Auto On */
}


/* ------------------------------------------------- PSOLA and the new modes */

/* Vowel-like test voice: f0 harmonics shaped by one formant at F Hz. */
static float vowel(double f0, double F, int i)
{
    double s = 0;
    for (int h = 1; h <= 30; h++) {
        double fh = f0 * h;
        if (fh > 8000) break;
        s += sin(2.0 * M_PI * fh * i / VT_SAMPLE_RATE) / (1.0 + pow((fh - F) / 150.0, 2));
    }
    return (float)(0.3 * s);
}

/* Normalised autocorrelation at a fractional lag: ~1 when x repeats with that period. */
static double nac(const float *x, int n, double lag)
{
    double sxy = 0, sxx = 0, syy = 0;
    int L = (int)lag;
    double fr = lag - L;
    for (int i = 0; i < n - L - 1; i++) {
        double y = x[i + L] * (1 - fr) + x[i + L + 1] * fr;
        sxy += x[i] * y; sxx += (double)x[i] * x[i]; syy += y * y;
    }
    return sxy / sqrt(sxx * syy);
}

/* Spectral centroid over the first 30 harmonics of f0. */
static double harmonic_centroid(const float *x, int n, double f0)
{
    double num = 0, den = 0;
    for (int h = 1; h <= 30; h++) { double e = goertzel(x, n, f0 * h); num += e * f0 * h; den += e; }
    return num / den;
}

static void test_psola_shifter(void)
{
    static cher_t c;
    static psola_voice v;
    enum { N = 3 * VT_SAMPLE_RATE / 2 };
    static float in[N], out[N];
    const double ratios[] = { 0.5, 0.75, 1.26, 2.0 }, formants[] = { 700.0, 250.0 };

    /* Pitch: the output repeats at the target period, for weak- and
     * strong-fundamental vowels alike. */
    for (int fi = 0; fi < 2; fi++)
        for (int k = 0; k < 4; k++) {
            cher_init(&c, VT_SAMPLE_RATE);
            psola_init(&v);
            for (int i = 0; i < N; i++) {
                cher_push(&c, vowel(220.0, formants[fi], i));
                out[i] = psola_process(&v, &c, c.voiced ? VT_SAMPLE_RATE / c.f0 : 0.0f, (float)ratios[k], 1.0f, 0.0f);
            }
            double a = nac(out + N - 8192, 8192, VT_SAMPLE_RATE / (220.0 * ratios[k]));
            if (a < 0.95) printf("  psola x%.2f (formant %.0f): NAC %.3f\n", ratios[k], formants[fi], a);
            CHECK(a > 0.95);
            for (int i = 0; i < N; i++) CHECK(isfinite(out[i]));
        }

    /* Formant: x1.41 moves the spectral envelope up, pitch stays put. */
    for (int k = 0; k < 2; k++) {
        cher_init(&c, VT_SAMPLE_RATE);
        psola_init(&v);
        for (int i = 0; i < N; i++) {
            in[i] = vowel(220.0, 700.0, i);
            cher_push(&c, in[i]);
            out[i] = psola_process(&v, &c, c.voiced ? VT_SAMPLE_RATE / c.f0 : 0.0f, 1.0f, k ? 1.41f : 0.71f, 0.0f);
        }
        double ratio = harmonic_centroid(out + N - 8192, 8192, 220.0) / harmonic_centroid(in + N - 8192, 8192, 220.0);
        CHECK(nac(out + N - 8192, 8192, VT_SAMPLE_RATE / 220.0) > 0.95);
        if (k) CHECK(ratio > 1.2); else CHECK(ratio < 0.85);
    }

    /* Unity on noise is an exact delayed copy (delay = lat - 1, the latency
     * the dry path is lined up with). */
    cher_init(&c, VT_SAMPLE_RATE);
    psola_init(&v);
    unsigned seed = 41;
    for (int i = 0; i < N; i++) {
        in[i] = 0.3f * lcg_noise(&seed);
        cher_push(&c, in[i]);
        out[i] = psola_process(&v, &c, 0.0f, 1.0f, 1.0f, 0.0f);
    }
    int lag = (int)lroundf(v.lat - 1.0f);
    double sxy = 0, sxx = 0, syy = 0;
    for (int i = N / 2; i < N; i++) { sxy += (double)in[i - lag] * out[i]; sxx += (double)in[i - lag] * in[i - lag]; syy += (double)out[i] * out[i]; }
    double corr = sxy / sqrt(sxx * syy);
    if (corr < 0.999) printf("  psola identity: corr %.4f at lag %d\n", corr, lag);
    CHECK(corr > 0.999);
}

/* Run one of the fx modes directly on a source, mono or stereo out. */
typedef float (*src_fn)(int i);
static float src_vowel_220(int i)  { return vowel(220.0, 700.0, i); }
static float src_tone_220(int i)   { return 0.5f * tone(220.0, i, 6, VT_SAMPLE_RATE); }

static void fx_run(fx_t *fx, cher_t *c, int mode, float c1, float c2, float md, int auto_on,
                   int root, int scale, src_fn src, int n, float *l, float *r)
{
    const int none[FX_VOICES] = { -1, -1, -1, -1 };
    cher_init(c, VT_SAMPLE_RATE);
    fx_init(fx, VT_SAMPLE_RATE);
    cher_set_params(c, root, scale, 0.0f, -60.0f, -1, 0.0f);
    for (int i = 0; i < n; i++) {
        if (i % 128 == 0) fx_set(fx, c, mode, c1, c2, md, 0.0f, 0.0f, auto_on, root, scale, none);
        fx_process(fx, c, src(i), &l[i], &r[i]);
    }
}

static void test_mode_snap_and_morph(void)
{
    static cher_t c;
    static fx_t fx;
    enum { N = 3 * VT_SAMPLE_RATE / 2 };
    static float l[N], r[N], base[N];

    /* Snap char1 = formant: neutral at 0.5, up at 1.0, with the pitch unchanged. */
    fx_run(&fx, &c, VT_MODE_SNAP, 0.5f, 0.5f, 0.0f, 0, 0, VT_SCALE_CHROMATIC, src_vowel_220, N, base, r);
    fx_run(&fx, &c, VT_MODE_SNAP, 1.0f, 0.5f, 0.0f, 0, 0, VT_SCALE_CHROMATIC, src_vowel_220, N, l, r);
    double up = harmonic_centroid(l + N - 8192, 8192, 220.0) / harmonic_centroid(base + N - 8192, 8192, 220.0);
    if (up < 1.3) printf("  snap formant +12: centroid x%.2f\n", up);
    CHECK(up > 1.3);
    CHECK(nac(l + N - 8192, 8192, VT_SAMPLE_RATE / 220.0) > 0.95);   /* 220 Hz is in tune: no shift */
    CHECK(fx_latency(&fx, &c) > 0.0f && fx_latency(&fx, &c) < 0.02f * VT_SAMPLE_RATE);   /* under 20 ms */

    /* Morph char2 = drive: adds harmonics, but the level doesn't jump. */
    double h3[2], lvl[2];
    for (int k = 0; k < 2; k++) {
        fx_run(&fx, &c, VT_MODE_MORPH, 0.5f, k ? 1.0f : 0.0f, 0.0f, 0, 0, VT_SCALE_CHROMATIC,
               src_tone_220, N, l, r);
        h3[k] = band_at(l + N - 8192, 8192, 1540.0) / band_at(l + N - 8192, 8192, 220.0);   /* 7th harmonic: not in the input */
        lvl[k] = rms_of(l + N - 8192, 8192);
    }
    if (!(h3[1] > 10.0 * h3[0])) printf("  morph drive: 7th harmonic ratio %.2e -> %.2e\n", h3[0], h3[1]);
    CHECK(h3[1] > 10.0 * h3[0]);
    CHECK(fabs(20.0 * log10(lvl[1] / lvl[0])) < 6.0);
}

static void test_mode_prism(void)
{
    static cher_t c;
    static fx_t fx;
    enum { N = 3 * VT_SAMPLE_RATE / 2 };
    static float l[N], r[N];
    const int tail = VT_SAMPLE_RATE / 2;
    /* Auto, C major, sing A3, standard spread: A2 A3 C4 E4 - the in-key chord. */
    fx_run(&fx, &c, VT_MODE_PRISM, 0.5f, 0.3f, 0.0f, 1, 0, VT_SCALE_MAJOR, src_tone_220, N, l, r);
    static float mid[N];                                           /* voices are panned: measure L+R */
    for (int i = 0; i < N; i++) mid[i] = 0.5f * (l[i] + r[i]);
    const float *t = mid + N - tail;
    double a2 = band_at(t, tail, 110.0), a3 = band_at(t, tail, 220.0), c4 = band_at(t, tail, 261.63);
    double e4 = band_at(t, tail, 329.63), cs4 = band_at(t, tail, 277.18);
    double weakest = fmin(fmin(a2, a3), fmin(c4, e4));
    if (!(weakest > 0.02 * fmax(fmax(a2, a3), fmax(c4, e4)) && c4 > 10.0 * cs4))
        printf("  prism: A2 %.2e A3 %.2e C4 %.2e E4 %.2e C#4 %.2e\n", a2, a3, c4, e4, cs4);
    CHECK(weakest > 0.02 * fmax(fmax(a2, a3), fmax(c4, e4)));
    CHECK(c4 > 10.0 * cs4);
    CHECK(fx.active == 4);
    for (int i = 0; i < N; i++) CHECK(isfinite(l[i]) && isfinite(r[i]) && fabsf(l[i]) < 2.0f);
}

static void test_mode_double(void)
{
    static cher_t c;
    static fx_t fx;
    enum { N = VT_SAMPLE_RATE };
    static float l[N], r[N];
    fx_run(&fx, &c, VT_MODE_DOUBLE, 0.5f, 0.0f, 0.5f, 0, 0, VT_SCALE_CHROMATIC, src_vowel_220, N, l, r);
    int same = 1;
    for (int i = 0; i < N; i++) if (l[i] != r[i]) same = 0;
    CHECK(same);                                                   /* width 0: just the lead, mono */

    fx_run(&fx, &c, VT_MODE_DOUBLE, 0.5f, 1.0f, 0.5f, 0, 0, VT_SCALE_CHROMATIC, src_vowel_220, N, l, r);
    double sxy = 0, sxx = 0, syy = 0;
    for (int i = N / 2; i < N; i++) { sxy += (double)l[i] * r[i]; sxx += (double)l[i] * l[i]; syy += (double)r[i] * r[i]; }
    double corr = sxy / sqrt(sxx * syy);
    if (!(corr < 0.98)) printf("  double: L/R corr %.3f\n", corr);
    CHECK(corr < 0.98);                                            /* width 1: two different doubles */
    CHECK(nac(l + N / 2, N / 2, VT_SAMPLE_RATE / 220.0) > 0.8);    /* still clearly at pitch */
}

static void test_mode_tape(void)
{
    static cher_t c;
    static fx_t fx;
    enum { N = 2 * VT_SAMPLE_RATE };
    static float l[N], r[N];
    static float nsdf[CHER_MAX_TAU + 2];

    /* char1 = 0: down an octave (the 220 Hz voice repeats at 110 Hz now). */
    fx_run(&fx, &c, VT_MODE_TAPE, 0.0f, 0.0f, 0.0f, 0, 0, VT_SCALE_CHROMATIC, src_tone_220, N, l, r);
    double at110 = nac(l + N - 8192, 8192, VT_SAMPLE_RATE / 110.0), at220 = nac(l + N - 8192, 8192, VT_SAMPLE_RATE / 220.0);
    if (!(at110 > 0.9 && at220 < 0.5)) printf("  tape -12: NAC@110 %.3f NAC@220 %.3f\n", at110, at220);
    CHECK(at110 > 0.9 && at220 < 0.5);

    /* char1 = 1: no drop. */
    fx_run(&fx, &c, VT_MODE_TAPE, 1.0f, 0.0f, 0.0f, 0, 0, VT_SCALE_CHROMATIC, src_tone_220, N, l, r);
    CHECK(nac(l + N - 8192, 8192, VT_SAMPLE_RATE / 220.0) > 0.9);

    /* char2 = wow & flutter: the pitch wanders; at 0 it doesn't. */
    double spread_c[2];
    for (int k = 0; k < 2; k++) {
        fx_run(&fx, &c, VT_MODE_TAPE, 1.0f, k ? 1.0f : 0.0f, 0.0f, 0, 0, VT_SCALE_CHROMATIC, src_tone_220, N, l, r);
        double lo = 1e9, hi = -1e9;
        for (int w = 0; w < 16; w++) {
            float f0 = 0, clar = 0;
            if (cher_mpm(l + N / 2 + w * 2048, 2048, VT_SAMPLE_RATE, 120.0f, 1000.0f, nsdf, &f0, &clar)) {
                double ct = cents(f0, 220.0);
                if (ct < lo) lo = ct;
                if (ct > hi) hi = ct;
            }
        }
        spread_c[k] = hi - lo;
    }
    if (!(spread_c[1] > 8.0 && spread_c[0] < 3.0)) printf("  tape wow: pitch spread %.1f vs %.1f cents\n", spread_c[1], spread_c[0]);
    CHECK(spread_c[1] > 8.0);
    CHECK(spread_c[0] < 3.0);
}


/* ------------------------------------------------ Freeze, Demo, Robot polish */

/* Feed `seconds` of a sine through the engine, capturing the left output. */
static void run_sine_cap(vt_t *vt, double f, float amp, double seconds, long *pos, float *cap, int *ncap)
{
    int blocks = (int)(seconds * VT_SAMPLE_RATE / 128.0);
    static float blk[2 * 128];
    *ncap = 0;
    for (int b = 0; b < blocks; b++) {
        for (int i = 0; i < 128; i++, (*pos)++)
            blk[2 * i] = blk[2 * i + 1] = amp * (float)sin(2.0 * M_PI * f * (double)(*pos) / VT_SAMPLE_RATE);
        dsp_process_block_f(vt, blk, 128);
        if (cap) for (int i = 0; i < 128; i++) cap[(*ncap)++] = blk[2 * i];
    }
}

/* The scope feed, parsed (read_scope is below, with the scope tests). */
typedef struct { int mode, frozen; float phase, loop_s, f0, target, cents; int bands[16]; int fields; } scope_t;
static scope_t read_scope(vt_t *vt);

static void test_freeze_looper(void)
{
    /* Robot with Mix 0 is a clean window onto the (possibly frozen) input. */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Robot");
    dsp_set_param(vt, "mix", "0");
    dsp_set_param(vt, "gain", "0");
    dsp_set_param(vt, "freeze_len", "Free");                       /* manual punch-out (bars: below) */
    long pos = 0;
    int n = 0;
    static float cap[17 * VT_SAMPLE_RATE];
    char v[16];
    const int tail = VT_SAMPLE_RATE;

    /* Punch in: only what is sung between Rec and Loop is the take. */
    run_sine_cap(vt, 330.0, 0.5f, 0.5, &pos, NULL, &n);            /* before Rec: not recorded */
    CHECK(dsp_set_param(vt, "freeze", "Rec") == 0);
    run_sine_cap(vt, 440.0, 0.5f, 2.0, &pos, cap, &n);             /* the take: 2 s of A4, heard live */
    CHECK(band_at(cap + n - tail, tail, 440.0) > 0.01);
    CHECK(dsp_set_param(vt, "freeze", "Loop") == 0);
    run_sine_cap(vt, 880.0, 0.5f, 5.0, &pos, cap, &n);             /* sing A5 at it: ignored */
    double a4 = band_at(cap + n - tail, tail, 440.0), a5 = band_at(cap + n - tail, tail, 880.0);
    double e3 = band_at(cap + n - tail, tail, 330.0);
    if (!(a4 > 100.0 * a5 && a4 > 100.0 * e3)) printf("  loop: 440 %.2e vs live 880 %.2e, pre-roll 330 %.2e\n", a4, a5, e3);
    CHECK(a4 > 100.0 * a5);                                        /* the loop plays, the live input doesn't */
    CHECK(a4 > 100.0 * e3);                                        /* and nothing from before Rec */
    scope_t sc = read_scope(vt);
    CHECK(sc.frozen == 1);
    if (!(fabs(sc.loop_s - 1.995) < 0.01)) printf("  loop length %.3f s (want 2.000 - 5 ms)\n", sc.loop_s);
    CHECK(fabs(sc.loop_s - 1.995) < 0.01);                         /* exactly the take, less the wrap fade */

    /* Seamless across the wrap (every ~2 s here): no sample-to-sample jump
     * bigger than a 0.5 sine's own slope allows (~0.03). */
    float worst = 0.0f;
    for (int i = (int)(0.05 * VT_SAMPLE_RATE); i < n; i++) {
        float d = fabsf(cap[i] - cap[i - 1]);
        if (d > worst) worst = d;
    }
    if (worst > 0.06f) printf("  loop worst step %.3f\n", worst);
    CHECK(worst < 0.06f);
    for (int i = 0; i < n; i++) CHECK(fabsf(cap[i]) < 0.75f);

    /* Off: live again, take kept. Loop again: the kept take, from its start. */
    CHECK(dsp_set_param(vt, "freeze", "Off") == 0);
    run_sine_cap(vt, 880.0, 0.5f, 0.5, &pos, cap, &n);
    CHECK(band_at(cap + n - 8192, 8192, 880.0) > 100.0 * band_at(cap + n - 8192, 8192, 440.0));
    CHECK(dsp_set_param(vt, "freeze", "Loop") == 0);
    run_sine_cap(vt, 880.0, 0.5f, 0.5, &pos, cap, &n);
    CHECK(band_at(cap + n - 8192, 8192, 440.0) > 100.0 * band_at(cap + n - 8192, 8192, 880.0));

    /* Loop -> Rec: a new take replaces it; Rec -> Off keeps it for later. */
    CHECK(dsp_set_param(vt, "freeze", "Rec") == 0);
    run_sine_cap(vt, 660.0, 0.5f, 1.0, &pos, NULL, &n);
    CHECK(dsp_set_param(vt, "freeze", "Off") == 0);
    run_sine_cap(vt, 880.0, 0.5f, 0.3, &pos, NULL, &n);
    CHECK(dsp_set_param(vt, "freeze", "Loop") == 0);
    run_sine_cap(vt, 880.0, 0.5f, 0.5, &pos, cap, &n);
    CHECK(band_at(cap + n - 8192, 8192, 660.0) > 100.0 * band_at(cap + n - 8192, 8192, 440.0));
    sc = read_scope(vt);
    CHECK(fabs(sc.loop_s - 0.995) < 0.01);

    /* Too short to loop (under 20 ms): stays live. */
    CHECK(dsp_set_param(vt, "freeze", "Rec") == 0);
    run_sine_cap(vt, 660.0, 0.5f, 0.005, &pos, NULL, &n);
    CHECK(dsp_set_param(vt, "freeze", "Loop") == 0);
    run_sine_cap(vt, 880.0, 0.5f, 0.3, &pos, cap, &n);
    CHECK(band_at(cap + n - 8192, 8192, 880.0) > 100.0 * band_at(cap + n - 8192, 8192, 660.0));

    /* A full buffer locks itself and starts looping; the knob follows. */
    CHECK(dsp_set_param(vt, "freeze", "Rec") == 0);
    run_sine_cap(vt, 550.0, 0.5f, 15.9, &pos, NULL, &n);
    sc = read_scope(vt);
    CHECK(sc.frozen == 2 && sc.phase > 0.98f && fabs(sc.loop_s - 15.9) < 0.01);   /* recording, nearly full */
    dsp_get_param(vt, "freeze", v, sizeof v); CHECK(strcmp(v, "Rec") == 0);
    run_sine_cap(vt, 880.0, 0.5f, 0.5, &pos, cap, &n);
    dsp_get_param(vt, "freeze", v, sizeof v); CHECK(strcmp(v, "Loop") == 0);
    sc = read_scope(vt);
    CHECK(sc.frozen == 1 && fabs(sc.loop_s - 15.995) < 0.01);      /* 16 s */
    CHECK(band_at(cap + n - 8192, 8192, 550.0) > 100.0 * band_at(cap + n - 8192, 8192, 880.0));

    /* Not part of the saved state, and a state can't switch it on. */
    char st[1024];
    CHECK(dsp_get_param(vt, "state", st, sizeof st) > 0 && strstr(st, "\"freeze\"") == NULL && strstr(st, "\"demo\"") == NULL);
    CHECK(strstr(st, "\"freeze_len\"") != NULL);                   /* the length setting is saved */
    CHECK(dsp_set_param(vt, "freeze", "On") == -1);                /* the old two-way option is gone */

    vt_t *b = dsp_init(VT_SAMPLE_RATE);
    CHECK(dsp_set_param(b, "state", "{\"mode\":\"Tape\",\"freeze\":\"Loop\",\"demo\":\"On\"}") == 0);
    dsp_get_param(b, "mode", v, sizeof v);   CHECK(strcmp(v, "Tape") == 0);
    dsp_get_param(b, "freeze", v, sizeof v); CHECK(strcmp(v, "Off") == 0);
    dsp_get_param(b, "demo", v, sizeof v);   CHECK(strcmp(v, "Off") == 0);
    dsp_cleanup(b);
    dsp_cleanup(vt);

    /* The loop feeds the engines like a live voice: pads repitch it in Snap
     * and play the vocoder in Robot, with no gap when they change. */
    const char *modes[2] = { "Snap", "Robot" };
    for (int m = 0; m < 2; m++) {
        vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "mode", modes[m]);
        dsp_set_param(vt, "freeze", "Rec");
        pos = 0;
        static float blk[2 * 128];
        for (int b2 = 0; b2 < 2 * VT_SAMPLE_RATE / 128; b2++) {      /* a 2 s harmonic take at A3 */
            for (int i = 0; i < 128; i++, pos++) blk[2 * i] = blk[2 * i + 1] = 0.5f * tone(220.0, (int)pos, 6, VT_SAMPLE_RATE);
            dsp_process_block_f(vt, blk, 128);
        }
        dsp_set_param(vt, "freeze", "Loop");
        const int pads[4] = { 60, 64, 67, 62 };
        double lo = 1e9, sum = 0;
        int nw = 0, follows = 1;
        for (int k = 0; k < 4; k++) {
            note_on(vt, pads[k]);
            for (int b2 = 0; b2 < (int)(0.5 * VT_SAMPLE_RATE / 128); b2++) {
                memset(blk, 0, sizeof blk);                          /* the mic is silent */
                dsp_process_block_f(vt, blk, 128);
                if (k == 0 && b2 < 20) continue;                     /* the live -> loop fade */
                double e = 0;
                for (int i = 0; i < 128; i++) e += blk[2 * i] * blk[2 * i];
                e = 10.0 * log10(e / 128 + 1e-20);
                if (e < lo) lo = e;
                sum += e; nw++;
            }
            if (m == 0) {
                double want = 440.0 * pow(2.0, (pads[k] - 69) / 12.0);
                if (fabs(viz_target(vt) - want) > 0.5) follows = 0;
            }
            note_off(vt, pads[k]);
        }
        double mean = sum / nw;
        if (!(lo > mean - 20.0 && follows)) printf("  %s on the loop: mean %.1f dB, quietest block %.1f dB, follows %d\n", modes[m], mean, lo, follows);
        CHECK(lo > mean - 20.0);                                   /* never drops out */
        CHECK(follows);
        CHECK(mean > -40.0);
        dsp_cleanup(vt);
    }
}

static void test_demo_mode(void)
{
    /* The demo voice itself: off-pitch on purpose, a 2 s loop, quiet enough. */
    CHECK(fabs(demo_pitch_at(0.35f) - 57.3f) < 0.25f);            /* A3 +30 ct (+-20 ct vibrato) */
    CHECK(fabs(demo_pitch_at(0.10f) - 57.3f) < 0.05f);            /* before the vibrato */
    static demo_t dm;
    demo_init(&dm, VT_SAMPLE_RATE);
    float peak = 0.0f, first[64];
    for (int i = 0; i < 2 * VT_SAMPLE_RATE; i++) { float y = demo_process(&dm); if (i < 64) first[i] = y; if (fabsf(y) > peak) peak = fabsf(y); }
    CHECK(peak > 0.1f && peak < 0.8f);
    int same = 1;                                                  /* exactly periodic: 2 s later it repeats */
    for (int i = 0; i < 64; i++) { float y = demo_process(&dm); if (fabsf(y - first[i]) > 0.02f) same = 0; }
    CHECK(same);
    int ch[4];
    demo_chord(0, ch); CHECK(ch[0] == 57 && ch[1] == 60 && ch[2] == 64);   /* Am */
    demo_chord(5, ch); CHECK(ch[0] == 53);                                 /* F (wraps every 4 bars) */

    /* Through the engine, with the mic silent. */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Tape");                             /* the user's mode */
    dsp_set_bpm(vt, 120.0f);                                       /* 4 bars = 8 s */
    static float blk[2 * 128];
    char m[16], buf[96];
    double rms_demo = 0.0;
    int nrms = 0;
    CHECK(dsp_set_param(vt, "demo", "On") == 0);
    for (int b = 0; b < (int)(17.0 * VT_SAMPLE_RATE / 128.0); b++) {
        memset(blk, 0, sizeof blk);
        dsp_process_block_f(vt, blk, 128);
        double t = (b + 1) * 128.0 / VT_SAMPLE_RATE;
        for (int i = 0; i < 128; i++) { CHECK(isfinite(blk[2 * i])); rms_demo += blk[2 * i] * blk[2 * i]; nrms++; }
        if (b == (int)(0.4 * VT_SAMPLE_RATE / 128.0)) {            /* the tour starts on Robot */
            dsp_get_param(vt, "mode", m, sizeof m); CHECK(strcmp(m, "Robot") == 0);
        }
        if (b == (int)(8.4 * VT_SAMPLE_RATE / 128.0)) {            /* Snap, second slot, mid first syllable */
            dsp_get_param(vt, "mode", m, sizeof m); CHECK(strcmp(m, "Snap") == 0);
            CHECK(dsp_get_param(vt, "viz", buf, sizeof buf) > 0);
            float f0 = 0, tgt = 0, ce = 0;
            sscanf(buf, "c,%f,%f,%f", &f0, &tgt, &ce);
            if (fabs(tgt - 220.0) > 0.5) printf("  demo snap: f0 %.2f target %.2f\n", f0, tgt);
            CHECK(fabs(tgt - 220.0) < 0.5);                        /* the sharp A3 gets pulled to A3 */
        }
        if (fabs(t - 8.5) < 0.002) { dsp_get_param(vt, "mode", m, sizeof m); CHECK(strcmp(m, "Snap") == 0); }
        if (fabs(t - 16.5) < 0.002) { dsp_get_param(vt, "mode", m, sizeof m); CHECK(strcmp(m, "Chords") == 0); }
    }
    CHECK(sqrt(rms_demo / nrms) > 0.02);                           /* audible with no mic at all */

    CHECK(dsp_set_param(vt, "demo", "Off") == 0);
    memset(blk, 0, sizeof blk);
    dsp_process_block_f(vt, blk, 128);
    dsp_get_param(vt, "mode", m, sizeof m);
    CHECK(strcmp(m, "Tape") == 0);                                 /* the user's mode comes back */
    float tail_peak = 0.0f;
    for (int b = 0; b < (int)(2.0 * VT_SAMPLE_RATE / 128.0); b++) {
        memset(blk, 0, sizeof blk);
        dsp_process_block_f(vt, blk, 128);
        if (b > 300) for (int i = 0; i < 256; i++) if (fabsf(blk[i]) > tail_peak) tail_peak = fabsf(blk[i]);
    }
    CHECK(tail_peak < 1e-3f);                                      /* and the mic (silent) is back */

    dsp_set_bpm(vt, 60.0f);                                        /* 4 bars = 16 s now */
    CHECK(dsp_set_param(vt, "demo", "On") == 0);
    for (int b = 0; b < (int)(10.0 * VT_SAMPLE_RATE / 128.0); b++) { memset(blk, 0, sizeof blk); dsp_process_block_f(vt, blk, 128); }
    dsp_get_param(vt, "mode", m, sizeof m);
    CHECK(strcmp(m, "Robot") == 0);
    dsp_cleanup(vt);
}

static void test_robot_formant_pan_ensemble(void)
{
    static daft_t d;
    static float l[VT_SAMPLE_RATE], r[VT_SAMPLE_RATE];
    const int half = VT_SAMPLE_RATE / 2;

    /* Formant +12 st: the 1 kHz modulator's energy comes out an octave up. */
    daft_prime(&d, 0, 0.0f, 12.0f, 0.5f, 0.0f);
    for (int i = 0; i < VT_SAMPLE_RATE; i++)
        daft_process_lr(&d, 0.3f * (float)sin(2.0 * M_PI * 1000.0 * i / VT_SAMPLE_RATE), &l[i], &r[i]);
    double up = band_energy(l + half, half, 1700, 2400), base = band_energy(l + half, half, 800, 1250);
    if (!(up > 5.0 * base)) printf("  robot formant +12: 1.7-2.4k %.2e vs 0.8-1.25k %.2e\n", up, base);
    CHECK(up > 5.0 * base);

    /* Odd/even panning: a tone on band 6 (even) goes right, band 7 (odd) left. */
    for (int k = 0; k < 2; k++) {
        daft_prime(&d, 0, 0.0f, 0.0f, 0.5f, 0.0f);
        daft_set_stereo(&d, 1.0f, 0.0f);
        double f = d.centre[6 + k];
        for (int i = 0; i < VT_SAMPLE_RATE; i++)
            daft_process_lr(&d, 0.3f * (float)sin(2.0 * M_PI * f * i / VT_SAMPLE_RATE), &l[i], &r[i]);
        double el = rms_of(l + half, half), er = rms_of(r + half, half);
        if (k == 0 && !(er > 1.5 * el)) printf("  band 6 (even): L %.3f R %.3f\n", el, er);
        if (k == 1 && !(el > 1.5 * er)) printf("  band 7 (odd): L %.3f R %.3f\n", el, er);
        CHECK(k == 0 ? er > 1.5 * el : el > 1.5 * er);   /* ~5 dB: neighbouring (other-parity) bands overlap a lone tone */
    }

    /* Spread 0 and no ensemble: mono. Ensemble on: stereo, same ballpark level. */
    double lvl[2];
    for (int k = 0; k < 2; k++) {
        daft_prime(&d, 0, 0.0f, 0.0f, 0.5f, 0.0f);
        daft_set_stereo(&d, 0.0f, k ? 1.0f : 0.0f);
        for (int i = 0; i < VT_SAMPLE_RATE; i++)
            daft_process_lr(&d, 0.3f * (float)sin(2.0 * M_PI * 1000.0 * i / VT_SAMPLE_RATE), &l[i], &r[i]);
        double sxy = 0, sxx = 0, syy = 0;
        int same = 1;
        for (int i = half; i < VT_SAMPLE_RATE; i++) {
            if (l[i] != r[i]) same = 0;
            sxy += (double)l[i] * r[i]; sxx += (double)l[i] * l[i]; syy += (double)r[i] * r[i];
        }
        if (k == 0) CHECK(same);
        else {
            double corr = sxy / sqrt(sxx * syy);
            if (!(corr < 0.98)) printf("  ensemble L/R corr %.3f\n", corr);
            CHECK(corr < 0.98);
        }
        lvl[k] = rms_of(l + half, half);
        for (int i = 0; i < VT_SAMPLE_RATE; i++) CHECK(isfinite(l[i]) && isfinite(r[i]));
    }
    CHECK(fabs(20.0 * log10(lvl[1] / lvl[0])) < 6.0);

    /* Through the engine: in Robot, Spread is band panning (not the ADT chorus). */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Robot");
    dsp_set_param(vt, "spread", "0");
    dsp_set_param(vt, "beauty", "0");                              /* Beauty adds band spread of its own */
    static float blk[2 * 128];
    int same = 1;
    for (int b = 0; b < 200; b++) {
        for (int i = 0; i < 128; i++) blk[2 * i] = blk[2 * i + 1] = 0.3f * (float)sin(2.0 * M_PI * 700.0 * (b * 128 + i) / VT_SAMPLE_RATE);
        dsp_process_block_f(vt, blk, 128);
        if (b > 100) for (int i = 0; i < 128; i++) if (blk[2 * i] != blk[2 * i + 1]) same = 0;
    }
    CHECK(same);
    dsp_cleanup(vt);
}


/* --------------------------------------------------------- OLED scope feed */


static scope_t read_scope(vt_t *vt)
{
    scope_t s;
    memset(&s, 0, sizeof s);
    char buf[256];
    if (dsp_get_param(vt, "scope", buf, sizeof buf) <= 0 || buf[0] != 'S') return s;
    char *p = buf + 1;
    float v[23];
    while (*p == ',' && s.fields < 23) { v[s.fields++] = strtof(p + 1, &p); }
    if (s.fields < 23) return s;
    s.mode = (int)v[0]; s.frozen = (int)v[1]; s.phase = v[2]; s.loop_s = v[3];
    s.f0 = v[4]; s.target = v[5]; s.cents = v[6];
    for (int i = 0; i < 16; i++) s.bands[i] = (int)v[7 + i];
    return s;
}

static void test_scope_feed(void)
{
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    long pos = 0;

    /* Snap: detected pitch, the snapped target, and the error in cents. */
    dsp_set_param(vt, "mode", "Snap");
    run_sine(vt, 445.0, 0.4f, 200, &pos, NULL, 0);
    scope_t s = read_scope(vt);
    CHECK(s.fields == 23);
    CHECK(s.mode == VT_MODE_SNAP && !s.frozen);
    CHECK(fabs(cents(s.f0, 445.0)) < 5.0 && fabs(s.target - 440.0) < 0.5 && s.cents > 15.0 && s.cents < 25.0);
    for (int i = 0; i < 16; i++) CHECK(s.bands[i] == 0);

    /* Tape: the target is the dropped output pitch. */
    dsp_set_param(vt, "mode", "Tape");
    dsp_set_param(vt, "char1", "0");                               /* -12 st */
    run_sine(vt, 440.0, 0.4f, 200, &pos, NULL, 0);
    s = read_scope(vt);
    CHECK(fabs(s.target - 220.0) < 3.0 && fabs(s.cents + 1200.0) < 1.0);
    dsp_set_param(vt, "char1", "0.5");

    /* Robot: the analysis bands, peaking where the voice is (1 kHz: bands 7-8). */
    dsp_set_param(vt, "mode", "Robot");
    run_sine(vt, 1000.0, 0.3f, 200, &pos, NULL, 0);
    s = read_scope(vt);
    int peak = 0;
    for (int i = 1; i < 16; i++) if (s.bands[i] > s.bands[peak]) peak = i;
    CHECK(s.mode == VT_MODE_ROBOT && (peak == 7 || peak == 8) && s.bands[peak] > 128);
    CHECK(s.f0 == 0.0f);

    /* Freeze: recording shows 2 with the seconds so far; looping shows 1 with
     * the loop length and a playhead that moves. */
    dsp_cleanup(vt);
    vt = dsp_init(VT_SAMPLE_RATE);
    pos = 0;
    dsp_set_param(vt, "mode", "Snap");
    run_sine(vt, 330.0, 0.4f, 100, &pos, NULL, 0);
    s = read_scope(vt);
    CHECK(s.frozen == 0 && s.loop_s == 0.0f);
    dsp_set_param(vt, "freeze", "Rec");
    run_sine(vt, 330.0, 0.4f, (int)(1.5 * VT_SAMPLE_RATE / 128), &pos, NULL, 0);
    s = read_scope(vt);
    CHECK(s.frozen == 2 && fabs(s.loop_s - 1.5) < 0.01 && fabs(s.phase - 1.5 / 4.005) < 0.01);   /* 1.5 s into a 2-bar take at 120 */
    dsp_set_param(vt, "freeze", "Loop");
    run_sine(vt, 330.0, 0.4f, 10, &pos, NULL, 0);
    scope_t a = read_scope(vt);
    run_sine(vt, 330.0, 0.4f, 100, &pos, NULL, 0);                 /* ~0.29 s later */
    scope_t b = read_scope(vt);
    CHECK(a.frozen == 1 && b.frozen == 1);
    if (!(fabs(a.loop_s - 1.5) < 0.02)) printf("  scope loop %.2f s\n", a.loop_s);
    CHECK(fabs(a.loop_s - 1.5) < 0.02);                            /* exactly the take */
    float moved = b.phase - a.phase;
    if (moved < 0) moved += 1.0f;
    CHECK(fabs(moved * a.loop_s - 100 * 128.0 / VT_SAMPLE_RATE) < 0.02);
    dsp_set_param(vt, "freeze", "Off");

    /* Short buffer: refuses rather than truncating. */
    char tiny[40];
    CHECK(dsp_get_param(vt, "scope", tiny, sizeof tiny) == -1);
    dsp_cleanup(vt);
}


/* ---------------------------------------- served metadata, Auto, pads (iter 7) */

static void test_served_metadata(void)
{
    /* The DSP answers chain_params / ui_hierarchy itself - that is what gets
     * `viz` to the UI so canvas.js mounts - and says exactly what module.json says. */
    if (!load_module_json()) { CHECK(!"module.json not found"); return; }
    const char *caps = js_get(g_module_json, "capabilities");
    const char *file_cp = js_get(caps, "chain_params");
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    static char served[8192];
    int n = dsp_get_param(vt, "chain_params", served, sizeof served);
    CHECK(n > 0 && served[0] == '[');
    CHECK(js_arr_len(served) == js_arr_len(file_cp) && js_arr_len(served) == VT_P_COUNT + 3);
    CHECK(strchr(served, '@') == NULL);                              /* every name placeholder filled */
    char a[64], b[64];
    int viz = 0;
    for (int i = 0; i < js_arr_len(served); i++) {
        const char *so = js_elem(served, i), *fo = js_elem(file_cp, i);
        CHECK(js_str(js_get(so, "key"), a, sizeof a) && js_str(js_get(fo, "key"), b, sizeof b) && strcmp(a, b) == 0);
        const char *v = js_get(so, "viz");
        if (v && js_str(js_get(v, "kind"), a, sizeof a) && strcmp(a, "custom:tinyvoice_scope") == 0) viz++;
        CHECK((js_get(so, "visible_if") != NULL) == (js_get(fo, "visible_if") != NULL));
    }
    CHECK(viz == 3);                                               /* survives the trip, unlike the host fallback */

    static char hier[4096];
    CHECK(dsp_get_param(vt, "ui_hierarchy", hier, sizeof hier) > 0);
    const char *lv = js_get(hier, "levels");
    CHECK(lv && js_get(lv, "vocoder") && js_get(lv, "VOICE") && js_get(lv, "PITCH") && js_get(lv, "MORE") && js_get(lv, "voicefx"));
    CHECK(js_get(hier, "modes") != NULL && js_get(hier, "mode_param") != NULL);          /* the host's Mode page writes `engine` */

    char small[64];
    CHECK(dsp_get_param(vt, "chain_params", small, sizeof small) == -1);   /* too small: refuses */
    dsp_cleanup(vt);
}

static double out_rms(vt_t *vt, double f, float amp, double seconds)
{
    long pos = 0;
    int blocks = (int)(seconds * VT_SAMPLE_RATE / 128.0), n = 0;
    static float blk[2 * 128];
    double acc = 0.0;
    for (int b = 0; b < blocks; b++) {
        for (int i = 0; i < 128; i++, pos++)
            blk[2 * i] = blk[2 * i + 1] = amp * (f > 0 ? (float)sin(2.0 * M_PI * f * pos / VT_SAMPLE_RATE) : 1.0f);
        dsp_process_block_f(vt, blk, 128);
        if (b >= blocks / 2) for (int i = 0; i < 128; i++, n++) acc += (double)blk[2 * i] * blk[2 * i];
    }
    return sqrt(acc / n);
}

static void test_auto_agc_and_gate(void)
{
    /* AGC: a soft voice (-40 dBFS RMS, just over the -42 dBFS RMS voice gate)
     * comes out 12 dB louder with Auto on (the cap); a loud one (-9 dBFS RMS)
     * is not pushed up; a -73 dBFS room is never boosted: the gate expands it
     * down. */
    const float amps[3] = { 0.0141f, 0.5f, 0.0003f };
    double gain_db[3];
    for (int k = 0; k < 3; k++) {
        double lvl[2];
        for (int au = 0; au < 2; au++) {
            vt_t *vt = dsp_init(VT_SAMPLE_RATE);
            dsp_set_param(vt, "mode", "Snap");
            dsp_set_param(vt, "gate", "-60");                      /* the manual level Auto Off returns to */
            dsp_set_param(vt, "auto", au ? "On" : "Off");
            lvl[au] = out_rms(vt, 220.0, amps[k], 3.0);
            dsp_cleanup(vt);
        }
        gain_db[k] = 20.0 * log10(lvl[1] / lvl[0]);
    }
    if (!(gain_db[0] > 11.5 && gain_db[0] < 12.5)) printf("  agc: soft voice lifted %.1f dB\n", gain_db[0]);
    CHECK(gain_db[0] > 11.5 && gain_db[0] < 12.5);                 /* lifted, up to the +12 dB cap */
    CHECK(gain_db[1] < 1.0);
    if (!(gain_db[2] < -20.0)) printf("  agc: room noise changed %.1f dB\n", gain_db[2]);
    CHECK(gain_db[2] < -20.0);

    /* ...and the screen gets told how much. */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Snap");
    out_rms(vt, 220.0, 0.0141f, 3.0);
    char buf[256];
    CHECK(dsp_get_param(vt, "scope", buf, sizeof buf) > 0);
    int commas = 0;
    const char *tail = buf;
    for (const char *p = buf; *p; p++) if (*p == ',' && ++commas == 24) tail = p + 1;
    CHECK(commas == 27);                                           /* ..., note-ons, transport beat */
    double agc_db = atof(tail);
    if (!(agc_db > 11.5 && agc_db < 12.5)) printf("  scope agc field %.1f\n", agc_db);
    CHECK(agc_db > 11.5 && agc_db < 12.5);
    dsp_cleanup(vt);

    /* Gate: closed by hand (0 dB) stops tracking; at Auto (its lowest
     * setting) the voice is tracked. */
    for (int au = 0; au < 2; au++) {
        vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "mode", "Snap");
        dsp_set_param(vt, "gate", au ? "Auto" : "0 dB");
        out_rms(vt, 220.0, 0.3f, 1.0);
        float f0 = 0, tgt = 0, ce = 0;
        dsp_get_param(vt, "viz", buf, sizeof buf);
        sscanf(buf, "c,%f,%f,%f", &f0, &tgt, &ce);
        if (au) CHECK(fabs(cents(f0, 220.0)) < 5.0);
        else CHECK(f0 == 0.0f);
        dsp_cleanup(vt);
    }
}

static void test_snap_pads_any_channel(void)
{
    /* Omni (a slot receiving All, or no slot context): a held pad sets Snap's
     * target exactly, on any MIDI channel, as a 3-byte message or a 4-byte
     * USB-MIDI packet; releasing it goes back to Key/Scale. */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Snap");
    dsp_set_param(vt, "key", "C");
    dsp_set_param(vt, "scale", "Major");
    long pos = 0;
    run_sine(vt, 430.0, 0.4f, 150, &pos, NULL, 0);
    CHECK(fabs(viz_target(vt) - 440.0) < 0.5);                     /* A4, from the scale */

    const uint8_t on_ch10[3] = { 0x99, 64, 100 };                  /* E4 on channel 10 */
    dsp_handle_midi(vt, on_ch10, 3);
    run_sine(vt, 430.0, 0.4f, 150, &pos, NULL, 0);
    CHECK(fabs(viz_target(vt) - 329.628) < 0.5);
    const uint8_t off_ch10[3] = { 0x89, 64, 0 };
    dsp_handle_midi(vt, off_ch10, 3);

    const uint8_t usb_on[4] = { 0x09, 0x95, 67, 100 };             /* G4, channel 6, USB-MIDI packet */
    dsp_handle_midi(vt, usb_on, 4);
    run_sine(vt, 430.0, 0.4f, 150, &pos, NULL, 0);
    CHECK(fabs(viz_target(vt) - 391.995) < 0.5);
    char buf[256];
    dsp_get_param(vt, "scope", buf, sizeof buf);
    int commas = 0;
    const char *held = buf;
    const char *ons = buf;
    for (const char *p = buf; *p; p++) {
        if (*p != ',') continue;
        ++commas;
        if (commas == 25) held = p + 1;
        if (commas == 26) ons = p + 1;
    }
    CHECK(atoi(held) == 67);                                       /* the screen shows the pad */
    CHECK(atoi(ons) == 2);                                         /* two note-ons counted */

    const uint8_t usb_off[4] = { 0x08, 0x85, 67, 0 };
    dsp_handle_midi(vt, usb_off, 4);
    run_sine(vt, 430.0, 0.4f, 150, &pos, NULL, 0);
    CHECK(fabs(viz_target(vt) - 440.0) < 0.5);                     /* back to the scale */
    dsp_cleanup(vt);
}

/* Run 1 s of a sung A3 through `mode` on a slot receiving channel 4, while
 * `midi` is sent before block `at` each of `n` messages; output to out. */
static void midi_run(const char *mode, int latch, const uint8_t (*midi)[3], int n, float *out)
{
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_midi_channel(vt, 3);
    dsp_set_param(vt, "mode", mode);
    if (latch) dsp_set_param(vt, "latch", "On");
    static float blk[2 * 128];
    for (int b = 0, k = 0; b < VT_SAMPLE_RATE / 128; b++) {
        for (int m = 0; m < n; m++) if (b == 40 + 20 * m) dsp_handle_midi(vt, midi[m], 3);
        for (int i = 0; i < 128; i++, k++) blk[2 * i] = blk[2 * i + 1] = 0.3f * tone(220.0, k, 6, VT_SAMPLE_RATE);
        dsp_process_block_f(vt, blk, 128);
        for (int i = 0; i < 128; i++) out[b * 128 + i] = blk[2 * i];
    }
    dsp_cleanup(vt);
}

static void test_midi_channel_filter(void)
{
    /* The slot's own channel only. Move's sequencer keeps sending notes on
     * muted tracks (with their MIDI Out on), and the chain host hands audio
     * FX that MIDI too: in omni it played Robot's carrier and Chords/Prism
     * voices on the device. Another channel's notes, sustain/all-notes-off
     * CCs and pitch bend change nothing, bit for bit, in every pad mode, Latch
     * included; the slot's own pad still plays. */
    static const uint8_t foreign[6][3] = {
        { 0x90, 64, 100 }, { 0x91, 67, 90 }, { 0xE2, 0, 80 },       /* ch 1 and 2 notes, ch 3 bend */
        { 0x80, 64, 0 }, { 0xB0, 123, 0 }, { 0x9F, 71, 127 },       /* off, ch 1 all-notes-off, ch 16 note */
    };
    static const uint8_t own[1][3] = { { 0x93, 64, 100 } };         /* channel 4: this slot's pad */
    static float base[VT_SAMPLE_RATE], got[VT_SAMPLE_RATE];
    static const char *const modes[3] = { "Robot", "Chords", "Prism" };
    for (int m = 0; m < 3; m++)
        for (int latch = 0; latch < 2; latch++) {
            midi_run(modes[m], latch, NULL, 0, base);
            midi_run(modes[m], latch, foreign, 6, got);
            const int same = memcmp(base, got, sizeof base) == 0;
            midi_run(modes[m], latch, own, 1, got);
            const int played = memcmp(base, got, sizeof base) != 0;
            if (!same || !played) printf("  %s latch %d: other channels %s, own channel %s\n",
                                         modes[m], latch, same ? "ignored" : "CHANGED THE OUTPUT", played ? "played" : "did nothing");
            CHECK(same);
            CHECK(played);
        }

    /* Lead modes too, and the bookkeeping: no voice, no latch entry, no
     * note-on counted for the screen; drops are counted in diag. */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_midi_channel(vt, 3);
    dsp_set_param(vt, "mode", "Snap");
    dsp_set_param(vt, "latch", "On");
    for (int m = 0; m < 6; m++) dsp_handle_midi(vt, foreign[m], 3);
    const uint8_t usb_other[4] = { 0x09, 0x95, 67, 100 };          /* USB-MIDI packet, channel 6 */
    dsp_handle_midi(vt, usb_other, 4);
    CHECK(dsp_active_voices(vt) == 0);
    char d[512];
    dsp_get_param(vt, "diag", d, sizeof d);
    CHECK(strstr(d, "latch -1 -1 -1 -1") != NULL);
    CHECK(strstr(d, "midi ch 4, foreign 7") != NULL);
    dsp_handle_midi(vt, own[0], 3);
    CHECK(dsp_active_voices(vt) == 1);
    const uint8_t panic_other[3] = { 0xB5, 123, 0 }, panic_own[3] = { 0xB3, 123, 0 };
    dsp_handle_midi(vt, panic_other, 3);
    CHECK(dsp_active_voices(vt) == 1);                             /* another track's panic: kept */
    dsp_handle_midi(vt, panic_own, 3);
    CHECK(dsp_active_voices(vt) == 0);

    /* Omni again (the slot set to All, or no slot context: -1 / -2). */
    dsp_set_midi_channel(vt, -2);
    dsp_handle_midi(vt, foreign[0], 3);
    CHECK(dsp_active_voices(vt) == 1);
    dsp_get_param(vt, "diag", d, sizeof d);
    CHECK(strstr(d, "midi ch 0,") != NULL);
    dsp_cleanup(vt);

    /* The host's broadcast of every track's MIDI to every audio FX (source 4)
     * is ignored, even in omni and on the slot's own channel (its own notes
     * also come on their own path, source 2 on the device; the slot channel
     * lookup answers -2 for an FX there). The source covers one message. */
    vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_midi_channel(vt, -2);
    const uint8_t bc_other[3] = { 0x90, 69, 100 }, bc_own[3] = { 0x93, 71, 100 };
    dsp_set_midi_source(vt, VT_MIDI_SOURCE_BROADCAST);
    dsp_handle_midi(vt, bc_other, 3);
    dsp_set_midi_source(vt, VT_MIDI_SOURCE_BROADCAST);
    dsp_handle_midi(vt, bc_own, 3);
    CHECK(dsp_active_voices(vt) == 0);
    dsp_set_midi_source(vt, 2);
    dsp_handle_midi(vt, bc_own, 3);
    CHECK(dsp_active_voices(vt) == 1);
    dsp_handle_midi(vt, bc_other, 3);                              /* no source given: the slot's */
    CHECK(dsp_active_voices(vt) == 2);
    dsp_get_param(vt, "diag", d, sizeof d);
    if (!strstr(d, "foreign 2, recv -2, last 90/69/s-1 93/71/s2 93/71/s4 90/69/s4")) printf("  diag: %s\n", d);
    CHECK(strstr(d, "foreign 2, recv -2, last 90/69/s-1 93/71/s2 93/71/s4 90/69/s4") != NULL);
    dsp_cleanup(vt);

    /* Robot, the device case: other tracks' broadcasts change nothing. */
    {
        static float r0[VT_SAMPLE_RATE], r1[VT_SAMPLE_RATE];
        for (int pass = 0; pass < 2; pass++) {
            vt = dsp_init(VT_SAMPLE_RATE);
            dsp_set_param(vt, "mode", "Robot");
            static float blk[2 * 128];
            float *o = pass ? r1 : r0;
            for (int b = 0, k = 0; b < VT_SAMPLE_RATE / 128; b++) {
                if (pass && b % 25 == 0) {
                    const uint8_t n[3] = { (uint8_t)(0x90 | (b / 25) % 3), (uint8_t)(50 + b % 20), 100 };
                    dsp_set_midi_source(vt, VT_MIDI_SOURCE_BROADCAST);
                    dsp_handle_midi(vt, n, 3);
                }
                for (int i = 0; i < 128; i++, k++) blk[2 * i] = blk[2 * i + 1] = 0.3f * tone(220.0, k, 6, VT_SAMPLE_RATE);
                dsp_process_block_f(vt, blk, 128);
                for (int i = 0; i < 128; i++) o[b * 128 + i] = blk[2 * i];
            }
            dsp_cleanup(vt);
        }
        CHECK(memcmp(r0, r1, sizeof r0) == 0);
    }
}

static void test_pad_octave_fold(void)
{
    /* Move's pads sent E5..G5 while singing ~A3 (seen on the device): past the
     * shifter's +-1 octave every pad clamped to the same pitch. Within +-12 st
     * of the voice a pad plays at its own octave (octave jumps are heard);
     * further away it folds only as far as it takes to get back inside. */
    CHECK(cher_fold_octave(57.0f, 64.0f, 0, 0) == 0);              /* E4, +7: as played */
    CHECK(cher_fold_octave(57.0f, 69.0f, 0, 0) == 0);              /* A4, +12: as played (an octave jump) */
    CHECK(cher_fold_octave(57.0f, 45.0f, 0, 0) == 0);              /* A2, -12: as played */
    CHECK(cher_fold_octave(57.0f, 76.0f, 0, 0) == -1);             /* E5, +19 -> E4 (+7) */
    CHECK(cher_fold_octave(57.0f, 81.0f, 0, 0) == -1);             /* A5, +24 -> A4 (+12), not A3 */
    CHECK(cher_fold_octave(57.0f, 33.0f, 0, 0) == 1);              /* A1, -24 -> A2 (-12) */
    CHECK(cher_fold_octave(57.0f, 24.0f, 0, 0) == 2 && cher_fold_octave(57.0f, 60.0f, 0, 0) == 0);   /* C1 -> C3, C4 stays: not the same */
    CHECK(cher_fold_octave(56.8f, 69.0f, 0, 0) == 0);              /* a flat A3 still gets the octave up */
    CHECK(cher_fold_octave(57.0f, 72.0f, -2, 0) == -1);            /* a new pad ignores the old fold */
    CHECK(cher_fold_octave(57.0f, 69.0f, -1, 0) == 0);             /* ... even right on the edge */
    /* the same pad held while the voice moves near the edge: no flipping */
    CHECK(cher_fold_octave(56.0f, 69.0f, 0, 1) == 0);              /* +13 now, but held at its octave */
    CHECK(cher_fold_octave(57.0f, 69.0f, -1, 1) == -1);            /* folded at +13, voice up to +12: held folded */
    CHECK(cher_fold_octave(58.0f, 69.0f, -1, 1) == 0);             /* +11: clearly inside, unfolds */
    CHECK(cher_fold_octave(54.0f, 69.0f, 0, 1) == -1);             /* +15: past the margin, refolds */
    CHECK(cher_fold_octave(0.0f, 76.0f, 1, 1) == 1 && cher_fold_octave(0.0f, 76.0f, 1, 0) == 0);   /* no pitch yet */
    int in[4] = { 76, 79, 83, -1 }, out[4];
    CHECK(cher_fold_chord(57.0f, in, out, 4, 0, 0) == -1);         /* mean +22 -> +10 */
    CHECK(out[0] == 64 && out[1] == 67 && out[2] == 71 && out[3] == -1);   /* voicing kept */

    /* Snap: each pad is a distinct, reachable pitch, and the audio gets there. */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Snap");
    dsp_set_param(vt, "mix", "1");
    long pos = 0;
    run_sine(vt, 220.0, 0.4f, 150, &pos, NULL, 0);
    const int pads[3] = { 76, 77, 79 };
    const double want[3] = { 329.628, 349.228, 391.995 };          /* E4 F4 G4 */
    static float cap[4096 + 512];
    static float nsdf[CHER_MAX_TAU + 2];
    for (int k = 0; k < 3; k++) {
        note_on(vt, pads[k]);
        run_sine(vt, 220.0, 0.4f, 150, &pos, NULL, 0);
        if (fabs(viz_target(vt) - want[k]) >= 0.5) printf("  pad %d -> target %.2f (want %.2f)\n", pads[k], viz_target(vt), want[k]);
        CHECK(fabs(viz_target(vt) - want[k]) < 0.5);
        run_sine(vt, 220.0, 0.4f, 40, &pos, cap, 4096 + 512);
        float f0 = 0, clar = 0;
        int ok = cher_mpm(cap + 512, 4096, VT_SAMPLE_RATE, 80.0f, 1000.0f, nsdf, &f0, &clar);
        if (!ok || fabs(cents(f0, want[k])) >= 15.0) printf("  pad %d output: ok=%d f0=%.2f\n", pads[k], ok, f0);
        CHECK(ok && fabs(cents(f0, want[k])) < 15.0);
        note_off(vt, pads[k]);
    }
    /* Octave jumps on the pads are heard, and far pads fold into range. */
    const int jump[5] = { 57, 69, 45, 81, 33 };                    /* A3 A4 A2 A5 A1 */
    const double jump_hz[5] = { 220.0, 440.0, 110.0, 440.0, 110.0 };
    for (int k = 0; k < 5; k++) {
        note_on(vt, jump[k]);
        run_sine(vt, 220.0, 0.4f, 60, &pos, NULL, 0);
        if (fabs(viz_target(vt) - jump_hz[k]) >= 0.5) printf("  pad %d -> target %.2f (want %.2f)\n", jump[k], viz_target(vt), jump_hz[k]);
        CHECK(fabs(viz_target(vt) - jump_hz[k]) < 0.5);
        note_off(vt, jump[k]);
    }
    dsp_cleanup(vt);

    /* Chords and Prism: a high pad chord sounds folded down (to within an
     * octave of the voice), voicing intact. */
    enum { N = 3 * VT_SAMPLE_RATE / 2 };
    static float l[N], r[N], mid[N];
    const int tail = VT_SAMPLE_RATE / 2;
    const int chord[4] = { 72, 76, 79, -1 };                       /* C5 E5 G5 -> C4 E4 G4 */
    static cher_t c;
    static imogen_t im;
    static fx_t fx;
    for (int eng = 0; eng < 2; eng++) {
        cher_init(&c, VT_SAMPLE_RATE);
        if (eng == 0) {
            imogen_init(&im, VT_SAMPLE_RATE);
            imogen_run(&im, &c, chord, 1, 0, VT_SCALE_MAJOR, 0.0f, 220.0, N, l, r);
        } else {
            fx_init(&fx, VT_SAMPLE_RATE);
            cher_set_params(&c, 0, VT_SCALE_MAJOR, 0.0f, -60.0f, -1, 0.0f);
            for (int i = 0; i < N; i++) {
                if (i % 128 == 0) fx_set(&fx, &c, VT_MODE_PRISM, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1, 0, VT_SCALE_MAJOR, chord);
                fx_process(&fx, &c, tone(220.0, i, 6, VT_SAMPLE_RATE), &l[i], &r[i]);
            }
        }
        for (int i = 0; i < N; i++) mid[i] = 0.5f * (l[i] + r[i]);
        const float *t = mid + N - tail;
        double lo = fmin(band_at(t, tail, 261.63), fmin(band_at(t, tail, 329.63), band_at(t, tail, 392.0)));
        double a3 = band_at(t, tail, 220.0), c3 = band_at(t, tail, 130.81);
        if (!(lo > 20.0 * a3 && lo > 20.0 * c3)) printf("  %s pad fold: C4/E4/G4 min %.2e, A3 %.2e, C3 %.2e\n",
                                                        eng ? "prism" : "chords", lo, a3, c3);
        CHECK(lo > 20.0 * a3);                                     /* all three folded notes sound */
        CHECK(lo > 20.0 * c3);                                     /* and not an octave lower */
        CHECK((eng ? fx.pad_oct : im.pad_oct) == -1);
    }
}

static float src_sine_220(int i) { return 0.5f * (float)sin(2.0 * M_PI * 220.0 * i / VT_SAMPLE_RATE); }

/* Snap at fx level: target note_a, then note_b from sample `at` on (block aligned). */
static void snap_run(fx_t *fx, cher_t *c, float char1, float retune, src_fn src,
                     int note_a, int note_b, int at, int n, float *out)
{
    const int none[FX_VOICES] = { -1, -1, -1, -1 };
    cher_init(c, VT_SAMPLE_RATE);
    fx_init(fx, VT_SAMPLE_RATE);
    for (int i = 0; i < n; i++) {
        if (i % 128 == 0) {
            cher_set_params(c, 0, VT_SCALE_CHROMATIC, retune, -60.0f, i < at ? note_a : note_b, 0.0f);
            fx_set(fx, c, VT_MODE_SNAP, char1, 0.5f, 0.0f, 0.0f, 0.0f, 0, 0, VT_SCALE_CHROMATIC, none);
        }
        float r;
        fx_process(fx, c, src(i), &out[i], &r);
    }
}

/* Frequency from rising zero crossings (interpolated), first to last. */
static double zc_freq(const float *x, int n)
{
    double first = -1, last = -1;
    int count = 0;
    for (int i = 1; i < n; i++)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            double t = i - 1 + x[i - 1] / (double)(x[i - 1] - x[i]);
            if (first < 0) first = t; else count++;
            last = t;
        }
    return count > 0 ? count * (double)VT_SAMPLE_RATE / (last - first) : 0.0;
}

static void test_snap_believe(void)
{
    static cher_t c;
    static fx_t fx;
    enum { N = 3 * VT_SAMPLE_RATE / 2, AT = 128 * 345 };          /* switch ~1 s in */
    static float out[N];

    /* Retune 0: a pad jump A3 -> D4 is at the new pitch within ~35 ms (a 10 ms
     * snap, no instant ratio step: that clicked), and it resets the grain
     * phase exactly once. A slow retune glides instead and never resets. */
    snap_run(&fx, &c, 0.5f, 0.0f, src_sine_220, 57, 62, AT, N, out);
    double f_hard = zc_freq(out + AT + 1550, 600);                 /* ~35-49 ms after the jump */
    unsigned resets_hard = fx.pv[0].resets;
    snap_run(&fx, &c, 0.5f, 200.0f, src_sine_220, 57, 62, AT, N, out);
    double f_slow = zc_freq(out + AT + 1550, 600);
    unsigned resets_slow = fx.pv[0].resets;
    snap_run(&fx, &c, 0.5f, 25.0f, src_sine_220, 57, 57, AT, N, out);
    unsigned resets_none = fx.pv[0].resets;
    if (!(fabs(cents(f_hard, 293.665)) < 30.0 && f_slow < 240.0 && resets_hard == 1 && resets_slow == 0 && resets_none == 0))
        printf("  believe jump: hard %.1f Hz (%u resets), retune 200 %.1f Hz (%u), no jump %u\n",
               f_hard, resets_hard, f_slow, resets_slow, resets_none);
    CHECK(fabs(cents(f_hard, 293.665)) < 30.0);
    CHECK(f_slow < 240.0);                                         /* still gliding */
    CHECK(resets_hard == 1);
    CHECK(resets_slow == 0);
    CHECK(resets_none == 0);                                       /* a held note never resets */
    for (int i = 0; i < N; i++) CHECK(isfinite(out[i]) && fabsf(out[i]) < 2.0f);

    /* Formant coupling: shifting a vowel up 7 st sounds brighter than shifting
     * it down 7 st (pure decoupled PSOLA would leave them about equal); more so
     * as char1 rises past 0.5 (less than it was: the 5 kHz band caps how
     * bright the up-shift gets; x1.13 measured, x1.17 before the band). */
    double skew[2];
    const float c1s[2] = { 0.5f, 0.75f };
    for (int k = 0; k < 2; k++) {
        snap_run(&fx, &c, c1s[k], 0.0f, src_vowel_220, 64, 64, N, N, out);
        double up = harmonic_centroid(out + N - 8192, 8192, 329.628);
        snap_run(&fx, &c, c1s[k], 0.0f, src_vowel_220, 50, 50, N, N, out);
        double down = harmonic_centroid(out + N - 8192, 8192, 146.832);
        skew[k] = up / down;
    }
    if (!(skew[0] > 1.08 && skew[1] > 1.08 * skew[0])) printf("  believe skew: char1 0.5 x%.3f, 0.75 x%.3f\n", skew[0], skew[1]);
    CHECK(skew[0] > 1.08);
    CHECK(skew[1] > 1.08 * skew[0]);

    /* Tight grains (char1 1) keep the pitch clean and the level within 4 dB. */
    snap_run(&fx, &c, 0.5f, 0.0f, src_tone_220, 62, 62, N, N, out);
    double lv0 = rms_of(out + N - 8192, 8192);
    snap_run(&fx, &c, 1.0f, 0.0f, src_tone_220, 62, 62, N, N, out);
    double lv1 = rms_of(out + N - 8192, 8192);
    double nac1 = nac(out + N - 8192, 8192, VT_SAMPLE_RATE / 293.665);
    if (!(nac1 > 0.95 && fabs(20 * log10(lv1 / lv0)) < 4.0)) printf("  believe tight: nac %.3f, level %+.1f dB\n", nac1, 20 * log10(lv1 / lv0));
    CHECK(nac1 > 0.95);
    CHECK(fabs(20 * log10(lv1 / lv0)) < 4.0);
}

static void test_macro_labels(void)
{
    /* The served chain_params names char1 / char2 / mod for the current mode. */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    static char cp[8192];
    char nm[64];
    for (int m = 0; m < VT_MODE_COUNT; m++) {
        char idx[8];
        snprintf(idx, sizeof idx, "%d", m);
        dsp_set_param(vt, "mode", idx);
        CHECK(dsp_get_param(vt, "chain_params", cp, sizeof cp) > 0);
        CHECK(js_arr_len(cp) == VT_P_COUNT + 3 && strchr(cp, '@') == NULL);
        const int ids[VT_MACROS] = { VT_P_CHAR1, VT_P_CHAR2, VT_P_MOD, VT_P_CHAR4, VT_P_CHAR5 };
        for (int k = 0; k < VT_MACROS; k++) {
            const char *want = dsp_macro_name(m, k);
            CHECK(want && want[0]);
            CHECK(js_str(js_get(js_elem(cp, ids[k]), "name"), nm, sizeof nm) && want && strcmp(nm, want) == 0);
        }
        CHECK(js_str(js_get(js_elem(cp, VT_P_RETUNE), "name"), nm, sizeof nm) && strcmp(nm, "Retune") == 0);
    }
    CHECK(strcmp(dsp_macro_name(VT_MODE_SNAP, 0), "Formant") == 0);
    CHECK(strcmp(dsp_macro_name(VT_MODE_ROBOT, 1), "Noise") == 0);
    CHECK(strcmp(dsp_macro_name(VT_MODE_TAPE, 0), "Drop") == 0);
    CHECK(dsp_macro_name(VT_MODE_COUNT, 0) == NULL && dsp_macro_name(0, VT_MACROS) == NULL && dsp_macro_name(0, -1) == NULL);
    dsp_cleanup(vt);

    /* is_loading pulses "1" after a mode change so the knob pages re-read the
     * names - but not during the first second (a set restore waits on it). */
    vt = dsp_init(VT_SAMPLE_RATE);
    long pos = 0;
    char v[8];
    CHECK(dsp_get_param(vt, "is_loading", v, sizeof v) == 1 && v[0] == '0');
    dsp_set_param(vt, "mode", "Robot");                            /* a restore, right after load */
    run_sine(vt, 220.0, 0.2f, 4, &pos, NULL, 0);
    CHECK(dsp_get_param(vt, "is_loading", v, sizeof v) == 1 && v[0] == '0');
    run_sine(vt, 220.0, 0.2f, 400, &pos, NULL, 0);                 /* past the first second */
    dsp_set_param(vt, "mode", "Chords");
    run_sine(vt, 220.0, 0.2f, 1, &pos, NULL, 0);
    CHECK(dsp_get_param(vt, "is_loading", v, sizeof v) == 1 && v[0] == '1');
    run_sine(vt, 220.0, 0.2f, 150, &pos, NULL, 0);                 /* ~0.45 s: still up */
    CHECK(dsp_get_param(vt, "is_loading", v, sizeof v) == 1 && v[0] == '1');
    run_sine(vt, 220.0, 0.2f, 100, &pos, NULL, 0);                 /* ~0.74 s: down again */
    CHECK(dsp_get_param(vt, "is_loading", v, sizeof v) == 1 && v[0] == '0');
    dsp_set_param(vt, "mix", "0.5");                               /* other params don't pulse */
    run_sine(vt, 220.0, 0.2f, 1, &pos, NULL, 0);
    CHECK(dsp_get_param(vt, "is_loading", v, sizeof v) == 1 && v[0] == '0');
    dsp_cleanup(vt);
}

static void test_demo_snap_shared(void)
{
    /* Snap's Demo plays the same sung phrase as every other mode, straight
     * through the live engine - no virtual pads - so Key and Scale are heard
     * snapping it: the phrase's second syllable (C4 -25 ct) is C4 in C
     * Chromatic and B3 in D major (no C there; B3 is nearer than C#4). */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_bpm(vt, 120.0f);
    dsp_set_param(vt, "demo", "On");
    static float blk[2 * 128];
    memset(blk, 0, sizeof blk);
    dsp_process_block_f(vt, blk, 128);
    CHECK(dsp_set_param(vt, "mode", "Snap") == 0);                 /* pin the tour on Snap */
    const char *settings[2][2] = { { "C", "Chromatic" }, { "D", "Major" } };
    const double want[2] = { 261.63, 246.94 };                     /* C4, B3 */
    for (int k = 0; k < 2; k++) {
        dsp_set_param(vt, "key", settings[k][0]);
        dsp_set_param(vt, "scale", settings[k][1]);
        double seen = 0.0;
        for (int b = 0; b < (int)(2.0 * VT_SAMPLE_RATE / 128); b++) {   /* a whole phrase */
            memset(blk, 0, sizeof blk);                            /* the mic is silent */
            dsp_process_block_f(vt, blk, 128);
            double t = fmod((double)(b + 1) * 128.0 / VT_SAMPLE_RATE + k * 2.0, 2.0);
            (void)t;
            float tg = viz_target(vt);
            if (fabs(tg - want[k]) < 0.5) seen = tg;
        }
        if (seen == 0.0) printf("  snap demo %s %s: never snapped to %.2f\n", settings[k][0], settings[k][1], want[k]);
        CHECK(seen != 0.0);
        CHECK(dsp_active_voices(vt) == 0);                         /* no virtual pads */
    }
    char m[16];
    for (int b = 0; b < (int)(9.0 * VT_SAMPLE_RATE / 128); b++) { memset(blk, 0, sizeof blk); dsp_process_block_f(vt, blk, 128); }
    dsp_get_param(vt, "mode", m, sizeof m);
    CHECK(strcmp(m, "Snap") == 0);                                 /* pinned past the 8 s tour step */
    dsp_set_param(vt, "demo", "Off");
    memset(blk, 0, sizeof blk);
    dsp_process_block_f(vt, blk, 128);
    dsp_get_param(vt, "mode", m, sizeof m);
    CHECK(strcmp(m, "Snap") == 0);                                 /* a pinned mode is kept */
    dsp_cleanup(vt);
}

static void test_robot_drone_key(void)
{
    /* The drone is root + fifth + octave of the Key, between F#2 and F3. */
    static daft_t d;
    daft_init(&d, VT_SAMPLE_RATE);
    CHECK(d.v[DAFT_VOICES].note == 48 && d.v[DAFT_VOICES + 1].note == 55 && d.v[DAFT_VOICES + 2].note == 60);   /* C by default */
    daft_set_drone_root(&d, 7);                                    /* G: G2 D3 G3 */
    CHECK(d.v[DAFT_VOICES].note == 43 && d.v[DAFT_VOICES + 1].note == 50 && d.v[DAFT_VOICES + 2].note == 55);
    daft_set_drone_root(&d, 5);                                    /* F: F3 C4 F4 */
    CHECK(d.v[DAFT_VOICES].note == 53);
    daft_set_drone_root(&d, 6);                                    /* F#: F#2 */
    CHECK(d.v[DAFT_VOICES].note == 42);

    /* Through the engine: Key G, no pads, Auto Off -> the drone is in G. */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Robot");
    dsp_set_param(vt, "auto", "Off");
    const char *keys[2] = { "G", "A" };
    const double root_hz[2] = { 98.0, 110.0 }, other_hz[2] = { 130.81, 98.0 };   /* vs C3; vs the old G2 */
    unsigned seed = 21;
    static float blk[2 * 128], cap[VT_SAMPLE_RATE / 2];
    for (int k = 0; k < 2; k++) {
        dsp_set_param(vt, "key", keys[k]);
        int n = 0;
        for (int b = 0; b < (int)(VT_SAMPLE_RATE / 128); b++) {
            for (int i = 0; i < 128; i++) blk[2 * i] = blk[2 * i + 1] = 0.2f * lcg_noise(&seed);
            dsp_process_block_f(vt, blk, 128);
            if (b >= (int)(VT_SAMPLE_RATE / 256)) for (int i = 0; i < 128 && n < VT_SAMPLE_RATE / 2; i++) cap[n++] = blk[2 * i];
        }
        double er = band_energy(cap, n, root_hz[k] * 0.99, root_hz[k] * 1.01);
        double eo = band_energy(cap, n, other_hz[k] * 0.99, other_hz[k] * 1.01);
        if (!(er > 5.0 * eo)) printf("  drone key %s: root %.2e vs other %.2e\n", keys[k], er, eo);
        CHECK(er > 5.0 * eo);
    }
    dsp_cleanup(vt);
}

static void test_pad_sets_chord_root(void)
{
    /* One pad in Chords or Prism = the chord root: the whole in-key voicing
     * moves to it, folded near the voice. Singing A3 in C major and pressing
     * D5 gives D minor: D4 F4 + D3, and the 5th, A4, is +12 over the voice so
     * it folds down to A3 (nothing above +11). Not a lone D against three voices. */
    const int pad[4] = { 74, -1, -1, -1 };
    enum { N = 3 * VT_SAMPLE_RATE / 2 };
    static float l[N], r[N], mid[N];
    const int tail = VT_SAMPLE_RATE / 2;
    static cher_t c;
    static imogen_t im;
    static fx_t fx;
    for (int eng = 0; eng < 2; eng++) {
        cher_init(&c, VT_SAMPLE_RATE);
        if (eng == 0) {
            imogen_init(&im, VT_SAMPLE_RATE);
            imogen_run(&im, &c, pad, 1, 0, VT_SCALE_MAJOR, 0.0f, 220.0, N, l, r);
            CHECK(im.active == 4 && im.auto_chord == 1);
            CHECK(im.v[0].note == 62 && im.v[1].note == 65 && im.v[2].note == 57 && im.v[3].note == 50);
        } else {
            fx_init(&fx, VT_SAMPLE_RATE);
            cher_set_params(&c, 0, VT_SCALE_MAJOR, 0.0f, -60.0f, -1, 0.0f);
            for (int i = 0; i < N; i++) {
                if (i % 128 == 0) fx_set(&fx, &c, VT_MODE_PRISM, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, VT_SCALE_MAJOR, pad);
                fx_process(&fx, &c, tone(220.0, i, 6, VT_SAMPLE_RATE), &l[i], &r[i]);
            }
            CHECK(fx.active == 4);                                 /* Auto Off: the pad still builds the chord */
            CHECK(fx.note[0] == 62 && fx.note[1] == 65 && fx.note[2] == 57 && fx.note[3] == 50);
        }
        for (int i = 0; i < N; i++) mid[i] = 0.5f * (l[i] + r[i]);
        const float *t = mid + N - tail;
        double d4 = band_at(t, tail, 293.66), f4 = band_at(t, tail, 349.23), a3 = band_at(t, tail, 220.0);
        double d3 = band_at(t, tail, 146.83), fs4 = band_at(t, tail, 369.99), cs4 = band_at(t, tail, 277.18);
        double weakest = fmin(fmin(d4, f4), fmin(a3, d3));
        if (!(weakest > 10.0 * fs4 && weakest > 10.0 * cs4))
            printf("  %s pad root: D3 %.2e D4 %.2e F4 %.2e A3 %.2e | F#4 %.2e C#4 %.2e\n",
                   eng ? "prism" : "chords", d3, d4, f4, a3, fs4, cs4);
        CHECK(weakest > 10.0 * fs4);                               /* minor 3rd: in key */
        CHECK(weakest > 10.0 * cs4);
    }
}

static void test_demo_level_and_flow(void)
{
    /* The demo phrase sits where Auto puts a real voice: Snap sounds the same
     * with Auto On or Off, and Auto's gain over the phrase averages ~0 dB. */
    double out_db[2], agc_mean = 0, in_db;
    {
        static demo_t dm;
        demo_init(&dm, VT_SAMPLE_RATE);
        double a = 0;
        for (int i = 0; i < 4 * VT_SAMPLE_RATE; i++) { float y = demo_process(&dm); a += y * y; }
        in_db = 10.0 * log10(a / (4.0 * VT_SAMPLE_RATE));
    }
    static float out[6 * VT_SAMPLE_RATE];
    for (int au = 0; au < 2; au++) {
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "auto", au ? "On" : "Off");
        dsp_set_param(vt, "demo", "On");
        static float blk[2 * 128];
        int k = 0, gn = 0;
        double gs = 0;
        for (int b = 0; b < 6 * VT_SAMPLE_RATE / 128; b++) {
            memset(blk, 0, sizeof blk);
            dsp_process_block_f(vt, blk, 128);
            if (b == 0) dsp_set_param(vt, "mode", "Snap");                    /* pin Snap */
            for (int i = 0; i < 128; i++) out[k++] = blk[2 * i];
            if (au && b > 2 * VT_SAMPLE_RATE / 128) {
                char sc[256];
                dsp_get_param(vt, "scope", sc, sizeof sc);
                const char *q = sc;
                for (int c = 0; c < 24 && q; c++) { q = strchr(q, ','); if (q) q++; }
                if (q) { gs += atof(q); gn++; }
            }
        }
        if (au) agc_mean = gs / (gn ? gn : 1);
        double e = 0;
        int n = 0;
        for (int i = 2 * VT_SAMPLE_RATE; i < 6 * VT_SAMPLE_RATE; i++) { e += out[i] * out[i]; n++; }
        out_db[au] = 10.0 * log10(e / n);
        dsp_cleanup(vt);
    }
    if (!(fabs(out_db[1] - out_db[0]) < 2.0 && fabs(agc_mean) < 2.0 && fabs(out_db[0] - in_db) < 2.0))
        printf("  demo level: in %.1f, out Auto Off %.1f / On %.1f dBFS, Auto gain %.1f dB\n", in_db, out_db[0], out_db[1], agc_mean);
    CHECK(fabs(out_db[1] - out_db[0]) < 2.0);                      /* Auto On = Auto Off */
    CHECK(fabs(agc_mean) < 2.0);                                   /* Auto has little to fix */
    CHECK(fabs(out_db[0] - in_db) < 2.0);                          /* Snap passes it at its own level */
}

static void test_chords_register_and_bleed(void)
{
    /* The register rule: every voice within [sung - 12, sung + 11]. */
    int n[4] = { 69, 76, 45, -1 };
    vt_fit_register(n, 4, 57);                                     /* sung A3 */
    CHECK(n[0] == 57 && n[1] == 64 && n[2] == 45 && n[3] == -1);   /* A4 +12 -> A3, E5 +19 -> E4, A2 -12 stays */
    int m[2] = { 68, 44 };
    vt_fit_register(m, 2, 57);
    CHECK(m[0] == 68 && m[1] == 56);                               /* +11 stays; -13 goes up */
    int u[1] = { 80 };
    vt_fit_register(u, 1, -1);
    CHECK(u[0] == 80);                                             /* no pitch heard: untouched */
    /* Any pad chord on a high voice: nothing above +11. */
    static cher_t c;
    static imogen_t im;
    static float l[VT_SAMPLE_RATE], r[VT_SAMPLE_RATE];
    for (int pad = 60; pad < 72; pad++) {
        const int held[4] = { pad, -1, -1, -1 };
        cher_init(&c, VT_SAMPLE_RATE);
        imogen_init(&im, VT_SAMPLE_RATE);
        imogen_run(&im, &c, held, 1, 0, VT_SCALE_MAJOR, 0.0f, 220.0, VT_SAMPLE_RATE / 2, l, r);
        for (int i = 0; i < IMOGEN_VOICES; i++)
            if (im.v[i].note >= 0) CHECK(im.v[i].note <= c.note + 11 && im.v[i].note >= c.note - 12);
    }

    /* Formants stay put on an upward shift: two pads on E4 over a sung A3
     * vowel -> the harmonics move up a 5th, the vowel's resonance doesn't. */
    enum { N = 3 * VT_SAMPLE_RATE / 2 };
    static float in[N], out_l[N], out_r[N];
    const int fifth[4] = { 64, 64, -1, -1 };
    cher_init(&c, VT_SAMPLE_RATE);
    imogen_init(&im, VT_SAMPLE_RATE);
    cher_set_params(&c, 0, VT_SCALE_CHROMATIC, 0.0f, -60.0f, -1, 0.0f);
    for (int i = 0; i < N; i++) {
        if (i % 128 == 0) imogen_set_targets(&im, &c, fifth, 0, 0, VT_SCALE_CHROMATIC, 0.0f, 0.0f, 0.0f, VT_VOICING_STANDARD);
        in[i] = vowel(220.0, 700.0, i);
        imogen_process(&im, &c, in[i], &out_l[i], &out_r[i]);
    }
    for (int i = 0; i < N; i++) out_l[i] = 0.5f * (out_l[i] + out_r[i]);
    double ratio = harmonic_centroid(out_l + N - 8192, 8192, 329.63) / harmonic_centroid(in + N - 8192, 8192, 220.0);
    if (!(ratio < 1.2)) printf("  chords formant: centroid x%.2f on a +7 st shift (tap shifting: ~x1.5)\n", ratio);
    CHECK(ratio < 1.2);
    CHECK(nac(out_l + N - 8192, 8192, VT_SAMPLE_RATE / 329.63) > 0.9);   /* and it is at E4 */

    /* Unvoiced passthrough keeps consonants, not pitch: Chords at 100% wet on
     * noise (nothing to track) passes the top, not the low end. */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Chords");
    dsp_set_param(vt, "auto", "Off");
    unsigned seed = 33;
    static float blk[2 * 128], nin[VT_SAMPLE_RATE / 2], nout[VT_SAMPLE_RATE / 2];
    int k = 0;
    for (int b = 0; b < VT_SAMPLE_RATE / 128; b++) {
        for (int i = 0; i < 128; i++) blk[2 * i] = blk[2 * i + 1] = 0.2f * lcg_noise(&seed);
        float keep[128];
        for (int i = 0; i < 128; i++) keep[i] = blk[2 * i];
        dsp_process_block_f(vt, blk, 128);
        if (b >= VT_SAMPLE_RATE / 256)
            for (int i = 0; i < 128 && k < VT_SAMPLE_RATE / 2; i++, k++) { nin[k] = keep[i]; nout[k] = blk[2 * i]; }
    }
    double lo_in = band_energy(nin, k, 200, 800), lo_out = band_energy(nout, k, 200, 800);
    double hi_in = band_energy(nin, k, 5000, 8000), hi_out = band_energy(nout, k, 5000, 8000);
    double lo_db = 10 * log10(lo_out / lo_in), hi_db = 10 * log10(hi_out / hi_in);
    if (!(lo_db - hi_db < -20.0 && hi_db > -3.0)) printf("  chords unvoiced: 200-800 Hz %.1f dB, 5-8 kHz %.1f dB\n", lo_db, hi_db);
    CHECK(lo_db - hi_db < -20.0);                                  /* no unshifted pitch region (vs what passes) */
    CHECK(hi_db > -3.0);                                           /* consonants come through */
    dsp_cleanup(vt);
}

/* Run `sec` seconds of a harmonic tone (amp, 6 partials) through vt; left channel to cap (if given). */
static int run_tone(vt_t *vt, double f, float amp, double sec, long *pos, float *cap)
{
    static float blk[2 * 128];
    int n = 0;
    for (int b = 0; b < (int)(sec * VT_SAMPLE_RATE / 128); b++) {
        for (int i = 0; i < 128; i++, (*pos)++) blk[2 * i] = blk[2 * i + 1] = amp * tone(f, (int)*pos, 6, VT_SAMPLE_RATE);
        dsp_process_block_f(vt, blk, 128);
        if (cap) for (int i = 0; i < 128; i++) cap[n++] = blk[2 * i];
    }
    return n;
}

static void test_chords_manual_level_and_gate(void)
{
    /* Chords with Auto Off: the harmony bus gets +1 dB more on top of its
     * +4.5 dB trim (+5.5 net), so at a raw mic level the cluster comes out
     * above the input without being driven into the limiter. Auto On is
     * levelled by the AGC and left as it was. */
    double gain_db[2];
    for (int a = 0; a < 2; a++) {
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "mode", "Chords");
        dsp_set_param(vt, "auto", a ? "On" : "Off");
        dsp_set_param(vt, "beauty", "0");                          /* the trims alone (Beauty pans the choir) */
        note_on(vt, 60);                                           /* a C chord over a sung A3 */
        long pos = 0;
        static float cap[VT_SAMPLE_RATE];
        run_tone(vt, 220.0, 0.1f, 1.5, &pos, NULL);
        int n = run_tone(vt, 220.0, 0.1f, 0.5, &pos, cap);
        static float ref[VT_SAMPLE_RATE / 2];
        for (int i = 0; i < n; i++) ref[i] = 0.1f * tone(220.0, i, 6, VT_SAMPLE_RATE);
        gain_db[a] = 20.0 * log10(rms_of(cap, n) / rms_of(ref, n));
        dsp_cleanup(vt);
    }
    if (!(gain_db[0] > 1.5 && gain_db[0] < 3.5)) printf("  chords Auto Off: %+.1f dB over the input (Auto On %+.1f)\n", gain_db[0], gain_db[1]);
    CHECK(gain_db[0] > 1.5 && gain_db[0] < 3.5);                   /* ~+2.5: +1.5 bus trim, +1 Auto Off */

    /* A normal-level loop through Chords with Auto Off and the Gate up at
     * -40 dB: the chord plays continuously, no gating holes. */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Chords");
    dsp_set_param(vt, "auto", "Off");
    dsp_set_param(vt, "gate", "-40");
    long pos = 0;
    dsp_set_param(vt, "freeze", "Rec");
    run_tone(vt, 220.0, 0.1f, 1.5, &pos, NULL);                    /* ~-23 dBFS */
    dsp_set_param(vt, "freeze", "Loop");
    note_on(vt, 60);
    static float cap[3 * VT_SAMPLE_RATE];
    int n = run_tone(vt, 220.0, 0.0f, 3.0, &pos, cap);             /* mic silent: only the loop */
    double sum = 0, lo = 1e9;
    int nw = 0;
    for (int s0 = (int)(0.1 * VT_SAMPLE_RATE); s0 + 882 <= n; s0 += 882) {
        double e = 0;
        for (int i = 0; i < 882; i++) e += cap[s0 + i] * cap[s0 + i];
        e = 10.0 * log10(e / 882 + 1e-20);
        sum += e; nw++;
        if (e < lo) lo = e;
    }
    double mean = sum / nw;
    if (!(lo > mean - 6.0 && mean > -35.0)) printf("  chords loop through gate -40: mean %.1f dB, quietest 20 ms %.1f dB\n", mean, lo);
    CHECK(lo > mean - 6.0);
    CHECK(mean > -35.0);
    CHECK(band_at(cap + n - 8192, 8192, 261.63) > 10.0 * band_at(cap + n - 8192, 8192, 277.18));   /* and it is the chord */
    dsp_cleanup(vt);
}

static void test_freeze_bars(void)
{
    /* Freeze Len in bars: Rec stops itself after exactly N bars at the host
     * tempo and loops, the knob moving to Loop; the loop period is exactly
     * the bars (the wrap crossfade comes out of 5 ms recorded past them). */
    struct { const char *len; float bpm; double want_s; } cases[] = {
        { "2 Bars", 120.0f, 4.0 },                                 /* the default */
        { "1 Bar",   90.0f, 240.0 / 90.0 },
        { "4 Bars",  60.0f, 16.0 - 0.005 },                        /* 16 s: clamped to the buffer */
    };
    char v[16];
    for (int k = 0; k < 3; k++) {
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_bpm(vt, cases[k].bpm);
        if (k > 0) dsp_set_param(vt, "freeze_len", cases[k].len);
        dsp_get_param(vt, "freeze_len", v, sizeof v); CHECK(strcmp(v, cases[k].len) == 0);
        long pos = 0;
        dsp_set_param(vt, "freeze", "Rec");
        run_tone(vt, 220.0, 0.3f, cases[k].want_s - 0.1, &pos, NULL);
        dsp_get_param(vt, "freeze", v, sizeof v); CHECK(strcmp(v, "Rec") == 0);       /* not yet */
        scope_t sc = read_scope(vt);
        CHECK(sc.frozen == 2 && sc.phase > 0.9f && sc.phase < 1.0f);                   /* nearly through the take */
        run_tone(vt, 220.0, 0.3f, 0.2, &pos, NULL);
        dsp_get_param(vt, "freeze", v, sizeof v); CHECK(strcmp(v, "Loop") == 0);      /* stopped by itself */
        sc = read_scope(vt);
        if (!(sc.frozen == 1 && fabs(sc.loop_s - cases[k].want_s) < 0.002))
            printf("  %s at %.0f BPM: loop %.4f s, want %.4f\n", cases[k].len, cases[k].bpm, sc.loop_s, cases[k].want_s);
        CHECK(sc.frozen == 1 && fabs(sc.loop_s - cases[k].want_s) < 0.002);
        dsp_cleanup(vt);
    }

    /* Stopping early still locks what was recorded; Free waits for you. */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    long pos = 0;
    dsp_set_param(vt, "freeze", "Rec");
    run_tone(vt, 220.0, 0.3f, 1.0, &pos, NULL);
    dsp_set_param(vt, "freeze", "Loop");
    run_tone(vt, 220.0, 0.3f, 0.1, &pos, NULL);
    scope_t sc = read_scope(vt);
    CHECK(sc.frozen == 1 && fabs(sc.loop_s - 0.995) < 0.01);
    dsp_set_param(vt, "freeze_len", "Free");
    dsp_set_param(vt, "freeze", "Rec");
    run_tone(vt, 220.0, 0.3f, 6.0, &pos, NULL);                    /* past any bar count at 120 */
    dsp_get_param(vt, "freeze", v, sizeof v); CHECK(strcmp(v, "Rec") == 0);
    dsp_cleanup(vt);

    /* A bar loop keeps feeding Chords and Robot while pads change: no gaps. */
    const char *modes[2] = { "Chords", "Robot" };
    for (int m = 0; m < 2; m++) {
        vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "mode", modes[m]);
        /* Robot's Beauty detune beats slowly (+-6 dB) in the few bands a
         * 6-harmonic test tone fills; that's not a gap, so leave it out. */
        if (m == 1) dsp_set_param(vt, "beauty", "0");
        dsp_set_param(vt, "freeze_len", "1 Bar");                  /* 2 s at 120 */
        dsp_set_param(vt, "freeze", "Rec");
        pos = 0;
        run_tone(vt, 220.0, 0.3f, 2.2, &pos, NULL);                /* records, then loops by itself */
        dsp_get_param(vt, "freeze", v, sizeof v); CHECK(strcmp(v, "Loop") == 0);
        const int pads[4] = { 60, 64, 67, 62 };
        static float cap[2 * VT_SAMPLE_RATE];
        double lo = 1e9, sum = 0;
        int nw = 0;
        for (int k = 0; k < 4; k++) {
            note_on(vt, pads[k]);
            int n = run_tone(vt, 220.0, 0.0f, 0.5, &pos, cap);       /* the mic is silent: only the loop */
            for (int s0 = 0; s0 + 882 <= n; s0 += 882) {
                double e = 0;
                for (int i = 0; i < 882; i++) e += cap[s0 + i] * cap[s0 + i];
                e = 10.0 * log10(e / 882 + 1e-20);
                if (e < lo) lo = e;
                sum += e; nw++;
            }
            note_off(vt, pads[k]);
        }
        double mean = sum / nw;
        if (!(lo > mean - 12.0 && mean > -35.0)) printf("  %s over a bar loop: mean %.1f dB, quietest 20 ms %.1f dB\n", modes[m], mean, lo);
        CHECK(lo > mean - 12.0);
        CHECK(mean > -35.0);
        dsp_cleanup(vt);
    }
}

/* Speech-like test vowel: f0 harmonics through three formant bumps. */
static float vowel3(double f0, const double *F, long i)
{
    double s = 0;
    for (int h = 1; h * f0 < 8000; h++) {
        double fh = h * f0, a = 0;
        for (int k = 0; k < 3; k++) a += (k == 0 ? 1.0 : k == 1 ? 0.6 : 0.3) / (1.0 + pow((fh - F[k]) / (60.0 + 40.0 * k), 2));
        s += a * sin(2 * M_PI * fh * i / VT_SAMPLE_RATE) / sqrt(h);
    }
    return (float)(0.12 * s);
}

static void test_robot_consonants_and_articulation(void)
{
    /* The consonant detector lets 's', 't' and 'k' through as noise and
     * never turns a sustained vowel (bright, breathy, high "ee" too) into hiss. */
    static const double ah[3] = { 730, 1090, 2440 }, ee[3] = { 270, 2290, 3010 };
    static daft_t d;
    daft_init(&d, VT_SAMPLE_RATE);
    daft_set_auto(&d, 1);
    daft_set_params(&d, 0, 8.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.0f, -60.0f);
    unsigned seed = 77;
    float h1x = 0, h1y = 0, h2x = 0, h2y = 0, lp1 = 0, lp2 = 0;
    long n = 0;
    for (long i = 0; i < VT_SAMPLE_RATE; i++, n++) daft_process(&d, vowel3(140, ah, n));
    /* kinds: 0 ah, 1 ee, 2 breathy ee, 3 high ee, 4 s, 5 t, 6 k, 7 closure */
    const int kind[] = { 0, 4, 1, 7, 5, 0, 7, 6, 1, 2, 3, 0 };
    const double sec[] = { .4, .15, .4, .04, .025, .3, .04, .03, .4, .4, .4, .3 };
    double vowel_max = 0, s_max = 0, t_max = 0, k_max = 0;
    for (int g = 0; g < 12; g++) {
        long len = (long)(sec[g] * VT_SAMPLE_RATE);
        float hpa = expf(-2.0f * (float)M_PI * (kind[g] == 4 ? 4000.0f : kind[g] == 5 ? 2500.0f : 1500.0f) / VT_SAMPLE_RATE);
        float lpa = 1.0f - expf(-2.0f * (float)M_PI * 4000.0f / VT_SAMPLE_RATE);
        for (long i = 0; i < len; i++, n++) {
            float x = 0.0f, w = lcg_noise(&seed);
            float y1 = hpa * (h1y + w - h1x); h1x = w; h1y = y1;
            float y2 = hpa * (h2y + y1 - h2x); h2x = y1; h2y = y2;
            switch (kind[g]) {
            case 0: x = vowel3(140, ah, n); break;
            case 1: x = vowel3(140, ee, n); break;
            case 2: x = vowel3(140, ee, n) + 0.03f * y2; break;
            case 3: x = vowel3(220, ee, n); break;
            case 4: x = 0.25f * y2; break;
            case 5: x = 0.5f * y2 * expf(-(float)i / (0.008f * VT_SAMPLE_RATE)); break;
            case 6: lp1 += (y2 - lp1) * lpa; lp2 += (lp1 - lp2) * lpa; x = 0.6f * lp2 * expf(-(float)i / (0.01f * VT_SAMPLE_RATE)); break;
            default: x = 0.0005f * w;
            }
            daft_process(&d, x);
            int settled = i > (long)(0.05 * VT_SAMPLE_RATE);
            if (kind[g] <= 3 && settled && d.sib > vowel_max) vowel_max = d.sib;
            if (kind[g] == 4 && d.sib > s_max) s_max = d.sib;
            if (kind[g] == 5 && d.sib > t_max) t_max = d.sib;
            if (kind[g] == 6 && d.sib > k_max) k_max = d.sib;
        }
    }
    if (!(vowel_max < 0.05 && s_max > 0.9 && t_max > 0.8 && k_max > 0.4))
        printf("  robot consonants: vowels %.3f, s %.2f, t %.2f, k %.2f\n", vowel_max, s_max, t_max, k_max);
    CHECK(vowel_max < 0.05);
    CHECK(s_max > 0.9);
    CHECK(t_max > 0.8);
    CHECK(k_max > 0.4);                                            /* used to be missed (5 kHz detector) */

    /* Articulation: 8 syllables a second keep deep gaps between them. */
    daft_init(&d, VT_SAMPLE_RATE);
    daft_set_auto(&d, 1);
    daft_set_params(&d, 0, 8.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.0f, -60.0f);
    for (long i = 0; i < VT_SAMPLE_RATE; i++) daft_process(&d, vowel3(140, ah, i));
    const long per = (long)(0.125 * VT_SAMPLE_RATE), on = (long)(0.075 * VT_SAMPLE_RATE);
    double e_on = 0, e_gap = 0;
    for (int syl = 0; syl < 16; syl++)
        for (long i = 0; i < per; i++) {
            long t = syl * per + i;
            float env = (float)fmin(1.0, fmin(i / (0.005 * VT_SAMPLE_RATE), (on - i) / (0.005 * VT_SAMPLE_RATE)));
            float x = i < on ? vowel3(140, ah, t) * env : 0.0005f * lcg_noise(&seed);
            float y = daft_process(&d, x);
            if (syl < 4) continue;
            if (i > on / 3 && i < on) e_on += y * y;
            if (i > on + (per - on) / 2) e_gap += y * y;
        }
    double depth = 10.0 * log10((e_on / (on * 2.0 / 3.0)) / (e_gap / ((per - on) / 2.0) + 1e-20));
    if (!(depth > 18.0)) printf("  robot syllable gaps: %.1f dB\n", depth);
    CHECK(depth > 18.0);                                           /* 25 ms release gave ~13 */

    /* Gate default -39 dB (Auto Off): -50 dBFS room noise leaves the vocoder
     * silent; a voice at -20 dBFS opens it. */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Robot");
    dsp_set_param(vt, "auto", "Off");
    char v[16];
    CHECK(dsp_get_param(vt, "gate", v, sizeof v) > 0 && atof(v) == -39.0);
    static float blk[2 * 128];
    double room = 0, voice = 0;
    for (int b = 0; b < 2 * VT_SAMPLE_RATE / 128; b++) {
        for (int i = 0; i < 128; i++) blk[2 * i] = blk[2 * i + 1] = 0.0045f * lcg_noise(&seed);   /* ~-52 dBFS RMS */
        dsp_process_block_f(vt, blk, 128);
        if (b > VT_SAMPLE_RATE / 128) for (int i = 0; i < 128; i++) room += blk[2 * i] * blk[2 * i];
    }
    for (int b = 0; b < VT_SAMPLE_RATE / 128; b++) {
        for (int i = 0; i < 128; i++) blk[2 * i] = blk[2 * i + 1] = 0.8f * vowel3(140, ah, (long)b * 128 + i);
        dsp_process_block_f(vt, blk, 128);
        if (b > VT_SAMPLE_RATE / 256) for (int i = 0; i < 128; i++) voice += blk[2 * i] * blk[2 * i];
    }
    room = 10.0 * log10(room / (VT_SAMPLE_RATE * 1.0) + 1e-20);
    voice = 10.0 * log10(voice / (VT_SAMPLE_RATE / 2.0) + 1e-20);
    if (!(room < -70.0 && voice > -30.0)) printf("  robot gate -39: room noise out %.1f dB, voice out %.1f dB\n", room, voice);
    CHECK(room < -70.0);
    CHECK(voice > -30.0);
    dsp_cleanup(vt);
}

static void test_snap_scale_switch(void)
{
    /* Turning Scale while singing is heard at once: a sung C#4 (Key C) snaps
     * to C4 within a few blocks of Major being chosen, and back to C#4 for
     * Chromatic - the target and the actual output pitch. A held pad still
     * wins over the scale (it's an explicit note). */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Snap");
    long pos = 0;
    static float cap[4096];
    static float nsdf[CHER_MAX_TAU + 2];
    run_tone(vt, 277.18, 0.3f, 0.5, &pos, NULL);
    CHECK(fabs(viz_target(vt) - 277.18) < 0.5);
    const char *scales[3] = { "Major", "Chromatic", "Pentatonic" };
    const double want[3] = { 261.63, 277.18, 261.63 };
    for (int k = 0; k < 3; k++) {
        dsp_set_param(vt, "scale", scales[k]);
        run_tone(vt, 277.18, 0.3f, 4 * 128.0 / VT_SAMPLE_RATE, &pos, NULL);   /* 4 blocks, ~12 ms */
        if (fabs(viz_target(vt) - want[k]) >= 0.5) printf("  scale %s: target %.1f after 4 blocks\n", scales[k], viz_target(vt));
        CHECK(fabs(viz_target(vt) - want[k]) < 0.5);
        run_tone(vt, 277.18, 0.3f, 0.1, &pos, NULL);
        int n = run_tone(vt, 277.18, 0.3f, 4096.0 / VT_SAMPLE_RATE, &pos, cap);
        float f0 = 0, clar = 0;
        int ok = cher_mpm(cap, n < 4096 ? n : 4096, VT_SAMPLE_RATE, 120.0f, 1000.0f, nsdf, &f0, &clar);
        if (!ok || fabs(cents(f0, want[k])) > 20.0) printf("  scale %s: output %.1f Hz\n", scales[k], f0);
        CHECK(ok && fabs(cents(f0, want[k])) < 20.0);
    }
    note_on(vt, 64);
    dsp_set_param(vt, "scale", "Pentatonic");
    run_tone(vt, 277.18, 0.3f, 0.1, &pos, NULL);
    CHECK(fabs(viz_target(vt) - 329.63) < 0.5);                    /* the pad, whatever the scale */
    dsp_cleanup(vt);
}

/* Output level (dBFS RMS, left) of a block run with input x(i) = f(i). */
typedef float (*sigfn)(long i, unsigned *seed);
static float sig_voice(long i, unsigned *seed) { (void)seed; return 0.5f * tone(220.0, (int)i, 6, VT_SAMPLE_RATE); }
static float sig_soft(long i, unsigned *seed) { (void)seed; return 0.012f * tone(220.0, (int)i, 6, VT_SAMPLE_RATE); }   /* ~-48 dBFS */
static float sig_mid(long i, unsigned *seed) { (void)seed; return 0.05f * tone(220.0, (int)i, 6, VT_SAMPLE_RATE); }    /* ~-36 dBFS */
static float sig_room50(long i, unsigned *seed) { (void)i; return 0.0045f * lcg_noise(seed); }    /* ~-50 */
static float sig_room46(long i, unsigned *seed) { (void)i; return 0.0075f * lcg_noise(seed); }    /* ~-46 */
static float sig_room70(long i, unsigned *seed) { (void)i; return 0.00045f * lcg_noise(seed); }   /* ~-70 */
static double run_sig(vt_t *vt, sigfn f, double sec, long *pos, unsigned *seed, double skip)
{
    static float blk[2 * 128];
    double e = 0;
    long n = 0;
    for (int b = 0; b < (int)(sec * VT_SAMPLE_RATE / 128); b++) {
        for (int i = 0; i < 128; i++, (*pos)++) blk[2 * i] = blk[2 * i + 1] = f(*pos, seed);
        dsp_process_block_f(vt, blk, 128);
        if (b * 128.0 / VT_SAMPLE_RATE >= skip) for (int i = 0; i < 128; i++, n++) e += blk[2 * i] * blk[2 * i];
    }
    return 10.0 * log10(e / (n ? n : 1) + 1e-20);
}
static double scope_agc_db(vt_t *vt)
{
    char buf[256];
    dsp_get_param(vt, "scope", buf, sizeof buf);
    const char *q = buf;
    for (int c = 0; c < 24 && q; c++) { q = strchr(q, ','); if (q) q++; }
    return q ? atof(q) : 0.0;
}

static void test_auto_silence_gate(void)
{
    /* Pauses stay quiet with Auto On: the AGC gain freezes at its singing
     * value and the input is expanded down, instead of the AGC climbing to
     * +24 dB on the room (measured before: Snap -28 dBFS, Robot -15 dBFS of
     * hiss 4 s into a pause in a -50 dBFS room). */
    const char *modes[2] = { "Snap", "Robot" };
    for (int m = 0; m < 2; m++) {
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "mode", modes[m]);
        long pos = 0;
        unsigned seed = 5;
        run_sig(vt, sig_voice, 2.0, &pos, &seed, 0.0);
        double g_sing = scope_agc_db(vt);
        double pause = run_sig(vt, sig_room50, 4.0, &pos, &seed, 0.5);
        double g_pause = scope_agc_db(vt);
        if (!(pause < -70.0 && fabs(g_pause - g_sing) < 1.0))
            printf("  %s pause: %.1f dBFS out, Auto gain %.1f -> %.1f dB\n", modes[m], pause, g_sing, g_pause);
        CHECK(pause < -70.0);
        CHECK(fabs(g_pause - g_sing) < 1.0);                       /* frozen */
        /* and the next phrase starts at once, not clipped */
        double onset = run_sig(vt, sig_voice, 0.03, &pos, &seed, 0.005);
        if (!(onset > -30.0)) printf("  %s onset after a pause: %.1f dBFS in its first 30 ms\n", modes[m], onset);
        CHECK(onset > -30.0);
        dsp_cleanup(vt);
    }

    /* The threshold is fixed at -42 dBFS whatever the room: a voice just above
     * it passes (and is lifted), one at -48 is treated as silence, even in a
     * quiet room. (An adaptive room-noise threshold missed pauses on the device.) */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Snap");
    long pos = 0;
    unsigned seed = 9;
    run_sig(vt, sig_room70, 3.0, &pos, &seed, 0.0);
    double mid = run_sig(vt, sig_mid, 2.0, &pos, &seed, 1.0);
    double soft = run_sig(vt, sig_soft, 2.0, &pos, &seed, 1.0);
    if (!(mid > -32.0 && soft < -70.0)) printf("  quiet room: -38 dBFS voice out %.1f, -48 dBFS voice out %.1f\n", mid, soft);
    CHECK(mid > -32.0);                                            /* above the threshold: lifted */
    CHECK(soft < -70.0);                                           /* below it: silence */
    dsp_cleanup(vt);

    vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Snap");
    pos = 0;
    double room = run_sig(vt, sig_room46, 4.0, &pos, &seed, 1.0);
    if (!(room < -70.0)) printf("  -46 dBFS room: %.1f dBFS out\n", room);
    CHECK(room < -70.0);
    dsp_cleanup(vt);

}

static void test_robot_silent_pauses_and_key(void)
{
    /* Robot in a pause (-50 dBFS room after singing) is exact digital
     * silence, Auto On or Off, and opens again on the next phrase. */
    for (int au = 0; au < 2; au++) {
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "mode", "Robot");
        dsp_set_param(vt, "auto", au ? "On" : "Off");
        long pos = 0;
        unsigned seed = 3;
        run_sig(vt, sig_voice, 2.0, &pos, &seed, 0.0);
        static float blk[2 * 128];
        long nonzero = 0;
        for (int b = 0; b < 3 * VT_SAMPLE_RATE / 128; b++) {
            for (int i = 0; i < 128; i++, pos++) blk[2 * i] = blk[2 * i + 1] = sig_room50(pos, &seed);
            dsp_process_block_f(vt, blk, 128);
            if (b > VT_SAMPLE_RATE / 4 / 128) for (int i = 0; i < 256; i++) nonzero += blk[i] != 0.0f;
        }
        if (nonzero) printf("  robot pause (Auto %s): %ld non-zero samples\n", au ? "On" : "Off", nonzero);
        CHECK(nonzero == 0);
        double back = run_sig(vt, sig_voice, 0.3, &pos, &seed, 0.05);
        if (!(back > -30.0)) printf("  robot after the pause (Auto %s): %.1f dBFS\n", au ? "On" : "Off", back);
        CHECK(back > -30.0);
        dsp_cleanup(vt);
    }

    /* Key moves the scale: a sung C#4 in Major snaps to C#4 in keys with a
     * C# (C#, D, E, F#, G#, A, B) and to a neighbour in the rest; in Chromatic
     * every key keeps C#4 (all 12 notes are in it). */
    static const char *const keys[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const int has_cs[12] = { 0, 1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1 };
    for (int chrom = 0; chrom < 2; chrom++) {
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "mode", "Snap");
        dsp_set_param(vt, "scale", chrom ? "Chromatic" : "Major");
        long pos = 0;
        for (int k = 0; k < 12; k++) {
            dsp_set_param(vt, "key", keys[k]);
            run_tone(vt, 277.18, 0.3f, 0.15, &pos, NULL);
            int on_cs = fabs(viz_target(vt) - 277.18) < 0.5;
            if (on_cs != (chrom || has_cs[k])) printf("  key %s (%s): target %.1f Hz\n", keys[k], chrom ? "Chromatic" : "Major", viz_target(vt));
            CHECK(on_cs == (chrom || has_cs[k]));
        }
        dsp_cleanup(vt);
    }
}

/* A running transport for `sec` seconds: the host's beat position is set at
 * each block start; the input is a tone at f_before until beat `switch_beat`,
 * f_after from then on (0 Hz = silence). Left output to cap (if given). */
static int run_transport(vt_t *vt, double *beat, float bpm, double f_before, double switch_beat,
                         double f_after, double sec, float *cap)
{
    static float blk[2 * 128];
    const double per_sample = bpm / 60.0 / VT_SAMPLE_RATE;
    int n = 0;
    for (int b = 0; b < (int)(sec * VT_SAMPLE_RATE / 128); b++) {
        dsp_set_transport(vt, *beat);
        for (int i = 0; i < 128; i++) {
            double bt = *beat + i * per_sample;
            double f = bt < switch_beat ? f_before : f_after;
            blk[2 * i] = blk[2 * i + 1] = f > 0 ? 0.4f * (float)sin(2.0 * M_PI * f * bt / per_sample / VT_SAMPLE_RATE) : 0.0f;
        }
        dsp_process_block_f(vt, blk, 128);
        if (cap) for (int i = 0; i < 128; i++) cap[n++] = blk[2 * i];
        *beat += 128 * per_sample;
    }
    return n;
}

static void test_freeze_downbeat(void)
{
    /* 1 Bar at 120 BPM (2 s). Rec turned mid-bar arms; the take starts on the
     * exact downbeat - the voice changes from 330 to 440 Hz right there, and
     * none of the 330 ends up in the loop - and the loop keeps the bar grid. */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Robot");
    dsp_set_param(vt, "mix", "0");                                 /* a clean window onto the input */
    dsp_set_param(vt, "freeze_len", "1 Bar");
    dsp_set_bpm(vt, 120.0f);
    double beat = 1.3;
    char v[16];
    run_transport(vt, &beat, 120.0f, 330.0, 4.0, 440.0, 0.1, NULL);
    CHECK(dsp_set_param(vt, "freeze", "Rec") == 0);
    run_transport(vt, &beat, 120.0f, 330.0, 4.0, 440.0, 0.05, NULL);
    scope_t sc = read_scope(vt);
    CHECK(sc.frozen == 3);                                         /* armed */
    dsp_get_param(vt, "freeze", v, sizeof v); CHECK(strcmp(v, "Rec") == 0);
    run_transport(vt, &beat, 120.0f, 330.0, 4.0, 440.0, (3.95 - beat) / 2.0, NULL);   /* to just before the downbeat */
    CHECK(read_scope(vt).frozen == 3);                             /* still waiting */
    run_transport(vt, &beat, 120.0f, 330.0, 4.0, 440.0, 0.2, NULL);
    sc = read_scope(vt);
    CHECK(sc.frozen == 2);                                         /* recording since the downbeat */
    run_transport(vt, &beat, 120.0f, 330.0, 4.0, 440.0, 2.0, NULL);   /* past the end of the bar */
    dsp_get_param(vt, "freeze", v, sizeof v); CHECK(strcmp(v, "Loop") == 0);
    sc = read_scope(vt);
    CHECK(sc.frozen == 1 && fabs(sc.loop_s - 2.0) < 0.002);
    static float cap[3 * VT_SAMPLE_RATE];
    int n = run_transport(vt, &beat, 120.0f, 0.0, 0.0, 0.0, 2.0, cap);    /* the mic falls silent: only the loop */
    double a440 = band_at(cap, n, 440.0), a330 = band_at(cap, n, 330.0);
    if (!(a440 > 1000.0 * a330)) printf("  downbeat take: 440 %.2e vs pre-downbeat 330 %.2e\n", a440, a330);
    CHECK(a440 > 1000.0 * a330);
    /* on the grid: at each downbeat the loop is at its start */
    for (int k = 0; k < 2; k++) {
        double next = ceil(beat / 4.0) * 4.0;
        run_transport(vt, &beat, 120.0f, 0.0, 0.0, 0.0, (next - beat) / 2.0, NULL);   /* to the downbeat (block-rounded) */
        sc = read_scope(vt);
        double off = sc.phase > 0.5 ? sc.phase - 1.0 : sc.phase;
        if (fabs(off) * 2.0 > 0.004) printf("  loop vs downbeat: %.4f of the loop (%.1f ms)\n", off, off * 2000.0);
        CHECK(fabs(off) * 2.0 < 0.004);                            /* within a block of the bar line */
        run_transport(vt, &beat, 120.0f, 0.0, 0.0, 0.0, 0.5, NULL);
    }
    dsp_cleanup(vt);

    /* Off while armed cancels: nothing is recorded, and Loop has nothing to play. */
    vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "freeze_len", "2 Bars");
    beat = 0.5;
    dsp_set_param(vt, "freeze", "Rec");
    run_transport(vt, &beat, 120.0f, 330.0, 0.0, 330.0, 0.1, NULL);
    CHECK(read_scope(vt).frozen == 3);
    dsp_set_param(vt, "freeze", "Off");
    run_transport(vt, &beat, 120.0f, 330.0, 0.0, 330.0, 2.0, NULL);   /* across the downbeat */
    CHECK(read_scope(vt).frozen == 0);
    dsp_set_param(vt, "freeze", "Loop");
    run_transport(vt, &beat, 120.0f, 330.0, 0.0, 330.0, 0.2, NULL);
    CHECK(read_scope(vt).frozen == 0);                             /* no take: live */
    dsp_cleanup(vt);

    /* Transport stopped, or Free: Rec records at once. */
    for (int k = 0; k < 2; k++) {
        vt = dsp_init(VT_SAMPLE_RATE);
        long pos = 0;
        if (k == 0) {
            dsp_set_transport(vt, -1.0);
            dsp_set_param(vt, "freeze", "Rec");
            run_tone(vt, 220.0, 0.3f, 0.05, &pos, NULL);
        } else {
            dsp_set_param(vt, "freeze_len", "Free");
            beat = 1.0;
            dsp_set_param(vt, "freeze", "Rec");
            run_transport(vt, &beat, 120.0f, 220.0, 0.0, 220.0, 0.05, NULL);
        }
        sc = read_scope(vt);
        if (sc.frozen != 2) printf("  %s: freeze state %d right after Rec\n", k ? "Free with transport" : "transport stopped", sc.frozen);
        CHECK(sc.frozen == 2);
        dsp_cleanup(vt);
    }
}

static void test_snap_no_clicks(void)
{
    /* Read-head protection: across hard pitch jumps (120 <-> 400 Hz) and pitch
     * ratio jumps, no grain ever reads a sample that hasn't been written yet
     * (that read the ring's ~90 ms-old contents: a click). Checked before
     * every sample, for every sounding grain. */
    static cher_t c;
    static psola_voice v;
    cher_init(&c, VT_SAMPLE_RATE);
    cher_set_params(&c, 0, VT_SCALE_CHROMATIC, 0.0f, -60.0f, -1, 0.0f);
    psola_init(&v);
    long ahead = 0, reads = 0;
    double ph = 0;
    for (long i = 0; i < 4 * VT_SAMPLE_RATE; i++) {
        double t = (double)i / VT_SAMPLE_RATE, f0 = ((long)(t * 4) % 2) ? 400.0 : 120.0;
        ph += 2 * M_PI * f0 / VT_SAMPLE_RATE;
        double x = 0;
        for (int h = 1; h * f0 < 8000; h++) x += sin(h * ph) / h;
        cher_push(&c, (float)(0.2 * x));
        for (int k = 0; k < PSOLA_MAX_GRAINS; k++) {
            const psola_grain *g = &v.g[k];
            if (!g->active) continue;
            reads++;
            if (g->start + (double)(g->t * g->rate) > c.n_written - 1.0 + 1e-6) ahead++;
        }
        float T0 = c.voiced ? VT_SAMPLE_RATE / c.f0 : 0.0f;
        float ratio = ((long)(t * 3) % 2) ? 1.6f : 0.7f;           /* the shift jumps too */
        psola_process(&v, &c, T0, ratio, ((long)(t * 5) % 2) ? 1.4f : 1.0f, 0.0f);
    }
    if (ahead) printf("  psola: %ld of %ld reads past the newest sample\n", ahead, reads);
    CHECK(ahead == 0 && reads > 100000);

    /* No hunting: a voice wobbling +-9 ct around the midpoint between two
     * semitones (C#4/D4) holds one note (stickiness 20 ct holds +-10 ct there; the old 10 ct flipped on this). */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Snap");
    static float blk[2 * 128];
    double phase = 0;
    int flips = 0;
    float last = -1.0f;
    for (int b = 0; b < 3 * VT_SAMPLE_RATE / 128; b++) {
        for (int i = 0; i < 128; i++) {
            double t = (double)(b * 128 + i) / VT_SAMPLE_RATE;
            double midi = 61.5 + 0.09 * sin(2 * M_PI * 5.5 * t);
            phase += 2 * M_PI * 440.0 * pow(2.0, (midi - 69.0) / 12.0) / VT_SAMPLE_RATE;
            double x = 0;
            for (int h = 1; h <= 6; h++) x += sin(h * phase) / h;
            blk[2 * i] = blk[2 * i + 1] = (float)(0.3 * x);
        }
        dsp_process_block_f(vt, blk, 128);
        float tg = viz_target(vt);
        if (b > VT_SAMPLE_RATE / 128 && tg > 0.0f) {
            if (last > 0.0f && fabsf(tg - last) > 1.0f) flips++;
            last = tg;
        }
    }
    if (flips) printf("  snap hunting: %d note flips in 2 s of +-9 ct wobble on a semitone edge\n", flips);
    CHECK(flips == 0);
    dsp_cleanup(vt);

    /* A Retune-0 pad jump (with its phase reset) puts no step in the output:
     * the largest sample-to-sample change around it stays near the steady
     * sine's own slope on either note (relative: Snap's telephone band takes
     * a 220 Hz sine down ~16 dB, and a click is broadband, so it would pass
     * the band). Same for Beauty 0's instant retune (retune < 0). */
    static fx_t fx;
    enum { NN = 2 * VT_SAMPLE_RATE, AT = 128 * 345 };
    static float out[NN];
    const float rts[2] = { 0.0f, -1.0f };
    for (int k = 0; k < 2; k++) {
        snap_run(&fx, &c, 0.5f, rts[k], src_sine_220, 57, 62, AT, NN, out);
        float worst = 0.0f, slope = 0.0f;
        for (int i = AT - 2000; i < AT + 4000; i++) worst = fmaxf(worst, fabsf(out[i] - out[i - 1]));
        for (int i = AT - 12000; i < AT - 6000; i++) slope = fmaxf(slope, fabsf(out[i] - out[i - 1]));
        for (int i = AT + 20000; i < AT + 26000; i++) slope = fmaxf(slope, fabsf(out[i] - out[i - 1]));
        if (!(worst < 1.3f * slope)) printf("  snap jump (retune %g): worst sample step %.4f, steady slope %.4f\n", rts[k], worst, slope);
        CHECK(worst < 1.3f * slope);
    }
}

/* A rough sung vowel for the tracker: f0 with +-30 ct vibrato, 1.5% jitter,
 * 10% shimmer per cycle, breath noise; vowel = 0 plain, 1 with F1 on the 2nd
 * harmonic (the case where H2 steals detection). leaps: octave up every other
 * half second. Returns octave-error frames; *followed = leaps caught within
 * 80 ms. */
static int octave_errors(double base, int vowel_h2, int leaps, int *followed)
{
    static cher_t c;
    cher_init(&c, VT_SAMPLE_RATE);
    cher_set_params(&c, 0, VT_SCALE_CHROMATIC, 0.0f, -60.0f, -1, 0.0f);
    unsigned seed = 17;
    double ph = 0, cyc = 0, jit = 0, shim = 1;
    float last = -1.0f;
    int errors = 0, caught = 0;
    long leap_at = -1;
    int leap_open = 0;
    double leap_target = 0;
    for (long i = 0; i < 6L * VT_SAMPLE_RATE; i++) {
        double t = (double)i / VT_SAMPLE_RATE;
        double f0 = base * pow(2.0, 0.3 / 12.0 * sin(2 * M_PI * 5.5 * t));
        int up = leaps && ((long)(t * 2) % 2);
        if (up) f0 *= 2.0;
        double fj = f0 * (1.0 + jit);
        ph += 2 * M_PI * fj / VT_SAMPLE_RATE;
        if (ph - cyc >= 2 * M_PI) { cyc += 2 * M_PI; jit = 0.015 * lcg_noise(&seed); shim = 1.0 + 0.1 * lcg_noise(&seed); }
        double x = 0;
        for (int h = 1; h * fj < 8000; h++) {
            double a = 1.0 / h;
            if (vowel_h2) {
                double fh = h * fj, F[3] = { 280, 2250, 2900 };
                a = 0;
                for (int k = 0; k < 3; k++) a += (k ? 0.4 : 1.0) / (1.0 + pow((fh - F[k]) / 60.0, 2));
                a /= sqrt(h);
            }
            x += a * sin(h * ph);
        }
        cher_push(&c, (float)(0.15 * x * shim + 0.03 * lcg_noise(&seed)));
        if (leaps) {
            long k = (long)(t * 2);
            if (k != leap_at) { leap_at = k; leap_open = 1; leap_target = (k % 2) ? base * 2 : base; }
            if (leap_open && c.voiced && fabs(12.0 * log2(c.f0 / leap_target)) < 1.0) {
                if (t - k * 0.5 < 0.08) caught++;
                leap_open = 0;
            }
        }
        if (c.voiced && c.f0 != last && t > 0.3) {
            last = c.f0;
            double tt = t - 0.017;
            double tf = base * pow(2.0, 0.3 / 12.0 * sin(2 * M_PI * 5.5 * tt));
            if (leaps && ((long)(tt * 2) % 2)) tf *= 2.0;
            if (leaps && fmod(tt, 0.5) < 0.08) continue;           /* settling after a leap */
            double err = 12.0 * log2(c.f0 / tf);
            if (fabs(fabs(err) - 12.0) < 1.5) errors++;
        }
    }
    if (followed) *followed = caught;
    return errors;
}

static void test_tracker_octaves(void)
{
    /* A vowel with F1 on its 2nd harmonic used to be read an octave up in
     * ~5% of frames (the harmonic stole the period); the fundamental bias
     * and the octave-glitch hold keep it in its octave. */
    int e_h2 = octave_errors(140.0, 1, 0, NULL);
    int e_plain = octave_errors(200.0, 0, 0, NULL);
    if (e_h2 || e_plain) printf("  tracker octave errors: vowel F1-on-H2 %d, plain %d\n", e_h2, e_plain);
    CHECK(e_h2 == 0);
    CHECK(e_plain == 0);
    /* ...while real octave leaps are still followed, within 80 ms */
    int caught = 0;
    int e_leaps = octave_errors(150.0, 0, 1, &caught);
    if (!(caught >= 11 && e_leaps == 0)) printf("  tracker octave leaps: %d of 12 caught within 80 ms, %d stray octave frames\n", caught, e_leaps);
    CHECK(caught >= 11);
    CHECK(e_leaps == 0);
}

static void test_pads_silent_without_voice(void)
{
    /* Robot with a pad chord held (three notes, the polyphonic carrier) and no
     * voice - a silent mic, room noise, or -45 dBFS RMS mic hiss - is digital
     * silence, Auto On or Off; the carrier is only ever heard through a voice,
     * and singing plays it at once. */
    const float rooms[4] = { 0.0f, 0.0018f, 0.0056f, 0.0098f };    /* silent; RMS -60, -50, -45 dBFS */
    for (int au = 0; au < 2; au++)
        for (int r = 0; r < 4; r++) {
            vt_t *vt = dsp_init(VT_SAMPLE_RATE);
            dsp_set_param(vt, "mode", "Robot");
            dsp_set_param(vt, "auto", au ? "On" : "Off");
            note_on(vt, 60); note_on(vt, 64); note_on(vt, 67);
            unsigned seed = 11;
            static float blk[2 * 128];
            long nonzero = 0;
            for (int b = 0; b < 2 * VT_SAMPLE_RATE / 128; b++) {
                for (int i = 0; i < 128; i++) blk[2 * i] = blk[2 * i + 1] = rooms[r] * lcg_noise(&seed);
                dsp_process_block_f(vt, blk, 128);
                if (b > VT_SAMPLE_RATE / 4 / 128) for (int i = 0; i < 256; i++) nonzero += blk[i] != 0.0f;
            }
            if (nonzero) printf("  robot Auto %s, pad chord held, room %.4f: %ld non-zero samples\n", au ? "On" : "Off", rooms[r], nonzero);
            CHECK(nonzero == 0);
            long pos = 0;
            static float cap[VT_SAMPLE_RATE / 2];
            int n = run_tone(vt, 220.0, 0.3f, 0.5, &pos, cap);
            double lvl = 20.0 * log10(rms_of(cap + n / 2, n / 2) + 1e-12);
            if (!(lvl > -35.0)) printf("  robot Auto %s: singing over the chord %.1f dBFS\n", au ? "On" : "Off", lvl);
            CHECK(lvl > -35.0);
            dsp_cleanup(vt);
        }
}

static void test_beauty(void)
{
    /* Beauty widens the stereo picture in every mode it polishes (L/R
     * correlation falls from 0 to full), without changing the total level
     * much, and keeps pauses at exact digital silence. */
    const char *modes[4] = { "Snap", "Robot", "Chords", "Prism" };
    for (int m = 0; m < 4; m++) {
        double corr[2], pow_db[2];
        for (int k = 0; k < 2; k++) {
            vt_t *vt = dsp_init(VT_SAMPLE_RATE);
            dsp_set_param(vt, "mode", modes[m]);
            dsp_set_param(vt, "spread", "0");
            dsp_set_param(vt, "beauty", k ? "1" : "0");
            if (m != 0) note_on(vt, 60);
            long pos = 0;
            static float blk[2 * 128];
            double sxy = 0, sxx = 0, syy = 0, pw = 0;
            long n = 0;
            for (int b = 0; b < 2 * VT_SAMPLE_RATE / 128; b++) {
                for (int i = 0; i < 128; i++, pos++) blk[2 * i] = blk[2 * i + 1] = 0.3f * tone(220.0, (int)pos, 6, VT_SAMPLE_RATE);
                dsp_process_block_f(vt, blk, 128);
                if (b > VT_SAMPLE_RATE / 128)
                    for (int i = 0; i < 128; i++, n++) {
                        double x = blk[2 * i], y = blk[2 * i + 1];
                        sxy += x * y; sxx += x * x; syy += y * y; pw += 0.5 * (x * x + y * y);
                    }
            }
            corr[k] = sxy / sqrt(sxx * syy + 1e-30);
            pow_db[k] = 10.0 * log10(pw / n + 1e-30);
            dsp_cleanup(vt);
        }
        if (!(corr[1] < corr[0] - 0.005 && fabs(pow_db[1] - pow_db[0]) < 3.0))
            printf("  beauty %s: L/R corr %.3f -> %.3f, level %+.1f dB\n", modes[m], corr[0], corr[1], pow_db[1] - pow_db[0]);
        CHECK(corr[1] < corr[0] - 0.005);                          /* wider */
        CHECK(fabs(pow_db[1] - pow_db[0]) < 3.0);                  /* not much louder or quieter */
    }

    /* Robot: Beauty puts a sub-octave under the carrier (C4 pad -> C3 energy). */
    double sub[2];
    for (int k = 0; k < 2; k++) {
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "mode", "Robot");
        dsp_set_param(vt, "beauty", k ? "1" : "0");
        note_on(vt, 60);
        long pos = 0;
        unsigned seed = 4;
        static float blk[2 * 128], cap[VT_SAMPLE_RATE];
        int n = 0;
        for (int b = 0; b < 2 * VT_SAMPLE_RATE / 128; b++) {
            for (int i = 0; i < 128; i++, pos++) blk[2 * i] = blk[2 * i + 1] = 0.2f * lcg_noise(&seed);
            dsp_process_block_f(vt, blk, 128);
            if (b > VT_SAMPLE_RATE / 128) for (int i = 0; i < 128 && n < VT_SAMPLE_RATE; i++) cap[n++] = 0.5f * (blk[2 * i] + blk[2 * i + 1]);
        }
        sub[k] = band_at(cap, n, 130.81) / (band_at(cap, n, 261.63) + 1e-30);
        dsp_cleanup(vt);
    }
    if (!(sub[1] > 4.0 * sub[0])) printf("  beauty robot sub: C3/C4 %.3f -> %.3f\n", sub[0], sub[1]);
    CHECK(sub[1] > 4.0 * sub[0]);

    /* Robot's pauses stay digital silence at any Beauty (pad chord held,
     * -50 dBFS room). */
    const char *gated[1] = { "Robot" };
    const char *bv[3] = { "0", "0.5", "1" };
    for (int m = 0; m < 1; m++)
        for (int k = 0; k < 3; k++)
            for (int au = 0; au < 2; au++) {
                vt_t *vt = dsp_init(VT_SAMPLE_RATE);
                dsp_set_param(vt, "mode", gated[m]);
                dsp_set_param(vt, "beauty", bv[k]);
                dsp_set_param(vt, "auto", au ? "On" : "Off");
                note_on(vt, 60); note_on(vt, 64); note_on(vt, 67);
                unsigned seed = 8;
                static float blk[2 * 128];
                long nonzero = 0;
                for (int b = 0; b < VT_SAMPLE_RATE / 128; b++) {
                    for (int i = 0; i < 128; i++) blk[2 * i] = blk[2 * i + 1] = 0.0056f * lcg_noise(&seed);
                    dsp_process_block_f(vt, blk, 128);
                    if (b > VT_SAMPLE_RATE / 4 / 128) for (int i = 0; i < 256; i++) nonzero += blk[i] != 0.0f;
                }
                if (nonzero) printf("  beauty %s %s Auto %s: %ld non-zero samples in a pause\n", gated[m], bv[k], au ? "On" : "Off", nonzero);
                CHECK(nonzero == 0);
                dsp_cleanup(vt);
            }
}

/* Knobs that do nothing in a mode (Key, Scale, Retune, Glide, Beauty) show
 * "N/A" there (canvas.js, tested in canvas_test.mjs against this same table)
 * and ignore turns: the stored value waits for the next mode that uses it. A
 * restored state, and any write in the first second after load, still set
 * them whatever the mode. */
static void test_inert_knobs(void)
{
    static const char *const modes[VT_MODE_COUNT] = {"Robot", "Snap", "Chords", "Prism", "Tape", "Nightcall", "Scream", "Dalek", "HardTune", "Harmony", "Walkie", "Morph", "Double"};
    static const struct { int id; const char *key, *a, *b; int enum_; int acts[VT_MODE_COUNT]; } k[5] = {
        /*                                                         Robot Snap Chords Prism Tape Night Scream Dalek HardT Harm Walkie Morph Double */
        { VT_P_KEY,    "key",    "D",     "A",     1, { 1, 1, 1, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1 } },
        { VT_P_SCALE,  "scale",  "Major", "Minor", 1, { 0, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0, 1, 1 } },
        { VT_P_RETUNE, "retune", "100",   "300",   0, { 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1 } },
        { VT_P_GLIDE,  "glide",  "100",   "300",   0, { 1, 1, 1, 1, 0, 1, 0, 0, 1, 0, 0, 1, 1 } },
        { VT_P_BEAUTY, "beauty", "0.3",   "0.9",   0, { 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    };
    static float zero[2 * 128];
    char buf[64];
#define SAME(vt, key, want) (dsp_get_param((vt), (key), buf, sizeof buf) > 0 &&         (k[i].enum_ ? strcmp(buf, (want)) == 0 : fabs(atof(buf) - atof(want)) < 1e-3))
    for (int i = 0; i < 5; i++)
        for (int m = 0; m < VT_MODE_COUNT; m++) {
            CHECK(vt_param_acts(k[i].id, m) == k[i].acts[m]);
            vt_t *vt = dsp_init(VT_SAMPLE_RATE);

            /* just loaded: writes land whatever the mode (a host may restore key by key) */
            dsp_set_param(vt, "mode", modes[m]);
            dsp_set_param(vt, k[i].key, k[i].a);
            CHECK(SAME(vt, k[i].key, k[i].a));

            for (int b = 0; b < VT_SAMPLE_RATE / 128 + 2; b++) dsp_process_block_f(vt, zero, 128);   /* loaded */
            CHECK(dsp_set_param(vt, k[i].key, k[i].b) == 0);
            CHECK(SAME(vt, k[i].key, k[i].acts[m] ? k[i].b : k[i].a));                     /* applied, or ignored */
            dsp_set_param(vt, "mode", "Snap");                                             /* the waiting value is back */
            CHECK(SAME(vt, k[i].key, k[i].acts[m] ? k[i].b : k[i].a));

            /* a restored state sets it in any mode, in either key order */
            char st[160];
            dsp_set_param(vt, "mode", modes[m]);
            snprintf(st, sizeof st, "{\"%s\":\"%s\",\"mode\":\"Tape\"}", k[i].key, k[i].b);
            dsp_set_param(vt, "state", st);
            CHECK(SAME(vt, k[i].key, k[i].b));
            snprintf(st, sizeof st, "{\"mode\":\"%s\",\"%s\":\"%s\"}", modes[m], k[i].key, k[i].a);
            dsp_set_param(vt, "state", st);
            CHECK(SAME(vt, k[i].key, k[i].a));
            dsp_cleanup(vt);
        }
#undef SAME

    /* every other knob acts in every mode */
    for (int id = 0; id < VT_P_COUNT; id++) {
        int gated = 0;
        for (int i = 0; i < 5; i++) gated |= id == k[i].id;
        if (!gated) for (int m = 0; m < VT_MODE_COUNT; m++) CHECK(vt_param_acts(id, m) == 1);
    }

    vt_t *w = dsp_init(VT_SAMPLE_RATE);                             /* the widgets get mode as an extra key */
    static char cp[16384];
    CHECK(dsp_get_param(w, "chain_params", cp, sizeof cp) > 0);
    for (int i = 0; i < 5; i++) {
        char kind[48];
        snprintf(kind, sizeof kind, "custom:tinyvoice_%s", k[i].key);
        CHECK(strstr(cp, kind) != NULL);
    }
    dsp_cleanup(w);
}

static void test_mode_switch_clean(void)
{
    /* Cycling the modes on a sustained voice (a pad held) never cuts one
     * engine's waveform into another's: the old engine fades out over 5 ms,
     * the new one takes over at the next block and fades in. The largest
     * sample step in the 40 ms after each switch stays near the steady
     * waveform's own (hard cuts were up to 16x; Robot <-> Morph worst). */
    static const char *const names[] = {"Robot", "Snap", "Chords", "Prism", "Tape", "Nightcall", "Scream", "Dalek", "HardTune", "Harmony", "Walkie", "Morph", "Double"};
    static const int order[] = { 1, 0, 2, 11, 3, 12, 4, 1, 4, 0, 1, 3, 0, 11, 0, 12, 2, 1,       /* the original tour of the old modes */
                                 5, 6, 7, 8, 9, 10, 0, 5, 1, 9, 2, 10, 3, 6, 4, 7, 12, 8, 11, 5, 0 };   /* then the new ones, between old ones */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    note_on(vt, 60);
    long pos = 0;
    static float blk[2 * 128];
    float prev = 0.0f;
    for (int k = 0; k < (int)(sizeof order / sizeof order[0]); k++) {
        dsp_set_param(vt, "mode", names[order[k]]);
        double jump_sw = 0, jump_st = 0;
        for (int b = 0; b < (int)(0.6 * VT_SAMPLE_RATE / 128); b++) {
            for (int i = 0; i < 128; i++, pos++) blk[2 * i] = blk[2 * i + 1] = tone(220.0, (int)pos, 6, VT_SAMPLE_RATE);
            dsp_process_block_f(vt, blk, 128);
            for (int i = 0; i < 128; i++) {
                const long t = (long)b * 128 + i;
                const double d = fabs(blk[2 * i] - prev);
                prev = blk[2 * i];
                if (t < VT_SAMPLE_RATE / 25) { if (d > jump_sw) jump_sw = d; }
                else if (t > VT_SAMPLE_RATE * 2 / 5 && d > jump_st) jump_st = d;
            }
        }
        if (k == 0) continue;                                      /* the tone's own start */
        if (!(jump_sw < 3.0 * jump_st))
            printf("  %s -> %s: step %.3f at the switch, %.3f steady\n", names[order[k - 1]], names[order[k]], jump_sw, jump_st);
        CHECK(jump_sw < 3.0 * jump_st);
    }

    /* A knob swept across several modes lands on the last one only. */
    dsp_set_param(vt, "mode", "Tape");
    run_tone(vt, 220.0, 0.3f, 0.2, &pos, NULL);
    dsp_set_param(vt, "mode", "Robot");
    run_tone(vt, 220.0, 0.3f, 128.0 / VT_SAMPLE_RATE, &pos, NULL);  /* one block: still fading out */
    dsp_set_param(vt, "mode", "Chords");
    run_tone(vt, 220.0, 0.3f, 128.0 / VT_SAMPLE_RATE, &pos, NULL);
    dsp_set_param(vt, "mode", "Snap");
    run_tone(vt, 220.0, 0.3f, 0.1, &pos, NULL);
    char v[32];
    dsp_get_param(vt, "mode", v, sizeof v);
    CHECK(strcmp(v, "Snap") == 0);
    CHECK(viz_target(vt) > 200.0);                                 /* Snap is running and tracking */
    dsp_cleanup(vt);
}

static void test_robot_phrase_onset(void)
{
    /* Robot (pad held): a phrase after a pause starts at the level it
     * settles to. In a pause the output is silent, so the leveller used to
     * climb to full makeup and the input AGC's follower decayed, and every
     * phrase opened 5-8 dB hot into the limiter for ~20 ms. The first phrase
     * ever (cold) is held to the same bound. */
    const double gaps[4] = { 0.0, 1.0, 3.0, 6.0 };
    for (int au = 0; au < 2; au++) {
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "auto", au ? "On" : "Off");
        dsp_set_param(vt, "mode", "Robot");
        note_on(vt, 60);
        static float blk[2 * 128];
        long pos = 0;
        for (int g = 0; g < 4; g++) {
            for (int b = 0; b < (int)(gaps[g] * VT_SAMPLE_RATE / 128); b++) {
                memset(blk, 0, sizeof blk);
                dsp_process_block_f(vt, blk, 128);
            }
            double pk_on = 0, pk_st = 0;
            for (int b = 0; b < VT_SAMPLE_RATE / 128; b++) {
                for (int i = 0; i < 128; i++, pos++) {
                    const long t = (long)b * 128 + i;
                    const float env = t < 220 ? 0.5f - 0.5f * cosf((float)M_PI * (float)t / 220.0f) : 1.0f;
                    blk[2 * i] = blk[2 * i + 1] = 0.8f * env * tone(220.0, (int)pos, 6, VT_SAMPLE_RATE);
                }
                dsp_process_block_f(vt, blk, 128);
                for (int i = 0; i < 128; i++) {
                    const long t = (long)b * 128 + i;
                    const double y = fabs(blk[2 * i]);
                    if (t < VT_SAMPLE_RATE / 50) { if (y > pk_on) pk_on = y; }
                    else if (t > VT_SAMPLE_RATE / 2 && y > pk_st) pk_st = y;
                }
            }
            const double over = 20.0 * log10(pk_on / (pk_st + 1e-12));
            if (!(over < 4.5 && pk_st > 0.1))
                printf("  robot Auto %s after %.0f s: onset %+.1f dB over steady (peak %.3f)\n", au ? "On" : "Off", gaps[g], over, pk_st);
            CHECK(over < 4.5 && pk_st > 0.1);
        }
        dsp_cleanup(vt);
    }
}

/* Robot output with pads held over a broadband voice (noise), Beauty 0 (no
 * sub-octave), as mono for spectral checks. */
static int robot_pads_capture(const int *notes, int nn, float *cap, int cap_n)
{
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Robot");
    dsp_set_param(vt, "beauty", "0");
    for (int k = 0; k < nn; k++) note_on(vt, notes[k]);
    unsigned seed = 21;
    static float blk[2 * 128];
    int n = 0;
    for (int b = 0; b < 2 * VT_SAMPLE_RATE / 128; b++) {
        for (int i = 0; i < 128; i++) blk[2 * i] = blk[2 * i + 1] = 0.2f * lcg_noise(&seed);
        dsp_process_block_f(vt, blk, 128);
        if (b >= VT_SAMPLE_RATE / 128)
            for (int i = 0; i < 128 && n < cap_n; i++) cap[n++] = 0.5f * (blk[2 * i] + blk[2 * i + 1]);
    }
    dsp_cleanup(vt);
    return n;
}

static void test_robot_carrier_register(void)
{
    /* Move's pads sit around E5 and up. Robot plays held pads with their
     * names and voicing but folded by whole octaves, as a block, until their
     * mean is in C3..B4: an E5 carrier sat above most of the voice's
     * formants, so no words came through, just a high tone (device report). */
    static float cap[VT_SAMPLE_RATE];
    const double C3 = 130.81, C4 = 261.63, E4 = 329.63, E5 = 659.26, G4 = 392.00, G5 = 783.99;

    const int e5[1] = { 76 };                                      /* E5 -> E4 */
    int n = robot_pads_capture(e5, 1, cap, VT_SAMPLE_RATE);
    double e4 = band_at(cap, n, E4), e5e = band_at(cap, n, E5);
    if (!(e4 > 0.3 * e5e)) printf("  robot E5 pad: E4 %.3g vs E5 %.3g (not folded)\n", e4, e5e);
    CHECK(e4 > 0.3 * e5e);

    const int c4[1] = { 60 };                                      /* in range: as played */
    n = robot_pads_capture(c4, 1, cap, VT_SAMPLE_RATE);
    double c3 = band_at(cap, n, C3), c4e = band_at(cap, n, C4);
    if (!(c3 < 0.05 * c4e)) printf("  robot C4 pad: C3 %.3g vs C4 %.3g (moved)\n", c3, c4e);
    CHECK(c3 < 0.05 * c4e);

    const int chord[3] = { 76, 79, 83 };                           /* E5 G5 B5 -> E4 G4 B4 */
    n = robot_pads_capture(chord, 3, cap, VT_SAMPLE_RATE);
    double g4 = band_at(cap, n, G4), g5 = band_at(cap, n, G5);
    if (!(g4 > 0.3 * g5)) printf("  robot E5-G5-B5 chord: G4 %.3g vs G5 %.3g\n", g4, g5);
    CHECK(g4 > 0.3 * g5);
}

static void test_input_hpf(void)
{
    /* Mic pre-conditioning: a 1-pole high-pass at 120 Hz on the whole input.
     * Through the dry path (Mix 0): 1 kHz passes (within 0.2 dB), a 40 Hz
     * pad thump is ~10 dB down. */
    double lv[2];
    const double fq[2] = { 1000.0, 40.0 };
    for (int k = 0; k < 2; k++) {
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "mode", "Robot");
        dsp_set_param(vt, "mix", "0");
        dsp_set_param(vt, "auto", "Off");
        static float blk[2 * 128];
        double e = 0;
        long pos = 0, cnt = 0;
        for (int b = 0; b < VT_SAMPLE_RATE / 128; b++) {
            for (int i = 0; i < 128; i++, pos++) blk[2 * i] = blk[2 * i + 1] = 0.3f * (float)sin(2 * M_PI * fq[k] * pos / VT_SAMPLE_RATE);
            dsp_process_block_f(vt, blk, 128);
            if (b > VT_SAMPLE_RATE / 2 / 128) for (int i = 0; i < 128; i++, cnt++) e += blk[2 * i] * blk[2 * i];
        }
        lv[k] = 20.0 * log10(sqrt(e / cnt) / (0.3 / sqrt(2.0)));
        dsp_cleanup(vt);
    }
    if (!(fabs(lv[0]) < 0.2 && lv[1] < -9.0)) printf("  input hpf: 1 kHz %+.2f dB, 40 Hz %+.1f dB\n", lv[0], lv[1]);
    CHECK(fabs(lv[0]) < 0.2);
    CHECK(lv[1] < -9.0);

    /* Robot with a pad held, Auto Off, Gate -38 dB: pad thumps (40 Hz) at
     * -30 dBFS RMS used to open the gate and play the carrier; now they stay
     * digital silence. A sung A3 at the same level still opens it. */
    for (int v = 0; v < 2; v++) {
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "mode", "Robot");
        dsp_set_param(vt, "auto", "Off");
        note_on(vt, 60);
        static float blk[2 * 128];
        long pos = 0, nonzero = 0;
        const float amp = 0.0447f;                                 /* -30 dBFS RMS */
        for (int b = 0; b < VT_SAMPLE_RATE / 128; b++) {
            for (int i = 0; i < 128; i++, pos++) {
                const double t = (double)pos / VT_SAMPLE_RATE;
                blk[2 * i] = blk[2 * i + 1] = amp * (float)sin(2 * M_PI * (v ? 220.0 : 40.0) * t);
            }
            dsp_process_block_f(vt, blk, 128);
            if (b > VT_SAMPLE_RATE / 4 / 128) for (int i = 0; i < 256; i++) nonzero += blk[i] != 0.0f;
        }
        if (v == 0 && nonzero) printf("  robot: 40 Hz thumps at -30 dBFS opened the gate (%ld samples)\n", nonzero);
        if (v == 1 && !nonzero) printf("  robot: a sung A3 at -30 dBFS no longer opens the gate\n");
        CHECK(v == 0 ? nonzero == 0 : nonzero > 0);
        dsp_cleanup(vt);
    }
}

/* Robot over a broadband voice, Beauty 0: energy at f relative to the whole. */
static double robot_band_after(vt_t *vt, double f, unsigned *seed)
{
    static float blk[2 * 128], cap[VT_SAMPLE_RATE / 2];
    int n = 0;
    for (int b = 0; b < VT_SAMPLE_RATE / 128; b++) {
        for (int i = 0; i < 128; i++) blk[2 * i] = blk[2 * i + 1] = 0.2f * lcg_noise(seed);
        dsp_process_block_f(vt, blk, 128);
        if (b >= VT_SAMPLE_RATE / 2 / 128) for (int i = 0; i < 128 && n < VT_SAMPLE_RATE / 2; i++) cap[n++] = 0.5f * (blk[2 * i] + blk[2 * i + 1]);
    }
    double tot = 0;
    for (int i = 0; i < n; i++) tot += (double)cap[i] * cap[i];
    return band_at(cap, n, f) / (tot + 1e-20);
}

static int scope_pad(vt_t *vt)                                    /* the scope's held-pad field */
{
    char buf[1024];
    dsp_get_param(vt, "scope", buf, sizeof buf);
    char *p = buf + strlen(buf);
    for (int c = 0; c < 3 && p > buf; ) if (*--p == ',') c++;
    return atoi(p + 1);
}

static void test_latch(void)
{
    /* Latch (Robot, Chords, Prism): the pad chord keeps playing after the
     * pads are let go; pads pressed after letting go replace it, pads
     * pressed while others are held add to it; Latch Off releases it at
     * once. With Latch Off nothing changes. */
    const double E4 = 329.63, A4 = 440.0;
    unsigned seed = 5;
    double off_rel, on_rel, on_new_e4, on_new_a4, after_off;
    {   /* Latch Off: let go of C-E-G and the drone (C3 G3 C4) takes over: no E4 */
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "mode", "Robot");
        dsp_set_param(vt, "beauty", "0");
        note_on(vt, 60); note_on(vt, 64); note_on(vt, 67);
        robot_band_after(vt, E4, &seed);
        note_off(vt, 60); note_off(vt, 64); note_off(vt, 67);
        off_rel = robot_band_after(vt, E4, &seed);
        dsp_cleanup(vt);
    }
    {
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "mode", "Robot");
        dsp_set_param(vt, "beauty", "0");
        dsp_set_param(vt, "latch", "On");
        note_on(vt, 60); note_on(vt, 64); note_on(vt, 67);
        robot_band_after(vt, E4, &seed);
        note_off(vt, 60); note_off(vt, 64); note_off(vt, 67);
        on_rel = robot_band_after(vt, E4, &seed);                  /* still C-E-G */
        CHECK(dsp_active_voices(vt) == 0);                        /* the pads themselves are up */
        CHECK(scope_pad(vt) == 67);                                /* the screen shows the latch */
        note_on(vt, 65); note_on(vt, 69); note_on(vt, 72);         /* a new chord: F-A-C */
        note_off(vt, 65); note_off(vt, 69); note_off(vt, 72);
        on_new_e4 = robot_band_after(vt, E4, &seed);
        on_new_a4 = robot_band_after(vt, A4, &seed);
        dsp_set_param(vt, "latch", "Off");                         /* releases: the drone again */
        after_off = robot_band_after(vt, A4, &seed);
        CHECK(scope_pad(vt) == -1);
        dsp_cleanup(vt);
    }
    if (!(on_rel > 10.0 * off_rel && on_new_e4 < 0.1 * on_rel && on_new_a4 > 10.0 * after_off))
        printf("  latch: E4 after release off %.2g / on %.2g; new chord E4 %.2g A4 %.2g; latch off A4 %.2g\n",
               off_rel, on_rel, on_new_e4, on_new_a4, after_off);
    CHECK(on_rel > 10.0 * off_rel);                                /* held after release */
    CHECK(on_new_e4 < 0.1 * on_rel);                               /* replaced, not added */
    CHECK(on_new_a4 > 10.0 * after_off);                           /* Latch Off let it go */

    /* Pressing while another pad is held adds to the chord; a press after
     * letting go starts a new one; a fifth note drops the oldest. */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", "Chords");
    dsp_set_param(vt, "latch", "On");
    static float blk[2 * 128];
    memset(blk, 0, sizeof blk);
    dsp_process_block_f(vt, blk, 128);
    note_on(vt, 60); note_on(vt, 64); note_off(vt, 60); note_on(vt, 67); note_off(vt, 64); note_off(vt, 67);
    dsp_process_block_f(vt, blk, 128);
    char d[512];
    dsp_get_param(vt, "diag", d, sizeof d);
    CHECK(strstr(d, "latch 60 64 67 -1") != NULL);                 /* overlapping presses: one chord */
    CHECK(scope_pad(vt) == 67);
    note_on(vt, 62); note_off(vt, 62);                             /* after letting go: a new chord */
    dsp_get_param(vt, "diag", d, sizeof d);
    CHECK(strstr(d, "latch 62 -1 -1 -1") != NULL);
    note_off(vt, 62);
    note_on(vt, 50); note_on(vt, 52); note_on(vt, 53); note_on(vt, 55); note_on(vt, 57);
    dsp_get_param(vt, "diag", d, sizeof d);
    CHECK(strstr(d, "latch 52 53 55 57") != NULL);                 /* five held: the oldest drops */
    uint8_t panic[3] = { 0xB0, 123, 0 };                           /* all notes off releases the latch */
    dsp_handle_midi(vt, panic, 3);
    dsp_get_param(vt, "diag", d, sizeof d);
    if (!strstr(d, "latch -1 -1 -1 -1")) printf("  latch diag: %s\n", d);
    CHECK(strstr(d, "latch -1 -1 -1 -1") != NULL);

    /* Turning Latch On while pads are held takes them as the chord. */
    dsp_set_param(vt, "latch", "Off");
    dsp_process_block_f(vt, blk, 128);
    note_on(vt, 57); note_on(vt, 60);
    dsp_set_param(vt, "latch", "On");
    dsp_process_block_f(vt, blk, 128);
    note_off(vt, 57); note_off(vt, 60);
    dsp_process_block_f(vt, blk, 128);
    dsp_get_param(vt, "diag", d, sizeof d);
    CHECK(strstr(d, "latch 57 60 -1 -1") != NULL);

    /* Lead modes don't latch: in Snap a released pad hands the target back
     * to the voice even with Latch On. */
    dsp_set_param(vt, "mode", "Snap");
    long pos = 0;
    run_tone(vt, 220.0, 0.3f, 0.4, &pos, NULL);
    CHECK(fabs(viz_target(vt) - 220.0) < 1.0);
    CHECK(scope_pad(vt) == -1);
    dsp_cleanup(vt);
}

static void test_consonants_with_rumble(void)
{
    /* Built-in mic: chassis hum (40 Hz) and pad thumps under the speech,
     * through the input high-pass, leave Robot's consonant detector as it
     * is clean: s and t (and k after its closure) read as consonants,
     * sustained vowels never do. */
    static const double ah[3] = { 730, 1090, 2440 }, ee[3] = { 270, 2290, 3010 };
    static daft_t d;
    daft_init(&d, VT_SAMPLE_RATE);
    daft_set_auto(&d, 1);
    daft_set_params(&d, 0, 8.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.0f, -60.0f);
    const float hpa = expf(-2.0f * (float)M_PI * VT_INPUT_HPF_HZ / VT_SAMPLE_RATE);
    unsigned seed = 77;
    float h1x = 0, h1y = 0, h2x = 0, h2y = 0, lp1 = 0, lp2 = 0, hx = 0, hy = 0;
    double thump = -1;
    long n = 0;
    const int kind[] = { 0, 4, 1, 7, 5, 0, 7, 6, 1, 2, 3, 0 };    /* as test_robot_consonants_and_articulation */
    const double sec[] = { 1.0, .15, .4, .04, .025, .3, .04, .03, .4, .4, .4, .3 };
    double vowel_max = 0, s_max = 0, t_max = 0, k_max = 0;
    for (int g = 0; g < 12; g++) {
        long len = (long)(sec[g] * VT_SAMPLE_RATE);
        float hpk = expf(-2.0f * (float)M_PI * (kind[g] == 4 ? 4000.0f : kind[g] == 5 ? 2500.0f : 1500.0f) / VT_SAMPLE_RATE);
        float lpa = 1.0f - expf(-2.0f * (float)M_PI * 4000.0f / VT_SAMPLE_RATE);
        for (long i = 0; i < len; i++, n++) {
            float x = 0.0f, w = lcg_noise(&seed);
            float y1 = hpk * (h1y + w - h1x); h1x = w; h1y = y1;
            float y2 = hpk * (h2y + y1 - h2x); h2x = y1; h2y = y2;
            switch (kind[g]) {
            case 0: x = vowel3(140, ah, n); break;
            case 1: x = vowel3(140, ee, n); break;
            case 2: x = vowel3(140, ee, n) + 0.03f * y2; break;
            case 3: x = vowel3(220, ee, n); break;
            case 4: x = 0.25f * y2; break;
            case 5: x = 0.5f * y2 * expf(-(float)i / (0.008f * VT_SAMPLE_RATE)); break;
            case 6: lp1 += (y2 - lp1) * lpa; lp2 += (lp1 - lp2) * lpa; x = 0.6f * lp2 * expf(-(float)i / (0.01f * VT_SAMPLE_RATE)); break;
            default: x = 0.0005f * w;
            }
            const double t = (double)n / VT_SAMPLE_RATE;              /* hum + a thump every 370 ms */
            x += 0.05f * (float)sin(2 * M_PI * 40.0 * t);
            if (fmod(t, 0.37) < 1.0 / VT_SAMPLE_RATE) thump = t;
            if (thump >= 0 && t - thump < 0.12) x += 0.2f * (float)(sin(2 * M_PI * 55.0 * (t - thump)) * exp(-(t - thump) / 0.03));
            const float yh = hpa * (hy + x - hx); hx = x; hy = yh;
            daft_process(&d, yh);
            int settled = i > (long)(0.05 * VT_SAMPLE_RATE);
            if (kind[g] <= 3 && settled && d.sib > vowel_max) vowel_max = d.sib;
            if (kind[g] == 4 && d.sib > s_max) s_max = d.sib;
            if (kind[g] == 5 && d.sib > t_max) t_max = d.sib;
            if (kind[g] == 6 && d.sib > k_max) k_max = d.sib;
        }
    }
    if (!(vowel_max < 0.05 && s_max > 0.9 && t_max > 0.6 && k_max > 0.4))
        printf("  consonants with rumble: vowels %.3f, s %.2f, t %.2f, k %.2f\n", vowel_max, s_max, t_max, k_max);
    CHECK(vowel_max < 0.05);
    CHECK(s_max > 0.9);
    CHECK(t_max > 0.6);
    CHECK(k_max > 0.4);
}

static void test_gate_unified(void)
{
    /* One Gate knob: option 0 is Auto, options 1..21 are -60..0 dB in 3 dB
     * steps (Auto Off with that threshold). The old "auto" key still works:
     * On = Auto, Off = back to the last manual level (-39 dB until one is
     * chosen). */
    char v[64];
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_get_param(vt, "gate", v, sizeof v); CHECK(strcmp(v, "Auto") == 0);   /* the default */
    dsp_get_param(vt, "auto", v, sizeof v); CHECK(strcmp(v, "On") == 0);
    dsp_set_param(vt, "auto", "Off");
    dsp_get_param(vt, "gate", v, sizeof v); CHECK(strcmp(v, "-39 dB") == 0);
    dsp_set_param(vt, "gate", "-45 dB");                           /* by name */
    dsp_get_param(vt, "gate", v, sizeof v); CHECK(strcmp(v, "-45 dB") == 0);
    dsp_set_param(vt, "auto", "On");
    dsp_get_param(vt, "gate", v, sizeof v); CHECK(strcmp(v, "Auto") == 0);
    dsp_set_param(vt, "auto", "Off");                              /* remembers -45 */
    dsp_get_param(vt, "gate", v, sizeof v); CHECK(strcmp(v, "-45 dB") == 0);
    dsp_set_param(vt, "gate", "0");                                /* index 0: Auto (the host's enum index) */
    dsp_get_param(vt, "gate", v, sizeof v); CHECK(strcmp(v, "Auto") == 0);
    dsp_set_param(vt, "gate", "8");                                /* index 8: -39 dB */
    dsp_get_param(vt, "gate", v, sizeof v); CHECK(strcmp(v, "-39 dB") == 0);
    dsp_set_param(vt, "gate", "-41 dB");                           /* off the grid, by name or number: the nearest step */
    dsp_get_param(vt, "gate", v, sizeof v); CHECK(strcmp(v, "-42 dB") == 0);
    dsp_set_param(vt, "gate", "-38");
    dsp_get_param(vt, "gate", v, sizeof v); CHECK(strcmp(v, "-39 dB") == 0);
    dsp_set_param(vt, "gate", "-12.4");                            /* a dB number (old sets, tests) */
    dsp_get_param(vt, "gate", v, sizeof v); CHECK(strcmp(v, "-12 dB") == 0);
    dsp_set_param(vt, "gate", "-99");                              /* clamps to -60 */
    dsp_get_param(vt, "gate", v, sizeof v); CHECK(strcmp(v, "-60 dB") == 0);
    dsp_set_param(vt, "gate", "99");                               /* clamps to the last option */
    dsp_get_param(vt, "gate", v, sizeof v); CHECK(strcmp(v, "0 dB") == 0);
    CHECK(dsp_set_param(vt, "gate", "loud") < 0);                  /* junk refused, value kept */
    dsp_get_param(vt, "gate", v, sizeof v); CHECK(strcmp(v, "0 dB") == 0);
    dsp_cleanup(vt);

    /* Sets saved before (an "auto" switch and a dB "gate"), either order. */
    const struct { const char *json, *want; } old[4] = {
        { "{\"mode\":\"Snap\",\"auto\":\"On\",\"gate\":-38}",  "Auto"   },
        { "{\"mode\":\"Snap\",\"auto\":\"Off\",\"gate\":-44}", "-45 dB" },
        { "{\"gate\":-50,\"auto\":\"Off\"}",                   "-51 dB" },
        { "{\"gate\":-50,\"auto\":\"On\"}",                    "Auto"   },
    };
    for (int k = 0; k < 4; k++) {
        vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "state", old[k].json);
        dsp_get_param(vt, "gate", v, sizeof v);
        if (strcmp(v, old[k].want) != 0) printf("  old set %s -> gate %s (want %s)\n", old[k].json, v, old[k].want);
        CHECK(strcmp(v, old[k].want) == 0);
        dsp_set_param(vt, "auto", "Off");                          /* the old manual level was remembered */
        dsp_get_param(vt, "gate", v, sizeof v);
        CHECK(strcmp(v, k == 0 ? "-39 dB" : k == 1 ? "-45 dB" : "-51 dB") == 0);   /* nearest 3 dB step */
        dsp_cleanup(vt);
    }

    /* New sets save Gate by name and no "auto". */
    vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "gate", "-42 dB");
    static char st[2048];
    dsp_get_param(vt, "state", st, sizeof st);
    CHECK(strstr(st, "\"gate\":\"-42 dB\"") != NULL && strstr(st, "\"auto\"") == NULL);
    dsp_cleanup(vt);

    /* What it does: every option either is Auto (the automatic front end)
     * or opens the voice gate at exactly its level. A tone 3 dB over the
     * threshold opens it and 3 dB under keeps Robot silent (Auto: -42 dBFS). */
    const int levels[4] = { 0, -51, -39, -21 };                    /* 0 = Auto */
    for (int k = 0; k < 4; k++)
        for (int over = 0; over < 2; over++) {
            vt = dsp_init(VT_SAMPLE_RATE);
            dsp_set_param(vt, "mode", "Robot");
            if (levels[k]) { char g[16]; snprintf(g, sizeof g, "%d dB", levels[k]); dsp_set_param(vt, "gate", g); }
            note_on(vt, 60);
            const double thr = levels[k] ? levels[k] : -42.0;
            const float amp = (float)(pow(10.0, (thr + (over ? 3.0 : -3.0)) / 20.0) * sqrt(2.0));
            static float blk[2 * 128];
            long pos = 0, nonzero = 0;
            for (int b = 0; b < VT_SAMPLE_RATE / 128; b++) {
                for (int i = 0; i < 128; i++, pos++) blk[2 * i] = blk[2 * i + 1] = amp * (float)sin(2 * M_PI * 440.0 * pos / VT_SAMPLE_RATE);
                dsp_process_block_f(vt, blk, 128);
                if (b > VT_SAMPLE_RATE / 4 / 128) for (int i = 0; i < 256; i++) nonzero += blk[i] != 0.0f;
            }
            if ((nonzero > 0) != over) printf("  gate %d dB: a tone %+d dB from it %s\n", levels[k], over ? 3 : -3, nonzero ? "opened" : "stayed shut");
            CHECK((nonzero > 0) == over);
            dsp_cleanup(vt);
        }
}

/* Instances share nothing (no globals but the host pointer): one singing,
 * one hearing silence, run interleaved block by block. The silent one stays
 * at exact zeros and never reports a pitch; the singing one is bit-identical
 * to running alone. */
static void test_instances_isolated(void)
{
    vt_t *a = dsp_init(VT_SAMPLE_RATE), *b = dsp_init(VT_SAMPLE_RATE), *solo = dsp_init(VT_SAMPLE_RATE);
    static float ba[2 * 128], bb[2 * 128], bs[2 * 128];
    int same = 1, b_silent = 1, b_pitch = 0;
    unsigned seed = 7;
    for (int k = 0; k < VT_SAMPLE_RATE / 128; k++) {
        for (int i = 0; i < 128; i++) {
            long p = (long)k * 128 + i;
            ba[2 * i] = ba[2 * i + 1] = bs[2 * i] = bs[2 * i + 1] = sig_voice(p, &seed);
            bb[2 * i] = bb[2 * i + 1] = 0.0f;
        }
        dsp_process_block_f(a, ba, 128);
        dsp_process_block_f(b, bb, 128);
        dsp_process_block_f(solo, bs, 128);
        same &= memcmp(ba, bs, sizeof ba) == 0;
        if (k > 3 * VT_SAMPLE_RATE / 10 / 128) {
            for (int i = 0; i < 256; i++) b_silent &= bb[i] == 0.0f;
            char vz[96];
            float f0 = 0;
            dsp_get_param(b, "viz", vz, sizeof vz);
            sscanf(vz, "c,%f", &f0);
            b_pitch |= f0 > 0.0f;
        }
    }
    CHECK(same);
    CHECK(b_silent);
    CHECK(!b_pitch);
    dsp_cleanup(a);
    dsp_cleanup(b);
    dsp_cleanup(solo);
}

/* Gain of fx_snap_voicing at f, in dB (steady sine). */
static double snap_band_db(double f)
{
    static fx_t fx;
    fx_init(&fx, VT_SAMPLE_RATE);
    enum { N = VT_SAMPLE_RATE / 2 };
    static float y[N];
    for (int i = 0; i < N; i++) y[i] = fx_snap_voicing(&fx, 0.1f * (float)sin(2.0 * M_PI * f * i / VT_SAMPLE_RATE));
    return 20.0 * log10(rms_of(y + N / 2, N / 2) / (0.1 / sqrt(2.0)) + 1e-12);
}

/* Output pitch of `mode` on a steady sine at f, measured over [t0, t1) s. */
static double mode_pitch(const char *mode, const char *beauty, double f, double t0, double t1)
{
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    dsp_set_param(vt, "mode", mode);
    dsp_set_param(vt, "gate", "-60 dB");
    dsp_set_param(vt, "retune", "500");
    dsp_set_param(vt, "glide", "500");
    dsp_set_param(vt, "beauty", beauty);
    dsp_set_param(vt, "spread", "0");
    enum { N = VT_SAMPLE_RATE / 2 };
    static float out[N];
    long pos = 0;
    run_sine(vt, f, 0.3f, N / 128, &pos, out, N);
    dsp_cleanup(vt);
    return zc_freq(out + (int)(t0 * VT_SAMPLE_RATE), (int)((t1 - t0) * VT_SAMPLE_RATE));
}

static void test_snap_space_age(void)
{
    /* The telephone band: 4th-order 350 Hz high-pass (chest gone), 4th-order
     * 5 kHz low-pass (air gone), a bite at 2.5 kHz. */
    const double g100 = snap_band_db(100.0), g1k = snap_band_db(1000.0);
    const double g2k5 = snap_band_db(2500.0), g10k = snap_band_db(10000.0);
    if (!(g100 < g1k - 30.0 && g10k < g1k - 18.0 && g2k5 > g1k + 2.0))
        printf("  snap band: 100 Hz %+.1f, 1 kHz %+.1f, 2.5 kHz %+.1f, 10 kHz %+.1f dB\n", g100, g1k, g2k5, g10k);
    CHECK(g100 < g1k - 30.0);
    CHECK(g10k < g1k - 18.0);
    CHECK(g2k5 > g1k + 2.0);

    /* Snap only: a low voice's fundamental is stripped in Snap and kept in
     * Morph and Double (same tracker, same note). */
    double share[3];
    static const char *const ms[3] = { "Snap", "Morph", "Double" };
    for (int m = 0; m < 3; m++) {
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "mode", ms[m]);
        dsp_set_param(vt, "gate", "-60 dB");
        dsp_set_param(vt, "spread", "0");
        enum { N = VT_SAMPLE_RATE };
        static float out[N];
        static float blk[2 * 128];
        int k = 0;
        for (long p = 0; p < N; p += 128) {
            for (int i = 0; i < 128; i++) blk[2 * i] = blk[2 * i + 1] = 0.5f * tone(146.832, (int)(p + i), 6, VT_SAMPLE_RATE);
            dsp_process_block_f(vt, blk, 128);
            for (int i = 0; i < 128 && k < N; i++) out[k++] = blk[2 * i];
        }
        const double r = rms_of(out + N / 2, N / 2);
        share[m] = 10.0 * log10(2.0 * goertzel(out + N / 2, N / 2, 146.832) / (r * r + 1e-20) + 1e-20);
        dsp_cleanup(vt);
    }
    if (!(share[0] < share[1] - 20.0 && share[0] < share[2] - 20.0))
        printf("  fundamental share: Snap %.1f, Morph %.1f, Double %.1f dB\n", share[0], share[1], share[2]);
    CHECK(share[0] < share[1] - 20.0);
    CHECK(share[0] < share[2] - 20.0);

    /* The formant sits +2.5 st up at char1 0.5: brighter than with the lift
     * cancelled, on a voice that needs no correction. */
    static cher_t c;
    static fx_t fx;
    enum { NV = VT_SAMPLE_RATE };
    static float vout[NV];
    snap_run(&fx, &c, 0.5f, 0.0f, src_vowel_220, 57, 57, NV, NV, vout);
    const double lifted = harmonic_centroid(vout + NV - 8192, 8192, 220.0);
    snap_run(&fx, &c, 0.5f - SNAP_FORMANT_LIFT / 24.0f, 0.0f, src_vowel_220, 57, 57, NV, NV, vout);
    const double flat = harmonic_centroid(vout + NV - 8192, 8192, 220.0);
    if (!(lifted > 1.04 * flat)) printf("  snap formant lift: centroid %.0f Hz vs %.0f Hz without\n", lifted, flat);
    CHECK(lifted > 1.04 * flat);

    /* Beauty 0: Snap retunes instantly, whatever Retune (500 ms) and Glide
     * say. A sine 46 ct under A#3 is on A#3 at once; at Beauty 0.5 it is
     * still sliding, and Morph ignores Beauty. */
    const double asharp = 233.082, f_in = 227.0;
    const double inst = cents(mode_pitch("Snap", "0", f_in, 0.15, 0.3), asharp);
    const double slow = cents(mode_pitch("Snap", "0.5", f_in, 0.15, 0.3), asharp);
    const double morph = cents(mode_pitch("Morph", "0", f_in, 0.15, 0.3), asharp);
    if (!(fabs(inst) < 8.0 && slow < -15.0 && morph < -15.0))
        printf("  instant retune: Snap Beauty 0 %+.1f ct, Beauty 0.5 %+.1f ct, Morph Beauty 0 %+.1f ct\n", inst, slow, morph);
    CHECK(fabs(inst) < 8.0);
    CHECK(slow < -15.0);
    CHECK(morph < -15.0);
}

/* ------------------------------------------------- pending engine tests */

#include "voice_tests.inc"

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);              /* a crash must not swallow the last test name */
    RUN(test_lifecycle);
    RUN(test_param_defaults_and_roundtrip);
    RUN(test_midi_voices);
    RUN(test_process_sane);
    RUN(test_viz_feed);
    RUN(test_pitch_tracking_accuracy);
    RUN(test_quantizer);
    RUN(test_snap_to_scale);
    RUN(test_cher_through_engine);
    RUN(test_dry_wet_alignment);
    RUN(test_midi_target_override);
    RUN(test_midi_override_glide);
    RUN(test_daft_follows_modulator);
    RUN(test_daft_silent_without_carrier);
    RUN(test_daft_drone);
    RUN(test_daft_auto_front_end);
    RUN(test_cher_quiet_input);
    RUN(test_daft_sibilance);
    RUN(test_filterbank_stability);
    RUN(test_daft_through_engine);
    RUN(test_dry_passthrough);
    RUN(test_imogen_auto_chord);
    RUN(test_imogen_midi_chord_and_lead);
    RUN(test_imogen_spread);
    RUN(test_imogen_through_engine);
    RUN(test_cher_auto_gate_rejects_hum);
    RUN(test_root_follow);
    RUN(test_tone_and_spread);
    RUN(test_daft_auto_off_through_engine);
    RUN(test_psola_shifter);
    RUN(test_mode_snap_and_morph);
    RUN(test_mode_prism);
    RUN(test_mode_double);
    RUN(test_mode_tape);
    RUN(test_freeze_looper);
    RUN(test_demo_mode);
    RUN(test_robot_formant_pan_ensemble);
    RUN(test_scope_feed);
    RUN(test_served_metadata);
    RUN(test_auto_agc_and_gate);
    RUN(test_pad_octave_fold);
    RUN(test_snap_believe);
    RUN(test_macro_labels);
    RUN(test_demo_snap_shared);
    RUN(test_robot_drone_key);
    RUN(test_pad_sets_chord_root);
    RUN(test_demo_level_and_flow);
    RUN(test_chords_register_and_bleed);
    RUN(test_chords_manual_level_and_gate);
    RUN(test_freeze_bars);
    RUN(test_freeze_downbeat);
    RUN(test_snap_no_clicks);
    RUN(test_tracker_octaves);
    RUN(test_pads_silent_without_voice);
    RUN(test_beauty);
    RUN(test_inert_knobs);
    RUN(test_mode_switch_clean);
    RUN(test_robot_phrase_onset);
    RUN(test_robot_carrier_register);
    RUN(test_input_hpf);
    RUN(test_latch);
    RUN(test_consonants_with_rumble);
    RUN(test_gate_unified);
    RUN(test_instances_isolated);
    RUN(test_snap_space_age);
    RUN(test_robot_consonants_and_articulation);
    RUN(test_snap_scale_switch);
    RUN(test_auto_silence_gate);
    RUN(test_robot_silent_pauses_and_key);
    RUN(test_snap_pads_any_channel);
    RUN(test_midi_channel_filter);
    RUN(test_state_roundtrip);
    RUN(test_module_json_matches_params);

    /* TinyVoice: the voice-transformation roster (tests/voice_tests.inc) */
    RUN(test_voice_roster);
    RUN(test_macro_slots);
    RUN(test_hilbert_pair);
    RUN(test_dalek);
    RUN(test_walkie);
    RUN(test_harmony);
    RUN(test_scream);
    RUN(test_hardtune);
    RUN(test_nightcall);
    RUN(test_prism_glass);
    RUN(test_voicefx);
    RUN(test_voicefx_isolation);
    RUN(test_voicefx_transitions);
    RUN(test_voicefx_numeric_safety);
    RUN(test_voicefx_presets);
    RUN(test_groove);
    RUN(test_voicefx_audition);
    RUN(test_midi_agnostic_modes);
    RUN(test_latency_budget);
    RUN(test_headroom);
    RUN(test_all_modes_numeric_safety);

    CHECK(g_live_allocs == 0);   /* global leak check after everything */

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

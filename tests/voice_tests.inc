/*
 * TinyVoice tests for the voice-transformation roster (Nightcall, Scream,
 * Dalek, HardTune, Harmony, Walkie), Prism's glass, the VOICE page's macro
 * slots, the -6.1 dBFS headroom stage, and numeric safety over every mode.
 * #included by test_main.c just before main(): it shares that file's helpers.
 */
#include "voice.h"
#include "ssb.h"
#include "vfx.h"
#include "presets.h"
#include "groove.h"

#define ALL_MODES_N 13
static const char *const k_all_modes[ALL_MODES_N] = {
    "Robot", "Snap", "Chords", "Prism", "Tape", "Nightcall", "Scream", "Dalek", "HardTune", "Harmony", "Walkie", "Morph", "Double"
};
static const char *const k_new_modes[6] = { "Nightcall", "Scream", "Dalek", "HardTune", "Harmony", "Walkie" };

/* A rig with Auto off (raw levels), no Spread, nothing but the engine: the five
 * macros as given. */
static vt_t *vm_rig(const char *mode, float c1, float c2, float md, float c4, float c5)
{
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    char b[32];
    dsp_set_param(vt, "mode", mode);
    dsp_set_param(vt, "gate", "-60 dB");
    dsp_set_param(vt, "spread", "0");
    snprintf(b, sizeof b, "%g", (double)c1); dsp_set_param(vt, "char1", b);
    snprintf(b, sizeof b, "%g", (double)c2); dsp_set_param(vt, "char2", b);
    snprintf(b, sizeof b, "%g", (double)md); dsp_set_param(vt, "mod", b);
    snprintf(b, sizeof b, "%g", (double)c4); dsp_set_param(vt, "char4", b);
    snprintf(b, sizeof b, "%g", (double)c5); dsp_set_param(vt, "char5", b);
    return vt;
}

typedef float (*vm_src)(long i, void *ctx);

/* Run `sec` seconds of a source through vt; both channels to l / r (if given). Returns frames. */
static int vm_run(vt_t *vt, vm_src f, void *ctx, double sec, long *pos, float *l, float *r)
{
    static float blk[2 * 128];
    int n = 0;
    for (int b = 0; b < (int)(sec * VT_SAMPLE_RATE / 128); b++) {
        for (int i = 0; i < 128; i++) blk[2 * i] = blk[2 * i + 1] = f((*pos)++, ctx);
        dsp_process_block_f(vt, blk, 128);
        for (int i = 0; i < 128; i++) { if (l) l[n] = blk[2 * i]; if (r) r[n] = blk[2 * i + 1]; n++; }
    }
    return n;
}

typedef struct { double f, amp; } vm_sine_t;
static float vm_sine(long i, void *c) { const vm_sine_t *s = c; return (float)(s->amp * sin(2.0 * M_PI * s->f * (double)i / VT_SAMPLE_RATE)); }
typedef struct { double f, amp; int nh; } vm_tone_t;
static float vm_tone(long i, void *c) { const vm_tone_t *t = c; return t->amp * tone(t->f, (int)i, t->nh, VT_SAMPLE_RATE); }
typedef struct { double f0, F, amp; } vm_vowel_t;
static float vm_vowel(long i, void *c) { const vm_vowel_t *v = c; return (float)(v->amp / 0.3) * vowel(v->f0, v->F, (int)i); }
static float vm_noise(long i, void *c) { (void)i; unsigned *s = c; return 0.3f * lcg_noise(s); }
static float vm_noise_lo(long i, void *c) { (void)i; unsigned *s = c; return 0.01f * lcg_noise(s); }   /* quiet: stays in the linear part of a soft clip */

/* Mean Goertzel power over [lo, hi] at 24 even points: a per-Hz level, comparable between bands of any width. */
static double vm_bavg(const float *x, int n, double lo, double hi)
{
    double e = 0;
    for (int k = 0; k < 24; k++) e += goertzel(x, n, lo + (hi - lo) * (k + 0.5) / 24.0);
    return e / 24.0;
}

/* The lag (samples) in [lo, hi] where x best repeats, refined to a fraction. */
static double vm_period(const float *x, int n, double lo, double hi)
{
    double best = lo, bv = -2;
    for (double lag = lo; lag <= hi; lag += 0.25) { double v = nac(x, n, lag); if (v > bv) { bv = v; best = lag; } }
    return best;
}
static double vm_hz_from_period(double lag) { return VT_SAMPLE_RATE / lag; }

/* ------------------------------------------------------- roster and slots */

static void test_voice_roster(void)
{
    /* the Mode knob: the vocoder and carrier modes first (Nightcall added), then the transformers */
    const vt_param_def *d = dsp_param_def(VT_P_MODE);
    CHECK(d->num_options == VT_MODE_COUNT && VT_MODE_COUNT == ALL_MODES_N);
    for (int m = 0; m < ALL_MODES_N; m++) CHECK(strcmp(d->options[m], k_all_modes[m]) == 0);
    CHECK(VT_MODE_ROBOT == 0 && VT_MODE_SNAP == 1 && VT_MODE_CHORDS == 2 && VT_MODE_PRISM == 3 && VT_MODE_TAPE == 4 &&
          VT_MODE_NIGHTCALL == 5 && VT_MODE_SCREAM == 6 && VT_MODE_DALEK == 7 && VT_MODE_HARDTUNE == 8 &&
          VT_MODE_HARMONY == 9 && VT_MODE_WALKIE == 10);
    CHECK(strcmp(d->options[(int)d->def], "Snap") == 0);

    /* knob labels: five per mode, under 8 characters, never empty */
    for (int m = 0; m < VT_MODE_COUNT; m++)
        for (int k = 0; k < VT_MACROS; k++) {
            const char *n = dsp_macro_name(m, k);
            CHECK(n && n[0] && strlen(n) <= 8);
        }

    /* each mode runs and says so */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    char got[32];
    for (int m = 0; m < ALL_MODES_N; m++) {
        CHECK(dsp_set_param(vt, "mode", k_all_modes[m]) == 0);
        char idx[8];
        snprintf(idx, sizeof idx, "%d", m);
        dsp_set_param(vt, "mode", "Robot");
        CHECK(dsp_set_param(vt, "mode", idx) == 0 && dsp_get_param(vt, "mode", got, sizeof got) > 0 && strcmp(got, k_all_modes[m]) == 0);
    }
    /* a saved Talkbox is still unknown: rejected, the other members restored */
    dsp_set_param(vt, "mode", "Harmony");
    CHECK(dsp_set_param(vt, "state", "{\"mode\":\"Talkbox\",\"key\":\"D\"}") == 0);
    CHECK(dsp_get_param(vt, "mode", got, sizeof got) > 0 && strcmp(got, "Harmony") == 0);
    CHECK(dsp_get_param(vt, "key", got, sizeof got) > 0 && strcmp(got, "D") == 0);
    /* every new mode survives a state round trip */
    for (int k = 0; k < 6; k++) {
        vt_t *a = dsp_init(VT_SAMPLE_RATE), *b = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(a, "mode", k_new_modes[k]);
        dsp_set_param(a, "char4", "0.81");
        dsp_set_param(a, "char5", "0.19");
        char st[1024], v1[32], v2[32];
        CHECK(dsp_get_param(a, "state", st, sizeof st) > 0 && dsp_set_param(b, "state", st) == 0);
        CHECK(dsp_get_param(b, "mode", v1, sizeof v1) > 0 && strcmp(v1, k_new_modes[k]) == 0);
        CHECK(dsp_get_param(a, "char4", v1, sizeof v1) > 0 && dsp_get_param(b, "char4", v2, sizeof v2) > 0 && strcmp(v1, v2) == 0);
        CHECK(dsp_get_param(a, "char5", v1, sizeof v1) > 0 && dsp_get_param(b, "char5", v2, sizeof v2) > 0 && strcmp(v1, v2) == 0);
        dsp_cleanup(a); dsp_cleanup(b);
    }
    dsp_cleanup(vt);
}

/* char4 / char5 are, in the older modes, the param of that name on a 0..1 scale: one value, two knobs. */
static void test_macro_slots(void)
{
    static const struct { const char *mode, *a4, *a5; } t[ALL_MODES_N] = {
        { "Robot", "beauty", "glide" },   { "Snap", "retune", "beauty" },  { "Chords", "retune", "beauty" },
        { "Prism", NULL, NULL },          { "Tape", "tone", NULL }    ,     { "Nightcall", NULL, NULL },
        { "Scream", NULL, NULL },         { "Dalek", NULL, NULL },         { "HardTune", "glide", NULL },
        { "Harmony", NULL, NULL },        { "Walkie", NULL, NULL },        { "Morph", "retune", "glide" },
        { "Double", "retune", "glide" },
    };
    for (int m = 0; m < ALL_MODES_N; m++) {
        for (int slot = 0; slot < 2; slot++) {
            const char *key = slot ? "char5" : "char4", *alias = slot ? t[m].a5 : t[m].a4;
            vt_t *vt = dsp_init(VT_SAMPLE_RATE);
            dsp_set_param(vt, "mode", t[m].mode);
            static float z[2 * 128];
            for (int b = 0; b < VT_SAMPLE_RATE / 128 + 2; b++) dsp_process_block_f(vt, z, 128);   /* past the first second */
            char before[VT_P_COUNT][32], after[32], buf[32];
            for (int i = 0; i < VT_P_COUNT; i++) dsp_get_param(vt, dsp_param_def(i)->key, before[i], sizeof before[i]);
            CHECK(dsp_set_param(vt, key, "0.8") == 0);
            CHECK(dsp_get_param(vt, key, after, sizeof after) > 0 && fabs(atof(after) - 0.8) < 1e-3);
            int changed = 0;
            for (int i = 0; i < VT_P_COUNT; i++) {
                dsp_get_param(vt, dsp_param_def(i)->key, buf, sizeof buf);
                if (strcmp(buf, before[i])) {
                    changed++;
                    if (strcmp(dsp_param_def(i)->key, key) != 0) {              /* something else moved with it */
                        if (!alias) printf("  %s %s changed %s\n", t[m].mode, key, dsp_param_def(i)->key);
                        else CHECK(strcmp(dsp_param_def(i)->key, alias) == 0);
                    }
                }
            }
            if (alias) {
                const int id = dsp_param_index(alias);
                const vt_param_def *ad = dsp_param_def(id);
                CHECK(vt_param_acts(id, m));                                     /* an alias only ever stands for a knob that acts */
                dsp_get_param(vt, alias, buf, sizeof buf);
                CHECK(fabs(atof(buf) - (ad->min + 0.8 * (ad->max - ad->min))) < 0.01 * (ad->max - ad->min));
                /* the other way: the param's own knob moves the macro */
                char s[32];
                snprintf(s, sizeof s, "%g", ad->min + 0.25 * (ad->max - ad->min));
                CHECK(dsp_set_param(vt, alias, s) == 0);
                CHECK(dsp_get_param(vt, key, buf, sizeof buf) > 0 && fabs(atof(buf) - 0.25) < 1e-3);
            } else {
                CHECK(changed == 1);                                              /* its own storage: nothing else moves */
            }
            dsp_cleanup(vt);
        }
    }
}

/* ---------------------------------------------- numeric safety, every mode */

static float vm_wave(int kind, long i, unsigned *seed)
{
    switch (kind) {
    case 0: return 0.5f * tone(180.0, (int)i, 8, VT_SAMPLE_RATE);                        /* a voice */
    case 1: return 0.9f * lcg_noise(seed);                                                /* loud noise */
    case 2: return ((i / 147) & 1) ? 1.0f : -1.0f;                                        /* full-scale square, 150 Hz */
    case 3: return 0.5f + 0.4f * tone(240.0, (int)i, 4, VT_SAMPLE_RATE);                  /* DC offset */
    case 4: return (i % 700) == 0 ? 1.0f : 0.0f;                                          /* impulses */
    default: return 0.0f;                                                                 /* silence */
    }
}

static void test_all_modes_numeric_safety(void)
{
    /* Every mode x every kind of input x extreme knobs: always finite, never past the
     * soft limit, and silence afterwards decays without leaving denormals. */
    static const struct { float c1, c2, md, c4, c5, mix; const char *spread; } sets[3] = {
        { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, "0" },
        { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, "1" },
        { 0.5f, 0.5f, 0.0f, 0.5f, 0.5f, 0.5f, "0.25" },
    };
    static float blk[2 * 128];
    int bad = 0, subnormal = 0, over = 0;
    for (int m = 0; m < ALL_MODES_N; m++)
        for (int s = 0; s < 3; s++)
            for (int kind = 0; kind < 6; kind++) {
                vt_t *vt = vm_rig(k_all_modes[m], sets[s].c1, sets[s].c2, sets[s].md, sets[s].c4, sets[s].c5);
                char b[32];
                snprintf(b, sizeof b, "%g", (double)sets[s].mix);
                dsp_set_param(vt, "mix", b);
                dsp_set_param(vt, "spread", sets[s].spread);
                dsp_set_param(vt, "gain", "12");
                note_on(vt, 60); note_on(vt, 64); note_on(vt, 67);
                unsigned seed = 12345u + (unsigned)(m * 100 + kind);
                long pos = 0;
                for (int blkn = 0; blkn < 260; blkn++) {                                  /* ~0.75 s */
                    for (int i = 0; i < 128; i++, pos++) blk[2 * i] = blk[2 * i + 1] = vm_wave(kind, pos, &seed);
                    dsp_process_block_f(vt, blk, 128);
                    for (int i = 0; i < 256; i++) {
                        if (!isfinite(blk[i])) bad++;
                        else if (fabsf(blk[i]) > 1.0f) over++;
                    }
                }
                note_off(vt, 60); note_off(vt, 64); note_off(vt, 67);
                for (int blkn = 0; blkn < 420; blkn++) {                                  /* ~1.2 s of silence */
                    memset(blk, 0, sizeof blk);
                    dsp_process_block_f(vt, blk, 128);
                    if (blkn > 100)
                        for (int i = 0; i < 256; i++) { if (!isfinite(blk[i])) bad++; if (fpclassify(blk[i]) == FP_SUBNORMAL) subnormal++; }
                }
                dsp_cleanup(vt);
            }
    if (bad || over || subnormal) printf("  non-finite %d, past the limit %d, subnormal %d\n", bad, over, subnormal);
    CHECK(bad == 0);
    CHECK(over == 0);
    CHECK(subnormal == 0);

    /* the int16 path, with blocks of every size */
    static int16_t pcm[2 * 2000];
    static const int sizes[] = { 1, 7, 64, 128, 512, 1999 };
    for (int m = 5; m < 11; m++) {
        vt_t *vt = vm_rig(k_all_modes[m], 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
        for (size_t z = 0; z < sizeof sizes / sizeof sizes[0]; z++)
            for (int rep = 0; rep < 4; rep++) {
                for (int i = 0; i < sizes[z]; i++) pcm[2 * i] = pcm[2 * i + 1] = (int16_t)(((i * 37 + rep) & 1) ? 32767 : -32768);
                dsp_process_block(vt, pcm, sizes[z]);
            }
        dsp_cleanup(vt);
    }
}

/* ----------------------------------------------------------- headroom */

static void test_headroom(void)
{
    /* The new modes keep every peak under TINYVOICE_OUTPUT_HEADROOM (-6.1 dBFS) at Level 0 dB,
     * whatever the knobs and the input: resonant peaks, squelch bursts and distortion included. */
    CHECK(fabs(20.0 * log10((double)TINYVOICE_OUTPUT_HEADROOM) - (-6.1)) < 0.01);
    static float l[2 * VT_SAMPLE_RATE], r[2 * VT_SAMPLE_RATE];
    static const float knobs[3][5] = { { 0.5f, 0.5f, 0.0f, 0.5f, 0.5f }, { 1, 1, 1, 1, 1 }, { 0, 0, 0, 0, 0 } };
    double worst = 0.0;
    for (int k = 0; k < 6; k++)
        for (int s = 0; s < 3; s++)
            for (int kind = 0; kind < 4; kind++) {
                vt_t *vt = vm_rig(k_new_modes[k], knobs[s][0], knobs[s][1], knobs[s][2], knobs[s][3], knobs[s][4]);
                dsp_set_param(vt, "gate", "-30 dB");
                if (s == 1) dsp_set_param(vt, "spread", "1");
                note_on(vt, 64); note_on(vt, 67);
                unsigned seed = 99;
                long pos = 0;
                static float blk[2 * 128];
                int n = 0;
                for (int b = 0; b < 8 * VT_SAMPLE_RATE / 10 / 128; b++) {
                    for (int i = 0; i < 128; i++, pos++) {
                        /* on for 0.5 s, off for 0.1 s, on again: the squelch fires at both edges */
                        const long ph = pos % (VT_SAMPLE_RATE * 6 / 10);
                        const float on = ph < VT_SAMPLE_RATE / 2 ? 1.0f : 0.0f;
                        blk[2 * i] = blk[2 * i + 1] = on * vm_wave(kind, pos, &seed);
                    }
                    dsp_process_block_f(vt, blk, 128);
                    for (int i = 0; i < 128 && n < 8 * VT_SAMPLE_RATE / 10; i++, n++) { l[n] = blk[2 * i]; r[n] = blk[2 * i + 1]; }
                }
                double pk = 0;
                for (int i = 0; i < n; i++) { pk = fmax(pk, fabs(l[i])); pk = fmax(pk, fabs(r[i])); }
                if (pk > worst) worst = pk;
                if (pk > TINYVOICE_OUTPUT_HEADROOM) printf("  %s knobs %d input %d: peak %.4f (%.2f dBFS)\n", k_new_modes[k], s, kind, pk, 20 * log10(pk));
                CHECK(pk <= (double)TINYVOICE_OUTPUT_HEADROOM);
                dsp_cleanup(vt);
            }
    CHECK(worst > 0.3);                                            /* ... and the ceiling is reached: the modes are not silent */

    /* Level is above the stage: +12 dB gets past it (a deliberate boost), still soft-limited */
    vt_t *vt = vm_rig("Walkie", 0.5f, 1.0f, 0.0f, 0.5f, 0.5f);
    dsp_set_param(vt, "gain", "12");
    vm_tone_t tn = { 220.0, 0.9, 6 };
    long pos = 0;
    int n = vm_run(vt, vm_tone, &tn, 0.6, &pos, l, NULL);
    double pk = 0;
    for (int i = 0; i < n; i++) pk = fmax(pk, fabs(l[i]));
    CHECK(pk > TINYVOICE_OUTPUT_HEADROOM && pk <= 1.0);
    dsp_cleanup(vt);

    /* The older modes are not under it (they were left exactly as they were) */
    CHECK(!vt_mode_headroom(VT_MODE_ROBOT) && !vt_mode_headroom(VT_MODE_SNAP) && !vt_mode_headroom(VT_MODE_CHORDS) &&
          !vt_mode_headroom(VT_MODE_PRISM) && !vt_mode_headroom(VT_MODE_TAPE));

    /* Calibration: a voice at a working level (Auto on) lands usefully loud in every new mode, under the ceiling */
    for (int k = 0; k < 6; k++) {
        vt_t *c = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(c, "mode", k_new_modes[k]);
        vm_vowel_t vw = { 180.0, 800.0, 0.4 };
        long p = 0;
        vm_run(c, vm_vowel, &vw, 1.0, &p, NULL, NULL);
        n = vm_run(c, vm_vowel, &vw, 1.5, &p, l, NULL);
        double ss = 0, peak = 0;
        for (int i = 0; i < n; i++) { ss += (double)l[i] * l[i]; peak = fmax(peak, fabs(l[i])); }
        const double rms_db = 10.0 * log10(ss / n + 1e-12), peak_db = 20.0 * log10(peak + 1e-9);
        if (!(rms_db > -26.0 && rms_db < -14.0 && peak <= (double)TINYVOICE_OUTPUT_HEADROOM)) printf("  %s: rms %.1f dBFS, peak %.1f dBFS\n", k_new_modes[k], rms_db, peak_db);
        CHECK(rms_db > -26.0 && rms_db < -14.0);
        CHECK(peak <= (double)TINYVOICE_OUTPUT_HEADROOM);
        (void)peak_db;
        dsp_cleanup(c);
    }
}

/* ---------------------------------------------------- Hilbert pair, Dalek */

static void test_hilbert_pair(void)
{
    /* I and Q 90 degrees apart, equal in level, from 150 Hz to 6 kHz */
    static const double fr[] = { 150, 300, 600, 1000, 2000, 3500, 5000, 6000 };
    for (size_t k = 0; k < sizeof fr / sizeof fr[0]; k++) {
        ssb_hilbert h;
        memset(&h, 0, sizeof h);
        double si = 0, ci = 0, sq = 0, cq = 0;
        const int N = 30000, skip = 10000;
        for (int n = 0; n < N; n++) {
            float i, q;
            ssb_hilbert_tick(&h, (float)sin(2.0 * M_PI * fr[k] * n / VT_SAMPLE_RATE), &i, &q);
            if (n >= skip) {
                const double w = 2.0 * M_PI * fr[k] * n / VT_SAMPLE_RATE;
                si += i * sin(w); ci += i * cos(w); sq += q * sin(w); cq += q * cos(w);
            }
        }
        const double pi_ = atan2(ci, si), pq = atan2(cq, sq);
        double dphi = (pq - pi_) * 180.0 / M_PI;
        while (dphi > 180) dphi -= 360;
        while (dphi < -180) dphi += 360;
        const double gain_db = 20.0 * log10(hypot(sq, cq) / hypot(si, ci));
        if (fabs(fabs(dphi) - 90.0) > 2.0 || fabs(gain_db) > 0.3) printf("  hilbert %g Hz: %.2f deg, %.2f dB\n", fr[k], dphi, gain_db);
        CHECK(fabs(fabs(dphi) - 90.0) < 2.0);
        CHECK(fabs(gain_db) < 0.3);
        CHECK(fabs(hypot(si, ci) / ((N - skip) / 2.0) - 1.0) < 0.02);       /* all-pass: unity gain */
    }
}

static double dk_rel_db(const float *x, int n, double f, double ref) { return 10.0 * log10((goertzel(x, n, f) + 1e-20) / (goertzel(x, n, ref) + 1e-20)); }

static void test_dalek(void)
{
    static float l[VT_SAMPLE_RATE], r[VT_SAMPLE_RATE];
    vm_sine_t s = { 1000.0, 0.3 };
    const int N = VT_SAMPLE_RATE / 2;
    /* Shift: linear, -500 .. +500 Hz, 0.5 = none; ring off, Drive at its lowest */
    static const struct { float c1; double hz; } sh[] = { { 0.7f, 1200 }, { 0.2f, 700 }, { 1.0f, 1500 }, { 0.0f, 500 }, { 0.6f, 1100 } };
    for (size_t k = 0; k < sizeof sh / sizeof sh[0]; k++) {
        vt_t *vt = vm_rig("Dalek", sh[k].c1, 0.0f, 0.0f, 0.0f, 0.0f);
        long pos = 0;
        vm_run(vt, vm_sine, &s, 0.4, &pos, NULL, NULL);
        vm_run(vt, vm_sine, &s, 0.5, &pos, l, r);
        const double mirror = 2000.0 - sh[k].hz;                              /* the image across 1 kHz */
        const double up = dk_rel_db(l, N, sh[k].hz, 1000.0), image = dk_rel_db(l, N, mirror, sh[k].hz);
        if (up < 25.0 || image > -22.0) printf("  dalek c1 %.2f: shifted tone %.1f dB over the original, image %.1f dB\n", (double)sh[k].c1, up, image);
        CHECK(up > 25.0);                                                      /* the tone moved: nothing left at 1 kHz */
        CHECK(image < -22.0);                                                  /* single sideband: the other side is gone */
        dsp_cleanup(vt);
    }
    /* No shift, no ring: the voice at 1 kHz as it was */
    {
        vt_t *vt = vm_rig("Dalek", 0.5f, 0.0f, 0.0f, 0.0f, 0.0f);
        long pos = 0;
        vm_run(vt, vm_sine, &s, 0.4, &pos, NULL, NULL);
        vm_run(vt, vm_sine, &s, 0.5, &pos, l, r);
        CHECK(dk_rel_db(l, N, 1000.0, 1000.0) == 0.0 && dk_rel_db(l, N, 1100.0, 1000.0) < -40.0 && dk_rel_db(l, N, 900.0, 1000.0) < -40.0);
        dsp_cleanup(vt);
    }
    /* Ring (the classic 30 Hz at 0.5): both sidebands, nothing at the carrier */
    {
        vt_t *vt = vm_rig("Dalek", 0.5f, 0.5f, 0.0f, 0.0f, 0.0f);
        long pos = 0;
        vm_run(vt, vm_sine, &s, 0.4, &pos, NULL, NULL);
        vm_run(vt, vm_sine, &s, 0.5, &pos, l, r);
        const double lo = dk_rel_db(l, N, 970.0, 970.0), hi = dk_rel_db(l, N, 1030.0, 970.0);
        CHECK(fabs(hi - lo) < 1.5);
        CHECK(dk_rel_db(l, N, 1000.0, 970.0) < -15.0);
        dsp_cleanup(vt);
    }
    /* Width: L and R shifted apart (+- 12 Hz at 1) */
    {
        vt_t *vt = vm_rig("Dalek", 0.5f, 0.0f, 0.0f, 0.0f, 1.0f);
        long pos = 0;
        vm_run(vt, vm_sine, &s, 0.4, &pos, NULL, NULL);
        vm_run(vt, vm_sine, &s, 0.5, &pos, l, r);
        CHECK(dk_rel_db(l, N, 1012.0, 1000.0) > 20.0 && dk_rel_db(r, N, 988.0, 1000.0) > 20.0);
        dsp_cleanup(vt);
    }
    /* Metal (comb feedback) rings: a click tails out; and decays (never runs away) */
    {
        vt_t *vt = vm_rig("Dalek", 0.5f, 0.0f, 1.0f, 0.0f, 0.0f);
        static float blk[2 * 128];
        float peak_early = 0, peak_late = 0;
        for (int b = 0; b < 400; b++) {
            memset(blk, 0, sizeof blk);
            if (b == 0) blk[0] = blk[1] = 0.5f;
            dsp_process_block_f(vt, blk, 128);
            for (int i = 0; i < 128; i++) { if (b >= 1 && b < 6) peak_early = fmaxf(peak_early, fabsf(blk[2 * i])); if (b > 300) peak_late = fmaxf(peak_late, fabsf(blk[2 * i])); }
        }
        CHECK(peak_early > 1e-3f && peak_late < peak_early * 0.01f);
        dsp_cleanup(vt);
    }
}

/* ------------------------------------------------------------- Walkie */

static void test_walkie(void)
{
    static float l[2 * VT_SAMPLE_RATE];
    /* The presence peak: 1.8 kHz stands out of the band around it (low level, so the clipper stays linear) */
    {
        vt_t *vt = vm_rig("Walkie", 0.5f, 0.0f, 0.0f, 0.0f, 0.5f);
        unsigned seed = 7;
        long pos = 0;
        vm_run(vt, vm_noise, &seed, 0.5, &pos, NULL, NULL);
        const int n = vm_run(vt, vm_noise, &seed, 1.5, &pos, l, NULL);
        const double pk = vm_bavg(l, n, 1750.0, 1850.0), below = vm_bavg(l, n, 900.0, 1000.0), above = vm_bavg(l, n, 2700.0, 2800.0);
        const double lo_cut = vm_bavg(l, n, 80.0, 160.0);
        if (!(pk > 1.8 * below && pk > 1.8 * above)) printf("  walkie peak %.3e, below %.3e, above %.3e\n", pk, below, above);
        CHECK(pk > 1.8 * below && pk > 1.8 * above);                           /* a broad +3 dB or more */
        CHECK(lo_cut < 0.01 * pk);                                             /* the 350 Hz high-pass */
        dsp_cleanup(vt);
    }
    /* The horn follows Band: lower at 0, higher at 1 */
    for (int k = 0; k < 2; k++) {
        vt_t *vt = vm_rig("Walkie", 0.5f, 0.0f, 0.0f, 0.0f, k ? 1.0f : 0.0f);
        unsigned seed = 7;
        long pos = 0;
        vm_run(vt, vm_noise, &seed, 0.5, &pos, NULL, NULL);
        const int n = vm_run(vt, vm_noise, &seed, 1.5, &pos, l, NULL);
        const double want = k ? 2550.0 : 1270.0;
        const double at = vm_bavg(l, n, want * 0.97, want * 1.03), mid = vm_bavg(l, n, 1750.0, 1850.0), far = vm_bavg(l, n, k ? 1000.0 : 2800.0, k ? 1100.0 : 2900.0);
        if (!(at > 1.3 * mid)) printf("  walkie band %d: at %.3e  1.8k %.3e  far %.3e\n", k, at, mid, far);
        CHECK(at > 1.3 * mid);
        dsp_cleanup(vt);
    }
    /* Diode clipping: lopsided (unequal forward drops), so it makes even harmonics as well as odd; harder with Drive */
    double h2[2], h3[2];
    for (int k = 0; k < 2; k++) {
        vt_t *vt = vm_rig("Walkie", 0.0f, k ? 0.5f : 0.0f, 0.0f, 0.0f, 0.5f);
        vm_sine_t s = { 600.0, 0.4 };
        long pos = 0;
        vm_run(vt, vm_sine, &s, 0.4, &pos, NULL, NULL);
        const int n = vm_run(vt, vm_sine, &s, 0.5, &pos, l, NULL);
        h2[k] = goertzel(l, n, 1200.0) / goertzel(l, n, 600.0);
        h3[k] = goertzel(l, n, 1800.0) / goertzel(l, n, 600.0);
        dsp_cleanup(vt);
    }
    if (!(h2[0] > 3e-4 && h3[1] > 3.0 * h3[0])) printf("  walkie harmonics: 2nd %.2e -> %.2e, 3rd %.2e -> %.2e\n", h2[0], h2[1], h3[0], h3[1]);
    CHECK(h2[0] > 3e-4 && h2[1] > 0.003);                          /* even harmonics: asymmetric, and more with Drive */
    CHECK(h3[1] > 3.0 * h3[0]);                                    /* Drive clips harder */
    /* PTT squelch: a noise burst as the voice stops (no input left to explain it), none at Squelch 0 */
    for (int k = 0; k < 2; k++) {
        vt_t *vt = vm_rig("Walkie", 0.5f, 0.5f, 0.0f, k ? 1.0f : 0.0f, 0.5f);
        dsp_set_param(vt, "gate", "-39 dB");
        vm_sine_t on = { 700.0, 0.3 }, off = { 700.0, 0.0 };
        long pos = 0;
        vm_run(vt, vm_sine, &on, 0.3, &pos, NULL, NULL);
        vm_run(vt, vm_sine, &off, 0.5, &pos, NULL, NULL);
        vm_run(vt, vm_sine, &on, 0.3, &pos, NULL, NULL);
        vm_run(vt, vm_sine, &off, 0.1, &pos, NULL, NULL);                   /* the hold runs out, the gate shuts */
        const int n = vm_run(vt, vm_sine, &off, 0.4, &pos, l, NULL);        /* silence in: whatever comes out is the burst */
        const double closing = rms_of(l, VT_SAMPLE_RATE / 10);               /* the first 100 ms after the gate shut */
        const double later = rms_of(l + n - VT_SAMPLE_RATE / 10, VT_SAMPLE_RATE / 10);
        if (k) { if (!(closing > 1.5e-3 && later < 0.1 * closing)) printf("  walkie closing burst %.2e, later %.2e\n", closing, later); CHECK(closing > 1.5e-3); CHECK(later < 0.1 * closing); }   /* it bursts, then it dies away */
        else CHECK(later < 1e-5);                                          /* no squelch: the receiver mutes the channel and it is silent */
        dsp_cleanup(vt);
    }
    /* ... and as it opens: a tone the horn nearly stops (6 kHz) opens the gate, and the burst is what is heard */
    {
        double op[2];
        for (int k = 0; k < 2; k++) {
            vt_t *vt = vm_rig("Walkie", 0.5f, 0.5f, 0.0f, k ? 1.0f : 0.0f, 0.5f);
            dsp_set_param(vt, "gate", "-39 dB");
            vm_sine_t hi = { 14000.0, 0.3 }, off = { 700.0, 0.0 };
            long pos = 0;
            vm_run(vt, vm_sine, &off, 0.6, &pos, NULL, NULL);
            vm_run(vt, vm_sine, &hi, 0.1, &pos, l, NULL);
            op[k] = band_energy(l, VT_SAMPLE_RATE / 20, 1500.0, 3000.0);          /* the burst sits around 2 kHz; the tone is at 14 kHz */
            dsp_cleanup(vt);
        }
        if (!(op[1] > 3.0 * op[0])) printf("  walkie opening burst: %.3e vs %.3e without\n", op[1], op[0]);
        CHECK(op[1] > 3.0 * op[0] && op[1] > 1e-7);
    }
    /* Static: an open channel hisses; closed, it is quiet */
    {
        vt_t *vt = vm_rig("Walkie", 0.5f, 0.5f, 1.0f, 0.0f, 0.5f);
        vm_sine_t s = { 700.0, 0.3 };
        long pos = 0;
        vm_run(vt, vm_sine, &s, 0.3, &pos, NULL, NULL);
        const int n = vm_run(vt, vm_sine, &s, 0.3, &pos, l, NULL);
        CHECK(band_energy(l, n, 5000.0, 9000.0) > 0.0);
        dsp_cleanup(vt);
    }
}

/* ------------------------------------------------------------ Harmony */

static void test_harmony(void)
{
    static float l[VT_SAMPLE_RATE], r[VT_SAMPLE_RATE], mid[VT_SAMPLE_RATE];
    const int N = VT_SAMPLE_RATE / 2;
    vm_sine_t s = { 440.0, 0.3 };
    /* Default stack: -12, +7, +12 st around the lead */
    {
        vt_t *vt = vm_rig("Harmony", 0.5f, 0.5f, 0.0f, 0.5f, 0.5f);
        long pos = 0;
        vm_run(vt, vm_sine, &s, 0.6, &pos, NULL, NULL);
        vm_run(vt, vm_sine, &s, 0.5, &pos, l, r);
        for (int i = 0; i < N; i++) mid[i] = 0.5f * (l[i] + r[i]);
        const double lead = goertzel(mid, N, 440.0), fifth = goertzel(mid, N, 440.0 * pow(2.0, 7.0 / 12.0));
        const double down = goertzel(mid, N, 220.0), up = goertzel(mid, N, 880.0), off = goertzel(mid, N, 550.0), off2 = goertzel(mid, N, 330.0);
        if (!(down > 0.05 * lead && up > 0.05 * lead && fifth > 0.05 * lead && off < 0.002 * lead))
            printf("  harmony: lead %.2e  220 %.2e  fifth %.2e  880 %.2e | 550 %.2e 330 %.2e\n", lead, down, fifth, up, off, off2);
        CHECK(lead > 0 && down > 0.05 * lead && up > 0.05 * lead && fifth > 0.05 * lead);   /* each within 13 dB of the lead */
        CHECK(off < 0.002 * lead && off2 < 0.002 * lead);                                    /* nothing between them */
        double dl = 0;                                                                       /* stereo: the taps are panned */
        for (int i = 0; i < N; i++) dl += fabs(l[i] - r[i]);
        CHECK(dl / N > 0.005);
        dsp_cleanup(vt);
    }
    /* MIDI: C-E-G held gives +4 and +7 (the chord's shape, from the voice), not the defaults */
    {
        vt_t *vt = vm_rig("Harmony", 0.5f, 0.5f, 0.0f, 0.5f, 0.5f);
        long pos = 0;
        vm_run(vt, vm_sine, &s, 0.3, &pos, NULL, NULL);
        note_on(vt, 60); note_on(vt, 64); note_on(vt, 67);
        vm_run(vt, vm_sine, &s, 0.6, &pos, NULL, NULL);
        vm_run(vt, vm_sine, &s, 0.5, &pos, l, r);
        for (int i = 0; i < N; i++) mid[i] = 0.5f * (l[i] + r[i]);
        const double lead = goertzel(mid, N, 440.0), maj3 = goertzel(mid, N, 440.0 * pow(2.0, 4.0 / 12.0)), p5 = goertzel(mid, N, 440.0 * pow(2.0, 7.0 / 12.0));
        const double down = goertzel(mid, N, 220.0), up = goertzel(mid, N, 880.0);
        if (!(maj3 > 0.05 * lead && p5 > 0.05 * lead && down < 0.01 * lead && up < 0.01 * lead))
            printf("  harmony midi: lead %.2e  M3 %.2e  P5 %.2e | 220 %.2e 880 %.2e\n", lead, maj3, p5, down, up);
        CHECK(maj3 > 0.05 * lead && p5 > 0.05 * lead);
        CHECK(down < 0.01 * lead && up < 0.01 * lead);
        /* a minor third instead (A C E: +3 +7) */
        note_off(vt, 60); note_off(vt, 64); note_off(vt, 67);
        note_on(vt, 57); note_on(vt, 60); note_on(vt, 64);
        vm_run(vt, vm_sine, &s, 0.6, &pos, NULL, NULL);
        vm_run(vt, vm_sine, &s, 0.5, &pos, l, r);
        for (int i = 0; i < N; i++) mid[i] = 0.5f * (l[i] + r[i]);
        CHECK(goertzel(mid, N, 440.0 * pow(2.0, 3.0 / 12.0)) > 0.05 * goertzel(mid, N, 440.0) &&
              goertzel(mid, N, 440.0 * pow(2.0, 4.0 / 12.0)) < 0.01 * goertzel(mid, N, 440.0));
        /* released: the defaults return */
        note_off(vt, 57); note_off(vt, 60); note_off(vt, 64);
        vm_run(vt, vm_sine, &s, 0.6, &pos, NULL, NULL);
        vm_run(vt, vm_sine, &s, 0.5, &pos, l, r);
        for (int i = 0; i < N; i++) mid[i] = 0.5f * (l[i] + r[i]);
        CHECK(goertzel(mid, N, 220.0) > 0.05 * goertzel(mid, N, 440.0));
        dsp_cleanup(vt);
    }
    /* Lead and Stack are separate levels */
    for (int k = 0; k < 2; k++) {
        vt_t *vt = vm_rig("Harmony", k ? 0.0f : 0.5f, k ? 0.5f : 0.0f, 0.0f, 0.5f, 0.5f);
        long pos = 0;
        vm_run(vt, vm_sine, &s, 0.6, &pos, NULL, NULL);
        vm_run(vt, vm_sine, &s, 0.5, &pos, l, r);
        for (int i = 0; i < N; i++) mid[i] = 0.5f * (l[i] + r[i]);
        const double lead = goertzel(mid, N, 440.0), stack = goertzel(mid, N, 220.0);
        if (k) CHECK(lead < 0.02 * stack); else CHECK(stack < 0.002 * lead);
        dsp_cleanup(vt);
    }
    /* Latency: the lead is the input, delayed by what vox_latency says, which is under 15 ms at every setting */
    for (int worst = 0; worst < 2; worst++) {
        static vox_t v;
        vox_init(&v, VT_SAMPLE_RATE);
        const int none[4] = { -1, -1, -1, -1 };
        static float in[24000], out[24000];
        unsigned seed = 5;
        for (int i = 0; i < 24000; i++) in[i] = 0.3f * lcg_noise(&seed);
        for (int i = 0; i < 24000; i++) {
            if (i % 128 == 0) vox_set_harmony(&v, 0.5f, 0.0f, 0.0f, worst ? 1.0f : 0.5f, worst ? 1.0f : 0.5f, 0.5f, none, 0.0f);   /* Stack off: the lead alone */
            float a, b;
            vox_process(&v, VT_MODE_HARMONY, in[i], 1, &a, &b);
            out[i] = a / 0.9f * 0.5f;
        }
        const double lat = vox_latency(&v, VT_MODE_HARMONY);
        double best = -1; int lag = 0;
        for (int d = 0; d < 800; d++) {
            double sxy = 0, sxx = 0, syy = 0;
            for (int i = 12000; i < 20000; i++) { sxy += (double)out[i] * in[i - d]; sxx += (double)out[i] * out[i]; syy += (double)in[i - d] * in[i - d]; }
            const double c = sxy / sqrt(sxx * syy);
            if (c > best) { best = c; lag = d; }
        }
        if (fabs(lag - lat) > 1.5 || lat >= 0.015 * VT_SAMPLE_RATE) printf("  harmony latency: measured %d, reported %.1f samples (%.2f ms)\n", lag, lat, 1000.0 * lat / VT_SAMPLE_RATE);
        CHECK(best > 0.97);                                                   /* (linear interpolation of the fractional delay costs ~1%) */
        CHECK(fabs(lag - lat) <= 1.5);
        CHECK(lat < 0.015 * VT_SAMPLE_RATE);
        if (worst) CHECK(lat > 0.011 * VT_SAMPLE_RATE);                       /* the grain and micro-delay knobs at their ends are really used */
    }
}

/* ------------------------------------------------------- Scream, HardTune */

static void test_scream(void)
{
    static float l[2 * VT_SAMPLE_RATE];
    const int N = VT_SAMPLE_RATE;
    /* pitch: 220 Hz down 8 semitones (138.8 Hz) */
    {
        vt_t *vt = vm_rig("Scream", 0.5f, 0.5f, 0.0f, 0.5f, 0.0f);
        vm_tone_t tn = { 220.0, 0.5, 8 };
        long pos = 0;
        vm_run(vt, vm_tone, &tn, 0.8, &pos, NULL, NULL);
        const int n = vm_run(vt, vm_tone, &tn, 1.0, &pos, l, NULL);
        const double want = VT_SAMPLE_RATE / (220.0 * pow(2.0, -8.0 / 12.0));
        const double got = vm_period(l + n - 8192, 8192, 200.0, 450.0);
        const double err = cents(vm_hz_from_period(got), VT_SAMPLE_RATE / want);
        if (fabs(err) > 15.0) printf("  scream: period %.2f, want %.2f (%.1f ct)\n", got, want, err);
        CHECK(fabs(err) < 15.0);
        dsp_cleanup(vt);
    }
    /* Pitch knob: -12 at 0 (an octave down), -3 at 1 */
    for (int k = 0; k < 2; k++) {
        vt_t *vt = vm_rig("Scream", k ? 1.0f : 0.0f, 0.5f, 0.0f, 0.5f, 0.0f);
        vm_tone_t tn = { 220.0, 0.5, 8 };
        long pos = 0;
        vm_run(vt, vm_tone, &tn, 0.8, &pos, NULL, NULL);
        const int n = vm_run(vt, vm_tone, &tn, 1.0, &pos, l, NULL);
        const double st = k ? -3.0 : -12.0;
        const double want_hz = 220.0 * pow(2.0, st / 12.0);
        const double got = vm_period(l + n - 8192, 8192, 0.93 * VT_SAMPLE_RATE / want_hz, 1.07 * VT_SAMPLE_RATE / want_hz);
        if (fabs(cents(vm_hz_from_period(got), want_hz)) > 20.0) printf("  scream pitch %.0f st: %.1f Hz, want %.1f\n", st, vm_hz_from_period(got), want_hz);
        CHECK(fabs(cents(vm_hz_from_period(got), want_hz)) < 20.0);
        dsp_cleanup(vt);
    }
    /* Throat: the formant scale moves the spectrum's weight */
    double cen[2];
    for (int k = 0; k < 2; k++) {
        vt_t *vt = vm_rig("Scream", 0.5f, k ? 1.0f : 0.0f, 0.0f, 0.5f, 0.0f);
        vm_vowel_t vw = { 220.0, 1500.0, 0.4 };
        long pos = 0;
        vm_run(vt, vm_vowel, &vw, 0.8, &pos, NULL, NULL);
        const int n = vm_run(vt, vm_vowel, &vw, 1.0, &pos, l, NULL);
        cen[k] = harmonic_centroid(l + n - 8192, 8192, 220.0 * pow(2.0, -8.0 / 12.0));
        dsp_cleanup(vt);
    }
    if (!(cen[1] > 1.25 * cen[0])) printf("  scream throat: centroid %.0f -> %.0f Hz\n", cen[0], cen[1]);
    CHECK(cen[1] > 1.25 * cen[0]);
    /* The band at the middle Narrow: 90 Hz .. 4.8 kHz, steep, and it keeps the chest (a phone band at
     * 350 Hz took the depth the drop makes). White noise has no pitch, so it passes the pitch
     * shifter's plain path and what is left is the band itself. */
    {
        unsigned seed = 21;
        vt_t *vt = vm_rig("Scream", 0.5f, 0.5f, 0.0f, 0.5f, 0.0f);
        long pos = 0;
        vm_run(vt, vm_noise_lo, &seed, 0.6, &pos, NULL, NULL);
        const int n = vm_run(vt, vm_noise_lo, &seed, 1.6, &pos, l, NULL);
        const double mid = vm_bavg(l, n, 700.0, 1400.0);
        const double low = vm_bavg(l, n, 20.0, 50.0), chest = vm_bavg(l, n, 250.0, 300.0);
        const double up = vm_bavg(l, n, 9000.0, 12000.0);
        if (!(low < 0.01 * mid && chest > 0.5 * mid && up < 0.03 * mid))
            printf("  scream band (rel. to 700-1400 Hz): 20-50 %.2e  250-300 %.2e  9-12k %.2e\n", low / mid, chest / mid, up / mid);
        CHECK(low < 0.01 * mid);                                    /* the rumble is gone */
        CHECK(chest > 0.5 * mid);                                   /* ... and the chest is still there */
        CHECK(up < 0.03 * mid);
        dsp_cleanup(vt);
    }
    /* Narrow closes the band in from both sides */
    {
        double w3k[2], w500[2];
        for (int k = 0; k < 2; k++) {
            unsigned seed = 21;
            vt_t *vt = vm_rig("Scream", 0.5f, 0.5f, 0.0f, k ? 1.0f : 0.0f, 0.0f);
            long pos = 0;
            vm_run(vt, vm_noise_lo, &seed, 0.6, &pos, NULL, NULL);
            const int n = vm_run(vt, vm_noise_lo, &seed, 1.6, &pos, l, NULL);
            w3k[k] = vm_bavg(l, n, 2800.0, 3400.0);
            w500[k] = vm_bavg(l, n, 120.0, 250.0);
            dsp_cleanup(vt);
        }
        if (!(w3k[0] > 3.0 * w3k[1] && w500[0] > 3.0 * w500[1])) printf("  scream narrow: 3 kHz %.2e -> %.2e, 120-250 Hz %.2e -> %.2e\n", w3k[0], w3k[1], w500[0], w500[1]);
        CHECK(w3k[0] > 3.0 * w3k[1]);
        CHECK(w500[0] > 3.0 * w500[1]);
    }
    /* Grit: the soft clip adds harmonics (asymmetric: even ones too); clean at 0. Pitch at -3 st (c1 = 1): read them at multiples of the 589 Hz that comes out. */
    double thd[2];
    for (int k = 0; k < 2; k++) {
        vt_t *vt = vm_rig("Scream", 1.0f, 0.5f, k ? 1.0f : 0.0f, 0.5f, 0.0f);
        vm_sine_t s = { 700.0, 0.1 };
        long pos = 0;
        vm_run(vt, vm_sine, &s, 0.6, &pos, NULL, NULL);
        const int n = vm_run(vt, vm_sine, &s, 0.4, &pos, l, NULL);
        thd[k] = (goertzel(l, n, 2.0 * 588.7) + goertzel(l, n, 3.0 * 588.7)) / goertzel(l, n, 588.7);
        dsp_cleanup(vt);
    }
    if (!(thd[1] > 5.0 * thd[0])) printf("  scream grit: harmonic ratio %.2e -> %.2e\n", thd[0], thd[1]);
    CHECK(thd[1] > 5.0 * thd[0]);
    (void)N;
}

static void test_hardtune(void)
{
    static float l[2 * VT_SAMPLE_RATE];
    /* Snaps to the nearest note at once: a 217 Hz voice (24 ct flat of A3) sings A3 */
    static const struct { double in, want; } snap[] = { { 217.0, 220.0 }, { 225.0, 220.0 }, { 230.0, 233.08 }, { 247.0, 246.94 } };
    for (size_t k = 0; k < sizeof snap / sizeof snap[0]; k++) {
        vt_t *vt = vm_rig("HardTune", 0.5f, 0.5f, 0.0f, 0.0f, 0.5f);
        vm_tone_t tn = { snap[k].in, 0.5, 6 };
        long pos = 0;
        vm_run(vt, vm_tone, &tn, 0.8, &pos, NULL, NULL);
        const int n = vm_run(vt, vm_tone, &tn, 0.8, &pos, l, NULL);
        const double got = vm_hz_from_period(vm_period(l + n - 8192, 8192, VT_SAMPLE_RATE / 300.0, VT_SAMPLE_RATE / 150.0));
        if (fabs(cents(got, snap[k].want)) > 4.0) printf("  hardtune %.1f Hz -> %.2f Hz, want %.2f (%.1f ct)\n", snap[k].in, got, snap[k].want, cents(got, snap[k].want));
        CHECK(fabs(cents(got, snap[k].want)) < 4.0);
        dsp_cleanup(vt);
    }
    /* Amount: 0 leaves the voice, 50% is halfway (217 -> 220 is +24 ct) */
    static const struct { float amt; double ct; } am[] = { { 0.0f, 0.0 }, { 0.25f, 12.0 }, { 0.5f, 24.0 }, { 1.0f, 48.0 } };
    for (size_t k = 0; k < sizeof am / sizeof am[0]; k++) {
        vt_t *vt = vm_rig("HardTune", am[k].amt, 0.5f, 0.0f, 0.0f, 0.5f);
        vm_tone_t tn = { 217.0, 0.5, 6 };
        long pos = 0;
        vm_run(vt, vm_tone, &tn, 0.8, &pos, NULL, NULL);
        const int n = vm_run(vt, vm_tone, &tn, 0.8, &pos, l, NULL);
        const double got = vm_hz_from_period(vm_period(l + n - 8192, 8192, VT_SAMPLE_RATE / 300.0, VT_SAMPLE_RATE / 150.0));
        const double moved = cents(got, 217.0);
        if (fabs(moved - am[k].ct) > 5.0) printf("  hardtune amount %.2f: moved %.1f ct, want %.0f\n", (double)am[k].amt, moved, am[k].ct);
        CHECK(fabs(moved - am[k].ct) < 5.0);
        dsp_cleanup(vt);
    }
    /* Scale: C major has no C#; a C#4 voice lands on a note of the scale */
    {
        vt_t *vt = vm_rig("HardTune", 0.5f, 0.5f, 0.0f, 0.0f, 0.5f);
        dsp_set_param(vt, "scale", "Major");
        vm_tone_t tn = { 277.18, 0.5, 6 };
        long pos = 0;
        vm_run(vt, vm_tone, &tn, 0.8, &pos, NULL, NULL);
        const int n = vm_run(vt, vm_tone, &tn, 0.8, &pos, l, NULL);
        const double got = vm_hz_from_period(vm_period(l + n - 8192, 8192, VT_SAMPLE_RATE / 340.0, VT_SAMPLE_RATE / 230.0));
        CHECK(fabs(cents(got, 261.63)) < 6.0 || fabs(cents(got, 293.66)) < 6.0);
        dsp_cleanup(vt);
    }
    /* MIDI: a held note is the target, whatever the voice sings (octave folded toward the voice) */
    static const struct { int pad; double want; } pads[] = { { 60, 261.63 }, { 62, 293.66 }, { 71, 246.94 }, { 57, 220.0 } };
    for (size_t k = 0; k < sizeof pads / sizeof pads[0]; k++) {
        vt_t *vt = vm_rig("HardTune", 0.5f, 0.5f, 0.0f, 0.0f, 0.5f);
        vm_tone_t tn = { 217.0, 0.5, 6 };
        long pos = 0;
        vm_run(vt, vm_tone, &tn, 0.4, &pos, NULL, NULL);
        note_on(vt, pads[k].pad);
        vm_run(vt, vm_tone, &tn, 0.5, &pos, NULL, NULL);
        const int n = vm_run(vt, vm_tone, &tn, 0.6, &pos, l, NULL);
        const double got = vm_hz_from_period(vm_period(l + n - 8192, 8192, VT_SAMPLE_RATE / 340.0, VT_SAMPLE_RATE / 190.0));
        if (fabs(cents(got, pads[k].want)) > 6.0) printf("  hardtune pad %d: %.2f Hz, want %.2f\n", pads[k].pad, got, pads[k].want);
        CHECK(fabs(cents(got, pads[k].want)) < 6.0);
        dsp_cleanup(vt);
    }
    /* Zero slew: after a pad jump A3 -> D4 the pitch is there within 40 ms */
    {
        vt_t *vt = vm_rig("HardTune", 0.5f, 0.5f, 0.0f, 0.0f, 0.5f);
        vm_tone_t tn = { 220.0, 0.5, 6 };
        long pos = 0;
        note_on(vt, 57);
        vm_run(vt, vm_tone, &tn, 0.8, &pos, NULL, NULL);
        note_on(vt, 62);
        vm_run(vt, vm_tone, &tn, 0.04, &pos, NULL, NULL);
        const int n = vm_run(vt, vm_tone, &tn, 0.06, &pos, l, NULL);
        const double got = vm_hz_from_period(vm_period(l + n - 1500, 1500, VT_SAMPLE_RATE / 340.0, VT_SAMPLE_RATE / 190.0));
        if (fabs(cents(got, 293.66)) > 30.0) printf("  hardtune jump: %.1f Hz 40-100 ms after the pad\n", got);
        CHECK(fabs(cents(got, 293.66)) < 30.0);
        dsp_cleanup(vt);
    }
    /* Formant stays: shifting a vowel down a fourth keeps its spectral envelope; the Formant knob moves it */
    double cen[3];
    for (int k = 0; k < 3; k++) {
        vt_t *vt = vm_rig("HardTune", 0.5f, k == 2 ? 1.0f : 0.5f, 0.0f, 0.0f, 0.5f);
        vm_vowel_t vw = { 220.0, 900.0, 0.4 };
        long pos = 0;
        if (k) note_on(vt, 52);                                     /* E3: 220 down 5 st */
        vm_run(vt, vm_vowel, &vw, 0.8, &pos, NULL, NULL);
        const int n = vm_run(vt, vm_vowel, &vw, 1.0, &pos, l, NULL);
        const double f0 = k ? 164.81 : 220.0;
        cen[k] = harmonic_centroid(l + n - 8192, 8192, f0);
        dsp_cleanup(vt);
    }
    if (!(cen[1] / cen[0] > 0.8 && cen[1] / cen[0] < 1.25 && cen[2] > 1.5 * cen[1])) printf("  hardtune formant: centroid %.0f / %.0f / %.0f\n", cen[0], cen[1], cen[2]);
    CHECK(cen[1] / cen[0] > 0.8 && cen[1] / cen[0] < 1.25);
    CHECK(cen[2] > 1.5 * cen[1]);
}

/* ---------------------------------------------------------- Nightcall */

/* |H| of a daft_bqf at f (dB) */
static double bqf_db(const daft_bqf *c, double f)
{
    const double w = 2.0 * M_PI * f / VT_SAMPLE_RATE;
    const double complex_re[3] = { cos(0.0), cos(w), cos(2.0 * w) }, complex_im[3] = { 0.0, -sin(w), -sin(2.0 * w) };
    double nr = c->b0 * complex_re[0] + c->b1 * complex_re[1] + c->b2 * complex_re[2], ni = c->b0 * complex_im[0] + c->b1 * complex_im[1] + c->b2 * complex_im[2];
    double dr = 1.0 + c->a1 * complex_re[1] + c->a2 * complex_re[2], di = c->a1 * complex_im[1] + c->a2 * complex_im[2];
    return 10.0 * log10((nr * nr + ni * ni) / (dr * dr + di * di));
}

static void test_nightcall(void)
{
    /* The carrier: a narrow pulse (duty 15-20%) with ~10% white noise. Straight off the daft engine. */
    {
        static float c[44100];
        daft_t d;
        daft_init(&d, VT_SAMPLE_RATE);
        daft_set_params(&d, 1, 0.0f, 0.0f, 0.5f, 0.0f, 0.4f, 0.0f, -60.0f);
        daft_set_talker(&d, 1, 0.18f, 0.0f, 0.175f, 6.0f);
        daft_set_voice(&d, 0, 45, 127);                                     /* A2, 110 Hz */
        for (int i = 0; i < 20000; i++) daft_carrier_sample(&d);            /* let the envelope settle */
        for (int i = 0; i < 44100; i++) c[i] = daft_carrier_sample(&d);
        int high = 0;
        double mean = 0;
        for (int i = 0; i < 44100; i++) mean += c[i];
        mean /= 44100;
        for (int i = 0; i < 44100; i++) if (c[i] - mean > 0.0f) high++;
        const double duty = (double)high / 44100.0;
        if (fabs(duty - 0.18) > 0.02) printf("  nightcall carrier duty %.3f\n", duty);
        CHECK(fabs(duty - 0.18) < 0.02);                                    /* a pulse, not a square or a saw */
        CHECK(fabs(duty - 0.5) > 0.25);
        const double rms_clean = rms_of(c, 44100);
        /* with rasp 10%: the extra power is the noise */
        daft_set_talker(&d, 1, 0.18f, 0.10f, 0.175f, 6.0f);
        for (int i = 0; i < 44100; i++) c[i] = daft_carrier_sample(&d);
        const double rms_noisy = rms_of(c, 44100);
        const double noise_rms = sqrt(fmax(rms_noisy * rms_noisy - rms_clean * rms_clean, 0.0));
        const double share = noise_rms / rms_clean;
        if (fabs(share - 0.10) > 0.03) printf("  nightcall noise share %.3f (clean %.4f noisy %.4f)\n", share, rms_clean, rms_noisy);
        CHECK(fabs(share - 0.10) < 0.03);
        /* duty range */
        daft_set_talker(&d, 1, 0.05f, 0.0f, 0.0f, 0.0f);
        CHECK(d.duty >= 0.10f);
        daft_set_talker(&d, 1, 0.9f, 0.0f, 0.0f, 0.0f);
        CHECK(d.duty <= 0.30f);
    }
    /* The formant shaping: steep 250 Hz low-cut, the cavity peak at 1.2 kHz, a crisp high shelf */
    {
        daft_t d;
        daft_init(&d, VT_SAMPLE_RATE);
        daft_set_talker(&d, 1, 0.18f, 0.1f, 0.175f, 6.0f);
        const double fr[] = { 60.0, 125.0, 250.0, 500.0, 800.0, 1200.0, 3000.0, 6000.0, 10000.0 };
        double g[9];
        for (size_t k = 0; k < 9; k++) {
            g[k] = 0;
            for (int s = 0; s < 4; s++) g[k] += bqf_db(&d.tk_eq[s], fr[k]);
        }
        if (!(g[1] < g[4] - 18.0)) printf("  nightcall eq (dB): 60 %.1f 125 %.1f 250 %.1f 500 %.1f 800 %.1f 1.2k %.1f 3k %.1f 6k %.1f 10k %.1f\n", g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8]);
        CHECK(g[2] < g[3] - 3.0 && g[2] > g[3] - 10.0);                      /* the corner: -6 dB at 250 Hz */
        CHECK(g[1] < g[4] - 18.0);                                           /* steep: an octave under it is >= 18 dB down */
        CHECK(g[0] < g[4] - 30.0);
        CHECK(g[5] > g[3] + 4.0 && g[5] > g[6] + 1.0);                       /* the cavity peak stands over its neighbours */
        CHECK(g[7] > g[3] + 3.0 && g[8] > g[3] + 3.0);                       /* the shelf: the top is lifted */
        /* the cavity knob moves the peak, and 0 dB takes it away */
        daft_set_talker(&d, 1, 0.18f, 0.1f, 0.175f, 12.0f);
        double hi = 0, lo_ = 0;
        for (int s = 0; s < 4; s++) { hi += bqf_db(&d.tk_eq[s], 1200.0); }
        daft_set_talker(&d, 1, 0.18f, 0.1f, 0.175f, 0.0f);
        for (int s = 0; s < 4; s++) { lo_ += bqf_db(&d.tk_eq[s], 1200.0); }
        CHECK(hi - lo_ > 10.0 && hi - lo_ < 13.0);
    }
    /* Through the engine: audible with a voice and the drone, silent without one, never past the ceiling */
    {
        static float l[44100];
        vt_t *vt = vm_rig("Nightcall", 0.5f, 0.5f, 0.0f, 0.5f, 0.5f);
        vm_vowel_t vw = { 180.0, 800.0, 0.4 };
        long pos = 0;
        vm_run(vt, vm_vowel, &vw, 0.5, &pos, NULL, NULL);
        int n = vm_run(vt, vm_vowel, &vw, 0.8, &pos, l, NULL);
        CHECK(rms_of(l, n) > 0.02);
        vm_sine_t off = { 100.0, 0.0 };
        vm_run(vt, vm_sine, &off, 0.4, &pos, NULL, NULL);
        n = vm_run(vt, vm_sine, &off, 0.3, &pos, l, NULL);
        CHECK(rms_of(l, n) < 1e-6);                                         /* the voice gate shuts it: exact silence */
        dsp_cleanup(vt);
    }
    /* The pads play the carrier: a held A3 puts its harmonics in the output (and not their neighbours) */
    {
        static float l[44100];
        vt_t *vt = vm_rig("Nightcall", 0.5f, 0.5f, 0.0f, 0.5f, 0.5f);
        unsigned seed = 3;
        note_on(vt, 57);
        long pos = 0;
        vm_run(vt, vm_noise, &seed, 0.6, &pos, NULL, NULL);
        const int n = vm_run(vt, vm_noise, &seed, 0.8, &pos, l, NULL);
        const double on = goertzel(l, n, 440.0) + goertzel(l, n, 660.0) + goertzel(l, n, 880.0);
        const double between = goertzel(l, n, 495.0) + goertzel(l, n, 740.0) + goertzel(l, n, 990.0);
        if (!(on > 8.0 * between)) printf("  nightcall pad: harmonics %.2e vs between %.2e\n", on, between);
        CHECK(on > 8.0 * between);
        dsp_cleanup(vt);
    }
    /* Breath bleed: the modulator's own > 3.5 kHz hiss is mixed into the output, in proportion to
     * Breath: the output follows the hiss's waveform (the vocoder alone only follows its level) */
    {
        static float hiss[VT_SAMPLE_RATE * 2], outv[VT_SAMPLE_RATE * 2], o[VT_SAMPLE_RATE * 2];
        unsigned seed = 11;
        float z1 = 0, z2 = 0;                                                /* a 2-pole high-pass at 5 kHz: whisper */
        const double w0 = 2.0 * M_PI * 5000.0 / VT_SAMPLE_RATE, al = sin(w0) / (2.0 * 0.7071), a0 = 1 + al;
        const double b0 = (1 + cos(w0)) / 2 / a0, b1 = -(1 + cos(w0)) / a0, a1 = -2 * cos(w0) / a0, a2 = (1 - al) / a0;
        for (int i = 0; i < VT_SAMPLE_RATE * 2; i++) {
            const float x = 0.4f * lcg_noise(&seed);
            const float y = (float)(b0 * x + z1);
            z1 = (float)(b1 * x - a1 * y + z2);
            z2 = (float)(b0 * x - a2 * y);
            hiss[i] = y;
        }
        double corr[2];
        for (int k = 0; k < 2; k++) {
            vt_t *vt = vm_rig("Nightcall", 0.5f, k ? 1.0f : 0.0f, 0.0f, 0.5f, 0.5f);
            static float blk[2 * 128];
            int nn = 0;
            for (int b = 0; b < VT_SAMPLE_RATE * 2 / 128; b++) {
                for (int i = 0; i < 128; i++) blk[2 * i] = blk[2 * i + 1] = hiss[b * 128 + i];
                dsp_process_block_f(vt, blk, 128);
                for (int i = 0; i < 128; i++) outv[nn++] = blk[2 * i];
            }
            float p1 = 0, p2 = 0;                                            /* the output's top end, same filter */
            for (int i = 0; i < nn; i++) {
                const float y = (float)(b0 * outv[i] + p1);
                p1 = (float)(b1 * outv[i] - a1 * y + p2);
                p2 = (float)(b0 * outv[i] - a2 * y);
                o[i] = y;
            }
            double best = 0;
            for (int lag = 0; lag < 80; lag++) {
                double sxy = 0, sxx = 0, syy = 0;
                for (int i = 40000; i < 80000; i++) { sxy += (double)o[i] * hiss[i - lag]; sxx += (double)o[i] * o[i]; syy += (double)hiss[i - lag] * hiss[i - lag]; }
                const double c = fabs(sxy / sqrt(sxx * syy));
                if (c > best) best = c;
            }
            corr[k] = best;
            dsp_cleanup(vt);
        }
        if (!(corr[0] < 0.05 && corr[1] > 0.15)) printf("  nightcall bleed: correlation with the hiss %.3f -> %.3f\n", corr[0], corr[1]);
        CHECK(corr[0] < 0.05 && corr[1] > 0.15);
        CHECK(dsp_macro_name(VT_MODE_NIGHTCALL, 1) && strcmp(dsp_macro_name(VT_MODE_NIGHTCALL, 1), "Breath") == 0);
    }
    /* Robot is untouched by all of it */
    {
        daft_t d;
        daft_init(&d, VT_SAMPLE_RATE);
        CHECK(d.talker == 0 && fabsf(d.lev_target - 0.1416f) < 1e-6f && fabsf(d.duty - 0.3f) < 1e-6f);
    }
}

/* ------------------------------------------------------------ Prism glass */

static void test_prism_glass(void)
{
    static float l[VT_SAMPLE_RATE], r[VT_SAMPLE_RATE];
    const int N = VT_SAMPLE_RATE / 2;
    vm_sine_t s = { 440.0, 0.3 };
    /* Refract 0 is the plain harmonizer: the voice at its own pitch. Refract up: partials land off the grid. */
    double pl[2][3], pr[2][2];
    for (int k = 0; k < 2; k++) {
        vt_t *vt = vm_rig("Prism", 0.5f, 0.3f, 0.0f, k ? 1.0f : 0.0f, 0.0f);
        long pos = 0;
        vm_run(vt, vm_sine, &s, 0.8, &pos, NULL, NULL);
        vm_run(vt, vm_sine, &s, 0.5, &pos, l, r);
        const double ref = goertzel(l, N, 440.0) + 1e-20;
        pl[k][0] = goertzel(l, N, 440.0) / ref;
        pl[k][1] = goertzel(l, N, 440.0 + 138.0) / ref;                  /* up by the glass shift */
        pl[k][2] = goertzel(l, N, 440.0 - 138.0) / ref;
        pr[k][0] = goertzel(r, N, 440.0 - 110.4) / ref;                  /* down by 0.8 x the shift */
        pr[k][1] = goertzel(r, N, 440.0 + 110.4) / ref;
        dsp_cleanup(vt);
    }
    if (!(pl[0][1] < 1e-3 && pl[1][1] > 0.2 && pr[1][0] > 0.2 && pr[0][0] < 1e-3))
        printf("  prism glass: L 578 Hz %.2e -> %.2e, R 330 Hz %.2e -> %.2e\n", pl[0][1], pl[1][1], pr[0][0], pr[1][0]);
    CHECK(pl[0][1] < 1e-3 && pl[0][2] < 1e-3 && pr[0][0] < 1e-3);        /* off: nothing of the sort */
    CHECK(pl[1][1] > 0.2);                                               /* L: a partial at 440 + 138 Hz */
    CHECK(pr[1][0] > 0.2);                                               /* R: one at 440 - 110 Hz */
    CHECK(pr[1][1] < pr[1][0] * 0.2);                                    /* single-sideband: only that side */
    /* Shimmer: the comb changes the sound and stays stable */
    {
        double d = 0;
        static float l2[VT_SAMPLE_RATE], r2[VT_SAMPLE_RATE];
        for (int k = 0; k < 2; k++) {
            vt_t *vt = vm_rig("Prism", 0.5f, 0.3f, 0.0f, 1.0f, k ? 1.0f : 0.0f);
            vm_tone_t tn = { 200.0, 0.4, 8 };
            long pos = 0;
            vm_run(vt, vm_tone, &tn, 0.8, &pos, NULL, NULL);
            vm_run(vt, vm_tone, &tn, 0.5, &pos, k ? l2 : l, k ? r2 : r);
            dsp_cleanup(vt);
        }
        for (int i = 0; i < N; i++) d += fabs(l[i] - l2[i]);
        CHECK(d / N > 0.003);
        double pk = 0;
        for (int i = 0; i < N; i++) pk = fmax(pk, fabs(l2[i]));
        CHECK(pk < 1.0);
    }
    /* The harmony voices are still the in-key chord with Refract at 0 (glass is on top of it, not instead) */
    {
        static cher_t c;
        static fx_t fx;
        enum { M = 3 * VT_SAMPLE_RATE / 2 };
        static float fl[M], fr_[M], mid[M];
        fx_run(&fx, &c, VT_MODE_PRISM, 0.5f, 0.3f, 0.0f, 1, 0, VT_SCALE_MAJOR, src_tone_220, M, fl, fr_);
        for (int i = 0; i < M; i++) mid[i] = 0.5f * (fl[i] + fr_[i]);
        const float *t = mid + M - VT_SAMPLE_RATE / 2;
        const double c4 = band_at(t, VT_SAMPLE_RATE / 2, 261.63), cs4 = band_at(t, VT_SAMPLE_RATE / 2, 277.18);
        CHECK(c4 > 10.0 * cs4);
    }
}

/* -------------------------------------------------------- Voice FX */

typedef struct { const char *k, *v; } vf_kv;

/* A Voice FX rig: engine VoiceFX, a clean base (no breath, tail, choir or attack so each test isolates its knob,
 * wet only at the Mix/Drive knee), then the given overrides. */
static vt_t *vf_rig(const vf_kv *kv, int n)
{
    static const vf_kv base[] = {
        { "engine", "VoiceFX" }, { "vf_vowel", "0" }, { "vf_tract", "0.5" }, { "vf_peak", "0.5" }, { "vf_choir", "0" },
        { "vf_breath", "0" }, { "vf_attack", "0" }, { "vf_release", "0" }, { "vf_mix", "0.7" },   /* 0.7 = all wet at the knee, almost no drive */
    };
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    for (size_t i = 0; i < sizeof base / sizeof base[0]; i++) dsp_set_param(vt, base[i].k, base[i].v);
    for (int i = 0; i < n; i++) dsp_set_param(vt, kv[i].k, kv[i].v);
    return vt;
}

/* Mean band level (per-Hz) of white noise through Voice FX with the given overrides. */
static double vf_noise_band(const vf_kv *kv, int n, double lo, double hi)
{
    static float l[2 * VT_SAMPLE_RATE];
    vt_t *vt = vf_rig(kv, n);
    unsigned seed = 77;
    long pos = 0;
    vm_run(vt, vm_noise, &seed, 0.6, &pos, NULL, NULL);
    const int m = vm_run(vt, vm_noise, &seed, 1.5, &pos, l, NULL);
    const double e = vm_bavg(l, m, lo, hi);
    dsp_cleanup(vt);
    return e;
}

static void test_voicefx(void)
{
    static float l[3 * VT_SAMPLE_RATE], r[3 * VT_SAMPLE_RATE];

    /* Vowel: A has its first formant near 730 Hz and I near 270 Hz with a high second formant at 2.3 kHz */
    {
        const vf_kv a[] = { { "vf_vowel", "0" } }, i[] = { { "vf_vowel", "0.5" } };
        const double a_f1 = vf_noise_band(a, 1, 700.0, 760.0), a_lo = vf_noise_band(a, 1, 240.0, 300.0), a_f2 = vf_noise_band(a, 1, 2200.0, 2380.0);
        const double i_f1 = vf_noise_band(i, 1, 700.0, 760.0), i_lo = vf_noise_band(i, 1, 240.0, 300.0), i_f2 = vf_noise_band(i, 1, 2200.0, 2380.0);
        if (!(a_f1 > 3.0 * a_lo && i_lo > 3.0 * i_f1 && i_f2 / i_f1 > 4.0 * (a_f2 / a_f1)))
            printf("  vowel: A F1 %.2e lo %.2e F2' %.2e | I F1 %.2e lo %.2e F2' %.2e\n", a_f1, a_lo, a_f2, i_f1, i_lo, i_f2);
        CHECK(a_f1 > 3.0 * a_lo);                                  /* A: energy at 730, little at 270 */
        CHECK(i_lo > 3.0 * i_f1);                                  /* I: the other way round */
        CHECK(i_f2 / i_f1 > 4.0 * (a_f2 / a_f1));                  /* and a bright second formant */
    }
    /* Sweep: the vowel knob is continuous (E, between A and I, sits between them) and U is dark again */
    {
        const vf_kv e[] = { { "vf_vowel", "0.25" } }, u[] = { { "vf_vowel", "1" } };
        const vf_kv a[] = { { "vf_vowel", "0" } }, i[] = { { "vf_vowel", "0.5" } };
        const double ra = vf_noise_band(a, 1, 2200.0, 2380.0) / vf_noise_band(a, 1, 700.0, 760.0);
        const double re = vf_noise_band(e, 1, 1700.0, 1980.0) / vf_noise_band(e, 1, 480.0, 580.0);
        const double ri = vf_noise_band(i, 1, 2200.0, 2380.0) / vf_noise_band(i, 1, 700.0, 760.0);
        const double rE = vf_noise_band(e, 1, 2200.0, 2380.0) / vf_noise_band(e, 1, 700.0, 760.0);
        CHECK(re > 0.05);                                          /* E has a second formant near 1.84 kHz */
        CHECK(rE > ra && rE < ri);                                 /* brightness rises monotonically A -> E -> I */
        CHECK(vf_noise_band(u, 1, 270.0, 330.0) > 3.0 * vf_noise_band(u, 1, 1400.0, 1600.0));   /* U: F1 near 300, nothing at 1.5 kHz */
    }
    /* Tract: the formants move up with the knob and down below the middle */
    {
        const vf_kv lo[] = { { "vf_tract", "0" } }, mid[] = { { "vf_tract", "0.5" } }, hi[] = { { "vf_tract", "1" } };
        const double up_hi = vf_noise_band(hi, 1, 1000.0, 1200.0) / vf_noise_band(hi, 1, 700.0, 760.0);
        const double up_mid = vf_noise_band(mid, 1, 1000.0, 1200.0) / vf_noise_band(mid, 1, 700.0, 760.0);
        const double dn_lo = vf_noise_band(lo, 1, 440.0, 520.0) / vf_noise_band(lo, 1, 700.0, 760.0);
        const double dn_mid = vf_noise_band(mid, 1, 440.0, 520.0) / vf_noise_band(mid, 1, 700.0, 760.0);
        if (!(up_hi > 3.0 * up_mid && dn_lo > 3.0 * dn_mid)) printf("  tract: up %.2f vs %.2f, down %.2f vs %.2f\n", up_hi, up_mid, dn_lo, dn_mid);
        CHECK(up_hi > 3.0 * up_mid);                               /* F1 730 -> ~1.1 kHz at 1 */
        CHECK(dn_lo > 3.0 * dn_mid);                               /* F1 730 -> ~480 Hz at 0 */
    }
    /* Peak: a narrower formant stands further out of the band around it */
    {
        const vf_kv p0[] = { { "vf_peak", "0" } }, p1[] = { { "vf_peak", "1" } };
        const double s0 = vf_noise_band(p0, 1, 715.0, 745.0) / vf_noise_band(p0, 1, 420.0, 500.0);
        const double s1 = vf_noise_band(p1, 1, 715.0, 745.0) / vf_noise_band(p1, 1, 420.0, 500.0);
        if (!(s1 > 2.0 * s0)) printf("  peak: centre/side %.2f -> %.2f\n", s0, s1);
        CHECK(s1 > 2.0 * s0);
    }
    /* Peak does not swing the loudness wildly (a level made up for the narrower bands) */
    {
        double rms[2];
        for (int k = 0; k < 2; k++) {
            const vf_kv kv[] = { { "vf_peak", k ? "1" : "0" } };
            vt_t *vt = vf_rig(kv, 1);
            vm_tone_t tn = { 190.0, 0.4, 8 };
            long pos = 0;
            vm_run(vt, vm_tone, &tn, 0.6, &pos, NULL, NULL);
            const int n = vm_run(vt, vm_tone, &tn, 1.0, &pos, l, NULL);
            rms[k] = rms_of(l, n);
            dsp_cleanup(vt);
        }
        if (!(fabs(20.0 * log10(rms[1] / rms[0])) < 5.0)) printf("  peak level: %.1f dB between Peak 0 and 1\n", 20.0 * log10(rms[1] / rms[0]));
        CHECK(fabs(20.0 * log10(rms[1] / rms[0])) < 5.0);
    }
    /* Choir: left and right are the same at 0 and decorrelate with the knob */
    {
        double corr[2];
        for (int k = 0; k < 2; k++) {
            const vf_kv kv[] = { { "vf_choir", k ? "1" : "0" } };
            vt_t *vt = vf_rig(kv, 1);
            vm_tone_t tn = { 190.0, 0.4, 8 };
            long pos = 0;
            vm_run(vt, vm_tone, &tn, 0.6, &pos, NULL, NULL);
            const int n = vm_run(vt, vm_tone, &tn, 1.5, &pos, l, r);
            double sxy = 0, sxx = 0, syy = 0;
            for (int i = 0; i < n; i++) { sxy += (double)l[i] * r[i]; sxx += (double)l[i] * l[i]; syy += (double)r[i] * r[i]; }
            corr[k] = sxy / sqrt(sxx * syy + 1e-30);
            dsp_cleanup(vt);
        }
        if (!(corr[0] > 0.999 && corr[1] < 0.95)) printf("  choir: L/R correlation %.4f -> %.4f\n", corr[0], corr[1]);
        CHECK(corr[0] > 0.999);
        CHECK(corr[1] < 0.95);
    }
    /* Breath: the voice's air above the formants comes back, noise follows the voice, and silence stays silent */
    {
        double air[2];
        for (int k = 0; k < 2; k++) {
            const vf_kv kv[] = { { "vf_breath", k ? "1" : "0" } };
            vt_t *vt = vf_rig(kv, 1);
            unsigned seed = 5;
            long pos = 0;
            vm_run(vt, vm_noise, &seed, 0.5, &pos, NULL, NULL);
            const int n = vm_run(vt, vm_noise, &seed, 1.0, &pos, l, NULL);
            air[k] = band_energy(l, n, 5000.0, 9000.0);
            dsp_cleanup(vt);
        }
        if (!(air[1] > 3.0 * air[0])) printf("  breath: 5-9 kHz %.2e -> %.2e\n", air[0], air[1]);
        CHECK(air[1] > 3.0 * air[0]);
        const vf_kv kv[] = { { "vf_breath", "1" } };
        vt_t *vt = vf_rig(kv, 1);
        vm_sine_t off = { 440.0, 0.0 };
        long pos = 0;
        vm_run(vt, vm_sine, &off, 0.5, &pos, NULL, NULL);
        const int n = vm_run(vt, vm_sine, &off, 0.5, &pos, l, NULL);
        CHECK(rms_of(l, n) < 1e-6);                                /* no hiss at rest */
        dsp_cleanup(vt);
    }
    /* Attack: the formants bloom in over the knob's time; a held note costs no level */
    {
        double t50[2], lvl[2];
        for (int k = 0; k < 2; k++) {
            const vf_kv kv[] = { { "vf_attack", k ? "1" : "0" } };
            vt_t *vt = vf_rig(kv, 1);
            vm_tone_t tn = { 190.0, 0.4, 8 };
            long pos = 0;
            vm_run(vt, vm_sine, &(vm_sine_t){ 440.0, 0.0 }, 0.5, &pos, NULL, NULL);
            const int n = vm_run(vt, vm_tone, &tn, 1.0, &pos, l, NULL);
            lvl[k] = rms_of(l + n - 4410, 4410);
            t50[k] = -1.0;
            for (int w = 0; w + 220 < n && t50[k] < 0; w += 110)
                if (rms_of(l + w, 220) > 0.5 * lvl[k]) t50[k] = w * 1000.0 / VT_SAMPLE_RATE;
            dsp_cleanup(vt);
        }
        if (!(t50[0] >= 0 && t50[1] > 2.0 * t50[0] + 15.0 && fabs(20.0 * log10(lvl[1] / lvl[0])) < 1.5))
            printf("  attack: half level after %.1f ms vs %.1f ms, held level %+.1f dB\n", t50[0], t50[1], 20.0 * log10(lvl[1] / lvl[0]));
        CHECK(t50[0] >= 0 && t50[1] > 2.0 * t50[0] + 15.0);
        CHECK(fabs(20.0 * log10(lvl[1] / lvl[0])) < 1.5);
    }
    /* Release: the vowel rings on after the voice stops, and at 0 it does not */
    {
        double tail[2], during[2];
        for (int k = 0; k < 2; k++) {
            const vf_kv kv[] = { { "vf_release", k ? "1" : "0" } };
            vt_t *vt = vf_rig(kv, 1);
            vm_tone_t tn = { 190.0, 0.4, 8 };
            vm_sine_t off = { 440.0, 0.0 };
            long pos = 0;
            const int n1 = vm_run(vt, vm_tone, &tn, 0.6, &pos, l, NULL);
            during[k] = rms_of(l + n1 - 4410, 4410);
            const int n2 = vm_run(vt, vm_sine, &off, 0.6, &pos, r, NULL);
            tail[k] = rms_of(r + 6615, n2 - 6615);                 /* 150 ms on */
            dsp_cleanup(vt);
        }
        if (!(tail[1] > 10.0 * tail[0] + 1e-6 && tail[0] < 0.02 * during[0] && tail[1] > 0.02 * during[1]))
            printf("  release: tail %.2e -> %.2e (voice %.2e -> %.2e)\n", tail[0], tail[1], during[0], during[1]);
        CHECK(tail[0] < 0.02 * during[0]);
        CHECK(tail[1] > 0.02 * during[1]);
        CHECK(tail[1] > 10.0 * tail[0] + 1e-6);
    }
    /* Mix/Drive: at 0 the voice passes dry (a 6 kHz tone the formants almost stop), at the knee (0.7) it is all formants */
    {
        double a6[2];
        for (int k = 0; k < 2; k++) {
            const vf_kv kv[] = { { "vf_mix", k ? "0.7" : "0" } };
            vt_t *vt = vf_rig(kv, 1);
            vm_sine_t s = { 6000.0, 0.2 };
            long pos = 0;
            vm_run(vt, vm_sine, &s, 0.5, &pos, NULL, NULL);
            const int n = vm_run(vt, vm_sine, &s, 0.5, &pos, l, NULL);
            a6[k] = goertzel(l, n, 6000.0);
            dsp_cleanup(vt);
        }
        if (!(a6[0] > 10.0 * a6[1])) printf("  mix: 6 kHz %.2e (dry) vs %.2e (wet)\n", a6[0], a6[1]);
        CHECK(a6[0] > 10.0 * a6[1]);
    }
    /* ... and above the knee the voice stays all wet while the drive climbs: more harmonics, no change in level */
    {
        double thd[3], lv[3];
        static const char *const m[3] = { "0.7", "0.85", "1" };
        for (int k = 0; k < 3; k++) {
            const vf_kv kv[] = { { "vf_mix", m[k] } };
            vt_t *vt = vf_rig(kv, 1);
            vm_sine_t s = { 600.0, 0.15 };
            long pos = 0;
            vm_run(vt, vm_sine, &s, 0.6, &pos, NULL, NULL);
            const int n = vm_run(vt, vm_sine, &s, 0.6, &pos, l, NULL);
            thd[k] = (goertzel(l, n, 1200.0) + goertzel(l, n, 1800.0) + goertzel(l, n, 2400.0)) / goertzel(l, n, 600.0);
            lv[k] = rms_of(l, n);
            dsp_cleanup(vt);
        }
        if (!(thd[1] > 2.0 * thd[0] && thd[2] > 2.0 * thd[1] && fabs(20.0 * log10(lv[2] / lv[0])) < 4.0))
            printf("  drive: harmonics %.2e %.2e %.2e, level %+.1f dB\n", thd[0], thd[1], thd[2], 20.0 * log10(lv[2] / lv[0]));
        CHECK(thd[1] > 2.0 * thd[0] && thd[2] > 2.0 * thd[1]);
        CHECK(fabs(20.0 * log10(lv[2] / lv[0])) < 4.0);
    }
    /* Level: a working-level voice lands near the vocoder side's loudness, under the headroom ceiling, at every vowel */
    for (int v = 0; v < 5; v++) {
        char b[8];
        snprintf(b, sizeof b, "%g", v * 0.25);
        const vf_kv kv[] = { { "vf_vowel", b }, { "vf_mix", "0.85" }, { "vf_choir", "0.4" }, { "vf_breath", "0.2" }, { "vf_attack", "0.15" }, { "vf_release", "0.25" } };
        vt_t *vt = vf_rig(kv, 6);
        vm_vowel_t vw = { 180.0, 800.0, 0.4 };
        long pos = 0;
        vm_run(vt, vm_vowel, &vw, 1.0, &pos, NULL, NULL);
        const int n = vm_run(vt, vm_vowel, &vw, 1.5, &pos, l, NULL);
        double ss = 0, pk = 0;
        for (int i = 0; i < n; i++) { ss += (double)l[i] * l[i]; pk = fmax(pk, fabs(l[i])); }
        const double db = 10.0 * log10(ss / n + 1e-12);
        if (!(db > -30.0 && db < -12.0 && pk <= (double)TINYVOICE_OUTPUT_HEADROOM)) printf("  voice fx vowel %d: %.1f dBFS rms, peak %.3f\n", v, db, pk);
        CHECK(db > -30.0 && db < -12.0);
        CHECK(pk <= (double)TINYVOICE_OUTPUT_HEADROOM);
        dsp_cleanup(vt);
    }
}

/* Voice FX has its own page and knows nothing of the vocoder side's: nothing set there can change it. */
static void test_voicefx_isolation(void)
{
    static float a[VT_SAMPLE_RATE], b[VT_SAMPLE_RATE];
    static const vf_kv vf[] = { { "vf_vowel", "0.4" }, { "vf_tract", "0.62" }, { "vf_peak", "0.7" }, { "vf_choir", "0.6" },
                                { "vf_breath", "0.5" }, { "vf_attack", "0.3" }, { "vf_release", "0.4" }, { "vf_mix", "0.8" } };
    static const vf_kv away[] = { { "mode", "Scream" }, { "key", "F#" }, { "scale", "Major" }, { "retune", "300" }, { "char1", "1" },
                                  { "char2", "0" }, { "char4", "1" }, { "char5", "0" }, { "mod", "1" }, { "gate", "-6 dB" },
                                  { "mix", "0.1" }, { "gain", "12" }, { "spread", "1" }, { "tone", "0.1" }, { "glide", "500" },
                                  { "beauty", "1" }, { "latch", "On" } };
    for (int k = 0; k < 2; k++) {
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "engine", "VoiceFX");
        if (k) for (size_t i = 0; i < sizeof away / sizeof away[0]; i++) dsp_set_param(vt, away[i].k, away[i].v);
        for (size_t i = 0; i < sizeof vf / sizeof vf[0]; i++) dsp_set_param(vt, vf[i].k, vf[i].v);
        if (k) { note_on(vt, 60); note_on(vt, 64); }
        vm_tone_t tn = { 190.0, 0.3, 8 };
        long pos = 0;
        vm_run(vt, vm_tone, &tn, 0.5, &pos, NULL, NULL);
        vm_run(vt, vm_tone, &tn, 1.0, &pos, k ? b : a, NULL);
        dsp_cleanup(vt);
    }
    int same = 1;
    for (int i = 0; i < VT_SAMPLE_RATE; i++) if (a[i] != b[i]) same = 0;
    CHECK(same);                                                   /* bit-identical: Mode, Key, Gate, Level, Mix, Tone, Spread, pads... are not read */
    CHECK(rms_of(a, VT_SAMPLE_RATE) > 1e-3);

    /* Engine names: the option name in any case (the host's Mode page may hand back its own spelling), or the index */
    vt_t *vt = dsp_init(VT_SAMPLE_RATE);
    char v[16];
    CHECK(dsp_set_param(vt, "engine", "voicefx") == 0 && dsp_get_param(vt, "engine", v, sizeof v) > 0 && strcmp(v, "VoiceFX") == 0);
    CHECK(dsp_set_param(vt, "engine", "VOCODER") == 0 && dsp_get_param(vt, "engine", v, sizeof v) > 0 && strcmp(v, "Vocoder") == 0);
    CHECK(dsp_set_param(vt, "engine", "1") == 0 && dsp_get_param(vt, "engine", v, sizeof v) > 0 && strcmp(v, "VoiceFX") == 0);
    CHECK(dsp_set_param(vt, "engine", "Talkbox") == -1);

    /* A change of engine pulses is_loading, which is what makes the host re-read chain_params and its pages */
    static float z[2 * 128];
    for (int i = 0; i < VT_SAMPLE_RATE / 128 + 2; i++) dsp_process_block_f(vt, z, 128);
    dsp_set_param(vt, "engine", "Vocoder");
    dsp_process_block_f(vt, z, 128);
    CHECK(dsp_get_param(vt, "is_loading", v, sizeof v) > 0 && strcmp(v, "1") == 0);
    for (int i = 0; i < 400; i++) dsp_process_block_f(vt, z, 128);
    CHECK(dsp_get_param(vt, "is_loading", v, sizeof v) > 0 && strcmp(v, "0") == 0);
    dsp_cleanup(vt);
}

/* Switching engines on a held voice does not click, and every engine path is finite and bounded. */
static void test_voicefx_transitions(void)
{
    static float out[4 * VT_SAMPLE_RATE];
    double steady[2] = { 0, 0 };
    for (int run = 0; run < 3; run++) {                            /* 0: Vocoder alone, 1: Voice FX alone, 2: switching every 250 ms */
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "mode", "Snap");
        vm_tone_t tn = { 190.0, 0.3, 8 };
        long pos = 0;
        int n = 0;
        for (int seg = 0; seg < 12; seg++) {
            dsp_set_param(vt, "engine", run == 1 || (run == 2 && (seg & 1)) ? "VoiceFX" : "Vocoder");
            n += vm_run(vt, vm_tone, &tn, 0.25, &pos, out + n, NULL);
        }
        double worst = 0;
        int bad = 0;
        for (int i = 1; i < n; i++) {
            if (!isfinite(out[i]) || fabsf(out[i]) > 1.0f) bad++;
            if (i > VT_SAMPLE_RATE / 2) worst = fmax(worst, fabs(out[i] - out[i - 1]));
        }
        CHECK(bad == 0);
        if (run < 2) steady[run] = worst; else {
            if (!(worst < 3.0 * fmax(steady[0], steady[1]))) printf("  engine switch: largest step %.3f, steady %.3f / %.3f\n", worst, steady[0], steady[1]);
            CHECK(worst < 3.0 * fmax(steady[0], steady[1]));       /* a fade, not a cut */
        }
        dsp_cleanup(vt);
    }
}

/* Voice FX under every kind of input and extreme knobs: finite, under the ceiling, and nothing denormal left after. */
static void test_voicefx_numeric_safety(void)
{
    static const char *const sets[3][8] = {
        { "0", "0", "0", "0", "0", "0", "0", "0" },
        { "1", "1", "1", "1", "1", "1", "1", "1" },
        { "0.5", "0.5", "0.5", "0.5", "0.5", "0.5", "0.5", "0.5" },
    };
    static const char *const keys[8] = { "vf_vowel", "vf_tract", "vf_peak", "vf_choir", "vf_breath", "vf_attack", "vf_release", "vf_mix" };
    static float blk[2 * 128];
    int bad = 0, over = 0, subnormal = 0;
    double peak = 0;
    for (int s = 0; s < 3; s++)
        for (int kind = 0; kind < 6; kind++) {
            vt_t *vt = dsp_init(VT_SAMPLE_RATE);
            dsp_set_param(vt, "engine", "VoiceFX");
            for (int k = 0; k < 8; k++) dsp_set_param(vt, keys[k], sets[s][k]);
            unsigned seed = 4242u + (unsigned)(s * 10 + kind);
            long pos = 0;
            for (int bn = 0; bn < 260; bn++) {                       /* ~0.75 s */
                for (int i = 0; i < 128; i++, pos++) blk[2 * i] = blk[2 * i + 1] = vm_wave(kind, pos, &seed);
                dsp_process_block_f(vt, blk, 128);
                for (int i = 0; i < 256; i++) {
                    if (!isfinite(blk[i])) bad++;
                    else { peak = fmax(peak, fabs(blk[i])); if (fabsf(blk[i]) > TINYVOICE_OUTPUT_HEADROOM) over++; }
                }
            }
            for (int k = 0; k < 8; k++) dsp_set_param(vt, keys[k], "0");
            for (int bn = 0; bn < 420; bn++) {                       /* 1.2 s of silence: the tail rings down */
                memset(blk, 0, sizeof blk);
                dsp_process_block_f(vt, blk, 128);
                if (bn > 200)
                    for (int i = 0; i < 256; i++) { if (!isfinite(blk[i])) bad++; if (fpclassify(blk[i]) == FP_SUBNORMAL) subnormal++; }
            }
            dsp_cleanup(vt);
        }
    if (bad || over || subnormal) printf("  voice fx: non-finite %d, over the ceiling %d, subnormal %d (peak %.3f)\n", bad, over, subnormal, peak);
    CHECK(bad == 0);
    CHECK(over == 0);
    CHECK(subnormal == 0);
    CHECK(peak > 0.05);                                            /* and it is not silent */
}

/* ------------------------------------------- Voice FX audition groove */

static void test_groove(void)
{
    /* The pattern: 32 sixteenths, D1..D2, syncopated (hits off the beat), a real groove not a pulse */
    int notes = 0, offbeat = 0, rests = 0, lo = 999, hi = 0;
    for (int s = 0; s < GROOVE_STEPS; s++) {
        const int n = groove_note(s);
        if (n == 0) { rests++; continue; }
        notes++;
        if (s % 4) offbeat++;
        if (n < lo) lo = n;
        if (n > hi) hi = n;
    }
    CHECK(groove_note(-1) == 0 && groove_note(GROOVE_STEPS) == 0);
    CHECK(lo == 26 && hi == 38);                                            /* D1 (36.7 Hz) .. D2, in this module's numbering (C4 = 60) */
    CHECK(groove_note(0) == 26);                                            /* it starts on the root */
    CHECK(notes >= 12 && notes <= 20 && rests >= 12);
    CHECK(offbeat >= 5);                                                    /* syncopation */

    static float a[4 * VT_SAMPLE_RATE + 200];
    groove_t g;
    groove_init(&g, VT_SAMPLE_RATE);
    int bad = 0;
    double pk = 0;
    for (int i = 0; i < 4 * VT_SAMPLE_RATE + 200; i++) {
        a[i] = groove_process(&g);
        if (!isfinite(a[i])) bad++;
        pk = fmax(pk, fabs(a[i]));
    }
    CHECK(bad == 0);
    if (!(pk > 0.1 && pk <= 0.3)) printf("  groove peak %.3f\n", pk);
    CHECK(pk > 0.1 && pk <= 0.3);                                           /* about what Auto steers a voice to */

    /* Two bars at 120 bpm are exactly 4 s: the loop repeats sample for sample */
    {
        static float b[4 * VT_SAMPLE_RATE];
        groove_t h;
        groove_init(&h, VT_SAMPLE_RATE);
        for (int i = 0; i < 4 * VT_SAMPLE_RATE; i++) b[i] = groove_process(&h);
        double worst = 0;
        for (int i = 0; i < 200; i++) worst = fmax(worst, fabs(a[4 * VT_SAMPLE_RATE + i] - a[i]));
        CHECK(worst < 1e-6);
    }
    /* A hit is punchy: it reaches its peak within a few ms, and the step ends nearly silent (rests are rests) */
    {
        const int step = VT_SAMPLE_RATE / 8;                                 /* a sixteenth at 120 bpm: 5512 samples */
        double early = 0, late = 0;
        for (int i = 0; i < step / 8; i++) early = fmax(early, fabs(a[i]));
        for (int i = step - step / 10; i < step; i++) late = fmax(late, fabs(a[i]));   /* step 0 is a note: its last tenth, past the gate */
        if (!(early > 0.3 * pk && late < 0.1 * pk)) printf("  groove hit: early %.3f late %.3f of peak %.3f\n", early, late, pk);
        CHECK(early > 0.3 * pk);
        CHECK(late < 0.1 * pk);
        double rest = 0;
        for (int i = step; i < 3 * step; i++) rest = fmax(rest, fabs(a[i] ));   /* steps 1 and 2 are rests after the hit: only its tail */
        CHECK(rest < 0.1 * pk);
    }
    /* Bright and harmonic-dense: a D1 hit has its fundamental at 37 Hz, so a voice made of it is all harmonics, and a
     * saw's falling 1/k^2 still leaves real energy at 600 Hz .. 3 kHz, where the formants live (and the Voice FX input
     * high-pass takes the lowest ones away) */
    {
        /* measured at the harmonics themselves (a 37 Hz fundamental puts lines 37 Hz apart: a probe grid wider than
         * the 8 Hz lines of a 125 ms window would miss most of them) */
        const double f0 = 440.0 * pow(2.0, (26.0 - 69.0) / 12.0);
        double body = 0, mid = 0, top = 0;
        int lines = 0;
        for (int k = 1; k * f0 < 8000.0; k++) {
            const double e = goertzel(a, VT_SAMPLE_RATE / 8, k * f0), f = k * f0;
            if (f >= 120.0 && f < 600.0) body += e;
            else if (f >= 600.0 && f < 3000.0) { mid += e; lines++; }
            else if (f >= 3000.0) top += e;
        }
        if (!(mid > 0.03 * body && top > 0.0 && top < mid && lines > 50)) printf("  groove spectrum: 120-600 %.2e, 600-3k %.2e, 3-8k %.2e (%d lines)\n", body, mid, top, lines);
        CHECK(lines > 50);                                                  /* dense: dozens of harmonics through the formant range */
        CHECK(mid > 0.03 * body);                                           /* with real weight there (a saw pair puts ~5 %), past the 120 Hz high-pass */
        CHECK(top > 0.0 && top < mid);                                      /* and the pluck filter rolls it off: it is not a hiss */
    }
    /* The tempo follows the host: at 90 bpm the loop is 5.33 s, so the second hit comes later */
    {
        groove_t h;
        groove_init(&h, VT_SAMPLE_RATE);
        groove_set_bpm(&h, 90.0f);
        int first = -1, second = -1, last_loud = -100000;                     /* an onset is a loud sample long after the last one (a saw crosses any threshold every cycle) */
        for (int i = 0; i < VT_SAMPLE_RATE && second < 0; i++) {
            if (fabs(groove_process(&h)) > 0.03) {
                if (i - last_loud > 1500) { if (first < 0) first = i; else second = i; }
                last_loud = i;
            }
        }
        /* steps 0 and 3 are both hits: three sixteenths apart */
        const double want = 3.0 * 60.0 / 90.0 / 4.0 * VT_SAMPLE_RATE;
        if (!(second > 0 && fabs((second - first) - want) < 0.01 * VT_SAMPLE_RATE)) printf("  groove tempo: hits %d and %d, want %.0f apart\n", first, second, want);
        CHECK(second > 0 && fabs((second - first) - want) < 0.01 * VT_SAMPLE_RATE);
    }
}

/* Demo in Voice FX is the groove, wherever the audio would have come from; with Demo off the track passes. */
static void test_voicefx_audition(void)
{
    static float on_a[VT_SAMPLE_RATE], on_b[VT_SAMPLE_RATE], off_a[VT_SAMPLE_RATE];
    for (int k = 0; k < 3; k++) {                                          /* 0: Demo On, silence in; 1: Demo On, loud noise in; 2: Demo Off, the same noise */
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "engine", "VoiceFX");
        if (k < 2) dsp_set_param(vt, "demo", "On");
        unsigned seed = 99;
        long pos = 0;
        static float blk[2 * 128];
        int n = 0;
        for (int b = 0; b < 2 * VT_SAMPLE_RATE / 128; b++) {
            for (int i = 0; i < 128; i++, pos++) blk[2 * i] = blk[2 * i + 1] = k ? 0.6f * lcg_noise(&seed) : 0.0f;
            dsp_process_block_f(vt, blk, 128);
            for (int i = 0; i < 128; i++) { if (b >= VT_SAMPLE_RATE / 128 && n < VT_SAMPLE_RATE) (k == 0 ? on_a : k == 1 ? on_b : off_a)[n++] = blk[2 * i]; }
        }
        dsp_cleanup(vt);
    }
    int same = 1;
    for (int i = 0; i < VT_SAMPLE_RATE; i++) if (on_a[i] != on_b[i]) same = 0;
    CHECK(same);                                                           /* Demo On replaces the input entirely: bit-identical whatever came in */
    CHECK(rms_of(on_a, VT_SAMPLE_RATE) > 2e-3);                            /* and it is heard */
    int differs = 0;
    for (int i = 0; i < VT_SAMPLE_RATE; i++) if (off_a[i] != on_a[i]) differs = 1;
    CHECK(differs);                                                        /* Off: the track passes, not the groove */

    /* Every preset can be auditioned at once: finite, rhythmic (hits and gaps), under the -6.1 dBFS ceiling, audible */
    for (int p = 0; p < 6; p++) {
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "engine", "VoiceFX");
        char idx[8];
        snprintf(idx, sizeof idx, "%d", p);
        dsp_set_param(vt, "vf_preset", idx);
        dsp_set_param(vt, "demo", "On");
        static float blk[2 * 128], l[4 * VT_SAMPLE_RATE];
        int n = 0, badn = 0;
        double pk = 0;
        for (int b = 0; b < 5 * VT_SAMPLE_RATE / 128; b++) {
            memset(blk, 0, sizeof blk);
            dsp_process_block_f(vt, blk, 128);
            for (int i = 0; i < 128; i++) {
                if (!isfinite(blk[2 * i])) badn++;
                pk = fmax(pk, fabs(blk[2 * i]));
                if (b >= VT_SAMPLE_RATE / 128 && n < 4 * VT_SAMPLE_RATE) l[n++] = blk[2 * i];
            }
        }
        /* 50 ms windows over the 4 s loop: the loudest and quietest */
        double hi = 0, lo2 = 1e9;
        for (int w = 0; w + 2205 <= n; w += 2205) { const double r = rms_of(l + w, 2205); if (r > hi) hi = r; if (r < lo2) lo2 = r; }
        const double db = 20.0 * log10(rms_of(l, n) + 1e-12);
        if (!(badn == 0 && pk <= (double)TINYVOICE_OUTPUT_HEADROOM && db > -40.0 && hi > 4.0 * lo2))
            printf("  audition preset %d: peak %.3f, rms %.1f dBFS, loudest/quietest window %.1fx\n", p, pk, db, hi / (lo2 + 1e-12));
        CHECK(badn == 0);
        CHECK(pk <= (double)TINYVOICE_OUTPUT_HEADROOM);
        CHECK(db > -40.0);
        CHECK(hi > 4.0 * lo2);                                             /* a groove: notes and gaps */
        dsp_cleanup(vt);
    }

    /* The vocoder's Demo is unchanged: the sung phrase, however the groove runs */
    {
        static float va[VT_SAMPLE_RATE], vb[VT_SAMPLE_RATE];
        for (int k = 0; k < 2; k++) {
            vt_t *vt = dsp_init(VT_SAMPLE_RATE);
            dsp_set_param(vt, "mode", "Snap");
            if (k) { dsp_set_param(vt, "engine", "VoiceFX"); dsp_set_param(vt, "engine", "Vocoder"); }   /* a visit to Voice FX leaves no trace */
            dsp_set_param(vt, "demo", "On");
            static float blk[2 * 128];
            int n = 0;
            for (int b = 0; b < 2 * VT_SAMPLE_RATE / 128; b++) {
                memset(blk, 0, sizeof blk);
                dsp_process_block_f(vt, blk, 128);
                for (int i = 0; i < 128; i++) if (b >= VT_SAMPLE_RATE / 128 && n < VT_SAMPLE_RATE) (k ? vb : va)[n++] = blk[2 * i];
            }
            dsp_cleanup(vt);
        }
        int same = 1;
        for (int i = 0; i < VT_SAMPLE_RATE; i++) if (va[i] != vb[i]) same = 0;
        CHECK(same);
        CHECK(rms_of(va, VT_SAMPLE_RATE) > 1e-3);
    }
}

/* ------------------------------------------------ Voice FX presets */

static void test_voicefx_presets(void)
{
    static const char *const names[6] = { "Yeah Boy", "Robo Talk", "Monster Throat", "Funky Talkbox", "Alien Choir", "Vowel Morph Solo" };
    static const char *const keys[VT_PRESET_VALUES] = { "vf_vowel", "vf_tract", "vf_peak", "vf_choir", "vf_breath", "vf_attack", "vf_release", "vf_mix" };
    char b[64];

    /* The bank is filtered by engine: six for Voice FX, none for the Vocoder */
    CHECK(vt_preset_count(VT_ENGINE_VOICEFX) == 6);
    CHECK(vt_preset_count(VT_ENGINE_VOCODER) == 0);
    CHECK(vt_preset_get(VT_ENGINE_VOCODER, 0) == NULL);
    CHECK(vt_preset_get(VT_ENGINE_VOICEFX, 6) == NULL && vt_preset_get(VT_ENGINE_VOICEFX, -1) == NULL);
    CHECK(VT_P_VF_MIX - VT_P_VF_VOWEL + 1 == VT_PRESET_VALUES);          /* the table's order is the page's */
    for (int i = 0; i < 6; i++) {
        const vt_preset *p = vt_preset_get(VT_ENGINE_VOICEFX, i);
        CHECK(p && p->engine == VT_ENGINE_VOICEFX && strcmp(p->name, names[i]) == 0 && strlen(p->name) <= 16);
        for (int k = 0; p && k < VT_PRESET_VALUES; k++) CHECK(p->v[k] >= 0.0f && p->v[k] <= 1.0f);
        CHECK(((uintptr_t)p % _Alignof(vt_preset)) == 0);
    }

    /* The page's defaults are the last preset's (Vowel Morph Solo) */
    {
        const vt_preset *p = vt_preset_get(VT_ENGINE_VOICEFX, 5);
        for (int k = 0; p && k < VT_PRESET_VALUES; k++) CHECK(fabs((double)dsp_param_def(VT_P_VF_VOWEL + k)->def - (double)p->v[k]) < 1e-6);
        CHECK((int)dsp_param_def(VT_P_VF_PRESET)->def == 5 && (int)dsp_param_def(VT_P_VF_PRESET)->max == 5);
        CHECK(dsp_param_def(VT_P_VF_PRESET)->transient);
    }

    /* What the presets say, in the terms of the brief */
    {
        const vt_preset *yeah = vt_preset_get(VT_ENGINE_VOICEFX, 0), *robo = vt_preset_get(VT_ENGINE_VOICEFX, 1);
        const vt_preset *monster = vt_preset_get(VT_ENGINE_VOICEFX, 2), *funky = vt_preset_get(VT_ENGINE_VOICEFX, 3);
        const vt_preset *alien = vt_preset_get(VT_ENGINE_VOICEFX, 4);
        CHECK(yeah->v[7] >= 0.9f && yeah->v[2] >= 0.75f && yeah->v[5] <= 0.05f);           /* hard drive, high resonance, snappy */
        CHECK(robo->v[0] >= 0.75f && robo->v[2] >= 0.8f && fabs((robo->v[1] - 0.5) * 1.2) < 1e-6);   /* o..u window, high Q, neutral tract */
        CHECK(fabs((monster->v[1] - 0.5) * 1.2 + 0.35) < 0.005 && monster->v[7] >= 0.95f && monster->v[5] == 0.0f);   /* tract -0.35 oct, hard drive, hard kick */
        CHECK(fabs((alien->v[1] - 0.5) * 1.2 - 0.4) < 0.005 && alien->v[3] == 1.0f && alien->v[4] >= 0.8f);   /* tract +0.4 oct, full width, air */
        CHECK(funky->v[7] <= VFX_MIX_KNEE && fabs(funky->v[2] - 0.5) < 0.1f && funky->v[5] <= 0.06f);          /* clean, medium Q, fast */
    }

    /* Loading: the browser writes the index, the eight knobs follow, the name and count are served */
    {
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "engine", "VoiceFX");
        CHECK(dsp_get_param(vt, "vf_preset_count", b, sizeof b) > 0 && strcmp(b, "6") == 0);
        for (int i = 0; i < 6; i++) {
            char idx[8];
            snprintf(idx, sizeof idx, "%d", i);
            CHECK(dsp_set_param(vt, "vf_preset", idx) == 0);
            const vt_preset *p = vt_preset_get(VT_ENGINE_VOICEFX, i);
            for (int k = 0; k < VT_PRESET_VALUES; k++) {
                CHECK(dsp_get_param(vt, keys[k], b, sizeof b) > 0 && fabs(atof(b) - (double)p->v[k]) < 1e-4);
            }
            CHECK(dsp_get_param(vt, "vf_preset", b, sizeof b) > 0 && atoi(b) == i);
            CHECK(dsp_get_param(vt, "vf_preset_name", b, sizeof b) > 0 && strcmp(b, names[i]) == 0);
        }
        CHECK(dsp_set_param(vt, "vf_preset", "99") == 0 && dsp_get_param(vt, "vf_preset", b, sizeof b) > 0 && atoi(b) == 5);   /* clamps to the last */
        CHECK(dsp_set_param(vt, "vf_preset", "-4") == 0 && dsp_get_param(vt, "vf_preset", b, sizeof b) > 0 && atoi(b) == 0);
        /* a saved state carries the knobs, not the preset index (a restore must not re-apply a preset over them) */
        static char st[1536];
        CHECK(dsp_get_param(vt, "state", st, sizeof st) > 0 && strstr(st, "vf_preset") == NULL && strstr(st, "\"vf_vowel\"") != NULL);
        dsp_cleanup(vt);
    }

    /* Presets never touch the Vocoder: loading one while it runs changes nothing it plays */
    {
        static float a[VT_SAMPLE_RATE], c[VT_SAMPLE_RATE];
        for (int k = 0; k < 2; k++) {
            vt_t *vt = dsp_init(VT_SAMPLE_RATE);
            dsp_set_param(vt, "mode", "Robot");
            if (k) { dsp_set_param(vt, "vf_preset", "2"); dsp_set_param(vt, "vf_preset", "4"); }
            note_on(vt, 60);
            vm_tone_t tn = { 190.0, 0.3, 8 };
            long pos = 0;
            vm_run(vt, vm_tone, &tn, 0.3, &pos, NULL, NULL);
            vm_run(vt, vm_tone, &tn, 1.0, &pos, k ? c : a, NULL);
            dsp_cleanup(vt);
        }
        int same = 1;
        for (int i = 0; i < VT_SAMPLE_RATE; i++) if (a[i] != c[i]) same = 0;
        CHECK(same);
    }

    /* Every preset under every kind of input stays finite and under the -6.1 dBFS ceiling, and a voice lands at a usable level */
    {
        static float blk[2 * 128];
        double worst = 0;
        int bad = 0, over = 0;
        for (int i = 0; i < 6; i++)
            for (int kind = 0; kind < 6; kind++) {
                vt_t *vt = dsp_init(VT_SAMPLE_RATE);
                dsp_set_param(vt, "engine", "VoiceFX");
                char idx[8];
                snprintf(idx, sizeof idx, "%d", i);
                dsp_set_param(vt, "vf_preset", idx);
                dsp_set_param(vt, "gain", "12");                     /* not read by Voice FX: the ceiling is its own */
                unsigned seed = 31u + (unsigned)(i * 7 + kind);
                long pos = 0;
                for (int bn = 0; bn < 400; bn++) {                    /* ~1.2 s */
                    for (int s = 0; s < 128; s++, pos++) blk[2 * s] = blk[2 * s + 1] = vm_wave(kind, pos, &seed);
                    dsp_process_block_f(vt, blk, 128);
                    for (int s = 0; s < 256; s++) {
                        if (!isfinite(blk[s])) bad++;
                        else { worst = fmax(worst, fabs(blk[s])); if (fabsf(blk[s]) > TINYVOICE_OUTPUT_HEADROOM) over++; }
                    }
                }
                dsp_cleanup(vt);
            }
        if (bad || over) printf("  presets: non-finite %d, over the ceiling %d (peak %.3f)\n", bad, over, worst);
        CHECK(bad == 0 && over == 0);
        CHECK(worst > 0.1);                                           /* they sound */
        static float l[VT_SAMPLE_RATE * 2];
        for (int i = 0; i < 6; i++) {
            vt_t *vt = dsp_init(VT_SAMPLE_RATE);
            dsp_set_param(vt, "engine", "VoiceFX");
            char idx[8];
            snprintf(idx, sizeof idx, "%d", i);
            dsp_set_param(vt, "vf_preset", idx);
            vm_vowel_t vw = { 180.0, 800.0, 0.4 };
            long pos = 0;
            vm_run(vt, vm_vowel, &vw, 1.0, &pos, NULL, NULL);
            const int n = vm_run(vt, vm_vowel, &vw, 1.5, &pos, l, NULL);
            double ss = 0, pk = 0;
            for (int s = 0; s < n; s++) { ss += (double)l[s] * l[s]; pk = fmax(pk, fabs(l[s])); }
            const double db = 10.0 * log10(ss / n + 1e-12);
            if (!(db > -32.0 && db < -10.0 && pk <= (double)TINYVOICE_OUTPUT_HEADROOM)) printf("  preset %d (%s): %.1f dBFS rms, peak %.3f\n", i, names[i], db, pk);
            CHECK(db > -32.0 && db < -10.0);
            CHECK(pk <= (double)TINYVOICE_OUTPUT_HEADROOM);
            dsp_cleanup(vt);
        }
        /* switching presets on a held voice is finite and bounded, however fast the browser is turned */
        vt_t *vt = dsp_init(VT_SAMPLE_RATE);
        dsp_set_param(vt, "engine", "VoiceFX");
        unsigned seed = 5;
        long pos = 0;
        int sw_bad = 0;
        for (int bn = 0; bn < 600; bn++) {
            char idx[8];
            snprintf(idx, sizeof idx, "%d", (bn / 3) % 6);
            dsp_set_param(vt, "vf_preset", idx);
            for (int s = 0; s < 128; s++, pos++) blk[2 * s] = blk[2 * s + 1] = 0.4f * tone(190.0, (int)pos, 8, VT_SAMPLE_RATE) + 0.02f * lcg_noise(&seed);
            dsp_process_block_f(vt, blk, 128);
            for (int s = 0; s < 256; s++) if (!isfinite(blk[s]) || fabsf(blk[s]) > 1.0f) sw_bad++;
        }
        CHECK(sw_bad == 0);
        dsp_cleanup(vt);
    }
}

/* ------------------------------------------------- MIDI: who listens */

static void test_midi_agnostic_modes(void)
{
    /* Scream, Dalek, Walkie and Tape ignore pads altogether: the same input gives the same bits.
     * HardTune, Harmony and Nightcall follow them. */
    static float a[VT_SAMPLE_RATE / 2], b[VT_SAMPLE_RATE / 2];
    static const char *const agnostic[4] = { "Scream", "Dalek", "Walkie", "Tape" };
    static const char *const follows[3] = { "HardTune", "Harmony", "Nightcall" };
    for (int k = 0; k < 7; k++) {
        const char *mode = k < 4 ? agnostic[k] : follows[k - 4];
        vt_t *x = vm_rig(mode, 0.5f, 0.5f, 0.0f, 0.5f, 0.5f), *y = vm_rig(mode, 0.5f, 0.5f, 0.0f, 0.5f, 0.5f);
        vm_vowel_t vw = { 180.0, 800.0, 0.4 };
        long px = 0, py = 0;
        vm_run(x, vm_vowel, &vw, 0.2, &px, NULL, NULL);
        vm_run(y, vm_vowel, &vw, 0.2, &py, NULL, NULL);
        note_on(y, 64); note_on(y, 67); note_on(y, 71);                      /* pads on the slot's own channel */
        vm_run(x, vm_vowel, &vw, 0.5, &px, a, NULL);
        vm_run(y, vm_vowel, &vw, 0.5, &py, b, NULL);
        int same = memcmp(a, b, sizeof a) == 0;
        if (k < 4) { if (!same) printf("  %s changed with pads held\n", mode); CHECK(same); }
        else { if (same) printf("  %s ignored the pads\n", mode); CHECK(!same); }
        dsp_cleanup(x); dsp_cleanup(y);
    }
    /* other tracks' MIDI never reaches any of them (broadcasts are dropped whole) */
    for (int k = 0; k < 6; k++) {
        vt_t *x = vm_rig(k_new_modes[k], 0.5f, 0.5f, 0.0f, 0.5f, 0.5f), *y = vm_rig(k_new_modes[k], 0.5f, 0.5f, 0.0f, 0.5f, 0.5f);
        vm_vowel_t vw = { 180.0, 800.0, 0.4 };
        long px = 0, py = 0;
        uint8_t m[3] = { 0x91, 64, 100 };
        dsp_set_midi_source(y, VT_MIDI_SOURCE_BROADCAST);
        dsp_handle_midi(y, m, 3);
        const int n1 = vm_run(x, vm_vowel, &vw, 0.4, &px, a, NULL);
        vm_run(y, vm_vowel, &vw, 0.4, &py, b, NULL);
        CHECK(memcmp(a, b, sizeof(float) * (size_t)n1) == 0);
        CHECK(dsp_active_voices(y) == 0);
        dsp_cleanup(x); dsp_cleanup(y);
    }
    /* Latch: Nightcall and Harmony keep a released chord; the pitch-agnostic ones don't care */
    {
        vt_t *x = vm_rig("Harmony", 0.5f, 0.5f, 0.0f, 0.5f, 0.5f);
        dsp_set_param(x, "latch", "On");
        note_on(x, 60); note_on(x, 64); note_on(x, 67);
        static float z[2 * 128];
        dsp_process_block_f(x, z, 128);
        note_off(x, 60); note_off(x, 64); note_off(x, 67);
        dsp_process_block_f(x, z, 128);
        char buf[512];
        CHECK(dsp_get_param(x, "diag", buf, sizeof buf) > 0 && strstr(buf, "latch 60 64 67") != NULL);
        dsp_cleanup(x);
    }
}

/* ---------------------------------------------------------- latency */

static void test_latency_budget(void)
{
    /* every new mode's wet path is under 15 ms algorithmic latency, at its slowest setting */
    const double lim = 0.015 * VT_SAMPLE_RATE;
    {
        static vox_t v;
        vox_init(&v, VT_SAMPLE_RATE);
        CHECK(vox_latency(&v, VT_MODE_DALEK) < lim);
        vox_init(&v, VT_SAMPLE_RATE);
        const int none[4] = { -1, -1, -1, -1 };
        for (int i = 0; i < 20000; i++) {
            if (i % 128 == 0) vox_set_harmony(&v, 0.5f, 0.5f, 0.0f, 1.0f, 1.0f, 0.5f, none, 0.0f);
            float a, b;
            vox_process(&v, VT_MODE_HARMONY, 0.1f, 1, &a, &b);
        }
        CHECK(vox_latency(&v, VT_MODE_HARMONY) < lim && vox_latency(&v, VT_MODE_WALKIE) == 0.0f);
    }
    /* Scream and HardTune: PSOLA at its worst (a 70 Hz voice, formant scaled) */
    static cher_t c;
    static fx_t fx;
    enum { M = VT_SAMPLE_RATE };
    static float l[M], r[M];
    for (int mode = VT_MODE_SCREAM; mode <= VT_MODE_HARDTUNE; mode += VT_MODE_HARDTUNE - VT_MODE_SCREAM) {
        const int none[FX_VOICES] = { -1, -1, -1, -1 };
        cher_init(&c, VT_SAMPLE_RATE);
        fx_init(&fx, VT_SAMPLE_RATE);
        cher_set_params(&c, 0, VT_SCALE_CHROMATIC, mode == VT_MODE_HARDTUNE ? -1.0f : 0.0f, -60.0f, -1, 0.0f);
        for (int i = 0; i < M; i++) {
            if (i % 128 == 0) { fx_set(&fx, &c, mode, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, VT_SCALE_CHROMATIC, none); fx_set_extra(&fx, 0.5f, 0.5f); }
            fx_process(&fx, &c, 0.5f * tone(70.0, i, 6, VT_SAMPLE_RATE), &l[i], &r[i]);
        }
        const float lat = fx_latency(&fx, &c);
        if (!(lat > 0.0f && lat < lim)) printf("  mode %d latency %.1f samples (%.1f ms)\n", mode, (double)lat, 1000.0 * lat / VT_SAMPLE_RATE);
        CHECK(lat > 0.0f && lat < lim);
    }
}

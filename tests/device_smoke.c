#define _GNU_SOURCE   /* clock_gettime, M_PI */
/*
 * On-device smoke test: loads the installed module the way Schwung's chain does
 * (dlopen by path, dlsym move_audio_fx_init_v2, create_instance) and exercises
 * every callback through the real ABI structs.
 *
 *   device_smoke /data/UserData/schwung/modules/audio_fx/tinyvoice/tinyvoice.so
 *
 * Exits 0 only if everything works. Built and run by scripts/smoke_on_device.sh.
 */
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audio_fx_api_v2.h"

static int g_fail;
#define CHECK(c) do { if (!(c)) { g_fail++; printf("  FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static void host_log(const char *m) { printf("  [host log] %s\n", m); }

/* The slot's receive channel as the chain host answers it: -2 = no slot (omni). */
static int g_recv = -2;
static int host_slot_recv_channel(void *inst) { (void)inst; return g_recv; }

static void run_audio(audio_fx_api_v2_t *api, void *inst, double hz, int blocks, int *nonzero, int *bad)
{
    static int16_t buf[2 * 128];
    static long pos;
    for (int b = 0; b < blocks; b++) {
        for (int i = 0; i < 128; i++, pos++)
            buf[2 * i] = buf[2 * i + 1] = (int16_t)(8000.0 * sin(2.0 * M_PI * hz * (double)pos / 44100.0));
        api->process_block(inst, buf, 128);
        for (int i = 0; i < 256; i++) { if (buf[i]) *nonzero = 1; }
    }
    (void)bad;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s path/to/module.so\n", argv[0]); return 2; }

    void *h = dlopen(argv[1], RTLD_NOW);
    if (!h) { printf("FAIL dlopen: %s\n", dlerror()); return 1; }
    printf("dlopen ok: %s\n", argv[1]);

    audio_fx_init_v2_fn init = (audio_fx_init_v2_fn)dlsym(h, AUDIO_FX_INIT_V2_SYMBOL);
    if (!init) { printf("FAIL dlsym(%s): %s\n", AUDIO_FX_INIT_V2_SYMBOL, dlerror()); return 1; }
    printf("dlsym ok: %s\n", AUDIO_FX_INIT_V2_SYMBOL);

    host_api_v1_t host;
    memset(&host, 0, sizeof host);
    host.api_version = MOVE_PLUGIN_API_VERSION;
    host.sample_rate = MOVE_SAMPLE_RATE;
    host.frames_per_block = MOVE_FRAMES_PER_BLOCK;
    host.log = host_log;
    host.slot_recv_channel = host_slot_recv_channel;

    audio_fx_api_v2_t *api = init(&host);
    CHECK(api != NULL);
    if (!api) return 1;
    CHECK(api->api_version == AUDIO_FX_API_VERSION_2);
    CHECK(api->create_instance && api->destroy_instance && api->process_block &&
          api->set_param && api->get_param && api->on_midi);
    if (g_fail) return 1;

    void *inst = api->create_instance("/data/UserData/schwung/modules/audio_fx/tinyvoice",
                                      "{\"mix\":0.8,\"unknown\":{\"x\":1}}");   /* config defaults path */
    CHECK(inst != NULL);
    if (!inst) return 1;

    char buf[1024];
    int n = api->get_param(inst, "mix", buf, sizeof buf);
    CHECK(n > 0 && atof(buf) > 0.79 && atof(buf) < 0.81);        /* config_json was applied */
    printf("  mix after config_json: %s\n", buf);

    int nonzero = 0, bad = 0;
    api->set_param(inst, "mode", "Snap");
    api->set_param(inst, "mix", "1");
    run_audio(api, inst, 220.0, 200, &nonzero, &bad);
    CHECK(nonzero);
    n = api->get_param(inst, "viz", buf, sizeof buf);
    printf("  Cher viz: %s\n", buf);
    CHECK(n > 0 && buf[0] == 'c');

    const uint8_t on[3] = {0x90, 60, 100}, off[3] = {0x80, 60, 0};
    api->on_midi(inst, on, 3, MOVE_MIDI_SOURCE_INTERNAL);
    api->get_param(inst, "voices", buf, sizeof buf);
    CHECK(atoi(buf) == 1);

    /* The chain host finds MIDI-capable FX by dlsym("move_audio_fx_on_midi"). */
    void (*on_midi_sym)(void *, const uint8_t *, int, int) =
        (void (*)(void *, const uint8_t *, int, int))dlsym(h, "move_audio_fx_on_midi");
    CHECK(on_midi_sym != NULL);
    if (on_midi_sym) {
        const uint8_t on2[3] = {0x90, 67, 100}, off2[3] = {0x80, 67, 0};
        on_midi_sym(inst, on2, 3, MOVE_MIDI_SOURCE_EXTERNAL);         /* the slot's own path */
        api->get_param(inst, "voices", buf, sizeof buf);
        CHECK(atoi(buf) == 2);
        printf("  move_audio_fx_on_midi exported and delivered a note (voices=%s)\n", buf);
        on_midi_sym(inst, off2, 3, MOVE_MIDI_SOURCE_EXTERNAL);

        /* The host's broadcast of every track's MIDI to every audio FX is
         * ignored, whatever the channel (on the device the slot's own notes
         * also arrive on their own path). */
        const uint8_t bc[3] = {0x93, 62, 100};
        on_midi_sym(inst, bc, 3, MOVE_MIDI_SOURCE_FX_BROADCAST);
        api->get_param(inst, "voices", buf, sizeof buf);
        CHECK(atoi(buf) == 1);
        printf("  FX broadcast note ignored (voices=%s)\n", buf);

        /* In a slot receiving channel 4 (byte 3): another channel's note is
         * dropped even on the slot's path, the slot's own pad (0x93) plays. */
        g_recv = 3;
        const uint8_t other[3] = {0x90, 64, 100}, own[3] = {0x93, 64, 100}, own_off[3] = {0x83, 64, 0};
        on_midi_sym(inst, other, 3, MOVE_MIDI_SOURCE_EXTERNAL);
        api->get_param(inst, "voices", buf, sizeof buf);
        CHECK(atoi(buf) == 1);                                        /* just the held C4 */
        on_midi_sym(inst, own, 3, MOVE_MIDI_SOURCE_INTERNAL);
        api->get_param(inst, "voices", buf, sizeof buf);
        CHECK(atoi(buf) == 2);
        printf("  slot channel 4: channel-1 note dropped, channel-4 note played (voices=%s)\n", buf);
        on_midi_sym(inst, own_off, 3, MOVE_MIDI_SOURCE_INTERNAL);
        g_recv = -2;
    }

    api->set_param(inst, "mode", "Robot");
    nonzero = 0;
    run_audio(api, inst, 1000.0, 200, &nonzero, &bad);
    CHECK(nonzero);
    n = api->get_param(inst, "viz", buf, sizeof buf);
    printf("  Robot viz: %s\n", buf);
    CHECK(n > 0 && buf[0] == 'd');
    {   /* chain_params served by the DSP, with the widget's viz in it (that is what mounts canvas.js) */
        static char cp[8192];
        int m = api->get_param(inst, "chain_params", cp, sizeof cp);
        CHECK(m > 0 && cp[0] == '[' && strstr(cp, "custom:tinyvoice_scope") != NULL);
        printf("  chain_params served: %d bytes, widget declared: %s\n", m, strstr(cp, "custom:tinyvoice_scope") ? "yes" : "NO");
    }
    n = api->get_param(inst, "scope", buf, sizeof buf);             /* the OLED widget's feed */
    printf("  scope: %s\n", buf);
    CHECK(n > 0 && buf[0] == 'S');
    api->on_midi(inst, off, 3, MOVE_MIDI_SOURCE_INTERNAL);

    /* Every mode runs on the real hardware: audible, and how long a block takes. */
    static const char *const modes[] = {"Robot", "Snap", "Chords", "Prism", "Tape", "Nightcall", "Scream", "Dalek",
                                        "HardTune", "Harmony", "Walkie", "Morph", "Double"};
    for (int m = 0; m < (int)(sizeof modes / sizeof modes[0]); m++) {
        api->set_param(inst, "mode", modes[m]);
        char got[32];
        api->get_param(inst, "mode", got, sizeof got);
        CHECK(strcmp(got, modes[m]) == 0);
        nonzero = 0;
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        run_audio(api, inst, 220.0, 400, &nonzero, &bad);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double ms = ((t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6) / 400.0;
        printf("  %-8s avg %.3f ms per 128-frame block%s\n", modes[m], ms, nonzero ? "" : "  (SILENT)");
        CHECK(nonzero);
        CHECK(ms < 2.0);                                           /* inside the ~2.3 ms SPI budget */
    }

    /* Heaviest new paths on real hardware: Robot with ensemble + band panning,
     * Freeze recording a take and then looping it, and Demo driving everything. */
    static const char *const extra[][3] = {
        { "Robot + ensemble", "mod", "1" },
        { "Freeze rec",       "freeze", "Rec" },
        { "Freeze loop",      "freeze", "Loop" },
        { "Demo on",          "demo", "On" },
        { "Voice FX",         "engine", "VoiceFX" },        /* the second personality, and its presets (Demo still on: it replaces the mic) */
        { "VFX Yeah Boy",     "vf_preset", "0" },
        { "VFX Alien Choir",  "vf_preset", "4" },
    };
    api->set_param(inst, "mode", "Robot");
    api->set_param(inst, "spread", "1");
    for (int k = 0; k < (int)(sizeof extra / sizeof extra[0]); k++) {
        api->set_param(inst, extra[k][1], extra[k][2]);
        nonzero = 0;
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        run_audio(api, inst, 220.0, 400, &nonzero, &bad);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double ms = ((t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6) / 400.0;
        printf("  %-16s avg %.3f ms per 128-frame block%s\n", extra[k][0], ms, nonzero ? "" : "  (SILENT)");
        CHECK(nonzero);
        CHECK(ms < 2.0);
    }
    /* Voice FX audition: Demo On plays the funk phrase through each of the six presets with nothing routed in
     * (silence on the input). Demo is the Showcase Tour, which moves Vowel, Tract, Peak, Choir and Mix/Drv, so the
     * presets differ here only in Breath, Attack and Release. Each must be audible, rhythmic (hits and gaps), under
     * the -6.1 dBFS ceiling and cheap. Then the tour over a whole 16 s loop, with the stored knobs untouched. */
    {
        api->set_param(inst, "freeze", "Off");                             /* a looping take (the scenarios above) would fill the groove's gaps: Freeze sits after the input */
        api->set_param(inst, "engine", "VoiceFX");
        api->set_param(inst, "demo", "On");
        char cnt[16] = "";
        CHECK(api->get_param(inst, "vf_preset_count", cnt, sizeof cnt) > 0 && atoi(cnt) == 6);
        for (int p = 0; p < 6; p++) {
            char idx[8];
            snprintf(idx, sizeof idx, "%d", p);
            api->set_param(inst, "demo", "Off");                           /* the browser is a hand on the tour: it ends it, so choose first ... */
            api->set_param(inst, "vf_preset", idx);
            api->set_param(inst, "demo", "On");                            /* ... then start Demo */
            char name[40] = "?";
            api->get_param(inst, "vf_preset_name", name, sizeof name);
            static int16_t sb[2 * 128];
            static double win[400];
            int nw = 0, peak = 0;
            double acc = 0;
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (int b = 0; b < 1100; b++) {                               /* ~2.8 s of the phrase, after 0.4 s to settle */
                memset(sb, 0, sizeof sb);
                api->process_block(inst, sb, 128);
                if (b < 140) continue;
                for (int i = 0; i < 128; i++) {
                    const int v = sb[2 * i];
                    if (abs(v) > peak) peak = abs(v);
                    acc += (double)v * v;
                }
                if (((b - 140) & 7) == 7 && nw < 400) { win[nw++] = sqrt(acc / (8.0 * 128.0)) / 32768.0; acc = 0; }   /* ~23 ms windows */
            }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            const double ms = ((t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6) / 1100.0;
            double hi = 0, lo = 1e9;
            for (int w = 0; w < nw; w++) { if (win[w] > hi) hi = win[w]; if (win[w] < lo) lo = win[w]; }
            printf("  audition %d %-17s loudest %6.1f dBFS, quietest %6.1f dBFS, peak %5.1f dBFS, %.3f ms/block\n", p, name,
                   20.0 * log10(hi + 1e-9), 20.0 * log10(lo + 1e-9), 20.0 * log10(peak / 32768.0 + 1e-9), ms);
            CHECK(hi > 0.005);                                             /* audible with nothing routed in */
            CHECK(hi > 4.0 * lo);                                          /* a groove: hits and gaps */
            CHECK(peak <= 16236);                                          /* TINYVOICE_OUTPUT_HEADROOM x 32768 */
            CHECK(ms < 2.0);
        }
        /* The Showcase Tour: a whole 8-bar loop (16 s at 120 bpm), the ghost hands moving Vowel, Tract, Peak, Choir and Mix/Drv.
         * Audible in every bar, under the ceiling, cheap, and the stored knobs come out as they went in. */
        {
            char before[5][16], after[16];
            static const char *const kn[5] = { "vf_vowel", "vf_tract", "vf_peak", "vf_choir", "vf_mix" };
            api->set_param(inst, "demo", "Off");
            api->set_param(inst, "vf_preset", "5");
            for (int k = 0; k < 5; k++) { before[k][0] = 0; api->get_param(inst, kn[k], before[k], sizeof before[k]); }
            api->set_param(inst, "demo", "On");
            static int16_t sb[2 * 128];
            double bar_acc[8] = { 0 };
            long bar_n[8] = { 0 };
            /* the screen's view: the host's UI re-reads every visible knob a few times a second through get_param, so the
             * tour is on the OLED if these move. (Only the five the tour turns: Vowel, Tract, Peak, Choir, Mix/Drv.) */
            static const char *const shown[5] = { "vf_vowel", "vf_tract", "vf_peak", "vf_choir", "vf_mix" };
            double lo_v[5] = { 9, 9, 9, 9, 9 }, hi_v[5] = { -9, -9, -9, -9, -9 };
            int polls = 0;
            int peak = 0, blocks = 16 * 44100 / 128;
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (int b = 0; b < blocks; b++) {
                memset(sb, 0, sizeof sb);
                api->process_block(inst, sb, 128);
                if (b % 28 == 27) {                                        /* ~80 ms */
                    for (int k = 0; k < 5; k++) {
                        char g[24] = "";
                        api->get_param(inst, shown[k], g, sizeof g);
                        const double v = atof(g);
                        if (v < lo_v[k]) lo_v[k] = v;
                        if (v > hi_v[k]) hi_v[k] = v;
                    }
                    polls++;
                }
                const int bar = (int)((double)b * 128.0 / (2.0 * 44100.0)) & 7;
                for (int i = 0; i < 128; i++) { const int v = sb[2 * i]; if (abs(v) > peak) peak = abs(v); bar_acc[bar] += (double)v * v; }
                bar_n[bar] += 128;
            }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            const double ms = ((t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6) / (double)blocks;
            printf("  tour, bar by bar (rms dBFS):");
            int quiet_bar = 0;
            for (int k = 0; k < 8; k++) {
                const double r = sqrt(bar_acc[k] / (double)(bar_n[k] ? bar_n[k] : 1)) / 32768.0;
                printf(" %.1f", 20.0 * log10(r + 1e-9));
                if (r < 0.003) quiet_bar = 1;
            }
            printf("   peak %.1f dBFS, %.3f ms/block\n", 20.0 * log10(peak / 32768.0 + 1e-9), ms);
            CHECK(!quiet_bar);
            CHECK(peak <= 16236);
            CHECK(ms < 2.0);
            printf("  ghost hands on the screen, %d polls: vowel %.2f..%.2f  tract %.2f..%.2f  peak %.2f..%.2f  choir %.2f..%.2f  mix/drv %.2f..%.2f\n", polls,
                   lo_v[0], hi_v[0], lo_v[1], hi_v[1], lo_v[2], hi_v[2], lo_v[3], hi_v[3], lo_v[4], hi_v[4]);
            for (int k = 0; k < 5; k++) CHECK(hi_v[k] - lo_v[k] > 0.5);        /* every one of the five turns through a good part of its range */
            /* Demo Off: the very next read of each knob is the user's own value again, as it was set */
            api->set_param(inst, "demo", "Off");
            for (int k = 0; k < 5; k++) { after[0] = 0; api->get_param(inst, kn[k], after, sizeof after); CHECK(strcmp(before[k], after) == 0); }
            /* a hand on a knob while the tour runs: Demo steps aside, the written value is kept */
            api->set_param(inst, "demo", "On");
            for (int b = 0; b < 300; b++) { memset(sb, 0, sizeof sb); api->process_block(inst, sb, 128); }
            api->set_param(inst, "vf_tract", "0.9");
            char dm[16] = "", tr[16] = "";
            api->get_param(inst, "demo", dm, sizeof dm);
            api->get_param(inst, "vf_tract", tr, sizeof tr);
            printf("  a hand on Tract mid-tour: Demo reads %s, Tract %s\n", dm, tr);
            CHECK(strcmp(dm, "Off") == 0 && fabs(atof(tr) - 0.9) < 1e-6);
            api->set_param(inst, "vf_tract", before[1]);                       /* put it back */
        }
    }
    /* Freeze: no accidental micro-loops. A 30 ms tap is dropped (it stays live); a 70 ms take records on until the
     * loop is 120 ms. Read from the scope feed: "S,<mode>,<freeze 0 live / 1 loop / 2 rec>,<phase>,<loop seconds>,..." */
    {
        api->set_param(inst, "engine", "Vocoder");
        api->set_param(inst, "demo", "Off");
        api->set_param(inst, "mode", "Robot");
        api->set_param(inst, "freeze_len", "Free");
        static const int taps_ms[2] = { 30, 70 };
        for (int t = 0; t < 2; t++) {
            static int16_t fb[2 * 128];
            api->set_param(inst, "freeze", "Off");
            for (int b = 0; b < 20; b++) { memset(fb, 0, sizeof fb); api->process_block(inst, fb, 128); }
            api->set_param(inst, "freeze", "Rec");
            for (int b = 0; b < taps_ms[t] * 441 / 10 / 128; b++) { for (int i = 0; i < 128; i++) fb[2 * i] = fb[2 * i + 1] = (int16_t)(6000.0 * sin(0.1 * (double)i)); api->process_block(inst, fb, 128); }
            api->set_param(inst, "freeze", "Loop");
            for (int b = 0; b < 80; b++) { for (int i = 0; i < 128; i++) fb[2 * i] = fb[2 * i + 1] = (int16_t)(6000.0 * sin(0.1 * (double)i)); api->process_block(inst, fb, 128); }
            char sc[400] = "";
            int sm = -1, sf = -1;
            float sp = 0.0f, sl = 0.0f;
            api->get_param(inst, "scope", sc, sizeof sc);
            sscanf(sc, "S,%d,%d,%f,%f", &sm, &sf, &sp, &sl);
            printf("  freeze, a %d ms take then Loop: scope says %s, loop %.3f s\n", taps_ms[t], sf == 1 ? "looping" : (sf == 0 ? "live" : "recording"), (double)sl);
            if (t == 0) CHECK(sf == 0);                                        /* the tap is dropped: live */
            else CHECK(sf == 1 && fabs((double)sl - 0.120) < 0.004);           /* extended to the 120 ms floor, then looping */
        }
        api->set_param(inst, "freeze", "Off");
    }
    /* Each mode keeps its own five contextual knobs: everything to max in HardTune, switch to Harmony, and Harmony is
     * on its own (default) values; back in HardTune, the maxed values are still there. */
    {
        api->set_param(inst, "engine", "Vocoder");
        api->set_param(inst, "demo", "Off");
        static const char *const ck[5] = { "char1", "char2", "mod", "char4", "char5" };
        api->set_param(inst, "mode", "HardTune");
        for (int k = 0; k < 5; k++) api->set_param(inst, ck[k], "1");
        api->set_param(inst, "mode", "Harmony");
        int own = 1;
        char kv[24];
        for (int k = 0; k < 5; k++) { kv[0] = 0; api->get_param(inst, ck[k], kv, sizeof kv); if (fabs(atof(kv) - (k == 2 ? 0.0 : 0.5)) > 1e-6) own = 0; }
        api->set_param(inst, "mode", "HardTune");
        int back = 1;
        for (int k = 0; k < 5; k++) { kv[0] = 0; api->get_param(inst, ck[k], kv, sizeof kv); if (fabs(atof(kv) - 1.0) > 1e-6) back = 0; }
        printf("  HardTune maxed, then Harmony: Harmony on its own knobs %s; back in HardTune, still maxed %s\n", own ? "yes" : "NO", back ? "yes" : "NO");
        CHECK(own && back);
    }
    api->set_param(inst, "engine", "Vocoder");
    api->set_param(inst, "demo", "Off");
    api->set_param(inst, "freeze", "Off");
    api->set_param(inst, "mod", "0");
    api->set_param(inst, "spread", "0.25");

    api->set_param(inst, "tone", "0.6");
    run_audio(api, inst, 440.0, 50, &nonzero, &bad);

    n = api->get_param(inst, "state", buf, sizeof buf);
    CHECK(n > 0);
    printf("  state (%d bytes): %s\n", n, buf);

    void *inst2 = api->create_instance("/x", buf);               /* a second instance restores from state */
    CHECK(inst2 != NULL);
    if (inst2) {
        char b2[1024];
        CHECK(api->get_param(inst2, "state", b2, sizeof b2) == n && strcmp(buf, b2) == 0);
        api->destroy_instance(inst2);
    }
    api->destroy_instance(inst);

    CHECK(dlclose(h) == 0);
    printf(g_fail ? "SMOKE FAILED (%d)\n" : "SMOKE OK\n", g_fail);
    return g_fail ? 1 : 0;
}

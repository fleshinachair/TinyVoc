#include "presets.h"

#include <stddef.h>

/* The Voice FX bank: classic talking-filter programs, translated to the eight
 * encoders. Tract is 2^((t - 0.5) x 1.2) octaves, so -0.35 oct = 0.208 and
 * +0.4 oct = 0.833. Mix/Drive is wet/dry up to 0.7 and drive above it (see vfx.h).
 * What the encoders cannot hold is noted per preset: there is no envelope ->
 * vowel modulation in the engine, so a sweep is Vowel ridden by hand, and
 * "dynamic" is Attack and Release with the drive's own level dependence.
 * "Vowel Morph Solo" is also the module's default Voice FX page. */
static const vt_preset k_presets[] = {
    /*                      vowel  tract  peak  choir breath attack release mix/drive */
    /* Yeah Boy: starts on /i/ (ride Vowel down to 0 for /a/); hard drive, snappy, resonant */
    { "Yeah Boy",        VT_ENGINE_VOICEFX, { 0.45f, 0.500f, 0.80f, 0.30f, 0.10f, 0.02f, 0.15f, 0.95f } },
    /* Robo Talk: a narrow /o/ -> /u/ window (0.875), high Q, neutral tract */
    { "Robo Talk",       VT_ENGINE_VOICEFX, { 0.875f, 0.500f, 0.85f, 0.10f, 0.05f, 0.05f, 0.20f, 0.80f } },
    /* Monster Throat: tract down 0.35 octave, hard pre-saturation, no attack at all */
    { "Monster Throat",  VT_ENGINE_VOICEFX, { 0.70f, 0.208f, 0.65f, 0.30f, 0.25f, 0.00f, 0.20f, 0.98f } },
    /* Funky Talkbox: clean (mix at the knee: no drive), medium Q, fast attack and release for rhythmic play */
    { "Funky Talkbox",   VT_ENGINE_VOICEFX, { 0.30f, 0.500f, 0.50f, 0.20f, 0.10f, 0.05f, 0.12f, 0.70f } },
    /* Alien Choir: tract up 0.4 octave, full width, lots of air */
    { "Alien Choir",     VT_ENGINE_VOICEFX, { 0.60f, 0.833f, 0.55f, 1.00f, 0.85f, 0.30f, 0.55f, 0.80f } },
    /* Vowel Morph Solo: neutral and balanced, for riding Vowel across A E I O U by hand (the default page) */
    { "Vowel Morph Solo", VT_ENGINE_VOICEFX, { 0.00f, 0.500f, 0.50f, 0.35f, 0.15f, 0.12f, 0.30f, 0.75f } },
};

#define N_PRESETS ((int)(sizeof k_presets / sizeof k_presets[0]))

/* The table is plain const data: its floats sit on their natural alignment inside each entry. */
_Static_assert(offsetof(vt_preset, v) % _Alignof(float) == 0, "preset values must be float-aligned");
_Static_assert(sizeof(vt_preset) % _Alignof(vt_preset) == 0, "preset entries must tile without padding surprises");

int vt_preset_count(int engine)
{
    int n = 0;
    for (int i = 0; i < N_PRESETS; i++) n += k_presets[i].engine == engine;
    return n;
}

const vt_preset *vt_preset_get(int engine, int idx)
{
    if (idx < 0) return NULL;
    for (int i = 0; i < N_PRESETS; i++)
        if (k_presets[i].engine == engine && idx-- == 0) return &k_presets[i];
    return NULL;
}

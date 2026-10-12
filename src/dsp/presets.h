/*
 * Factory presets, tagged with the engine they belong to. Voice FX has a bank
 * of talking-filter programs; the Vocoder personality has none (its layout has
 * no preset browser). A preset is eight numbers, in the order of the Voice FX
 * page (vf_vowel .. vf_mix); loading one writes them to those params. Static
 * const data: no allocation, nothing to free.
 */
#ifndef TINYVOICE_PRESETS_H
#define TINYVOICE_PRESETS_H

#include "dsp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VT_PRESET_VALUES 8       /* vowel, tract, peak, choir, breath, attack, release, mix/drive */

typedef struct {
    const char *name;            /* short enough for the OLED list */
    int engine;                  /* vt_engine this preset belongs to (the filter tag) */
    float v[VT_PRESET_VALUES];   /* 0..1 each */
} vt_preset;

/* How many presets carry `engine`'s tag (0 for the Vocoder). */
int vt_preset_count(int engine);
/* The i-th preset tagged `engine`, in bank order; NULL when i is out of range. */
const vt_preset *vt_preset_get(int engine, int i);

#ifdef __cplusplus
}
#endif
#endif

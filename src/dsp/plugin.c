/*
 * Schwung audio_fx v2 adapter: maps audio_fx_api_v2_t onto the engine in dsp.h.
 * Headers come from src/host/ (run scripts/fetch_host_headers.sh).
 */
#include "audio_fx_api_v2.h"
#include "dsp.h"

static const host_api_v1_t *g_host;

static void *vt_create_instance(const char *module_dir, const char *config_json)
{
    (void)module_dir;
    int sr = (g_host && g_host->sample_rate > 0) ? g_host->sample_rate : VT_SAMPLE_RATE;
    vt_t *vt = dsp_init(sr);
    /* Host-supplied defaults use the same flat {"key": value} shape as "state".
     * Unknown or nested members are ignored. */
    if (vt && config_json) (void)dsp_set_param(vt, "state", config_json);
    return vt;
}

static void vt_destroy_instance(void *inst)
{
    dsp_cleanup((vt_t *)inst);
}

static void vt_process_block(void *inst, int16_t *audio_inout, int frames)
{
    if (g_host && g_host->get_bpm) dsp_set_bpm((vt_t *)inst, g_host->get_bpm());   /* Demo's and Freeze's bar length */
    /* Transport position for Freeze's downbeat-synced takes; NULL on older hosts. */
    dsp_set_transport((vt_t *)inst, g_host && g_host->get_beat_position ? g_host->get_beat_position() : -1.0);
    dsp_process_block((vt_t *)inst, audio_inout, frames);
}

static void vt_set_param(void *inst, const char *key, const char *val)
{
    /* "state" (full snapshot restore for presets/sets) is handled by the engine. */
    (void)dsp_set_param((vt_t *)inst, key, val);
}

static int vt_get_param(void *inst, const char *key, char *buf, int buf_len)
{
    /* chain_params / ui_hierarchy come from module.json; "state" is the full
     * 16-param snapshot used for autosave and presets. */
    return dsp_get_param((vt_t *)inst, key, buf, buf_len);
}

static void vt_on_midi(void *inst, const uint8_t *msg, int len, int source)
{
    (void)source;
    dsp_handle_midi((vt_t *)inst, msg, len);
}

/* The chain host decides whether to send an FX any MIDI by looking this symbol
 * up with dlsym() (see ducker/capicola/magneto, which all export it); having
 * on_midi in the struct alone is not enough, and pad notes never arrive. */
__attribute__((visibility("default")))
void move_audio_fx_on_midi(void *inst, const uint8_t *msg, int len, int source)
{
    vt_on_midi(inst, msg, len, source);
}

static audio_fx_api_v2_t g_api = {
    .api_version     = AUDIO_FX_API_VERSION_2,
    .create_instance = vt_create_instance,
    .destroy_instance = vt_destroy_instance,
    .process_block   = vt_process_block,
    .set_param       = vt_set_param,
    .get_param       = vt_get_param,
    .on_midi         = vt_on_midi,
};

__attribute__((visibility("default")))
audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host)
{
    g_host = host;
    return &g_api;
}

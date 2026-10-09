#include "imogen.h"
#include "dsp.h"      /* vt_scale */

#include <math.h>
#include <string.h>

#define PI_F      3.14159265358979f
#define FADE_SEC  0.01f     /* voice fade in/out: no clicks when the chord changes */

void imogen_init(imogen_t *im, float sr)
{
    memset(im, 0, sizeof(*im));
    im->sr = sr;
    im->retune_a = 1.0f;
    im->glide_a = 1.0f;
    im->fade_a = 1.0f - expf(-1.0f / (FADE_SEC * sr));
    im->hp_a = vt_unvoiced_hp_coef(sr);
    im->harmony_level = 1.0f;
    for (int i = 0; i < IMOGEN_VOICES; i++) {
        im->v[i].note = -1;
        im->pad_last[i] = -1;
        psola_init(&im->v[i].pv);
        im->pan_l[i] = im->pan_r[i] = 1.0f;
    }
}

void vt_chord(int note, int root, int scale, int voicing, int out[IMOGEN_VOICES])
{
    int third, fifth;
    if (scale == VT_SCALE_CHROMATIC) {             /* no diatonic 3rd to find: major triad */
        third = note + 4;
        fifth = note + 7;
    } else {
        third = cher_scale_step(note, 2, root, scale);   /* a 3rd above, in key */
        fifth = cher_scale_step(note, 4, root, scale);   /* a 5th above, in key */
    }
    out[0] = note;                                 /* the lead, snapped */
    if (voicing == VT_VOICING_CLOSE) {
        out[1] = third; out[2] = fifth; out[3] = note + 12;
    } else if (voicing == VT_VOICING_WIDE) {
        out[1] = note - 12; out[2] = fifth; out[3] = third + 12;
    } else {
        out[1] = third; out[2] = fifth; out[3] = note - 12;
    }
}

void vt_fit_register(int *notes, int n, int ref)
{
    if (ref < 0) return;
    for (int i = 0; i < n; i++) {
        if (notes[i] < 0) continue;
        while (notes[i] > ref + VT_REG_UP) notes[i] -= 12;
        while (notes[i] < ref - VT_REG_DOWN) notes[i] += 12;
    }
}

float vt_unvoiced_hp_coef(float sr)
{
    return expf(-2.0f * PI_F * VT_UNVOICED_HP_HZ / sr);
}

float vt_unvoiced_hp(float *z, float x, float a)
{
    /* Two one-pole high-passes in series. z = { last x, last y1, last y2 }. */
    float y1 = a * (z[1] + x - z[0]);
    float y2 = a * (z[2] + y1 - z[1]);
    z[0] = x;
    z[1] = fabsf(y1) < 1e-15f ? 0.0f : y1;
    z[2] = fabsf(y2) < 1e-15f ? 0.0f : y2;
    return y2;
}

void imogen_auto_chord(int note, int root, int scale, int out[IMOGEN_VOICES])
{
    vt_chord(note, root, scale, VT_VOICING_STANDARD, out);
}

void imogen_set_beauty(imogen_t *im, float beauty)
{
    im->beauty = beauty < 0.0f ? 0.0f : (beauty > 1.0f ? 1.0f : beauty);
}

void imogen_set_character(imogen_t *im, float harmony_level, float humanize)
{
    im->harmony_level = harmony_level;
    im->humanize = humanize;
}

void imogen_set_targets(imogen_t *im, const cher_t *c, const int held[IMOGEN_VOICES],
                        int auto_on, int root, int scale,
                        float retune_ms, float glide_ms, float spread, int voicing)
{
    im->retune_a = retune_ms <= 0.0f ? 1.0f : 1.0f - expf(-1000.0f / (retune_ms * im->sr));
    im->glide_a = glide_ms <= 0.0f ? 1.0f : 1.0f - expf(-1000.0f / (glide_ms * im->sr));

    int target[IMOGEN_VOICES] = { -1, -1, -1, -1 };
    int n_held = 0, pad = -1;
    for (int i = 0; i < IMOGEN_VOICES; i++) if (held[i] >= 0) { n_held++; pad = held[i]; }

    int same = 1;                                    /* the same pads as last block? */
    for (int i = 0; i < IMOGEN_VOICES; i++) { same &= held[i] == im->pad_last[i]; im->pad_last[i] = held[i]; }

    im->auto_chord = 0;
    if (n_held == 1) {
        /* One pad = the chord ROOT: the whole in-key voicing moves to it (the
         * same chord the sung note would get), near the voice. A lone pad
         * voice against the other three used to clash. */
        im->pad_oct = cher_fold_octave(c->f0_midi, (float)pad, im->pad_oct, same);
        int r = pad + 12 * im->pad_oct;
        while (r < 12) r += 12;
        vt_chord(r, root, scale, voicing, target);
        im->auto_chord = 1;
    } else if (n_held > 1) {
        /* A chord on the pads: exactly those notes, moved as a block. */
        im->pad_oct = cher_fold_chord(c->f0_midi, held, target, IMOGEN_VOICES, im->pad_oct, same);
    } else if (c->voiced && c->note >= 0) {
        if (auto_on) { vt_chord(c->note, root, scale, voicing, target); im->auto_chord = 1; }
        else target[0] = c->note;
    }
    /* Natural register: nothing more than +11 st above the sung note (a 3rd or
     * 5th that would be goes down an octave), nothing more than an octave
     * below it (the shifter's floor). */
    vt_fit_register(target, IMOGEN_VOICES, c->voiced ? c->note : -1);

    im->active = 0;
    for (int i = 0; i < IMOGEN_VOICES; i++) {
        imogen_voice *v = &im->v[i];
        if (target[i] >= 0) {
            im->active++;
            if (v->note >= 0 && v->amp > 0.01f && target[i] != v->note)
                v->glide_off += (float)(v->note - target[i]);    /* keep sounding pitch, retarget */
            else if (v->note < 0 || v->amp <= 0.01f)
                v->glide_off = 0.0f;                             /* fresh voice: start on pitch */
        }
        v->note = target[i];

        /* equal-power pan, spread across the field: 3rd left, 5th right, octave
         * and root inside them; scaled so centre is unity on both sides */
        static const float pos[IMOGEN_VOICES] = { -0.33f, -1.0f, 1.0f, 0.33f };
        float p = (pos[i] * spread + 1.0f) * 0.25f * PI_F;
        im->pan_l[i] = cosf(p) * 1.41421356f;
        im->pan_r[i] = sinf(p) * 1.41421356f;
    }
}

float imogen_latency(const imogen_t *im)
{
    return im->v[0].pv.lat - 1.0f;                   /* reads index now - lat; newest is now - 1 */
}

void imogen_process(imogen_t *im, cher_t *c, float in, float *out_l, float *out_r)
{
    cher_push(c, in);

    /* Sum of N independent voices: 1/sqrt(N) keeps the level near the input's. */
    const float norm = im->active > 0 ? 1.0f / sqrtf((float)im->active) : 1.0f;
    const float T0 = c->voiced ? im->sr / c->f0 : 0.0f;
    static const float det_pattern[IMOGEN_VOICES] = { 0.0f, 1.0f, -0.8f, 0.6f };   /* humanize spread */
    im->t += 1.0f / im->sr;
    if (im->t > 1000.0f) im->t -= 1000.0f;
    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < IMOGEN_VOICES; i++) {
        imogen_voice *v = &im->v[i];
        const int on = v->note >= 0 && c->voiced;
        /* Humanize: up to ~15 cents static detune plus a slow drift, per voice. */
        float hum = 0.0f;
        if (im->humanize > 0.0f && i > 0)
            hum = im->humanize * (0.15f * det_pattern[i] +
                                  0.06f * sinf(2.0f * PI_F * (0.11f + 0.07f * (float)i) * im->t));
        static const float bdet[IMOGEN_VOICES] = { 0.0f, 0.04f, -0.04f, 0.025f };   /* Beauty: +-4 ct */
        hum += im->beauty * bdet[i];
        float level = (im->auto_chord && i > 0) ? im->harmony_level : 1.0f;
        /* Voice 0 also carries unvoiced sound (consonants), so it never fades. */
        float goal_amp = (on || i == 0) ? 1.0f : 0.0f;
        v->amp += (goal_amp - v->amp) * im->fade_a;
        if (v->amp < 1e-4f && goal_amp == 0.0f) {
            v->amp = 0.0f;
            if (i > 0) im->mt[i][im->mt_w] = 0.0f;           /* a silent voice leaves silence in its line */
            continue;
        }

        float lr_goal = 0.0f;
        if (on) {
            v->glide_off *= 1.0f - im->glide_a;
            if (fabsf(v->glide_off) < 1e-5f) v->glide_off = 0.0f;
            lr_goal = ((float)v->note + v->glide_off + hum - c->f0_midi) * (1.0f / 12.0f);
            if (lr_goal > 1.0f) lr_goal = 1.0f;                  /* the shifter's +-1 octave */
            if (lr_goal < -1.0f) lr_goal = -1.0f;
        }
        if (v->amp <= im->fade_a || !on) v->lr = lr_goal;        /* starting, or unvoiced: no slew */
        v->lr += (lr_goal - v->lr) * im->retune_a;

        /* Formant-preserving shift (PSOLA, formant ratio 1): a 3rd or 5th up
         * keeps the singer's vowel instead of turning into a chipmunk. */
        float s = psola_process(&v->pv, c, T0, exp2f(v->lr), 1.0f, 0.0f) * v->amp * (on ? norm * level : 1.0f);
        if (i > 0) {                                         /* Beauty: harmonies 8 / 10 / 12 ms late */
            static const float bms[IMOGEN_VOICES] = { 0.0f, 0.008f, 0.010f, 0.012f };
            im->mt[i][im->mt_w] = s;
            im->mt_d[i] += (im->beauty * bms[i] * im->sr - im->mt_d[i]) * 0.001f;
            if (im->mt_d[i] > 0.5f) {
                float pos = (float)(im->mt_w + 1) - (im->mt_d[i] + 1.0f);
                float fl = floorf(pos), fr = pos - fl;
                int i0 = (int)fl & 1023, i1 = (i0 + 1) & 1023;
                s = im->mt[i][i0] + fr * (im->mt[i][i1] - im->mt[i][i0]);
            }
        }
        if (i == 0) {
            /* Unvoiced, voice 0 runs at ratio 1, i.e. the raw voice: only its
             * top end (consonants, breath) passes, so a note the tracker lost
             * doesn't bleed through as unshifted pitch at 100% wet. */
            im->voiced_w += ((on ? 1.0f : 0.0f) - im->voiced_w) * im->fade_a;
            float hp = vt_unvoiced_hp(im->hp_z, s, im->hp_a);
            s = im->voiced_w * s + (1.0f - im->voiced_w) * hp;
        }
        l += s * im->pan_l[i];
        r += s * im->pan_r[i];
    }
    im->mt_w = (im->mt_w + 1) & 1023;
    *out_l = l * VT_HARMONY_TRIM;
    *out_r = r * VT_HARMONY_TRIM;
}

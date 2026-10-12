#include "freeze.h"

#include <math.h>
#include <string.h>

void freeze_init(freeze_t *f, float sr)
{
    memset(f, 0, sizeof(*f));
    f->len = (int)(FREEZE_SEC * sr);
    if (f->len > FREEZE_MAX) f->len = FREEZE_MAX;
    if (f->len < 1) f->len = 1;
    f->xf = (int)(FREEZE_XF_SEC * sr);
    if (f->xf < 1) f->xf = 1;
    f->k_mix = 1.0f - expf(-1.0f / (0.01f * sr));
    f->to_downbeat = -1;
    f->min_len = (int)(FREEZE_MIN_SEC * sr) + f->xf;
    f->reject_len = (int)(FREEZE_REJECT_SEC * sr);
    if (f->min_len > f->len) f->min_len = f->len;
    f->pend = -1;
}

void freeze_set_clock(freeze_t *f, int samples_to_downbeat)
{
    f->to_downbeat = samples_to_downbeat < 0 ? -1 : samples_to_downbeat;
}

int freeze_armed(const freeze_t *f)
{
    return f->state == FREEZE_ARMED;
}

static void punch_in(freeze_t *f)
{
    f->rec_len = 0;                                  /* a fresh take from 0 */
    f->pend = -1;
    f->state = FREEZE_REC;
}

static inline float rd(const freeze_t *f, int i)
{
    return (float)f->buf[i] * (1.0f / 32767.0f);
}

/* Lock the take: a loop needs room for its wrap crossfade and a bit more.
 * A bar take that ran its full length started on a downbeat and was recorded
 * xf past the bar line, so playback starts xf in: the loop's beat 1 then lands
 * on the grid's next downbeat, not 5 ms after it. */
static void lock_take(freeze_t *f)
{
    f->loop_len = f->rec_len >= 4 * f->xf ? f->rec_len : 0;
    f->pos = (f->target > 0 && f->loop_len == f->target + f->xf) ? f->xf : 0;
}

void freeze_set_target(freeze_t *f, int samples)
{
    if (samples < 0) samples = 0;
    if (samples > f->len - f->xf) samples = f->len - f->xf;
    f->target = samples;
}

int freeze_looping(const freeze_t *f)
{
    return f->state == FREEZE_LOOP && f->loop_len > 0;
}

float freeze_phase(const freeze_t *f)
{
    if (f->state == FREEZE_REC)
        return (float)f->rec_len / (float)(f->target > 0 ? f->target + f->xf : f->len);
    if (!freeze_looping(f)) return 0.0f;
    return (float)f->pos / (float)(f->loop_len - f->xf);
}

float freeze_loop_seconds(const freeze_t *f, float sr)
{
    if (f->state == FREEZE_REC) return (float)f->rec_len / sr;
    return freeze_looping(f) ? (float)(f->loop_len - f->xf) / sr : 0.0f;
}

int freeze_take_full(freeze_t *f)
{
    int r = f->full;
    f->full = 0;
    return r;
}

float freeze_process(freeze_t *f, float in, int want, float *mix)
{
    /* Act on a change of what is asked for, not on a mismatch with the state:
     * a full take moves itself to Loop while the param still says Rec. */
    if (want != f->asked) {
        f->asked = want;
        if (want == FREEZE_REC && f->state == FREEZE_REC) f->pend = -1;   /* Rec again while a short take is being extended: an ordinary take */
        if (want != f->state) {
            if (want == FREEZE_REC) {
                /* Bar mode with the transport running: wait for the downbeat. */
                if (f->target > 0 && f->to_downbeat >= 0) f->state = FREEZE_ARMED;
                else punch_in(f);
            } else if (f->state == FREEZE_REC) {
                if (f->rec_len < f->reject_len) {        /* an accidental tap: nothing loops, you stay live */
                    f->loop_len = 0;
                    f->pos = 0;
                    f->state = want;
                } else if (f->rec_len < f->min_len) {    /* too short for a loop: record on until it is long enough */
                    f->pend = want;
                } else {
                    lock_take(f);                        /* stop: exactly what was recorded */
                    f->state = want;
                }
            } else {
                if (want == FREEZE_LOOP) f->pos = 0;     /* replay the kept take from its start */
                f->state = want;                         /* (from Armed: cancelled) */
            }
        }
    }

    if (f->state == FREEZE_ARMED) {
        /* The downbeat (or the transport stopping, or Free chosen): punch in. */
        if (f->to_downbeat <= 0 || f->target <= 0) punch_in(f);
    }
    if (f->to_downbeat > 0) f->to_downbeat--;

    if (f->state == FREEZE_REC) {
        float c = in > 1.0f ? 1.0f : (in < -1.0f ? -1.0f : in);
        f->buf[f->rec_len++] = (int16_t)lrintf(c * 32767.0f);
        /* Bar mode: the period is the target, plus the 5 ms the wrap crossfade
         * eats. Free: until the buffer is full. Either way, lock and loop. */
        const int stop = f->target > 0 ? f->target + f->xf : f->len;
        if (f->pend >= 0 && f->rec_len >= f->min_len) {
            lock_take(f);                                /* the extended take is long enough: what was asked for, now */
            f->state = f->pend;
            f->pend = -1;
        } else if (f->rec_len >= stop || f->rec_len >= f->len) {
            lock_take(f);
            f->state = FREEZE_LOOP;
            f->pend = -1;
            f->full = 1;
        }
    }

    const int looping = freeze_looping(f);
    f->mix += ((looping ? 1.0f : 0.0f) - f->mix) * f->k_mix;
    if (f->mix < 1e-4f && !looping) { f->mix = 0.0f; *mix = 0.0f; return 0.0f; }
    if (f->loop_len <= 0) { *mix = f->mix; return 0.0f; }   /* fading out of a take just discarded */

    /* Play 0 .. P with P = loop_len - xf: over the first xf samples the take's
     * last xf samples fade out as its first fade in, so the wrap is seamless. */
    const int P = f->loop_len - f->xf;
    float y = rd(f, f->pos);
    if (f->pos < f->xf) {
        float t = ((float)f->pos + 0.5f) / (float)f->xf;
        y = sinf(t * 1.5707963f) * y + cosf(t * 1.5707963f) * rd(f, P + f->pos);   /* equal power */
    }
    if (++f->pos >= P) f->pos = 0;

    *mix = f->mix;
    return y;
}

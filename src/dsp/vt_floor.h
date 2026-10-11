/*
 * Noise-floor estimate by minimum statistics: the floor is the lowest level
 * seen over the last VT_FLOOR_SLOTS sub-windows (~3 s). It drops within one
 * sub-window when things get quieter and rises only as old minima age out, so
 * speech pauses keep pulling it back to the real noise floor.
 *
 * Callers cap the threshold they derive from it, because a long steady note
 * looks exactly like a loud noise floor to this estimator.
 */
#ifndef TINYVOICE_FLOOR_H
#define TINYVOICE_FLOOR_H

#define VT_FLOOR_SLOTS 12

typedef struct {
    float slot_min[VT_FLOOR_SLOTS];
    float cur;              /* minimum within the sub-window being filled */
    float floor;
    int   idx, count, per;  /* per = updates per sub-window */
} vt_floor_t;

/* `initial` is used only until the first sub-window completes; slots that have
 * never been filled don't count, so the floor reflects real input after
 * ~0.25 s instead of waiting for the whole ~3 s window to age out. */
static inline void vt_floor_init(vt_floor_t *f, int updates_per_slot, float initial)
{
    for (int i = 0; i < VT_FLOOR_SLOTS; i++) f->slot_min[i] = 1e30f;
    f->cur = 1e30f;
    f->floor = initial;
    f->idx = 0;
    f->count = 0;
    f->per = updates_per_slot > 0 ? updates_per_slot : 1;
}

static inline float vt_floor_update(vt_floor_t *f, float level)
{
    if (level < f->cur) f->cur = level;
    if (f->cur < f->floor) f->floor = f->cur;              /* get quieter immediately */
    if (++f->count >= f->per) {
        f->slot_min[f->idx] = f->cur;
        f->idx = (f->idx + 1) % VT_FLOOR_SLOTS;
        f->cur = 1e30f;
        f->count = 0;
        float m = f->slot_min[0];
        for (int i = 1; i < VT_FLOOR_SLOTS; i++) if (f->slot_min[i] < m) m = f->slot_min[i];
        f->floor = m;
    }
    return f->floor;
}

#endif

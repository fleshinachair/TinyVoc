/*
 * TinyVoc's OLED widget for the Schwung param pages
 * (shared/param_pages/widget_registry.mjs).
 *
 * custom:tinyvoc_scope spans TUNE knobs 5-7 (the read-only Pitch, Target and
 * Cents readouts), drawn above those knobs' labels. It shows:
 *   pitch modes (Snap, Chords, Morph, Prism, Double, Tape)
 *       the target note name, and a needle for how far the sung pitch is from
 *       it (+-50 cents across the bar); "--" when nothing is pitched
 *   Robot   16 bars: the vocoder's analysis bands
 *   Freeze  a playhead line along the bottom while a take loops; while
 *           recording, a bar that fills as the take does, blinking head; and a
 *           tag top right: "ARM" (blinking) while waiting for the downbeat,
 *           "REC", "LOOP" (it takes the Auto readout's place)
 *   Auto    "+NN" when the input AGC is lifting a soft voice by NN dB
 *   pads    the target note drawn inverted while a pad sets it, and a dot
 *           that flashes on every note-on (shows pads are reaching the FX)
 *
 * custom:tinyvoc_freeze spans SPACE knobs 7-8 (Freeze, Freeze Len). Off: the
 * two values, centred in their cells like the stock knobs. Active: the box is
 * cleared and taken over - "[ *** ARMED *** ]" flashing inverted with the beats
 * left to bar 1 as squares, an inverted "[ REC ]" with the take filling,
 * "[ LOOP 2 BARS ]" with a playhead.
 *
 * Draw hooks get cached values only: the host reads the `scope` key on its own
 * rotation (~5 Hz), so this eases the bars and needle between reads and runs
 * the loop playhead (and the recording bar) on by itself. Feed format
 * (src/dsp/dsp.c format_scope); <freeze> is 0 live, 1 looping, 2 recording,
 * 3 armed (waiting for the downbeat), and while recording <loop phase>/<loop s>
 * are how far through the take and the seconds so far:
 *   "S,<mode>,<freeze>,<loop phase>,<loop s>,<f0>,<target>,<cents>,<b0>..<b15>,
 *    <auto gain dB>,<held pad note or -1>,<note-on count>,<transport beat or -1>"
 *   (the last four optional)
 */
(function () {
    const MODE_ROBOT = 1;
    const NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
    const BAR_FALL_PER_MS = 255 / 400;   /* a full bar falls in 0.4 s */
    const NEEDLE_EASE_MS = 120;
    const STALE_MS = 1500;               /* no fresh read for this long: stop extrapolating */
    const FLASH_MS = 300;                /* note-on activity dot */

    const st = {
        raw: null, seenAt: 0, lastDraw: 0,
        mode: 0, frozen: false, rec: false, armed: false, phase: 0, loopS: 0,
        f0: 0, target: 0, cents: 0, shownCents: 0,
        bars: new Array(16).fill(0), shown: new Array(16).fill(0),
        agcDb: 0, pad: -1, noteOns: -1, flashAt: -1e9, beat: -1,
    };

    function noteName(hz) {
        if (!(hz > 0)) return "--";
        const m = Math.round(69 + 12 * Math.log2(hz / 440));
        return NAMES[((m % 12) + 12) % 12] + (Math.floor(m / 12) - 1);
    }

    function parse(raw, now) {
        if (typeof raw !== "string" || raw === st.raw) return;
        const f = raw.split(",");
        if (f[0] !== "S" || f.length < 24) return;
        const n = f.slice(1).map(Number);
        if (!n.every(Number.isFinite)) return;
        st.raw = raw;
        st.seenAt = now;
        st.mode = n[0] | 0;
        st.frozen = n[1] === 1;
        st.rec = n[1] === 2;
        st.armed = n[1] === 3;
        st.phase = Math.min(1, Math.max(0, n[2]));
        st.loopS = Math.max(0, n[3]);
        st.f0 = n[4];
        st.target = n[5];
        st.cents = Math.max(-50, Math.min(50, n[6]));
        for (let i = 0; i < 16; i++) st.bars[i] = Math.max(0, Math.min(255, n[7 + i]));
        if (n.length >= 26) {
            st.agcDb = n[23];
            st.pad = n[24] | 0;
            const ons = n[25] | 0;
            if (st.noteOns >= 0 && ons !== st.noteOns) st.flashAt = now;   /* a note arrived */
            st.noteOns = ons;
        }
        st.beat = n.length >= 27 ? n[26] : -1;
    }

    function midiName(note) {
        return NAMES[((note % 12) + 12) % 12] + (Math.floor(note / 12) - 1);
    }

    function ease(now) {
        const dt = st.lastDraw ? Math.min(100, Math.max(0, now - st.lastDraw)) : 0;
        st.lastDraw = now;
        for (let i = 0; i < 16; i++) {           /* rise at once, fall smoothly */
            const target = st.bars[i];
            st.shown[i] = target >= st.shown[i] ? target : Math.max(target, st.shown[i] - BAR_FALL_PER_MS * dt);
        }
        const k = dt > 0 ? 1 - Math.exp(-dt / NEEDLE_EASE_MS) : 1;
        st.shownCents += (st.cents - st.shownCents) * k;
        return dt;
    }

    /* Loop playhead, run on locally between reads like a clock. */
    function loopPhase(now) {
        if (!st.frozen || !(st.loopS > 0)) return -1;
        if (now - st.seenAt > STALE_MS) return st.phase;
        const p = st.phase + (now - st.seenAt) / (st.loopS * 1000);
        return p - Math.floor(p);
    }

    /* Recording: buffer fill 0..1, run on locally between reads. */
    function recFill(now) {
        if (!st.rec) return -1;
        if (now - st.seenAt > STALE_MS || !(st.loopS > 0) || !(st.phase > 0)) return st.phase;
        const perMs = st.phase / (st.loopS * 1000);               /* fill per ms, from fill / seconds */
        return Math.min(1, st.phase + (now - st.seenAt) * perMs);
    }

    function drawBars(ctx, top, h) {
        const W = ctx.width;
        const bw = Math.max(1, Math.floor(W / 16) - 1);
        const step = Math.floor(W / 16);
        const x0 = Math.floor((W - step * 16 + 1) / 2);
        for (let i = 0; i < 16; i++) {
            const bh = Math.round((st.shown[i] / 255) * h);
            if (bh > 0) ctx.fillRect(x0 + i * step, top + h - bh, bw, bh, 1);
            else ctx.fillRect(x0 + i * step, top + h - 1, bw, 1, 1);   /* floor tick: the meter is alive */
        }
    }

    function drawPitch(ctx, top, h, right) {
        const W = ctx.width;
        const lw = typeof ctx.textWidth === "function" ? ctx.textWidth("C#4") : 18;
        const ty = top + Math.max(0, Math.floor((h - 6) / 2));
        if (st.pad >= 0) {                                        /* a pad sets the target: inverted label */
            ctx.fillRect(0, top, lw + 2, h, 1);
            ctx.print(1, ty, midiName(st.pad), 0);
        } else {
            ctx.print(0, ty, st.f0 > 0 ? noteName(st.target) : "--", 1);
        }
        const x0 = lw + 4, x1 = W - 1 - right;
        const mid = Math.round((x0 + x1) / 2), half = Math.floor((x1 - x0) / 2);
        const cy = top + Math.floor(h / 2);
        ctx.fillRect(x0, cy, x1 - x0 + 1, 1, 1);                      /* scale line */
        ctx.fillRect(mid, top, 1, h, 1);                              /* in tune */
        for (const c of [-25, 25]) ctx.fillRect(mid + Math.round((c / 50) * half), cy - 1, 1, 3, 1);
        if (st.f0 > 0) {
            const nx = mid + Math.round((st.shownCents / 50) * half);
            ctx.fillRect(Math.min(mid, nx), cy - 1, Math.abs(nx - mid) + 1, 3, 1);   /* the error */
            ctx.fillRect(nx - 1, top, 3, h, 1);                                      /* the needle */
        }
    }

    function draw(ctx, payload) {
        const p = payload || {};
        const values = p.values || {};
        const now = typeof p.nowMs === "number" ? p.nowMs : Date.now();
        parse(values.scope, now);
        ease(now);
        if (!st.raw) return;

        const H = ctx.height, W = ctx.width;
        const lp = loopPhase(now), rf = recFill(now);
        const body = lp >= 0 || rf >= 0 ? H - 3 : H;                  /* room for the loop line */
        const tw = (s) => (typeof ctx.textWidth === "function" ? ctx.textWidth(s) : s.length * 6);
        /* Freeze's state tag, or (otherwise) Auto's boost, top right */
        const freezeOn = st.armed || st.rec || st.frozen;
        const tag = st.armed ? (Math.floor(now / 250) % 2 === 0 ? "ARM" : "") : st.rec ? "REC" : st.frozen ? "LOOP" : "";
        const agcTxt = !freezeOn && st.agcDb >= 1 ? "+" + Math.round(st.agcDb) : "";
        const right = freezeOn ? tw("LOOP") + 2 : agcTxt ? tw("+24") + 2 : 0;
        if (st.mode === MODE_ROBOT) drawBars(ctx, 0, body);
        else drawPitch(ctx, 0, body, right);
        const topRight = tag || agcTxt;                               /* Freeze's state, or Auto's lift */
        if (topRight) {
            const x = W - tw(topRight);
            if (st.mode === MODE_ROBOT) ctx.fillRect(x - 1, 0, W - x + 1, 7, 0);
            ctx.print(x, 0, topRight, 1);
        }
        if (now - st.flashAt < FLASH_MS) ctx.fillRect(W - 2, H - (body < H ? 5 : 2), 2, 2, 1);  /* note-on */

        if (lp >= 0) {
            for (let x = 0; x < W; x += 2) ctx.fillRect(x, H - 1, 1, 1, 1);   /* the loop, dotted */
            const px = Math.round(lp * (W - 1));
            ctx.fillRect(0, H - 1, px + 1, 1, 1);                              /* played so far */
            ctx.fillRect(Math.max(0, px - 1), H - 3, 3, 3, 1);                 /* playhead */
        }
        if (rf >= 0) {
            const px = Math.round(rf * (W - 1));
            ctx.fillRect(0, H - 2, px + 1, 2, 1);                              /* recorded so far */
            if (Math.floor(now / 250) % 2 === 0)
                ctx.fillRect(Math.min(W - 3, px), H - 3, 3, 3, 1);             /* blinking head */
        }
    }

    /* ---- Freeze banner (SPACE knobs 7-8) ----
     *
     * The frame is the two cells' graphic area, above the host's label band
     * ("FREEZE", "FRELEN"). The host clips text at the frame's right edge but
     * not its bottom, so text sits on ONE line, vertically centred, and the
     * rest (countdown, take, playhead) is drawn as graphics on the bottom rows:
     * a second text line used to run into the label band, and two strings on
     * one line ran into each other ("FREE2E"). */
    const LEN_OPTS = ["Free", "1 Bar", "2 Bars", "4 Bars"];
    const FREEZE_OPTS = ["Off", "Rec", "Loop"];
    function optName(v, opts, dflt) {
        if (typeof v === "string") {
            const t = v.trim();
            if (opts.indexOf(t) >= 0) return t;
            const n = Number(t);
            if (t !== "" && Number.isInteger(n) && n >= 0 && n < opts.length) return opts[n];
            return dflt;
        }
        return Number.isInteger(v) && v >= 0 && v < opts.length ? opts[v] : dflt;
    }
    const TEXT_H = 8;                                                 /* the display font, with room */

    function drawFreeze(ctx, payload) {
        const p = payload || {};
        const values = p.values || {};
        const now = typeof p.nowMs === "number" ? p.nowMs : Date.now();
        parse(values.scope, now);
        const W = ctx.width, H = ctx.height;
        const tw = (s) => (typeof ctx.textWidth === "function" ? ctx.textWidth(s) : s.length * 6);
        const ty = Math.max(0, Math.min(H - TEXT_H, Math.floor((H - TEXT_H - 3) / 2)));
        const fit = (cands) => { for (const c of cands) if (tw(c) <= W - 2) return c; return ""; };
        const centre = (txt, color) => { if (txt) ctx.print(Math.max(0, Math.floor((W - tw(txt)) / 2)), ty, txt, color); };
        const len = optName(values.freeze_len, LEN_OPTS, "2 Bars");
        const clock = st.raw && st.beat >= 0 && now - st.seenAt < STALE_MS;

        if (!(st.armed || st.rec || st.frozen)) {
            /* OFF: what the stock knobs would show - each value, centred in its
             * own cell. No banner, no extra strings. */
            /* Each centred in its cell; a value wider than its cell borrows
             * room from beside the other (3 px apart, never over it), and only
             * if the two can't both fit is the length shortened. */
            const half = Math.floor(W / 2), GAP = 3;
            const left = optName(values.freeze, FREEZE_OPTS, "Off");
            let right = len;
            while (right.length > 1 && tw(left) + GAP + tw(right) > W) right = right.slice(0, -1);
            let lx = Math.max(0, Math.floor((half - tw(left)) / 2));
            let rx = half + Math.floor((W - half - tw(right)) / 2);
            if (rx + tw(right) > W) rx = W - tw(right);
            if (rx < lx + tw(left) + GAP) {
                rx = Math.max(rx, Math.min(W - tw(right), lx + tw(left) + GAP));
                lx = Math.max(0, Math.min(lx, rx - GAP - tw(left)));
            }
            ctx.print(lx, ty, left, 1);
            ctx.print(rx, ty, right, 1);
            return;
        }

        ctx.fillRect(0, 0, W, H, 0);                                  /* clear the box first */
        if (st.armed) {
            /* ARMED: flashes inverted <-> outlined at 4 Hz; the beats left to
             * bar 1 as filled squares along the bottom */
            const on = Math.floor(now / 125) % 2 === 0;
            const fg = on ? 0 : 1;
            if (on) ctx.fillRect(0, 0, W, H, 1);
            else { ctx.fillRect(0, 0, W, 1, 1); ctx.fillRect(0, H - 1, W, 1, 1); ctx.fillRect(0, 0, 1, H, 1); ctx.fillRect(W - 1, 0, 1, H, 1); }
            centre(fit(["[ *** ARMED *** ]", "*** ARMED ***", "* ARMED *", "ARMED"]), fg);
            if (clock) {
                const left = Math.max(1, Math.min(4, Math.ceil(4 - (((st.beat % 4) + 4) % 4) - 1e-6)));
                const sq = 4, gap = 3, total = 4 * sq + 3 * gap, x0 = Math.floor((W - total) / 2);
                for (let k = 0; k < 4; k++) {
                    const x = x0 + k * (sq + gap);
                    if (k < left) ctx.fillRect(x, H - 3, sq, 2, fg);
                    else ctx.fillRect(x, H - 2, sq, 1, fg);
                }
            }
            return;
        }
        if (st.rec) {
            /* REC: solid inverted, a dot, the take filling along the bottom */
            ctx.fillRect(0, 0, W, H, 1);
            const txt = fit(["[ REC ]", "REC"]);
            const tx = Math.max(6, Math.floor((W - tw(txt) + 6) / 2));
            ctx.fillRect(tx - 6, ty + 2, 4, 4, 0);                    /* the record dot */
            ctx.print(tx, ty, txt, 0);
            const f = recFill(now);
            if (f > 0) ctx.fillRect(1, H - 2, Math.max(1, Math.round(f * (W - 2))), 1, 0);
            return;
        }
        /* LOOP: the length, and the playhead along the bottom */
        centre(fit(["[ LOOP " + len.toUpperCase() + " ]", "[ LOOP ]", "LOOP"]), 1);
        ctx.fillRect(0, H - 2, W, 1, 1);
        const lp = loopPhase(now);
        if (lp >= 0) ctx.fillRect(Math.max(0, Math.round(lp * (W - 3))), H - 3, 3, 3, 1);
    }

    /* custom:tinyvoc_latch, MORE knob 7. In the modes Latch acts in (Robot,
     * Chords, Prism) the value as the stock enum knob would show it, On
     * inverted so a held chord is seen at a glance; in the others a dimmed
     * "---" (every other pixel cleared: the 1-bit screen's grey). The cell,
     * its name and the knob never move; the knob still turns everywhere. Until
     * the mode has been read it shows the value. */
    const MODES = ["Snap", "Robot", "Chords", "Morph", "Prism", "Double", "Tape"];
    const LATCH_MODES = ["Robot", "Chords", "Prism"];
    function drawLatch(ctx, payload) {
        const values = (payload && payload.values) || {};
        const W = ctx.width, H = ctx.height;
        const tw = (s) => (typeof ctx.textWidth === "function" ? ctx.textWidth(s) : s.length * 6);
        const ty = Math.max(0, Math.min(H - TEXT_H, Math.floor((H - TEXT_H - 3) / 2)));
        const mode = optName(values.mode, MODES, null);
        if (mode && LATCH_MODES.indexOf(mode) < 0) {
            const txt = "---", x = Math.max(0, Math.floor((W - tw(txt)) / 2));
            ctx.print(x, ty, txt, 1);
            if (typeof ctx.setPixel === "function")
                for (let y = ty; y < Math.min(H, ty + TEXT_H); y++)
                    for (let xx = x; xx < Math.min(W, x + tw(txt)); xx++)
                        if ((xx + y) & 1) ctx.setPixel(xx, y, 0);
            return;
        }
        const on = optName(values.latch, ["Off", "On"], "Off") === "On";
        const txt = on ? "On" : "Off", x = Math.max(0, Math.floor((W - tw(txt)) / 2));
        if (on) ctx.fillRect(Math.max(0, x - 2), Math.max(0, ty - 1), Math.min(W, tw(txt) + 4), Math.min(H, TEXT_H + 1), 1);
        ctx.print(x, ty, txt, on ? 0 : 1);
    }

    globalThis.canvas_overlay = {
        widgetKinds: {
            "custom:tinyvoc_scope": { draw, nominal: { w: 96, h: 15 } },
            "custom:tinyvoc_freeze": { draw: drawFreeze, nominal: { w: 64, h: 15 } },
            "custom:tinyvoc_latch": { draw: drawLatch, nominal: { w: 32, h: 15 } },
        },
    };
    /* test seam (tests/canvas_test.mjs) */
    globalThis.TINYVOC_SCOPE_FOR_TEST = { draw, drawFreeze, drawLatch, parse, loopPhase, recFill, noteName, st };
})();

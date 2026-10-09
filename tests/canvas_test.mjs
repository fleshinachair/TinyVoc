// Runs src/ui/canvas.js (the OLED widget) against a mock frame, the way
// Schwung's widget_registry drawCustom hands it one. Needs node; run by
// scripts/test.sh when node is available.
//
//   node tests/canvas_test.mjs
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import vm from "node:vm";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
vm.runInThisContext(readFileSync(join(root, "src/ui/canvas.js"), "utf8"), { filename: "canvas.js" });

let failures = 0, checks = 0;
const check = (cond, what) => { checks++; if (!cond) { failures++; console.log("  FAIL", what); } };

const W = 128, H = 15;
function mockCtx() {
    const px = new Uint8Array(W * H);
    const texts = [];
    let outOfBounds = 0;
    return {
        width: W, height: H, px, texts,
        get outOfBounds() { return outOfBounds; },
        fillRect(x, y, w, h, c) {
            if (x < 0 || y < 0 || x + w > W || y + h > H || w < 0 || h < 0) outOfBounds++;
            for (let j = Math.max(0, y); j < Math.min(H, y + h); j++)
                for (let i = Math.max(0, x); i < Math.min(W, x + w); i++) px[j * W + i] = c ? 1 : 0;
        },
        print(x, y, s, c) { texts.push(String(s)); },
        textWidth(s) { return String(s).length * 6; },
    };
}
const lit = (ctx) => ctx.px.reduce((a, b) => a + b, 0);
const litRow = (ctx, y) => { let n = 0; for (let i = 0; i < W; i++) n += ctx.px[y * W + i]; return n; };
const litCol = (ctx, x, y0, y1) => { let n = 0; for (let j = y0; j <= y1; j++) n += ctx.px[j * W + x]; return n; };

const ov = globalThis.canvas_overlay;
check(ov && ov.widgetKinds && typeof ov.widgetKinds["custom:tinyvoc_scope"].draw === "function", "registers custom:tinyvoc_scope");
const draw = ov.widgetKinds["custom:tinyvoc_scope"].draw;
const T = globalThis.TINYVOC_SCOPE_FOR_TEST;
const scope = (mode, frozen, phase, loopS, f0, tgt, cents, bands) =>
    ["S", mode, frozen, phase, loopS, f0, tgt, cents, ...(bands || new Array(16).fill(0))].join(",");
const run = (values, nowMs) => { const ctx = mockCtx(); draw(ctx, { values, nowMs, group: { keys: ["retune"] } }); return ctx; };

// note names
check(T.noteName(440) === "A4" && T.noteName(261.63) === "C4" && T.noteName(0) === "--", "note names");

// pitch view: target note, needle off-centre for +20 cents
let c = run({ scope: scope(0, 0, 0, 0, 445, 440, 20) }, 1000);
c = run({ scope: scope(0, 0, 0, 0, 445, 440, 20) }, 2000);       // let the needle settle
check(c.texts.includes("A4"), "pitch: target note shown");
check(c.outOfBounds === 0, "pitch: stays inside the frame");
const mid = Math.round(((18 + 4) + (W - 1)) / 2);
let right = 0, left = 0;
for (let x = mid + 3; x < W; x++) right += litCol(c, x, 0, 3);
for (let x = 22; x < mid - 2; x++) left += litCol(c, x, 0, 3);
check(right > 0 && left === 0, "pitch: sharp voice puts the needle right of centre");

// unvoiced: "--", no needle
c = run({ scope: scope(0, 0, 0, 0, 0, 0, 0) }, 3000);
check(c.texts.includes("--"), "pitch: unvoiced shows --");

// robot bars: a peak in bands 7-8, nothing drawn outside the frame
const bands = new Array(16).fill(0); bands[7] = 255; bands[8] = 200;
c = run({ scope: scope(1, 0, 0, 0, 0, 0, 0, bands) }, 4000);
check(c.outOfBounds === 0, "bars: inside the frame");
const step = Math.floor(W / 16);
check(litCol(c, 7 * step + 1, 0, H - 1) === H, "bars: band 7 full height");
check(litCol(c, 2 * step + 1, 0, H - 1) === 1, "bars: quiet band shows only its floor tick");

// bars fall smoothly instead of vanishing
c = run({ scope: scope(1, 0, 0, 0, 0, 0, 0, new Array(16).fill(0)) }, 4100);
const h7 = litCol(c, 7 * step + 1, 0, H - 1);
check(h7 > 1 && h7 < H, "bars: a silenced band decays over time, not instantly");

// freeze: dotted loop line plus a playhead that moves on between reads
const fz = scope(0, 1, 0.25, 2.0, 220, 220, 0);
let c1 = run({ scope: fz }, 10000);
let c2 = run({ scope: fz }, 10500);                                // same reading, 0.5 s later
const head = (ctx) => { for (let x = W - 1; x >= 0; x--) if (ctx.px[(H - 3) * W + x]) return x; return -1; };
check(litRow(c1, H - 1) > W / 4, "freeze: loop line drawn");
const moved = head(c2) - head(c1);
check(Math.abs(moved - Math.round(0.25 * (W - 1))) <= 2, `freeze: playhead runs on by itself (moved ${moved}px)`);
check(c1.outOfBounds === 0 && c2.outOfBounds === 0, "freeze: inside the frame");

// recording: a bar that fills with the buffer, growing on between reads
const rec = scope(0, 2, 0.25, 4.0, 220, 220, 0);                  // a quarter full after 4 s
c1 = run({ scope: rec }, 20250);                                   // (times with the blinking head off)
c2 = run({ scope: rec }, 21250);                                   // 1 s later, no new read
const recEnd = (ctx) => { for (let x = W - 1; x >= 0; x--) if (ctx.px[(H - 1) * W + x]) return x; return -1; };
check(Math.abs(recEnd(c1) - Math.round(0.25 * (W - 1))) <= 1, `rec: bar at the fill (${recEnd(c1)}px)`);
check(Math.abs(recEnd(c2) - Math.round(0.3125 * (W - 1))) <= 2, `rec: bar grows on by itself (${recEnd(c2)}px)`);
const c3 = run({ scope: rec }, 22000);                              // later, head on (stale: holds the fill)
check(recEnd(c3) >= recEnd(c1), "rec: blinking head at the end of the bar");
check(c1.outOfBounds === 0 && c2.outOfBounds === 0, "rec: inside the frame");

// freeze tags top right: ARM (blinking, while waiting for the downbeat), REC, LOOP
c = run({ scope: scope(0, 3, 0, 0, 220, 220, 0) }, 30000);         // 30000/250 = 120: even, shown
check(c.texts.includes("ARM"), "freeze tag: ARM while armed");
c = run({ scope: scope(0, 3, 0, 0, 220, 220, 0) }, 30250);         // odd: blinked off
check(!c.texts.includes("ARM"), "freeze tag: ARM blinks");
c = run({ scope: scope(0, 2, 0.25, 4.0, 220, 220, 0) }, 31000);
check(c.texts.includes("REC") && !c.texts.includes("ARM"), "freeze tag: REC while recording");
c = run({ scope: scope(1, 1, 0.5, 2.0, 0, 0, 0, new Array(16).fill(100)) }, 32000);
check(c.texts.includes("LOOP"), "freeze tag: LOOP while looping (Robot bars too)");
check(c.outOfBounds === 0, "freeze tag: inside the frame");

// ---- Freeze banner (SPACE knobs 7-8) ----
check(ov.widgetKinds["custom:tinyvoc_freeze"] && typeof ov.widgetKinds["custom:tinyvoc_freeze"].draw === "function",
      "registers custom:tinyvoc_freeze");
const drawFz = ov.widgetKinds["custom:tinyvoc_freeze"].draw;
const fzScope = (frozen, phase, loopS, beat) =>
    ["S", 0, frozen, phase, loopS, 220, 220, 0, ...new Array(16).fill(0), 0, -1, 0, beat].join(",");
/* a two-cell frame that records where every string lands (6 px glyphs, 8 px tall) */
function fzCtx(Wf) {
    const Hf = 15, px = new Uint8Array(Wf * Hf), runs = [];
    let oob = 0;
    return {
        width: Wf, height: Hf, px, runs,
        get outOfBounds() { return oob; },
        fillRect(x, y, w, h, c) {
            if (x < 0 || y < 0 || x + w > Wf || y + h > Hf) oob++;
            for (let jj = Math.max(0, y); jj < Math.min(Hf, y + h); jj++)
                for (let ii = Math.max(0, x); ii < Math.min(Wf, x + w); ii++) px[jj * Wf + ii] = c ? 1 : 0;
        },
        print(x, y, t, c) { const w = String(t).length * 6; runs.push({ x, y, w, t: String(t) }); if (x < 0 || x + w > Wf || y < 0 || y + 8 > Hf) oob++; },
        textWidth(t) { return String(t).length * 6; },
    };
}
const runFz = (values, nowMs, Wf = 64) => { const k = fzCtx(Wf); drawFz(k, { values, nowMs, group: { keys: ["freeze", "freeze_len"] } }); return k; };
const overlaps = (k) => { for (let a = 0; a < k.runs.length; a++) for (let b = a + 1; b < k.runs.length; b++) {
    const A = k.runs[a], B = k.runs[b];
    if (Math.abs(A.y - B.y) < 8 && A.x < B.x + B.w && B.x < A.x + A.w) return true; } return false; };
const texts = (k) => k.runs.map((r) => r.t);
const litFz = (k) => k.px.reduce((a2, b2) => a2 + b2, 0);

// OFF: just the two values, each centred in its own cell - no banner, no status strings
let fk = runFz({ scope: fzScope(0, 0, 0, 5.2), freeze: "Off", freeze_len: "2 Bars" }, 40000);
check(texts(fk).join("|") === "Off|2 Bars", `banner off: only the two values (${texts(fk).join("|")})`);
check(fk.runs[0].x + fk.runs[0].w <= 32, "banner off: the Freeze value inside its own cell");
check(fk.runs[1].x + fk.runs[1].w <= 64 && fk.runs[1].x > fk.runs[0].x + fk.runs[0].w, "banner off: the length to its right, whole");
check(!overlaps(fk) && fk.outOfBounds === 0, "banner off: no collisions, inside the frame");
fk = runFz({ scope: fzScope(0, 0, 0, -1), freeze: 0, freeze_len: 3 }, 40100);       // index forms
check(texts(fk).join("|") === "Off|4 Bars", "banner off: index values read as names");

// ARMED: cleared and taken over, flashing; the beats left to bar 1 as squares
fk = runFz({ scope: fzScope(3, 0, 0, 5.2), freeze_len: "2 Bars" }, 41000);         // 41000/125 even: inverted
check(texts(fk).length === 1 && /ARMED/.test(texts(fk)[0]), `banner armed: one ARMED line (${texts(fk)[0]})`);
check(litFz(fk) > 64 * 15 / 2, "banner armed: inverted");
const sqLit = (k) => { let n = 0; for (let x = 0; x < 64; x++) n += k.px[(15 - 3) * 64 + x] === 0 ? 1 : 0; return n; };
check(sqLit(fk) === 3 * 4, `banner armed: 3 beats left drawn as 3 squares (${sqLit(fk) / 4})`);
const fkOff = runFz({ scope: fzScope(3, 0, 0, 5.2), freeze_len: "2 Bars" }, 41125);
check(/ARMED/.test(texts(fkOff)[0] || "") && litFz(fkOff) < 64 * 15 / 2, "banner armed: flashes");
check(texts(runFz({ scope: fzScope(3, 0, 0, 5.2) }, 41000, 120))[0] === "[ *** ARMED *** ]", "banner armed: full brackets when they fit");

// REC and LOOP
fk = runFz({ scope: fzScope(2, 0.5, 2.0, 9.0), freeze_len: "2 Bars" }, 42000);
check(texts(fk).length === 1 && /REC/.test(texts(fk)[0]) && litFz(fk) > 64 * 15 / 2, `banner rec: inverted REC (${texts(fk)[0]})`);
fk = runFz({ scope: fzScope(1, 0.25, 4.0, 12.0), freeze_len: "2 Bars" }, 43000);
check(texts(fk).length === 1 && /LOOP/.test(texts(fk)[0]), `banner loop: one LOOP line (${texts(fk)[0]})`);

// every state, narrow or wide: in the frame, no collisions, text clear of the bottom rows
let clean = true;
for (const Wf of [50, 60, 64, 120])
    for (const stt of [0, 1, 2, 3])
        for (const tm of [44000, 44125]) {
            const k = runFz({ scope: fzScope(stt, 0.9, 4.0, 3.9), freeze: "Rec", freeze_len: "4 Bars" }, tm, Wf);
            if (k.outOfBounds || overlaps(k) || k.runs.some((r) => r.y + 8 > 15 - 2)) clean = false;
        }
check(clean, "banner: every state fits 50-120 px frames without collisions");

// not frozen: no loop line
c = run({ scope: scope(0, 0, 0, 0, 220, 220, 0) }, 11000);
check(litRow(c, H - 1) === 0 || litRow(c, H - 1) < 4, "live: no loop line");

// junk in: no throw, keeps the last good picture's state
let threw = false;
try {
    run({ scope: "garbage" }, 12000);
    run({ scope: "S,1,2,3" }, 12001);
    run({ scope: undefined }, 12002);
    run({}, 12003);
    draw(mockCtx(), undefined);
} catch (e) { threw = true; console.log("  threw:", e); }
check(!threw, "junk input never throws");

// Auto gain, pad and note-on indicators (the three optional trailing fields)
const scope2 = (agc, pad, ons) => scope(0, 0, 0, 0, 220, 220, 0) + `,${agc},${pad},${ons}`;
c = run({ scope: scope2(18.2, -1, 5) }, 20000);
check(c.texts.includes("+18"), "auto: input boost shown as +18");
check(c.outOfBounds === 0, "auto: inside the frame");
c = run({ scope: scope2(0.4, -1, 5) }, 20100);
check(!c.texts.some((t) => t.startsWith("+")), "auto: no readout when not boosting");

c = run({ scope: scope2(0, 64, 6) }, 20200);                          // a pad (E4) arrived
check(c.texts.includes("E4"), "pad: its note is the label");
check(c.px[0 * W + 0] === 1, "pad: label drawn inverted (lit box behind it)");
check(c.px[(H - 2) * W + (W - 1)] === 1, "pad: note-on flashes the activity dot");
c = run({ scope: scope2(0, 64, 6) }, 21000);                          // no new note-on since
check(c.px[(H - 2) * W + (W - 1)] === 0, "pad: the dot goes out again");

c = run({ scope: scope(1, 0, 0, 0, 0, 0, 0, new Array(16).fill(255)) + ",12,-1,6" }, 22000);
check(c.texts.includes("+12") && c.outOfBounds === 0, "auto readout over the bars, inside the frame");

// ---- Latch (MORE knob 7): On / Off where it acts, a dimmed "---" elsewhere ----
check(ov.widgetKinds["custom:tinyvoc_latch"] && typeof ov.widgetKinds["custom:tinyvoc_latch"].draw === "function",
      "registers custom:tinyvoc_latch");
const drawLt = ov.widgetKinds["custom:tinyvoc_latch"].draw;
function ltCtx(Wl) {
    const Hl = 15, px = new Uint8Array(Wl * Hl), runs = [];
    let oob = 0, cleared = 0;
    return {
        width: Wl, height: Hl, px, runs,
        get outOfBounds() { return oob; }, get cleared() { return cleared; },
        fillRect(x, y, w, h, c) {
            if (x < 0 || y < 0 || x + w > Wl || y + h > Hl) oob++;
            for (let jj = Math.max(0, y); jj < Math.min(Hl, y + h); jj++)
                for (let ii = Math.max(0, x); ii < Math.min(Wl, x + w); ii++) px[jj * Wl + ii] = c ? 1 : 0;
        },
        setPixel(x, y, c) { if (x < 0 || y < 0 || x >= Wl || y >= Hl) oob++; else { px[y * Wl + x] = c ? 1 : 0; if (!c) cleared++; } },
        print(x, y, t, c) { const w = String(t).length * 6; runs.push({ x, y, w, t: String(t), c }); if (x < 0 || x + w > Wl || y < 0 || y + 8 > Hl) oob++; },
        textWidth(t) { return String(t).length * 6; },
    };
}
const runLt = (values, Wl = 32) => { const k = ltCtx(Wl); drawLt(k, { values, nowMs: 0, group: { keys: ["latch"] } }); return k; };
const MODES = ["Snap", "Robot", "Chords", "Morph", "Prism", "Double", "Tape"];
const ACTS = ["Robot", "Chords", "Prism"];
let ltOk = true;
for (const Wl of [24, 32, 40])
    for (let mi = 0; mi < MODES.length; mi++)
        for (const latch of ["Off", "On", 0, 1]) {
            for (const modeVal of [MODES[mi], mi, String(mi)]) {             // names and index forms
                const k = runLt({ mode: modeVal, latch }, Wl);
                const on = latch === "On" || latch === 1;
                const want = ACTS.includes(MODES[mi]) ? (on ? "On" : "Off") : "---";
                const one = k.runs.length === 1 && k.runs[0].t === want;
                const dim = want !== "---" || k.cleared > 0;                  // "---" is dimmed
                const inverted = want !== "On" || (k.runs[0] && k.runs[0].c === 0 && k.px.reduce((a, b) => a + b, 0) > 0);
                const centred = k.runs[0] && Math.abs((k.runs[0].x + k.runs[0].w / 2) - Wl / 2) <= 3;
                const below = k.runs.every((r) => r.y + 8 <= 15 - 2);         // clear of the bottom rows
                if (!(one && dim && inverted && centred && below && k.outOfBounds === 0)) {
                    ltOk = false;
                    console.log("  latch widget", Wl, MODES[mi], JSON.stringify(modeVal), latch, JSON.stringify(k.runs), k.cleared, k.outOfBounds);
                }
            }
        }
check(ltOk, "latch: every mode x value x width shows On/Off (acts) or a dimmed --- (doesn't), centred, in the frame");
const same = (a, b) => JSON.stringify(a.runs.map((r) => [r.x, r.y])) === JSON.stringify(b.runs.map((r) => [r.x, r.y]));
check(same(runLt({ mode: "Robot", latch: "Off" }), runLt({ mode: "Chords", latch: "Off" })) &&
      same(runLt({ mode: "Snap", latch: "On" }), runLt({ mode: "Tape", latch: "Off" })), "latch: same place in every mode (nothing shifts)");
check(runLt({ latch: "On" }).runs[0].t === "On" && runLt({ mode: "junk", latch: "Off" }).runs[0].t === "Off",
      "latch: mode not read yet shows the value");
threw = false;
try { drawLt(ltCtx(32), undefined); runLt({}); runLt({ mode: null, latch: "maybe" }); drawLt({ width: 32, height: 15, print() {}, fillRect() {} }, { values: { mode: "Snap" } }); }
catch (e) { threw = true; console.log("  threw:", e); }
check(!threw, "latch: junk input never throws (no setPixel on the frame either)");

console.log(`canvas.js: ${checks} checks, ${failures} failed`);
process.exit(failures ? 1 : 0);

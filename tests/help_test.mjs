// help.json is what Schwung's Help viewer shows for this module. The host's loader reads only the top-level
// `children`: a file that names its topics anything else is discarded without a word. A line is drawn on a
// 128 px screen and never wrapped, so each line is held to 21 characters here (a quick guard; the real budget
// is pixels, measured by Schwung's tests/host/test_help_content_width.sh), and only ASCII has a glyph.
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const help = JSON.parse(readFileSync(join(root, "src", "help.json"), "utf8"));
let checks = 0, failed = 0;
const check = (ok, msg) => { checks++; if (!ok) { failed++; console.error("FAIL: " + msg); } };

check(Array.isArray(help.children) && help.children.length > 0, "top level has no `children`");
const titles = (help.children || []).map(c => c.title);
for (const want of ["Overview", "Setup", "Vocoder", "Voice FX"]) check(titles.includes(want), "missing section " + want);

const leafTitles = [];
let lines = 0;
(function walk(node, trail) {
    const here = trail + "/" + node.title;
    check(typeof node.title === "string" && node.title.length > 0, here + ": no title");
    check(typeof node.title !== "string" || node.title.length <= 21, here + ": title too long");
    const kids = node.children || [], ls = node.lines || [];
    check(kids.length > 0 || ls.length > 0, here + ": neither children nor lines");
    check(!(kids.length && ls.length), here + ": both children and lines");
    for (const l of ls) {
        lines++;
        check(typeof l === "string", here + ": a line is not a string");
        check(/^[\x20-\x7e]*$/.test(l), here + ": non-ASCII character in " + JSON.stringify(l));
        check(l.length <= 21, here + ": line over 21 characters: " + JSON.stringify(l));
    }
    if (ls.length) leafTitles.push(node.title);
    for (const k of kids) walk(k, here);
})(help, "");

// Every mode has a page, and it names the five knobs.
const MODES = ["Robot", "Snap", "Chords", "Prism", "Tape", "Nightcall", "Scream", "Dalek", "HardTune", "Harmony", "Walkie", "Morph", "Double"];
const modesNode = ((help.children || []).find(c => c.title === "Vocoder") || { children: [] }).children.find(c => c.title === "Modes");
check(!!modesNode, "no Vocoder > Modes");
for (const m of MODES) {
    const leaf = modesNode && modesNode.children.find(c => c.title === m);
    check(!!leaf, "no page for mode " + m);
    if (leaf) check(leaf.lines.filter(l => /^ [2-6] /.test(l)).length === 5, m + ": expected the five knob names");
}
// The six Voice FX presets are named.
const presets = (((help.children || []).find(c => c.title === "Voice FX") || { children: [] }).children.find(c => c.title === "Presets") || { lines: [] }).lines.join("\n");
for (const p of ["Yeah Boy", "Robo Talk", "Monster Throat", "Funky Talkbox", "Alien Choir", "Vowel Morph Solo"]) check(presets.includes(p), "preset not listed: " + p);

console.log(`help.json: ${checks} checks, ${failed} failed (${lines} lines, ${leafTitles.length} pages)`);
process.exit(failed ? 1 : 0);

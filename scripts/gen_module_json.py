"""Regenerate TinyVoice's module.json AND src/dsp/module_meta.h from one table:
21 params on four pages (VOICE, PITCH, SPACE, MORE), plus three read-only
readouts (pitch, target, cents) that carry the OLED scope on PITCH knobs 5-7.
VOICE is the home page: Mode, five contextual knobs, Mix, Level.

    python scripts/gen_module_json.py [module.json] [src/dsp/module_meta.h]

The header carries chain_params and ui_hierarchy as JSON strings for the DSP to
answer get_param("chain_params" / "ui_hierarchy") with. That matters: the chain
host's own fallback re-serialises chain_params from its parsed table and drops
`viz`, so a custom widget (canvas.js) only mounts when the module answers
itself, as tinyk, hank and monksynth do.

The served chain_params names char1 / char2 / mod / char4 / char5 per mode: the
header's copy has @1@ .. @5@ in their "name" fields, which dsp.c fills from its
k_macro_names table. module.json keeps the generic names.

Order of PARAMS must match vt_param_id / k_params in src/dsp/dsp.c; the
consistency test in tests/test_main.c checks every field against the C table.
"""
import copy, json, sys

# No "Follow": Schwung does not pass Move's project key to modules, so the key is set here.
KEYS = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

def enum(key, name, options, default):
    return {"key": key, "name": name, "type": "enum", "options": options, "default": default}

def flt(key, name, lo, hi, step, default, unit=None):
    p = {"key": key, "name": name, "type": "float", "min": lo, "max": hi, "step": step}
    if unit: p["unit"] = unit
    p["default"] = default
    return p

# The vocoder and carrier modes first, then the voice transformers; Morph and
# Double (the older PSOLA modes) after the roster. Same order as vt_mode in dsp.h.
MODES = ["Robot", "Snap", "Chords", "Prism", "Tape", "Nightcall",
         "Scream", "Dalek", "HardTune", "Harmony", "Walkie", "Morph", "Double"]

PARAMS = [
    enum("mode", "Mode", MODES, 1),
    enum("key", "Key", KEYS, 0),
    enum("scale", "Scale", ["Chromatic", "Major", "Minor", "Pentatonic", "Dorian"], 0),
    flt("retune", "Retune", 0, 500, 1, 0, "ms"),
    flt("char1", "Char 1", 0, 1, 0.01, 0.5, "%"),
    flt("char2", "Char 2", 0, 1, 0.01, 0.5, "%"),
    # Gate and Auto in one knob: Auto (the automatic front end), then a manual
    # threshold -60..0 dB in 3 dB steps, which is Auto Off. Nothing hides any more.
    enum("gate", "Gate", ["Auto"] + ["%d dB" % db for db in range(-60, 1, 3)], 0),   # 3 dB steps
    flt("mix", "Mix", 0, 1, 0.01, 1, "%"),
    flt("gain", "Level", -12, 12, 0.5, 0, "dB"),
    flt("delay", "Delay", 0, 1000, 1, 0, "ms"),
    flt("feedback", "Feedback", 0, 0.95, 0.01, 0.3, "%"),
    flt("spread", "Spread", 0, 1, 0.01, 0.25, "%"),
    flt("tone", "Tone", 0, 1, 0.01, 1, "%"),
    flt("glide", "Glide", 0, 500, 1, 0, "ms"),
    flt("mod", "Mod", 0, 1, 0.01, 0, "%"),
    flt("reverb", "Reverb", 0, 1, 0.01, 0, "%"),
    enum("freeze_len", "Freeze Len", ["Free", "1 Bar", "2 Bars", "4 Bars"], 2),   # Rec stops itself after N bars
    flt("beauty", "Beauty", 0, 1, 0.01, 0.5, "%"),          # per-mode polish: stereo, detune, air, warmth
    enum("latch", "Latch", ["Off", "On"], 0),               # pad chord holds after release (Robot, Chords, Prism, Nightcall, Harmony)
    flt("char4", "Char 4", 0, 1, 0.01, 0.5, "%"),           # macros 4 and 5: per mode a new control, or an alias of an old param
    flt("char5", "Char 5", 0, 1, 0.01, 0.5, "%"),
    # performance toggles (knobs on SPACE; not saved in "state")
    enum("freeze", "Freeze", ["Off", "Rec", "Loop"], 0),       # punch-in looper (src/dsp/freeze.h)
    enum("demo", "Demo", ["Off", "On"], 0),
]

# Read-only readouts the DSP serves (not params: never set, never saved).
# They sit under the OLED scope on PITCH knobs 5-7, so turning those knobs
# does nothing, and their names label the cells under the picture.
def readout(key, name, lo, hi, step, unit):
    r = flt(key, name, lo, hi, step, 0, unit)
    r["access"] = "read"
    return r

READOUTS = [
    readout("pitch", "Pitch", 0, 2000, 0.1, "Hz"),
    readout("target", "Target", 0, 2000, 0.1, "Hz"),
    readout("cents", "Cents", -50, 50, 1, "ct"),
]
for role, r in zip(["a", "b", "c"], READOUTS):
    r["viz"] = {"kind": "custom:tinyvoice_scope", "group": "scope", "role": role, "extra_keys": ["scope"]}

# The Freeze banner spans SPACE knobs 7-8 (Freeze, Freeze Len): Off / ARMED /
# REC / LOOP, the take length, and whether a transport clock is seen. The knobs
# under it still turn; their values show in the header while touched.
for role, k in zip(["a", "b"], ["freeze", "freeze_len"]):
    p = next(x for x in PARAMS if x["key"] == k)
    p["viz"] = {"kind": "custom:tinyvoice_freeze", "group": "freeze", "role": role, "extra_keys": ["scope"]}

# Latch (MORE knob 2) draws its own value: On / Off in the modes it acts in
# (Robot, Chords, Prism, Nightcall, Harmony), a dimmed "---" in the others.
next(x for x in PARAMS if x["key"] == "latch")["viz"] = {"kind": "custom:tinyvoice_latch", "group": "latch", "role": "a", "extra_keys": ["mode"]}

# Knobs that do nothing in some modes draw their own value (a dimmed "N/A"
# where they are inert): Key, Scale, Retune, Glide, Beauty (all on PITCH). Same
# cell, same knob. The table of modes is vt_param_acts in dsp.h and GATED in
# src/ui/canvas.js.
for k in ["key", "scale", "retune", "glide", "beauty"]:
    next(x for x in PARAMS if x["key"] == k)["viz"] = {"kind": "custom:tinyvoice_" + k, "group": k, "role": "a", "extra_keys": ["mode"]}

# Pages. VOICE is the home page (and the root's knobs): Mode, the five
# contextual knobs, Mix, Level. PITCH: the tuning knobs and the scope on 5-7.
# SPACE: delay, reverb, Freeze (its banner spans knobs 7-8). MORE: Gate and Latch.
VOICE = ["mode", "char1", "char2", "mod", "char4", "char5", "mix", "gain"]
PITCH = ["key", "scale", "retune", "glide", "pitch", "target", "cents", "beauty"]
SPACE = ["delay", "feedback", "reverb", "spread", "tone", "demo", "freeze", "freeze_len"]
MORE  = ["gate", "latch"]
MENU_ONLY = {}

CHAIN = PARAMS + READOUTS

def entry(k):
    return k

def line(o): return json.dumps(o, separators=(", ", ": "))

out = []
a = out.append
a("{")
for k, v in [("id", "tinyvoice"), ("name", "TinyVoice"), ("abbrev", "TVO"), ("version", "1.0.0"),
             ("description", "Vocal processor: vocoder and carrier modes (Robot, Snap, Chords, Prism, Tape, Nightcall) and voice transformers (Scream, Dalek, HardTune, Harmony, Walkie)"),
             ("author", "Mat Taylor"), ("dsp", "tinyvoice.so"), ("api_version", 2), ("component_type", "audio_fx")]:
    a("    %s: %s," % (json.dumps(k), json.dumps(v)))
a('    "capabilities": {')
a('        "chainable": true,')
a('        "component_type": "audio_fx",')
a('        "audio_in": true,')
a('        "midi_in": true,')
a('        "ui_hierarchy": {')
a('            "modes": null,')
a('            "levels": {')
a('                "root": {')
a('                    "name": "TinyVoice",')
a('                    "knobs": %s,' % line(VOICE))
a('                    "params": [')
a('                        {"level": "VOICE", "label": "Voice"},')
a('                        {"level": "PITCH", "label": "Pitch"},')
a('                        {"level": "SPACE", "label": "Space"},')
a('                        {"level": "MORE", "label": "More"}')
a('                    ]')
a('                },')
for name, label, keys, last in [("VOICE", "Voice", VOICE, False), ("PITCH", "Pitch", PITCH, False), ("SPACE", "Space", SPACE, False), ("MORE", "More", MORE, True)]:
    a('                "%s": {' % name)
    a('                    "label": "%s",' % label)
    a('                    "knobs": %s,' % line(keys))
    a('                    "params": %s' % line([entry(k) for k in keys + MENU_ONLY.get(name, [])]))
    a('                }%s' % ("" if last else ","))
a('            }')
a('        },')
a('        "chain_params": [')
for j, p in enumerate(CHAIN):
    a('            %s%s' % (line(p), "," if j < len(CHAIN) - 1 else ""))
a('        ]')
a('    }')
a('}')
text = "\n".join(out) + "\n"
doc = json.loads(text)
open(sys.argv[1] if len(sys.argv) > 1 else "module.json", "w").write(text)

BS, DQ = chr(92), chr(34)
def c_string(js):
    return DQ + js.replace(BS, BS + BS).replace(DQ, BS + DQ) + DQ

# The served copy: per-mode name placeholders for the three macros.
served = copy.deepcopy(doc["capabilities"]["chain_params"])
for p in served:
    slot = {"char1": "@1@", "char2": "@2@", "mod": "@3@", "char4": "@4@", "char5": "@5@"}.get(p["key"])
    if slot: p["name"] = slot
cp = json.dumps(served, separators=(",", ":"))
assert "@" not in json.dumps(doc["capabilities"]["chain_params"]), "an @ in the table would be read as a placeholder"
uh = json.dumps(doc["capabilities"]["ui_hierarchy"], separators=(",", ":"))
hdr = sys.argv[2] if len(sys.argv) > 2 else "src/dsp/module_meta.h"
with open(hdr, "w") as f:
    f.write("/* GENERATED by scripts/gen_module_json.py from the same table as module.json. Do not edit. */\n")
    f.write("#ifndef TINYVOICE_MODULE_META_H\n#define TINYVOICE_MODULE_META_H\n\n")
    f.write("/* chain_params with @1@ .. @5@ for the char1 / char2 / mod / char4 / char5 names (filled per mode by dsp.c) */\n")
    f.write("static const char VT_CHAIN_PARAMS_JSON[] = " + c_string(cp) + ";\n\n")
    f.write("static const char VT_UI_HIERARCHY_JSON[] = " + c_string(uh) + ";\n\n#endif\n")
print("wrote", len(PARAMS), "params +", len(READOUTS), "readouts;", hdr, "with", len(cp), "+", len(uh), "bytes of JSON")

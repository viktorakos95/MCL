#!/usr/bin/env python3
"""Measure the pitch knob of every MachineDrum machine and emit MCL tuning tables.

For each machine the script assigns it to track 1, then for every pitch CC value
(0..127) plays a note, measures the fundamental frequency, and finally turns the
cc -> pitch curve into a `tuning_t` entry for src/mcl/Drivers/MD/MDParams.cpp.

Backends
  offline  Renders the Gearmulator MachineDrum plugin (VST3/AU) straight to memory
           with `pedalboard`. No audio device, much faster than real time.
  live     Sends MIDI to a running Gearmulator and records its output through an
           audio loopback device (`mido` + `sounddevice`). Real time, slower.

Examples
  pip install numpy pedalboard
  python measure_tunings.py offline --plugin "C:/.../Gearmulator MD.vst3" \
      --state md_x14.state --out tunings.json
  python measure_tunings.py report tunings.json          # prints C++ snippets
  python measure_tunings.py selftest                     # no plugin needed

--state is the plugin state captured after loading the firmware (save a preset in
the plugin's own UI, or dump `plugin.raw_state`). The firmware has to be loaded
or the new machines will not exist.
"""

import argparse
import json
import math
import re
import sys
from pathlib import Path

import numpy as np

SR = 44100
NOTE_ON_S = 0.30      # how long the key is held
SLOT_S = 0.45         # time per probe (hold + release tail)
ANALYSIS = (0.04, 0.26)  # seconds after note-on used for pitch detection
REPO = Path(__file__).resolve().parents[2]

# --- MachineDrum MIDI --------------------------------------------------------

def assign_machine_sysex(track, model, init=0):
    """Same bytes as MDClass::assignMachine (MD.cpp)."""
    flag = 1 if model >= 128 else 0
    return [0x00, 0x20, 0x3C, 0x02, 0x00, 0x5B, track, model & 0x7F, flag, init]


def param_cc(track, param):
    """Track 1..4 of a channel use CC 16-39, 40-63, 64-87, 88-111."""
    return 16 + 24 * (track % 4) + param


def probe_events(model, args):
    """List of (time_s, kind, payload) for one machine; probe k starts at t0+k*SLOT_S."""
    ev = [(0.0, "sysex", assign_machine_sysex(0, model))]
    t = 0.2
    ev.append((t, "cc", (param_cc(0, 23), 127)))              # level
    ev.append((t, "cc", (param_cc(0, 1), args.decay)))        # decay
    t0 = 0.5
    for v in range(128):
        ts = t0 + v * SLOT_S
        ev.append((ts, "cc", (param_cc(0, 0), v)))
        ev.append((ts + 0.005, "on", args.note))
        ev.append((ts + NOTE_ON_S, "off", args.note))
    return ev, t0


# --- pitch detection ---------------------------------------------------------

def detect_pitch(x, sr=SR, fmin=18.0, fmax=6000.0):
    """Normalised autocorrelation. Returns (freq_hz, confidence 0..1)."""
    x = x - np.mean(x)
    if np.max(np.abs(x)) < 1e-4:
        return None, 0.0
    n = len(x)
    f = np.fft.rfft(x * np.hanning(n), 2 * n)
    ac = np.fft.irfft(f * np.conj(f))[:n]
    ac /= ac[0] + 1e-12
    lo, hi = int(sr / fmax), min(int(sr / fmin), n - 2)
    seg = ac[lo:hi]
    if len(seg) < 3:
        return None, 0.0
    # first strong local maximum, not just the global one (avoids octave errors down)
    peak_thr = 0.9 * np.max(seg)
    idx = None
    for i in range(1, len(seg) - 1):
        if seg[i] >= peak_thr and seg[i] >= seg[i - 1] and seg[i] >= seg[i + 1]:
            idx = i
            break
    if idx is None:
        return None, 0.0
    a, b, c = seg[idx - 1], seg[idx], seg[idx + 1]
    denom = a - 2 * b + c
    shift = 0.5 * (a - c) / denom if denom else 0.0
    lag = lo + idx + shift
    return sr / lag, float(seg[idx])


def hz_to_mcl_note(f):
    """MCL note numbers are MIDI-12 (MIDI_NOTE_B1 == 23 is Elektron B1 == MIDI 35)."""
    return 69 + 12 * math.log2(f / 440.0) - 12


def analyse_slots(audio, t0, sr=SR):
    out = []
    for v in range(128):
        s = int((t0 + v * SLOT_S + ANALYSIS[0]) * sr)
        e = int((t0 + v * SLOT_S + ANALYSIS[1]) * sr)
        f, conf = detect_pitch(audio[s:e], sr)
        out.append({"cc": v, "hz": f, "conf": round(conf, 3)})
    return out


# --- curve -> tuning table ---------------------------------------------------

def build_tuning(points, min_conf=0.6, max_err=0.35):
    """points: [{cc, hz, conf}]. Returns dict describing the pitch knob."""
    good = [(p["cc"], hz_to_mcl_note(p["hz"])) for p in points
            if p["hz"] and p["conf"] >= min_conf]
    res = {"pitched_points": len(good)}
    if len(good) < 8:
        res["verdict"] = "unpitched"      # nothing to tune; chromatic makes no sense
        return res
    cc = np.array([g[0] for g in good], float)
    st = np.array([g[1] for g in good])
    a, b = np.polyfit(cc, st, 1)
    resid = np.max(np.abs(st - (a * cc + b)))
    res.update(semitones_per_cc=round(float(a), 5), offset=round(float(b), 3),
               max_linear_error=round(float(resid), 3))
    res["verdict"] = "linear" if resid < max_err else "table"
    # table: for each whole semitone find the cc whose measured pitch is closest
    order = np.argsort(st)
    st_s, cc_s = st[order], cc[order]
    notes = list(range(int(math.ceil(st_s[0])), int(math.floor(st_s[-1])) + 1))
    entries = []
    for n in notes:
        j = int(np.argmin(np.abs(st_s - n)))
        err = abs(st_s[j] - n)
        entries.append((n, int(cc_s[j]), float(err)))
    # keep the longest contiguous run of in-tune notes
    best, cur = [], []
    for e in entries:
        if e[2] <= max_err:
            cur.append(e)
            if len(cur) > len(best):
                best = list(cur)
        else:
            cur = []
    if best:
        ccs = [e[1] for e in best]
        res["base_note"] = best[0][0]
        res["table"] = ccs
        res["tolerance_cc"] = int(max(1, math.ceil(1 / max(abs(a), 1e-6) * max_err)))
        res["monotonic"] = all(x < y for x, y in zip(ccs, ccs[1:]))
    return res


NOTE_NAMES = ["C", "CS", "D", "DS", "E", "F", "FS", "G", "GS", "A", "AS", "B"]


def note_macro(n):
    return "MIDI_NOTE_%s%d" % (NOTE_NAMES[n % 12], n // 12)


def cpp_snippet(name, model_macro, r):
    if "table" not in r:
        return "// %s: %s\n" % (name, r.get("verdict"))
    t = ", ".join(str(x) for x in r["table"])
    var = name.lower().replace("-", "_") + "_tuning"
    return ("// %s: %s, %.3f st/cc, max linear error %.2f st\n"
            "static const uint8_t %s[] PROGMEM = {\n  %s\n};\n"
            "  { %s, %s, sizeof(%s), %d, %s },\n") % (
        name, r["verdict"], r.get("semitones_per_cc", 0), r.get("max_linear_error", 0),
        var, t, model_macro, note_macro(r["base_note"]), var, r["tolerance_cc"], var)


# --- machine list ------------------------------------------------------------

def machine_ids():
    """Every melodic-candidate machine id from the long-name table (skip MIDI, CTR, ROM
    except ROM-01, RAM, INP duplicates)."""
    txt = (REPO / "resource/machine_names_long.cpp").read_text()
    body = txt.split("mnm_machine_names")[0]
    out = {}
    for name, mid in re.findall(r'\{"([^"]+)",\s*(\d+)\}', body):
        mid = int(mid)
        if name.startswith(("MID-", "CTR-", "RAM-")) or (name.startswith("ROM-") and name != "ROM-01"):
            continue
        out[name] = mid
    return out


# --- backends ----------------------------------------------------------------

def render_offline(plugin, events, total_s):
    import mido
    msgs = []
    for t, kind, p in events:
        if kind == "sysex":
            m = mido.Message("sysex", data=p)
        elif kind == "cc":
            m = mido.Message("control_change", channel=0, control=p[0], value=p[1])
        elif kind == "on":
            m = mido.Message("note_on", channel=0, note=p, velocity=127)
        else:
            m = mido.Message("note_off", channel=0, note=p, velocity=0)
        msgs.append((m.bytes(), t))
    out = plugin(msgs, duration=total_s, sample_rate=SR, num_channels=2, reset=False)
    return out.mean(axis=0)


def cmd_offline(a):
    import pedalboard
    plugin = pedalboard.load_plugin(a.plugin)
    if a.state:
        plugin.raw_state = Path(a.state).read_bytes()
    ids = machine_ids()
    if a.ids:
        want = set(a.ids.split(","))
        ids = {n: i for n, i in ids.items() if n in want or str(i) in want}
    results = {}
    for name, mid in ids.items():
        ev, t0 = probe_events(mid, a)
        total = t0 + 128 * SLOT_S + 0.5
        audio = render_offline(plugin, ev, total)
        pts = analyse_slots(audio, t0)
        results[name] = {"id": mid, "points": pts, "tuning": build_tuning(pts)}
        print("%-8s id %3d  %s" % (name, mid, results[name]["tuning"]["verdict"]), flush=True)
    Path(a.out).write_text(json.dumps(results, indent=1))
    print("wrote", a.out)


def cmd_live(a):
    import mido, sounddevice as sd, time
    out = mido.open_output(a.midi_out)
    ids = machine_ids()
    if a.ids:
        want = set(a.ids.split(","))
        ids = {n: i for n, i in ids.items() if n in want or str(i) in want}
    results = {}
    for name, mid in ids.items():
        ev, t0 = probe_events(mid, a)
        total = t0 + 128 * SLOT_S + 0.5
        rec = sd.rec(int(total * SR), samplerate=SR, channels=1, device=a.audio_in)
        start = time.perf_counter()
        for t, kind, p in sorted(ev, key=lambda e: e[0]):
            while time.perf_counter() - start < t:
                time.sleep(0.0005)
            if kind == "sysex":
                out.send(mido.Message("sysex", data=p))
            elif kind == "cc":
                out.send(mido.Message("control_change", channel=0, control=p[0], value=p[1]))
            elif kind == "on":
                out.send(mido.Message("note_on", channel=0, note=p, velocity=127))
            else:
                out.send(mido.Message("note_off", channel=0, note=p))
        sd.wait()
        pts = analyse_slots(rec[:, 0], t0)
        results[name] = {"id": mid, "points": pts, "tuning": build_tuning(pts)}
        print("%-8s id %3d  %s" % (name, mid, results[name]["tuning"]["verdict"]), flush=True)
    Path(a.out).write_text(json.dumps(results, indent=1))


def cmd_report(a):
    res = json.loads(Path(a.file).read_text())
    for name, r in res.items():
        t = r["tuning"]
        print("%-8s id %3d  %-9s" % (name, r["id"], t["verdict"]),
              {k: t[k] for k in ("semitones_per_cc", "max_linear_error", "base_note") if k in t})
    print()
    for name, r in res.items():
        macro = re.sub(r"[^A-Z0-9]", "_", name.upper()) + "_MODEL"
        print(cpp_snippet(name, macro, r["tuning"]))


def cmd_selftest(a):
    """Synthetic machines: a linear 0.5 st/cc knob and a curved one."""
    def synth(curve):
        audio = np.zeros(int((0.5 + 128 * SLOT_S + 0.5) * SR))
        for v in range(128):
            f = curve(v)
            s = int((0.5 + v * SLOT_S) * SR)
            t = np.arange(int(NOTE_ON_S * SR)) / SR
            audio[s:s + len(t)] += np.sin(2 * np.pi * f * t) + 0.3 * np.sin(4 * np.pi * f * t)
        return audio
    lin = lambda v: 440 * 2 ** ((24 + 0.5 * v - 69) / 12)
    crv = lambda v: 55 * 2 ** ((v / 127) ** 1.3 * 4)
    for nm, c in (("linear", lin), ("curved", crv)):
        r = build_tuning(analyse_slots(synth(c), 0.5))
        print(nm, {k: r.get(k) for k in ("verdict", "semitones_per_cc", "max_linear_error", "base_note")},
              "table len", len(r.get("table", [])), "monotonic", r.get("monotonic"))
    # stock sanity: TRX-BD tuning in MDParams.cpp starts at 1 -> B1 (61.7 Hz)
    print("B1 61.74 Hz ->", round(hz_to_mcl_note(61.74), 2), "(MIDI_NOTE_B1 = 23)")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    for nm in ("offline", "live"):
        s = sub.add_parser(nm)
        s.add_argument("--ids", help="comma list of names (AN-BD) or numeric ids; default all")
        s.add_argument("--out", default="tunings.json")
        s.add_argument("--note", type=int, default=36, help="trig note of track 1 (default 36)")
        s.add_argument("--decay", type=int, default=100)
        if nm == "offline":
            s.add_argument("--plugin", required=True)
            s.add_argument("--state")
        else:
            s.add_argument("--midi-out", required=True)
            s.add_argument("--audio-in")
    sub.add_parser("report").add_argument("file")
    sub.add_parser("selftest")
    a = p.parse_args()
    {"offline": cmd_offline, "live": cmd_live, "report": cmd_report, "selftest": cmd_selftest}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())

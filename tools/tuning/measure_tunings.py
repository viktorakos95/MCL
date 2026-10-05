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
ANALYSIS = (0.08, 0.27)  # seconds after the slot start used for pitch detection (tolerates ~50 ms latency)
REPO = Path(__file__).resolve().parents[2]

# --- MachineDrum MIDI --------------------------------------------------------

def assign_machine_sysex(track, model, init=0):
    """Same bytes as MDClass::assignMachine (MD.cpp)."""
    flag = 1 if model >= 128 else 0
    return [0x00, 0x20, 0x3C, 0x02, 0x00, 0x5B, track, model & 0x7F, flag, init]


def param_cc(track, param):
    """Track 1..4 of a channel use CC 16-39, 40-63, 64-87, 88-111."""
    return 16 + 24 * (track % 4) + param


# Parameters forced to 0 before sweeping, so the pitch knob is measured on the main oscillator only.
# {model id: {param index: value}} - indices come from the model packs' knob labels.
#   MM-SAW 13: UNIL=2, SUBX=5, SUB1=6, SUB2=7   MM-PLS 175: UNIL=4, SUB1=6, SUB2=7   SAWPW 47: SUB=4, CHOR=5
PRESETS = {13: {2: 0, 5: 0, 6: 0, 7: 0}, 175: {4: 0, 6: 0, 7: 0}, 47: {4: 0, 5: 0}}


def probe_events(model, args, assign=True):
    """List of (time_s, kind, payload) for one machine; probe k starts at t0+k*SLOT_S."""
    ev = [(0.0, "sysex", assign_machine_sysex(0, model))] if assign else []
    t = 0.2
    ev.append((t, "cc", (param_cc(0, 23), 127)))              # level
    ev.append((t, "cc", (param_cc(0, 1), args.decay)))        # decay
    for idx, val in PRESETS.get(model, {}).items():
        ev.append((t, "cc", (param_cc(0, idx), val)))
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
    # skip the lobe around lag 0 (it stays ~1.0 for very low tones), then take the first
    # strong local maximum, not just the global one (avoids octave errors down)
    neg = np.nonzero(seg < 0)[0]
    start = int(neg[0]) if len(neg) else 0
    rest = seg[start:]
    if len(rest) < 3:
        return None, 0.0
    peak_thr = 0.9 * np.max(rest)
    idx = None
    for i in range(start + 1, len(seg) - 1):
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
    """MCL note numbers are plain MIDI numbers of the sounding pitch (MIDI_NOTE_B1 == 23
    == 30.9 Hz). Checked against TRX-BD: the emulator's cc 44 sounds 49 Hz == MIDI 31,
    and MCL's stock table has note 31 at cc 44."""
    return 69 + 12 * math.log2(f / 440.0)


def analyse_slots(audio, t0, sr=SR):
    out = []
    for v in range(128):
        s = int((t0 + v * SLOT_S + ANALYSIS[0]) * sr)
        e = int((t0 + v * SLOT_S + ANALYSIS[1]) * sr)
        f, conf = detect_pitch(audio[s:e], sr)
        out.append({"cc": v, "hz": f, "conf": round(conf, 3)})
    return out


# --- curve -> tuning table ---------------------------------------------------

def _longest_rising_chain(pts, max_slope=4.0):
    """pts: sorted [(cc, note, conf)]. Longest chain with note rising with cc (slope <= max_slope st/cc).
    Detection glitches (octave errors, noise) break monotonicity and fall out of the chain."""
    n = len(pts)
    best = [1] * n
    prev = [-1] * n
    for i in range(n):
        for j in range(i):
            dc = pts[i][0] - pts[j][0]
            dn = pts[i][1] - pts[j][1]
            if dc > 0 and 0 <= dn <= max_slope * dc and best[j] + 1 > best[i]:
                best[i], prev[i] = best[j] + 1, j
    k = max(range(n), key=lambda x: best[x])
    chain = []
    while k >= 0:
        chain.append(pts[k])
        k = prev[k]
    return chain[::-1]


def build_tuning(points, min_conf=0.5, max_err=0.35):
    """points: [{cc, hz, conf}] from a 0..127 pitch-knob sweep -> description + MCL table.

    The note law is recovered from the longest monotonic run of confident points, so isolated
    detection glitches do not corrupt the table. table[i] is the CC that sounds note base_note+i.
    """
    good = sorted((p["cc"], hz_to_mcl_note(p["hz"]), p["conf"]) for p in points
                  if p["hz"] and p["conf"] >= min_conf)
    res = {"pitched_points": len(good)}
    if len(good) < 8:
        res["verdict"] = "unpitched"
        return res
    chain = _longest_rising_chain(good)
    res["chain_points"] = len(chain)
    if len(chain) < 8:
        res["verdict"] = "unpitched"
        return res
    cc = np.array([c[0] for c in chain], float)
    nt = np.array([c[1] for c in chain])
    # robust (Theil-Sen) line: ignores isolated glitches that still happen to be monotonic
    slopes = [(nt[j] - nt[i]) / (cc[j] - cc[i]) for i in range(len(cc)) for j in range(i + 1, len(cc))]
    a = float(np.median(slopes))
    b = float(np.median(nt - a * cc))
    inl = np.abs(nt - (a * cc + b)) <= 0.4
    law_is_line = inl.sum() >= 0.8 * len(cc) and inl.sum() >= 8
    if law_is_line:
        cc, nt = cc[inl], nt[inl]
        a, b = np.polyfit(cc, nt, 1)
    resid = float(np.max(np.abs(nt - (a * cc + b))))
    res.update(semitones_per_cc=round(float(a), 5), offset=round(float(b), 3),
               max_linear_error=round(resid, 3), cc_range=[int(cc[0]), int(cc[-1])],
               note_range=[round(float(nt[0]), 2), round(float(nt[-1]), 2)],
               outliers_dropped=int(len(chain) - len(cc)))
    if abs(a) < 0.05 or nt[-1] - nt[0] < 6:
        res["verdict"] = "flat"          # pitch knob does not move the pitch (or hardly)
        return res
    res["verdict"] = "linear" if resid < 0.45 else "curve"
    if law_is_line:
        lo, hi = int(cc[0]), int(cc[-1])
        entries = []
        for n in range(int(math.ceil(a * lo + b)), int(math.floor(a * hi + b)) + 1):
            c = int(round((n - b) / a))
            if lo <= c <= hi and abs(a * c + b - n) <= 0.5:
                entries.append((n, c))
        if entries:
            res["base_note"] = entries[0][0]
            res["table"] = [c for _, c in entries]
            res["tolerance_cc"] = int(max(1, round(0.5 / max(abs(a), 0.05))))
            res["monotonic"] = all(x < y for x, y in zip(res["table"], res["table"][1:]))
        return res
    # whole-semitone table by inverting the measured law (piecewise linear between chain points)
    allcc = np.arange(int(cc[0]), int(cc[-1]) + 1)
    law = np.interp(allcc, cc, nt)
    entries = []
    for n in range(int(math.ceil(nt[0])), int(math.floor(nt[-1])) + 1):
        j = int(np.argmin(np.abs(law - n)))
        err = abs(float(law[j]) - n)
        entries.append((n, int(allcc[j]), err))
    best, cur = [], []
    for e in entries:
        if e[2] <= max_err and (not cur or e[1] > cur[-1][1]):
            cur.append(e)
            if len(cur) > len(best):
                best = list(cur)
        else:
            cur = [e] if e[2] <= max_err else []
    if best:
        res["base_note"] = best[0][0]
        res["table"] = [e[1] for e in best]
        res["tolerance_cc"] = int(max(1, round(0.5 / max(abs(a), 0.05))))
        res["monotonic"] = True
    return res


NOTE_NAMES = ["C", "CS", "D", "DS", "E", "F", "FS", "G", "GS", "A", "AS", "B"]


def note_macro(n):
    return "MIDI_NOTE_%s%d" % (NOTE_NAMES[n % 12], n // 12)


def cpp_snippet(name, model_macro, r):
    if "table" not in r:
        return "// %s: %s\n" % (name, r.get("verdict"))
    t = ", ".join(str(x) for x in r["table"])
    var = re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_") + "_tuning"
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

CH = 0  # MIDI channel (0-based) of track 1; set from --channel


def render_offline(plugin, events, total_s):
    import mido
    msgs = []
    for t, kind, p in events:
        if kind == "sysex":
            m = mido.Message("sysex", data=p)
        elif kind == "cc":
            m = mido.Message("control_change", channel=CH, control=p[0], value=p[1])
        elif kind == "on":
            m = mido.Message("note_on", channel=CH, note=p, velocity=127)
        else:
            m = mido.Message("note_off", channel=CH, note=p, velocity=0)
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


def _pick(names, wanted, what):
    """Resolve a device by index or case-insensitive substring."""
    if wanted is None:
        raise SystemExit("%s not given. Run `devices` to list them." % what)
    if str(wanted).isdigit() and int(wanted) < len(names):
        return int(wanted)
    hits = [i for i, n in enumerate(names) if str(wanted).lower() in n.lower()]
    if len(hits) != 1:
        raise SystemExit("%s %r matched %d devices: %s" % (what, wanted, len(hits), [names[i] for i in hits] or names))
    return hits[0]


def cmd_devices(a):
    import mido, sounddevice as sd
    print("MIDI outputs:")
    for i, n in enumerate(mido.get_output_names()):
        print("  [%d] %s" % (i, n))
    print("Audio inputs:")
    for i, d in enumerate(sd.query_devices()):
        if d["max_input_channels"] > 0:
            print("  [%d] %s  (%d input channels)" % (i, d["name"], d["max_input_channels"]))


def cmd_diag(a):
    """Find out why a live run hears nothing: sends notes to the MD while metering EVERY input channel."""
    import mido, sounddevice as sd, time
    mnames = mido.get_output_names()
    out = mido.open_output(mnames[_pick(mnames, a.midi_out, "MIDI output")])
    devs = sd.query_devices()
    adev = _pick([d["name"] for d in devs], a.audio_in, "Audio input")
    nch = devs[adev]["max_input_channels"]
    print("Part 1 (3 s): do NOT trigger anything - measuring the noise floor...")
    rec = sd.rec(int(3 * SR), samplerate=SR, channels=nch, device=adev, dtype="float32"); sd.wait()
    floor = np.max(np.abs(rec), axis=0)
    print("  noise floor per input:", ["in%d=%.4f" % (i + 1, f) for i, f in enumerate(floor)])
    for note, label in ((a.note, "trig note %d" % a.note),):
        for ch in sorted({a.channel, 1, 2, 10}):
            rec = sd.rec(int(2.5 * SR), samplerate=SR, channels=nch, device=adev, dtype="float32")
            for k in range(4):
                out.send(mido.Message("note_on", channel=ch - 1, note=note, velocity=127)); time.sleep(0.2)
                out.send(mido.Message("note_off", channel=ch - 1, note=note, velocity=0)); time.sleep(0.4)
            sd.wait()
            pk = np.max(np.abs(rec), axis=0)
            print("  MIDI channel %2d, %s -> peaks:" % (ch, label), ["in%d=%.4f" % (i + 1, f) for i, f in enumerate(pk)])
    print("Part 3 (6 s): now PRESS TRIG KEYS ON THE MD YOURSELF for 6 seconds...")
    rec = sd.rec(int(6 * SR), samplerate=SR, channels=nch, device=adev, dtype="float32"); sd.wait()
    print("  peaks:", ["in%d=%.4f" % (i + 1, f) for i, f in enumerate(np.max(np.abs(rec), axis=0))])
    print("Reading: if Part 3 shows a loud input, use that as --audio-channel. If only Part 3 is loud, MIDI is not "
          "reaching the MD (cable/port/channel/trig note). If neither, the MD output isn't connected to the M4.")


def cmd_assign(a):
    """Put one machine on track 1 and stop, so you can read the MD display and confirm the id mapping."""
    import mido
    global CH
    CH = a.channel - 1
    mnames = mido.get_output_names()
    out = mido.open_output(mnames[_pick(mnames, a.midi_out, "MIDI output")])
    ids = machine_ids()
    for tok in a.ids.split(","):
        mid = ids.get(tok, int(tok) if tok.isdigit() else None)
        if mid is None:
            raise SystemExit("unknown machine %r" % tok)
        out.send(mido.Message("sysex", data=assign_machine_sysex(0, mid)))
        print("sent %s (id %d) to track 1 - check the MD display. Press Enter for the next one." % (tok, mid))
        if len(a.ids.split(",")) > 1:
            input()


def _open_live(a):
    """Open MIDI + audio for a real MachineDrum, run the sound check, return play(events, seconds)."""
    import mido, sounddevice as sd, time
    global CH
    CH = a.channel - 1
    mnames = mido.get_output_names()
    midi_name = mnames[_pick(mnames, a.midi_out, "MIDI output")]
    devs = sd.query_devices()
    anames = [d["name"] for d in devs]
    adev = _pick(anames, a.audio_in, "Audio input")
    nch = devs[adev]["max_input_channels"]
    if a.audio_channel > nch:
        raise SystemExit("audio device has only %d input channels" % nch)
    print("MIDI out: %s | audio in: %s, input %d | MIDI channel %d" % (midi_name, anames[adev], a.audio_channel, a.channel))
    out = mido.open_output(midi_name)

    def play(ev, total):
        rec = sd.rec(int(total * SR), samplerate=SR, channels=nch, device=adev, dtype="float32")
        start = time.perf_counter()
        for t, kind, p in sorted(ev, key=lambda e: e[0]):
            while time.perf_counter() - start < t:
                time.sleep(0.0002)
            if kind == "sysex":
                out.send(mido.Message("sysex", data=p))
            elif kind == "cc":
                out.send(mido.Message("control_change", channel=CH, control=p[0], value=p[1]))
            elif kind == "on":
                out.send(mido.Message("note_on", channel=CH, note=p, velocity=127))
            else:
                out.send(mido.Message("note_off", channel=CH, note=p, velocity=0))
        sd.wait()
        return rec[:, a.audio_channel - 1].copy()

    # sound check on a stock machine (TRX-BD) so a wrong input / channel fails fast
    sc = [(0.0, "sysex", assign_machine_sysex(0, 16)), (0.3, "cc", (param_cc(0, 23), 127)),
          (0.3, "cc", (param_cc(0, 1), a.decay)), (0.6, "cc", (param_cc(0, 0), 90)),
          (0.7, "on", a.note), (1.2, "off", a.note)]
    peak = float(np.max(np.abs(play(sc, 1.6))))
    print("sound check: peak level %.3f" % peak)
    if peak < 0.005:
        raise SystemExit("No sound on that input. Check: MD audio out -> that interface input, MIDI channel/trig note "
                         "(--channel, --note), MD set to receive MIDI, input gain/phantom off.")
    if peak > 0.98:
        print("WARNING: the signal clips. Lower the interface input gain; clipped audio hurts pitch detection.")
    return play


def _select(a):
    ids = machine_ids()
    if a.ids:
        want = set(a.ids.split(","))
        ids = {n: i for n, i in ids.items() if n in want or str(i) in want}
    return ids


def _sweep(play, mid, a, outdir, tonal=False, assign=True):
    f = outdir / ("m%d%s.f32" % (mid, "t" if tonal else ""))
    ev, t0 = probe_events(mid, a, assign=assign)
    audio = play(ev, t0 + 128 * SLOT_S + 0.5)
    audio.tofile(f)  # raw take kept so analysis can be redone: from-audio <outdir>
    pk = float(np.max(np.abs(audio)))
    r = build_tuning(analyse_slots(audio / (pk + 1e-9), t0)) if pk > 1e-4 else {"verdict": "silent"}
    return pk, r


def cmd_live(a):
    """Probe a real MachineDrum: MIDI out to the MD, audio from one input channel."""
    play = _open_live(a)
    outdir = Path(a.outdir); outdir.mkdir(parents=True, exist_ok=True)
    for name, mid in _select(a).items():
        if (outdir / ("m%d.f32" % mid)).exists() and not a.redo:
            print("%-8s id %3d  already measured (use --redo)" % (name, mid)); continue
        pk, r = _sweep(play, mid, a, outdir)
        print("%-8s id %3d  peak %.2f  %s" % (name, mid, pk, r["verdict"]), flush=True)
    print("done. Now: python measure_tunings.py from-audio %s --out tunings.json && python measure_tunings.py report tunings.json" % outdir)


def untabled_ids():
    """Machines (name -> id) that MCL has no chromatic table for yet: neither a stock/new table nor the ROM range."""
    src = (REPO / "src/mcl/Drivers/MD/MDParams.cpp").read_text()
    hdr = (REPO / "src/mcl/Drivers/MD/MDParams.h").read_text()
    macro_id = {k: int(v) for k, v in re.findall(r"#define (\w+_MODEL) (\d+)", hdr)}
    start = src.index("static const tuning_t tunings[]")
    body = src[start:src.index("};", start)]
    have = {macro_id[k] for k in re.findall(r"\{\s*(\w+_MODEL),", body) if k in macro_id}
    effects = {2, 3, 7, 8, 9, 19, 22, 23, 25, 80, 81, 82, 83, 84, 85, 86, 87}   # GND-NS/IM, NFX-*, TRX-CP/CH/OH/MA, INP-*: effects, inputs, unpitched percussion
    patcher = {6, 10, 11, 12, 13, 14, 15, 30, 31, 40, 41, 42, 43, 44, 45, 46, 47, 73, 74, 75, 76, 124, 126, 127, 175}
    return {n: i for n, i in machine_ids().items()
            if i not in have and i not in patcher and i not in effects and not (128 <= i <= 191) and i != 0}


def cmd_guided(a):
    """Walk through machines one by one. Pass 1 (normal mode) is automatic; the tonal pass (--tonal) pauses
    per machine so you can switch the machine's TUNING to TONAL on the MD, then records it."""
    play = _open_live(a)
    outdir = Path(a.outdir); outdir.mkdir(parents=True, exist_ok=True)
    ids = _select(a) if a.ids else untabled_ids()
    todo = list(ids.items())
    print("%d machines to do: %s" % (len(todo), ", ".join(n for n, _ in todo)))
    for k, (name, mid) in enumerate(todo, 1):
        print("\n[%d/%d] NEXT: %s (id %d)" % (k, len(todo), name, mid))
        if not a.tonal:
            if (outdir / ("m%d.f32" % mid)).exists() and not a.redo:
                print("   already measured, skipping (use --redo)"); continue
            pk, r = _sweep(play, mid, a, outdir)
            print("   peak %.2f  %s" % (pk, r["verdict"]), flush=True)
            continue
        if (outdir / ("m%dt.f32" % mid)).exists() and not a.redo:
            print("   tonal take exists, skipping (use --redo)"); continue
        play([(0.0, "sysex", assign_machine_sysex(0, mid))], 0.6)   # put the machine on track 1
        ans = input("   Track 1 now has %s. Set its TUNING to TONAL on the MD, then press Enter to record "
                    "(s = this machine has no tonal option, q = quit): " % name).strip().lower()
        if ans == "q":
            break
        if ans == "s":
            continue
        pk, r = _sweep(play, mid, a, outdir, tonal=True, assign=False)
        print("   tonal: peak %.2f  %s" % (pk, r["verdict"]), flush=True)
    print("\ndone. Analyse with: python measure_tunings.py from-audio %s --out tunings.json && "
          "python measure_tunings.py report tunings.json" % outdir)
    if a.tonal:
        print("Remember to set the machines you changed back to their normal tuning.")


def cmd_fromaudio(a):
    """Analyse a directory of m<id>[t].f32 files written by md_pitch_probe."""
    names = {i: n for n, i in machine_ids().items()}
    extra = json.loads(Path(a.names).read_text()) if a.names else {}
    names.update({int(k): v for k, v in extra.items()})
    results = {}
    for f in sorted(Path(a.dir).glob("m*.f32"), key=lambda p: (int(re.sub(r"\D", "", p.stem)), p.stem)):
        tonal = f.stem.endswith("t")
        mid = int(re.sub(r"\D", "", f.stem))
        audio = np.fromfile(f, dtype=np.float32)
        peak = float(np.max(np.abs(audio))) if len(audio) else 0.0
        key = "%s%s" % (names.get(mid, "ID%d" % mid), " (tonal)" if tonal else "")
        if peak < 1e-4:
            results[key] = {"id": mid, "tonal": tonal, "points": [], "tuning": {"verdict": "silent"}}
            continue
        pts = analyse_slots(audio / peak, 0.5)
        results[key] = {"id": mid, "tonal": tonal, "points": pts, "tuning": build_tuning(pts)}
    Path(a.out).write_text(json.dumps(results, indent=1))
    print("analysed", len(results), "->", a.out)


def cmd_report(a):
    res = json.loads(Path(a.file).read_text())
    for name, r in res.items():
        t = r["tuning"]
        print("%-8s id %3d  %-9s" % (name, r["id"], t["verdict"]),
              {k: t[k] for k in ("semitones_per_cc", "max_linear_error", "base_note") if k in t})
    print()
    for name, r in res.items():
        macro = re.sub(r"[^A-Z0-9]", "_", name.split(" (")[0].upper()) + "_MODEL"
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
    # TRX-BD in MDParams.cpp starts at cc 1 -> MIDI_NOTE_B1 (30.87 Hz)
    print("30.87 Hz ->", round(hz_to_mcl_note(30.87), 2), "(MIDI_NOTE_B1 = 23)")


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
            s.add_argument("--midi-out", help="MIDI output name (substring) or index")
            s.add_argument("--audio-in", help="audio input device name (substring) or index")
            s.add_argument("--audio-channel", type=int, default=1, help="1-based input channel of that device")
            s.add_argument("--channel", type=int, default=1, help="MD base MIDI channel 1-16 (track 1)")
            s.add_argument("--outdir", default="takes", help="raw takes are kept here")
            s.add_argument("--redo", action="store_true", help="re-measure machines that already have a take")
    gd = sub.add_parser("guided", help="step through machines one by one on a real MD (older machines without a table by default)")
    gd.add_argument("--ids", help="machines to do (names or ids); default: every machine MCL has no table for")
    gd.add_argument("--tonal", action="store_true", help="tonal pass: pauses so you can switch the machine to TONAL")
    gd.add_argument("--note", type=int, default=36); gd.add_argument("--decay", type=int, default=100)
    gd.add_argument("--midi-out"); gd.add_argument("--audio-in"); gd.add_argument("--audio-channel", type=int, default=1)
    gd.add_argument("--channel", type=int, default=1); gd.add_argument("--outdir", default="takes"); gd.add_argument("--redo", action="store_true")
    sub.add_parser("devices", help="list MIDI outputs and audio inputs")
    asg = sub.add_parser("assign", help="put machines on track 1 one by one to check ids against the MD display")
    asg.add_argument("--midi-out"); asg.add_argument("--ids", required=True); asg.add_argument("--channel", type=int, default=1)
    dg = sub.add_parser("diag", help="meter every input while sending MIDI notes, to find the problem")
    dg.add_argument("--midi-out"); dg.add_argument("--audio-in")
    dg.add_argument("--note", type=int, default=36); dg.add_argument("--channel", type=int, default=1)
    fa = sub.add_parser("from-audio", help="analyse m<id>[t].f32 files from md_pitch_probe")
    fa.add_argument("dir"); fa.add_argument("--out", default="tunings.json")
    fa.add_argument("--names", help="json {id: name} for ids missing from machine_names_long.cpp")
    sub.add_parser("report").add_argument("file")
    sub.add_parser("selftest")
    a = p.parse_args()
    {"offline": cmd_offline, "live": cmd_live, "devices": cmd_devices, "guided": cmd_guided, "diag": cmd_diag, "assign": cmd_assign, "from-audio": cmd_fromaudio, "report": cmd_report, "selftest": cmd_selftest}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())

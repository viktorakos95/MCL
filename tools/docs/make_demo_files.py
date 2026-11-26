#!/usr/bin/env python3
"""Fill a capture data folder's MCL card with synthetic projects, sounds and samples,
for the manual's file-browser screenshots. Only names (and WAV sizes, which the
Sample Manager shows) matter: nothing here is loaded, so no capture may load one.

The browsers list entries in directory order, which on an APFS volume follows a
hash of each name. These names were picked so that they list in order there
(samples 01 to 07, sounds alphabetically); other volumes may order them differently.

usage:
    python3 tools/docs/make_demo_files.py <capture data dir>
(after a first capture with --data-dir <dir> has created project000 there;
make_demo_rows.py fills that project's rows).
"""
import shutil, struct, sys, wave
from pathlib import Path

mcl = Path(sys.argv[1]) / "modules/mcl/data/MCL"
proj = mcl / "Projects"
template = next(proj.glob("project000/project000.mcl"))
def project(parent, name):
    d = parent / name; d.mkdir(parents=True, exist_ok=True)
    shutil.copy(template, d / f"{name}.mcl")
for name in ["DEMO", "JAM01", "SONG_A", "TECHNO"]:
    project(proj, name)
for name in ["SET1", "SET2"]:
    project(proj / "LIVESET", name)

snd = mcl / "Sounds"; snd.mkdir(exist_ok=True)
(snd / "KICKS").mkdir(exist_ok=True)
for name in ["BD_DEEP1", "BD_THUD9", "CB_BELL2", "CP_WIDE5", "HH_OPEN9", "RS_RIM", "SD_FAT3", "TOM_MID2"]:
    (snd / f"{name}.snd").write_bytes(b"\0" * 64)
for name in ["BD_SUB", "BD_CLICK"]:
    (snd / "KICKS" / f"{name}.snd").write_bytes(b"\0" * 64)

sizes = iter([9000, 6200, 2400, 7800, 5300, 11600, 22000, 30000, 26000])
def tiny_wav(path):
    # Silent mono 16-bit audio of a plausible drum-sample length, so the
    # browser's size readout looks real.
    frames = next(sizes, 8000)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(44100)
        w.writeframes(b"\0\0" * frames)
wav = mcl / "Samples/WAV"; wav.mkdir(parents=True, exist_ok=True)
(wav / "DRUMS").mkdir(exist_ok=True)
for name in ["01KICK1", "02SNR", "03HIHAT", "04CLAPS9", "05TOM2", "06HEY8", "07CHORD"]:
    tiny_wav(wav / f"{name}.wav")
for name in ["BREAK1", "BREAK2"]:
    tiny_wav(wav / "DRUMS" / f"{name}.wav")
print("demo files written under", mcl)

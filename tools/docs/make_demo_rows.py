#!/usr/bin/env python3
"""Write synthetic rows into a capture project's Grid X file, for manual screenshots.

Only the row headers are written (row active, name, and each column's track type
and Machinedrum machine); the Grid page draws slot labels and row names from these
alone, so the slots themselves stay empty and must not be loaded in a capture.

usage:
    1. Run one capture with --data-dir <dir> so MCL creates its project there.
    2. python3 tools/docs/make_demo_rows.py <dir>/modules/mcl/data/MCL/Projects/project000/project000.0
    3. Capture with --data-dir pointing at a copy of <dir> for each screenshot.
"""
import struct, sys
from pathlib import Path

SLOT = 4096; WIDTH = 16; MD = 4; EMPTY = 0
M = {  # second half of the short name -> a Machinedrum machine id
    "BD": 16, "SD": 17, "XT": 18, "CP": 19, "RS": 20, "CB": 21, "CH": 22, "OH": 23, "CY": 24,
    "MA": 25, "CL": 26, "XC": 27, "B2": 28, "S2": 29, "HH": 38,
    "HT": 50, "LT": 51, "RC": 57, "CC": 58, "BR": 59, "TA": 60, "TR": 61, "SH": 62, "BC": 63,
    "MT": 66, "ML": 67, "SN": 1, "NS": 2, "IM": 3, "PU": 5, "EV": 7, "AL": 112, "8P": 113,
    "RE": 120, "EQ": 122, "01": 128, "02": 129, "07": 134, "R1": 160, "P1": 162,
}
ROWS = [
    ("INTRO",    "BD SD -- CH -- RS CB OH SN -- CY -- MA -- EV --"),
    ("GROOVE A", "BD SD HT CH OH CP RS CY B2 S2 MA CL TA SH NS AL"),
    ("GROOVE B", "B2 S2 LT HH OH CP RC CY ML MT PU CC BR TR IM 8P"),
    ("BREAK",    "-- SD MT -- HH -- CC RC 01 02 -- R1 P1 -- RE EQ"),
    ("FILL",     "BD SD HT LT MT CP CB CY XT XC CL MA TA BC SH NS"),
    ("DROP",     "BD -- XT CH OH CP -- CY B2 -- MA -- 07 R1 -- AL"),
    ("OUTRO",    "BD SD -- -- OH -- RC -- -- -- CY -- EV -- -- --"),
    ("AMBIENT",  "-- -- MT ML -- -- CC RC SN NS IM PU -- -- RE --"),
]

grid = Path(sys.argv[1])
data = bytearray(grid.read_bytes())
for row, (name, cols) in enumerate(ROWS):
    labels = cols.split()
    assert len(labels) == WIDTH, name
    types = bytes(EMPTY if l == "--" else MD for l in labels)
    models = bytes(0 if l == "--" else M[l] for l in labels)
    header = struct.pack("<?17s16s16s", True, name.encode().ljust(17, b"\0"), types, models)
    off = row * (WIDTH + 1) * SLOT
    data[off:off + len(header)] = header
grid.write_bytes(bytes(data))
print("wrote", len(ROWS), "rows to", grid)

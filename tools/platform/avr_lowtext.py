#!/usr/bin/env python3
"""Choose the functions linked below 128 KB on the AVR build.

AVR function pointers are 16-bit word addresses. A function whose address is
taken (virtual functions, callbacks, menu and ISR tables) and that lands
above 128 KB needs a 4-byte linker trampoline. avr6_mcl.x links the sections
listed in avr_lowtext.ld first, in .lowtext, directly after PROGMEM.

Address-taken code is larger than the space below 128 KB, and moving a
function away from its neighbours costs 2 bytes for every short call
(rcall/rjmp, within 4 KB) that becomes a long one. So the list is chosen by a
minimum cut: each address-taken function below 128 KB saves 4 bytes, each
short call between a listed and an unlisted function costs 2, and the listed
functions must fit below 128 KB. They keep their link order, so short calls
between listed functions stay short.

usage:
    MCL_AVR_LOWTEXT_FREE=1 pio run -e megacmd
    python3 tools/platform/avr_lowtext.py .pio/build/megacmd/firmware.elf
    pio run -e megacmd

The first build ignores the list (the compiler's own order); the AVR link
keeps relocations (-Wl,-q, image unchanged), which name every address-taken
function. A stale list is still correct: an entry that matches nothing is
ignored, and a missing function only keeps its trampoline.
"""
import bisect
import collections
import pathlib
import re
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
LOW_LIMIT = 0x20000
MARGIN = 512  # room for code growth before listed functions pass 128 KB


def tool(name):
    found = shutil.which(name)
    if found:
        return found
    return str(pathlib.Path.home() / ".platformio/packages/toolchain-atmelavr/bin" / name)


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def load_functions(elf):
    """Functions in address order: (address, size, names)."""
    by_address = collections.OrderedDict()
    symbols = {}
    for line in run(tool("avr-nm"), "-S", "-n", elf).splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0], 16)
        if len(parts) == 4 and parts[2] in "TtWw":
            address, size = int(parts[0], 16), int(parts[1], 16)
            entry = by_address.setdefault(address, [size, []])
            entry[0] = max(entry[0], size)
            entry[1].append(parts[3])
        if len(parts) == 4:
            symbols[parts[3]] = int(parts[0], 16)
    return [(a, s, n) for a, (s, n) in by_address.items()], symbols


def owner_lookup(functions):
    starts = [f[0] for f in functions]

    def owner(address):
        i = bisect.bisect_right(starts, address) - 1
        if i >= 0 and address < functions[i][0] + max(functions[i][1], 2):
            return i
        return None
    return owner


def address_taken(elf, owner):
    taken = set()
    pattern = re.compile(r"R_AVR_(16_PM|LO8_LDI_GS)\s+([0-9a-f]+)\s+\S+\s+\+\s+([0-9a-f]+)")
    for match in pattern.finditer(run(tool("avr-readelf"), "-rW", elf)):
        target = int(match.group(2), 16) + int(match.group(3), 16)
        index = owner(target)
        if index is not None:
            taken.add(index)
    return taken


def short_calls(elf, owner):
    weight = collections.Counter()
    dump = run(tool("avr-objdump"), "-d", "-j", ".text", elf)
    pattern = re.compile(
        r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} ){2}\s*(?:rcall|rjmp)\s+\.[+-]\d+\s+;\s+0x([0-9a-f]+)",
        re.M)
    for match in pattern.finditer(dump):
        a, b = owner(int(match.group(1), 16)), owner(int(match.group(2), 16))
        if a is not None and b is not None and a != b:
            weight[(min(a, b), max(a, b))] += 2
    return weight


def min_cut(count, source_cap, sink_cap, pairs):
    """Nodes 0..count-1; returns the set on the source (listed) side."""
    source, sink = count, count + 1
    graph = [[] for _ in range(count + 2)]

    def add(u, v, cap_uv, cap_vu=0):
        graph[u].append([v, cap_uv, len(graph[v])])
        graph[v].append([u, cap_vu, len(graph[u]) - 1])

    for i in range(count):
        if source_cap[i]:
            add(source, i, source_cap[i])
        if sink_cap[i]:
            add(i, sink, sink_cap[i])
    for (a, b), w in pairs.items():
        add(a, b, w, w)
    while True:  # Dinic
        level = [-1] * (count + 2)
        level[source] = 0
        queue = collections.deque([source])
        while queue:
            u = queue.popleft()
            for v, cap, _ in graph[u]:
                if cap > 0 and level[v] < 0:
                    level[v] = level[u] + 1
                    queue.append(v)
        if level[sink] < 0:
            break
        pointer = [0] * (count + 2)

        def push(u, flow):
            if u == sink:
                return flow
            while pointer[u] < len(graph[u]):
                edge = graph[u][pointer[u]]
                v, cap, back = edge
                if cap > 0 and level[v] == level[u] + 1:
                    pushed = push(v, min(flow, cap))
                    if pushed:
                        edge[1] -= pushed
                        graph[v][back][1] += pushed
                        return pushed
                pointer[u] += 1
            return 0

        sys.setrecursionlimit(10000)
        while push(source, float("inf")):
            pass
    seen = {source}
    queue = collections.deque([source])
    while queue:
        u = queue.popleft()
        for v, cap, _ in graph[u]:
            if cap > 0 and v not in seen:
                seen.add(v)
                queue.append(v)
    return {i for i in seen if i < count}


def pattern(symbol):
    # Clone suffixes (.constprop.N, .lto_priv.N, .isra.N, .part.N) change
    # from build to build; match any number.
    return re.sub(r"\.\d+", ".*", symbol)


def main():
    elf = sys.argv[1]
    listing = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else HERE / "avr_lowtext.ld"
    functions, symbols = load_functions(elf)
    low_start = symbols.get("__ctors_start")
    if low_start is None:
        sys.exit("no __ctors_start in " + elf)
    owner = owner_lookup(functions)
    taken = address_taken(elf, owner)
    if not taken:
        sys.exit("no relocations in " + elf + " (link with -Wl,-q)")
    pairs = short_calls(elf, owner)
    # Only compiler output can be listed: library code has other section names.
    movable = [i for i, (a, s, names) in enumerate(functions)
               if a >= low_start and not any(n.startswith("__") for n in names)]
    index = {f: k for k, f in enumerate(movable)}
    local_pairs = {(index[a], index[b]): w for (a, b), w in pairs.items()
                   if a in index and b in index}
    budget = LOW_LIMIT - low_start - MARGIN
    sizes = [functions[f][1] for f in movable]
    best = set()
    low, high = 0.0, 8.0  # cost per byte of low space, binary searched
    for _ in range(30):
        price = (low + high) / 2
        chosen = min_cut(len(movable),
                         [4 if f in taken else 0 for f in movable],
                         [price * size for size in sizes],
                         local_pairs)
        if sum(sizes[k] for k in chosen) <= budget:
            best, high = chosen, price
        else:
            low = price
    listed = sorted(movable[k] for k in best)
    stubs = sum(1 for f in taken if f not in set(listed))
    lines = []
    for f in listed:
        for name in functions[f][2]:
            lines.append(f"*(.text.{pattern(name)})")
    header = ("/* Generated by tools/platform/avr_lowtext.py from the compiler's own\n"
              "   order: functions linked below 128 KB (see that script). */\n")
    listing.write_text(header + "".join(f"{line}\n" for line in lines))
    used = sum(functions[f][1] for f in listed)
    print(f"{listing}: {len(listed)} functions, {used} of {budget} bytes, "
          f"{len(taken)} address-taken, at most {stubs} keep a trampoline")


if __name__ == "__main__":
    main()

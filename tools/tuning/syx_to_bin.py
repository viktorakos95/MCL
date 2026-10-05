#!/usr/bin/env python3
"""Convert a MachineDrum OS update (.syx) into an 8 MiB flash image (.bin).

Same decoding as the md-hotswap patcher: the OS payload is written at 0x4000 of an
image filled with 0xFF. With --base, the payload is written into a copy of an
existing image instead (use your official .bin so bootloader and everything outside
the OS area stay byte-identical).

  python syx_to_bin.py patched.syx patched.bin
  python syx_to_bin.py patched.syx patched.bin --base official.bin
"""
import argparse
import sys

OS_AT = 0x4000
OS_END = 0x100000
FLASH_SIZE = 8 * 1024 * 1024
HEADER = bytes([0x00, 0x20, 0x3C, 0x02, 0x00])


def decode(syx):
    out, pos, expect, total = bytearray(), 0, OS_AT, None
    while True:
        s = syx.find(b"\xf0", pos)
        if s < 0:
            break
        e = syx.find(b"\xf7", s)
        if e < 0:
            raise SystemExit("unterminated SysEx message")
        m, pos = syx[s + 1:e], e + 1
        if m[:5] != HEADER:
            raise SystemExit("not a Machinedrum OS update packet")
        if m[5] == 0x7F:
            total = int("".join(format(b, "x") for b in m[6:12]), 16)
        elif m[5] == 0x7E and len(m) > 14:
            addr = int("".join(format(b, "x") for b in m[8:14]), 16)
            if addr != expect:
                raise SystemExit("gap in the OS update at %x" % addr)
            data = bytearray()
            for u in range(14, len(m) - 2, 3):
                v = (m[u] & 3) << 14 | (m[u + 1] & 127) << 7 | (m[u + 2] & 127)
                data += bytes([v >> 8, v & 255])
            chk = (addr >> 16 & 255) + (addr >> 8 & 255) + (addr & 255) + sum(data)
            if m[6] != (chk >> 4 & 15) or m[7] != (chk & 15):
                raise SystemExit("checksum error at %x" % addr)
            out += data
            expect += len(data)
    if total != len(out):
        raise SystemExit("the end message does not match the data length")
    return bytes(out)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("syx"); p.add_argument("bin"); p.add_argument("--base")
    a = p.parse_args()
    os_data = decode(open(a.syx, "rb").read())
    if OS_AT + len(os_data) > OS_END:
        raise SystemExit("OS update larger than the OS area")
    img = bytearray(open(a.base, "rb").read()) if a.base else bytearray(b"\xff" * FLASH_SIZE)
    if len(img) != FLASH_SIZE:
        print("warning: base image is %d bytes, expected %d" % (len(img), FLASH_SIZE), file=sys.stderr)
    img[OS_AT:OS_AT + len(os_data)] = os_data
    open(a.bin, "wb").write(img)
    print("wrote %s (%d bytes, OS %d bytes at 0x%x)" % (a.bin, len(img), len(os_data), OS_AT))


if __name__ == "__main__":
    main()

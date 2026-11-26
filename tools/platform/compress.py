"""Compress one MCL asset for src/mcl/ResourceManager/unpack.cpp.

The format is in the style of ZX0. Bits go eight to a byte, first bit
highest, and a bit byte sits where the decoder fetches it: just before
whatever follows its first bit. A number (1 or more) is written most
significant bit first, without its top bit, as pairs (0, bit) closed by a 1.

Tokens, each chosen by one flag bit:

    after a copy, and at the start:
        0  literal run: number(length), then the bytes
        1  copy from a new offset
    after a literal run:
        0  copy from the last offset: number(length)
        1  copy from a new offset

A copy from a new offset is number(((offset - 1) >> 7) + 1), then a byte
holding the low 7 bits of offset - 1 above the first bit of
number(length - 1), then the rest of that number. A high part of 256 ends
the stream, so offsets reach 32640. The last offset starts at 1.

The encoder picks the cheapest token sequence by exact bit cost, following
the last offset, and decodes its own output before writing it.
"""

import sys

END_HIGH = 256         # the high part that ends the stream
KEEP = 64              # arrivals kept at a position: (last offset, state) pairs
MAX_CHAIN = 2048       # earlier positions tried as a copy source, nearest first
NEAR = 512             # nearest copy sources always tried, at lengths up to SHORT
SHORT = 64
EVERY_LENGTH = 96      # copies up to this long are tried at every length

AFTER_COPY = 0         # also the state at the start
AFTER_LITERALS = 1


def number_bits(value):
    """Bits a number takes, including the closing 1."""
    return 2 * (value.bit_length() - 1) + 1


def literals_bits(count):
    return 1 + number_bits(count) + 8 * count


def last_offset_bits(length):
    return 1 + number_bits(length)


def new_offset_bits(offset, length):
    # flag, high part, the byte, and the length less the bit kept in the byte
    return 1 + number_bits(((offset - 1) >> 7) + 1) + 8 + number_bits(length - 1) - 1


def lengths_to_try(shortest, longest):
    """Every length for a short copy; for a long one, every length up to
    EVERY_LENGTH and then the longest of each cost step and the full length."""
    if longest <= EVERY_LENGTH:
        return range(shortest, longest + 1)
    lengths = list(range(shortest, EVERY_LENGTH + 1))
    step = EVERY_LENGTH * 2
    while step < longest:
        lengths.append(step)
        step *= 2
    lengths.append(longest)
    return lengths


def match_length(data, position, offset, limit):
    source = position - offset
    length = 0
    while length < limit and data[source + length] == data[position + length]:
        length += 1
    return length


def copy_sources(data, position, earlier, limit):
    """(offset, longest length to try) for earlier positions worth copying
    from, nearest first. A source that copies more than every nearer one is
    listed at its full length: a nearer offset never costs more bits. The
    NEAR nearest others are listed up to SHORT bytes, because a short copy
    from them can set up a cheap last-offset copy for what follows."""
    found = []
    best = 1
    tried = 0
    for source in reversed(earlier):
        tried += 1
        if tried > MAX_CHAIN or (tried > NEAR and best >= limit):
            break
        offset = position - source
        # Cheap test first: does it match one byte further than the best so far?
        if best < limit and (data[source:source + best + 1] ==
                             data[position:position + best + 1]):
            length = match_length(data, position, offset, limit)
            best = length
        elif tried <= NEAR:
            length = match_length(data, position, offset, min(limit, SHORT))
        else:
            continue
        if length >= 2:
            found.append((offset, length))
    return found


def choose_tokens(data):
    """The cheapest way to write data, as a list of tokens:
    ("literals", count), ("last", length) or ("new", length, offset)."""
    size = len(data)
    # arrivals[p]: (last offset, state) -> (bits so far, literal run length,
    #                                       previous position, previous key, token)
    arrivals = [dict() for _ in range(size + 1)]
    arrivals[0][(1, AFTER_COPY)] = (0, 0, -1, None, None)
    seen = {}                      # two bytes -> positions they start at

    def arrive(position, key, bits, run, back):
        current = arrivals[position].get(key)
        if current is None or bits < current[0]:
            arrivals[position][key] = (bits, run) + back

    for position in range(size):
        here = sorted(arrivals[position].items(), key=lambda item: item[1][0])
        here = here[:KEEP]
        arrivals[position] = dict(here)
        limit = size - position

        for key, entry in here:
            offset, state = key
            bits, run = entry[0], entry[1]
            literal_key = (offset, AFTER_LITERALS)
            if state == AFTER_LITERALS:
                # one more byte in this run
                arrive(position + 1, literal_key,
                       bits + 8 + number_bits(run + 1) - number_bits(run), run + 1,
                       (position, key, ("literal",)))
                if offset <= position:
                    longest = match_length(data, position, offset, limit)
                    for length in lengths_to_try(1, longest):
                        arrive(position + length, (offset, AFTER_COPY),
                               bits + last_offset_bits(length), 0,
                               (position, key, ("last", length)))
            else:
                arrive(position + 1, literal_key, bits + literals_bits(1), 1,
                       (position, key, ("literal",)))

        # A copy from a new offset costs the same whatever came before it,
        # so only the cheapest arrival needs to try it.
        cheapest_key, cheapest = here[0]
        if limit >= 2:
            pair = data[position:position + 2]
            for offset, longest in copy_sources(data, position, seen.get(pair, ()), limit):
                for length in lengths_to_try(2, longest):
                    arrive(position + length, (offset, AFTER_COPY),
                           cheapest[0] + new_offset_bits(offset, length), 0,
                           (position, cheapest_key, ("new", length, offset)))
            seen.setdefault(pair, []).append(position)

    key = min(arrivals[size], key=lambda k: arrivals[size][k][0])
    tokens = []
    position = size
    while position > 0:
        _, _, previous, previous_key, token = arrivals[position][key]
        if token[0] == "literal":
            if tokens and tokens[-1][0] == "literals":
                tokens[-1] = ("literals", tokens[-1][1] + 1)
            else:
                tokens.append(("literals", 1))
        else:
            tokens.append(token)
        position, key = previous, previous_key
    tokens.reverse()
    return tokens


class BitWriter:
    def __init__(self):
        self.out = bytearray()
        self.bit_byte = 0
        self.used = 8

    def bit(self, value):
        if self.used == 8:
            self.bit_byte = len(self.out)
            self.out.append(0)
            self.used = 0
        if value:
            self.out[self.bit_byte] |= 0x80 >> self.used
        self.used += 1

    def number(self, value, first_bit_written=False):
        bits = []
        for digit in bin(value)[3:]:
            bits += [0, int(digit)]
        bits.append(1)
        for b in bits[1 if first_bit_written else 0:]:
            self.bit(b)

    @staticmethod
    def first_bit(value):
        return 1 if value == 1 else 0


def write_stream(data, tokens):
    writer = BitWriter()
    position = 0
    for token in tokens:
        if token[0] == "literals":
            count = token[1]
            writer.bit(0)
            writer.number(count)
            writer.out += data[position:position + count]
            position += count
        elif token[0] == "last":
            writer.bit(0)
            writer.number(token[1])
            position += token[1]
        else:
            length, offset = token[1], token[2]
            high = ((offset - 1) >> 7) + 1
            if high >= END_HIGH:
                raise ValueError("offset too far for the format")
            writer.bit(1)
            writer.number(high)
            writer.out.append(((offset - 1) & 0x7F) << 1 |
                              BitWriter.first_bit(length - 1))
            writer.number(length - 1, first_bit_written=True)
            position += length
    writer.bit(1)
    writer.number(END_HIGH)
    return bytes(writer.out)


def unpack(stream):
    """unpack.cpp in Python. Returns the output and the bytes it read."""
    read = 0
    buf = 0x80
    out = bytearray()
    offset = 1

    def next_bit():
        nonlocal buf, read
        b = buf
        buf = (b << 1) & 0xFF
        if buf == 0:
            b = stream[read]
            read += 1
            buf = ((b << 1) | 1) & 0xFF
        return b >> 7

    def number(control):
        value = 1
        while not control:
            value = ((value << 1) | next_bit()) & 0xFFFF
            control = next_bit()
        return value

    def copy(distance, length):
        source = len(out) - distance
        if source < 0:
            raise ValueError("copy from before the start of the output")
        for _ in range(length):
            out.append(out[source])
            source += 1

    while True:
        if not next_bit():
            count = number(next_bit())
            out.extend(stream[read:read + count])
            read += count
            if not next_bit():
                copy(offset, number(next_bit()))
                continue
        high = number(next_bit())
        if high == END_HIGH:
            return bytes(out), read
        low = stream[read]
        read += 1
        offset = ((high - 1) << 7 | (low >> 1)) + 1
        copy(offset, number(low & 1) + 1)


def compress(data):
    stream = write_stream(data, choose_tokens(data))
    decoded, read = unpack(stream)
    if decoded != data or read != len(stream):
        raise RuntimeError("the compressed stream does not decode to its input")
    return stream


def main():
    if len(sys.argv) < 3:
        print(f"Usage: python {sys.argv[0]} <infile> <outfile>")
        sys.exit(1)

    infile, outfile = sys.argv[1], sys.argv[2]
    print(f"Packing file: {infile}")
    try:
        with open(infile, "rb") as f:
            data = f.read()
    except FileNotFoundError:
        print(f"Error: Input file not found: {infile}", file=sys.stderr)
        sys.exit(1)

    stream = compress(data)
    with open(outfile, "wb") as f:
        f.write(stream)

    print(f"In file size  = {len(data):08X} ({len(data)})")
    print(f"Out file size = {len(stream):08X} ({len(stream)})")


if __name__ == "__main__":
    main()

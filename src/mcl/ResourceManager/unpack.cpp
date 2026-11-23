#include "unpack.h"

// Decodes an asset written by tools/platform/compress.py, which describes
// the format. Bits come eight to a byte, first bit highest; a number is
// pairs (0, bit) closed by a 1.

struct Bits {
  const uint8_t *src;
  uint8_t buf;
};

static uint8_t next_bit(Bits &in) {
  uint8_t b = in.buf;
  in.buf = b << 1;
  if (in.buf == 0) {
    b = pgm_read_byte(in.src++);
    in.buf = (b << 1) | 1;
  }
  return b >> 7;
}

// The number's first control bit may already have been read.
static uint16_t number(Bits &in, uint8_t control) {
  uint16_t v = 1;
  while (!control) {
    v = (v << 1) | next_bit(in);
    control = next_bit(in);
  }
  return v;
}

static uint8_t *copy(uint8_t *dst, uint16_t offset, uint16_t len) {
  const uint8_t *from = dst - offset;
  do {
    *dst++ = *from++;
  } while (--len);
  return dst;
}

size_t unpack(uint8_t *packed, uint8_t *dst) {
  Bits in = {packed, 0x80};
  uint8_t *start = dst;
  uint16_t offset = 1;
  for (;;) {
    if (!next_bit(in)) {
      // literal run, then a copy from the last offset or a new one
      uint16_t len = number(in, next_bit(in));
      do {
        *dst++ = pgm_read_byte(in.src++);
      } while (--len);
      if (!next_bit(in)) {
        dst = copy(dst, offset, number(in, next_bit(in)));
        continue;
      }
    }
    // copy from a new offset; a high part of 256 ends the stream
    uint16_t high = number(in, next_bit(in));
    if (high == 256) {
      return dst - start;
    }
    uint8_t low = pgm_read_byte(in.src++);
    offset = ((high - 1) << 7 | (low >> 1)) + 1;
    dst = copy(dst, offset, number(in, low & 1) + 1);
  }
}

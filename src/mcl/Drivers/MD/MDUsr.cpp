#include "MDUsr.h"
#include <string.h>
#include "MD.h"
#include "MCLMemory.h"

MDUsrCache md_usr;

#if defined(__AVR__)
// The bank-zero globals end almost at 0xffff. Use the unallocated bank-one
// tail, after the existing track caches; never bank-switch over a global copy.
static constexpr uint32_t kUsrCache = BANK1_USR_CACHE_START;
static_assert(BANK1_USR_CACHE_LEN == MD_USR_SLOTS * MD_USR_RECORD_BYTES,
              "USR metadata layout differs from the reserved SRAM region");
static_assert(kUsrCache + MD_USR_SLOTS * MD_USR_RECORD_BYTES <= 0x10000UL,
              "USR cache overlaps the end of AVR bank one");
// Parameter labels have their own bank-one region, after kit 65.1's fields;
// readByte/writeByte offsets are relative to the slot records.
static constexpr uint16_t kUsrLabels = BANK1_USR_LABELS_START - BANK1_USR_CACHE_START;
static_assert(BANK1_USR_LABELS_LEN == MD_USR_SLOTS * MD_USR_LABEL_BYTES,
              "USR label layout differs from the reserved SRAM region");
#else
// Parameter labels follow the slot records.
static constexpr uint16_t kUsrLabels = MD_USR_SLOTS * MD_USR_RECORD_BYTES;
#endif

uint8_t MDUsrCache::readByte(uint16_t offset) const {
#if defined(__AVR__)
  return get_byte_bank1(reinterpret_cast<volatile uint8_t *>(uint16_t(kUsrCache + offset)));
#else
  return records_[offset];
#endif
}

void MDUsrCache::writeByte(uint16_t offset, uint8_t value) {
#if defined(__AVR__)
  put_byte_bank1(reinterpret_cast<volatile uint8_t *>(uint16_t(kUsrCache + offset)), value);
#else
  records_[offset] = value;
#endif
}

uint16_t MDUsrCache::labels(uint8_t slot) const {
  const uint16_t offset = kUsrLabels + uint16_t(slot) * MD_USR_LABEL_BYTES;
  return readByte(offset) == (0x80 | label_epoch_) ? offset + 1 : 0;
}

void MDUsrCache::reset() {
  valid_pages_ = 0x80;
  slot_count_ = 0;
  pending_ = 255;
  kit_received_ = kit_dirty_ = false;
  retries_ = 0;
#if defined(__AVR__)
  // External SRAM is not cleared at power-on: no label is valid until read.
  for (uint8_t slot = 0; slot < MD_USR_SLOTS; ++slot)
    writeByte(kUsrLabels + uint16_t(slot) * MD_USR_LABEL_BYTES, 0);
#endif
}

// Callers have checked FW_CAP_USR_QUERY.
bool MDUsrCache::request(uint8_t page, uint8_t kit) {
  if (++tag_ == 127) tag_ = 0; // $7f is reserved for unsolicited notices.
  uint8_t message[] = {0x70, MD_USR_QUERY, MD_USR_VERSION, tag_, page, kit};
  pending_ = page;
  requested_at_ = read_clock_ms();
  if (MD.sendRequest(message, sizeof(message)) != 0) return true;
  pending_ = 255;
  return false;
}

void MDUsrCache::kitChanged() {
  // Called by the MIDI listener. All requests stay in the foreground loop.
  kit_dirty_ = true;
  kit_received_ = false;
  pending_ = 255;
  retries_ = 0;
}

void MDUsrCache::retry() {
  requested_at_ = read_clock_ms();
  if (++retries_ < 3) {
    pending_ = 254; // One-second delay between bounded delivery retries.
  } else {
    pending_ = 255;
    kit_dirty_ = false;
    // Wait for the next notification, reconnect or explicit kit read.
    slot_count_ = 0;
    valid_pages_ = 0x87;
  }
}

void MDUsrCache::service() {
  if (!MD.connected || !(MD.fw_caps & FW_CAP_USR_QUERY)) return;
  if (pending_ != 255) {
    if (uint16_t(read_clock_ms() - requested_at_) < 1000) return;
    if (pending_ != 254) {
      retry();
      return;
    }
    pending_ = 255;
  }
  if (kit_dirty_) {
    kit_dirty_ = false;
    if (!MD.getBlockingKit(127, 1000)) {
      // A newer event during the read already scheduled a fresh attempt.
      if (!kit_dirty_) { kit_dirty_ = true; retry(); }
      return;
    }
    retries_ = 0;
    if (kit_dirty_) return;
  }
  for (uint8_t page = 0; page < 3; ++page) {
    if (!(valid_pages_ & (1u << page))) {
      if (!request(page)) retry();
      return;
    }
  }
  // Then the labels of the USR machines in the kit, one slot per request.
  if (MD.fw_caps & FW_CAP_USR_LABELS) {
    for (uint8_t track = 0; track < 16; ++track) {
      const uint8_t slot = uint8_t(MD.kit.get_model(track) - MD_USR_FIRST);
      if (slot < slot_count_ && !labels(slot)) {
        label_slot_ = slot;
        if (!request(MD_USR_LABEL_PAGE, slot)) retry();
        return;
      }
    }
  }
  // Cache is complete. Further reads are driven by MD notifications.
}

#if !defined(__AVR__)
bool MDUsrCache::readKitTuning(uint8_t kit, uint16_t timeout) {
  if (!(MD.fw_caps & FW_CAP_USR_QUERY)) return true;
  kit_received_ = false;
  if (!request(MD_USR_KIT_PAGE, kit)) return false;
  // Called only as the second part of an already blocking kit read. The
  // ordinary foreground loop owns all background metadata requests.
  while (pending_ != 255) {
    const uint16_t elapsed = uint16_t(read_clock_ms() - requested_at_);
    if (elapsed >= timeout) break;
    MD.waitBlocking(timeout - elapsed);
  }
  pending_ = 255;
  return kit_received_;
}
#endif

void MDUsrCache::receive(const SysexView &view) {
  // Excludes F0/F7: header[5], 72, command, then from byte 7: version, tag,
  // page, count, five little-endian 7-bit generation bytes, then
  // fixed-size records from byte 16.
  const uint16_t len = view.get_recordLen();
  if (len < 16) return;
  uint8_t head[9];
  for (uint8_t n = 0; n < sizeof(head); ++n) head[n] = view.getByte(7 + n);
  const uint8_t tag = head[1], page = head[2], count = head[3];
  const uint8_t *generation = head + 4;
  if (head[0] != MD_USR_VERSION || generation[4] > 15) return;
  if (count != 0 && count != 16 && count != 48) return;
  if (tag == 127) {
    if (page != MD_USR_STATUS_PAGE || len != 16) return;
    // Also catches same-slot replacements and changes to unassigned machines.
    invalidate();
    memcpy(generation_, generation, sizeof(generation_));
    slot_count_ = count;
    pending_ = 255;
    retries_ = 0;
    if (count == 0) valid_pages_ |= 7;
    return;
  }
  if (pending_ > MD_USR_LABEL_PAGE || tag != tag_ || page != pending_) return;
  if (count == 0 && len == 16) {
    // Try a busy peer briefly, then wait for its completion notice.
    slot_count_ = 0;
    invalidate();
    retry();
    return;
  }
  const uint8_t slots = page < 3 && page * 16 < count ? 16 : 0;
  // page is the one requested: the AVR product asks for pages 0-2 and the
  // label page (it reads tuning from kit 65.1, not the kit page).
#if defined(__AVR__)
  const uint8_t bytes = page == MD_USR_LABEL_PAGE ? MD_USR_LABEL_BYTES
      : slots * MD_USR_RECORD_BYTES;
#else
  const uint8_t bytes = page == MD_USR_KIT_PAGE ? 32
      : page == MD_USR_LABEL_PAGE ? MD_USR_LABEL_BYTES
      : slots * MD_USR_RECORD_BYTES;
#endif
  if (len != uint16_t(16 + bytes)) return;
  if (page == MD_USR_LABEL_PAGE) {
    // label() shows only printable characters.
    if (view.getByte(16) != label_slot_) return;
  } else if (page < 3) {
    for (uint8_t record = 16; record < 16 + bytes; record += MD_USR_RECORD_BYTES) {
      const uint8_t flags = view.getByte(record);
      if ((flags & 0x40) || ((flags & MD_USR_TONAL) && !(flags & MD_USR_PITCH)) ||
          (!(flags & MD_USR_PRESENT) && flags != 0)) return;
      for (uint8_t n = 1; n < MD_USR_RECORD_BYTES; ++n) {
        const uint8_t value = view.getByte(record + n);
        if (value != 0 && (value < 32 || value > 126)) return;
      }
    }
  }
#if !defined(__AVR__)
  else if (page == MD_USR_KIT_PAGE) {
    for (uint8_t track = 0; track < 16; ++track) {
      const uint32_t model = mdModelFromMachineFlags(view.getByte(16 + track * 2),
                                                    view.getByte(17 + track * 2));
      if (uint8_t(model) != MD.kit.get_model(track)) return;
    }
  }
#endif
  if (memcmp(generation, generation_, sizeof(generation_)) != 0 ||
      count != slot_count_)
    invalidate();
  memcpy(generation_, generation, sizeof(generation_));
  slot_count_ = count;
#if !defined(__AVR__)
  if (page == MD_USR_KIT_PAGE) {
    for (uint8_t track = 0; track < 16; ++track)
      MD.kit.models[track] = mdModelFromMachineFlags(view.getByte(16 + track * 2),
                                                    view.getByte(17 + track * 2));
    kit_received_ = true;
  } else
#endif
  {
    // Name records go to their page's place; a label page goes to its
    // slot, after the validity byte that takes the place of the slot number.
    uint16_t offset = uint16_t(page) * 16 * MD_USR_RECORD_BYTES;
    uint8_t first = 0;
    if (page == MD_USR_LABEL_PAGE) {
      offset = kUsrLabels + uint16_t(label_slot_) * MD_USR_LABEL_BYTES;
      writeByte(offset, 0x80 | label_epoch_);
      first = 1;
    } else {
      valid_pages_ |= 1u << page;
      if (count == 16) valid_pages_ |= 6;
    }
    for (uint8_t n = first; n < bytes; ++n) writeByte(offset + n, view.getByte(16 + n));
    // New names or labels: the Grid page's slot labels and the current
    // track's page read them again (MCL::loop).
    valid_pages_ |= 0x80;
  }
  pending_ = 255;
  retries_ = 0;
}

uint8_t MDUsrCache::descriptor(uint8_t model) const {
  const uint8_t slot = model - MD_USR_FIRST;
  return slot < MD_USR_SLOTS && slot < slot_count_ && (valid_pages_ & (1u << (slot / 16)))
      ? readByte(uint16_t(slot) * MD_USR_RECORD_BYTES) : 0;
}

const char *MDUsrCache::label(uint8_t model, uint8_t param) {
  const uint8_t slot = model - MD_USR_FIRST;
  if (slot >= MD_USR_SLOTS || param >= 8) return nullptr;
  uint16_t offset = labels(slot);
  if (!offset) return nullptr;
  offset += param * 4;
  for (uint8_t n = 0; n < 3; ++n) {
    const uint8_t c = readByte(offset + n);
    name_[n] = c >= 32 && c < 127 ? c : 0;
  }
  name_[3] = 0;
  return name_[0] ? name_ : nullptr;
}

const char *MDUsrCache::name(uint8_t model) {
  if (!(descriptor(model) & MD_USR_PRESENT)) return nullptr;
  const uint16_t offset = uint16_t(model - MD_USR_FIRST) * MD_USR_RECORD_BYTES + 1;
  for (uint8_t n = 0; n < 5; ++n) name_[n < 3 ? n : n + 1] = readByte(offset + n);
  name_[3] = '-';
  name_[6] = 0;
  return name_;
}

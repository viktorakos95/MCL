#pragma once
#include "unpack.h"
#include "R.h"

#if defined(__AVR__)
// The largest set a page loads together is the menu page's: short machine
// names, knob icons, menu options, menu layouts and one option row for each
// device, 5,624 bytes.
#define RM_BUFSIZE 5624
#elif defined(PLATFORM_TBD)
// Every rp2040 resource set loaded at once, plus one menu option row for each
// device, is 14,344 + NUM_DEVS * sizeof(menu_option_t) bytes (checked in
// ResourceManager.cpp).
#define RM_BUFSIZE 16384
#else
#define RM_BUFSIZE 32768
#endif

// The unpacked size of a set of resources.
template <typename... T> struct rm_set_size {
  static constexpr size_t value = 0;
};
template <typename T, typename... Rest> struct rm_set_size<T, Rest...> {
  static constexpr size_t value = T::__total_size + rm_set_size<Rest...>::value;
};

// Resources are unpacked into the buffer without a bounds check. A page that
// loads several states its set here, with the bytes it takes with Allocate(),
// and the build stops if the set outgrows the buffer.
#define RM_REQUIRE(extra, ...)                                                 \
  static_assert(rm_set_size<__VA_ARGS__>::value + (extra) <= RM_BUFSIZE,      \
                "resource set does not fit RM_BUFSIZE")

class ResourceManager {
private:
	uint8_t m_buffer[RM_BUFSIZE];
	uint16_t m_bufsize;
	uint16_t m_clears;
	uint16_t m_persistent_size;
#if !defined(__AVR__)
	__T_machine_param_names *m_persistent_machine_param_names = nullptr;
	__T_machine_names_short *m_persistent_machine_names_short = nullptr;
#endif
#if defined(PLATFORM_TBD)
	__T_icons_knob *m_persistent_icons_knob = nullptr;
#endif
	byte* __use_resource(const void* pgm);

public:
	ResourceManager() = default;
	void Clear();
	void SetPersistent();
	void Save(uint8_t* buf, size_t* sz);
	void Restore(uint8_t* buf, size_t sz);
	// allocate memory in a stack-like manner
	byte* Allocate(size_t sz);
	// free memory in a stack-like manner
	void Free(size_t sz);
	void restore_menu_layout_deps();
	void restore_page_entry_deps();
	size_t Size();
	// Counts Clear() calls. A page that loads an extra resource once keeps
	// the count it loaded it at: once the count has moved on, another page
	// cleared the buffer and the resource is gone, even if its address is
	// still inside what that page loaded since.
	uint16_t Clears() const { return m_clears; }

#include "ResMan.h"
};

extern ResourceManager R;

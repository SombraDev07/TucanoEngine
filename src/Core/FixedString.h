#pragma once

// A string that lives inline, with a compile-time capacity.
//
// Exists because ECS components must be trivially copyable — `EntityManager` moves them between
// archetypes with `memcpy` and swap-removes them the same way, so a `std::string` member would be
// copied bit for bit and then double-freed. That `static_assert` is a deliberate part of the
// archetype-SoA design, not an accident, so the string bends rather than the ECS.
//
// This is the same trade Unity DOTS makes with `FixedString`, and for the same reason. The cost is
// a hard maximum length; the alternative — teaching the ECS to move and destroy non-trivial
// components — is real work on a tested hot path and is recorded as an open decision rather than
// done in passing.
//
// Truncation is silent at the API level and *reported* at the serialization level, because the one
// place a too-long name actually hurts is a file that then loads back different.

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

namespace tucano {

template <size_t Capacity>
struct FixedString {
	static_assert(Capacity >= 8, "a fixed string this short is a bug, not a budget");

	// Always null-terminated, so c_str() is safe even when the buffer was filled to the brim.
	char data[Capacity] = {};

	FixedString() = default;
	FixedString(std::string_view text) { assign(text); }

	// Returns false when the text did not fit, so a caller that cares can say so.
	bool assign(std::string_view text) {
		const size_t n = std::min(text.size(), Capacity - 1);
		std::memcpy(data, text.data(), n);
		data[n] = '\0';
		return n == text.size();
	}

	FixedString& operator=(std::string_view text) {
		assign(text);
		return *this;
	}

	const char* c_str() const { return data; }
	std::string_view view() const { return std::string_view(data); }
	std::string str() const { return std::string(data); }
	bool empty() const { return data[0] == '\0'; }
	size_t size() const { return std::strlen(data); }
	static constexpr size_t capacity() { return Capacity; }

	bool operator==(const FixedString& other) const { return std::strcmp(data, other.data) == 0; }
	bool operator!=(const FixedString& other) const { return !(*this == other); }
	bool operator==(std::string_view other) const { return view() == other; }
};

// The two sizes actually used. Names are read in a list, so long ones are unreadable anyway; paths
// have to survive a deep folder tree plus a file name.
using NameString = FixedString<64>;
using PathString = FixedString<192>;

} // namespace tucano

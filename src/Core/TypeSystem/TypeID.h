#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

// TypeID — a type's identity as a number.
//
// Derived from Esoterica (MIT) — Code/Base/TypeSystem/TypeID.h
//
// Comparing types by string is too slow for something a property grid does per row per frame, and
// a pointer to TypeInfo cannot be written to a file. The hash is both: cheap to compare and stable
// enough to serialise, so a saved scene can name the types it holds.
//
// FNV-1a rather than std::hash: the standard makes no promise that std::hash gives the same value
// across runs or builds, and an id that changes between sessions cannot appear in a file.

namespace tucano {

class TypeID {
public:
	constexpr TypeID() = default;
	explicit constexpr TypeID(std::string_view name) : m_value(hash(name)) {}
	// For ids read back from a file.
	static constexpr TypeID fromValue(uint32_t value) {
		TypeID id;
		id.m_value = value;
		return id;
	}

	constexpr uint32_t value() const { return m_value; }
	constexpr bool isValid() const { return m_value != 0; }

	constexpr bool operator==(const TypeID& other) const { return m_value == other.m_value; }
	constexpr bool operator!=(const TypeID& other) const { return m_value != other.m_value; }
	constexpr bool operator<(const TypeID& other) const { return m_value < other.m_value; }

	static constexpr uint32_t hash(std::string_view s) {
		uint32_t h = 2166136261u;
		for (char c : s) {
			h ^= static_cast<uint32_t>(static_cast<unsigned char>(c));
			h *= 16777619u;
		}
		// Zero is reserved for "no type", so a string that happens to hash to it is nudged rather
		// than silently reading as invalid.
		return h != 0 ? h : 1u;
	}

private:
	uint32_t m_value = 0;
};

} // namespace tucano

template <>
struct std::hash<tucano::TypeID> {
	size_t operator()(const tucano::TypeID& id) const noexcept { return id.value(); }
};

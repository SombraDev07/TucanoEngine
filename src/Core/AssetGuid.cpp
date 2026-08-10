#include "Core/AssetGuid.h"

#include <cctype>
#include <cstdio>
#include <string_view>

namespace tucano::asset {

std::string AssetGuid::toString() const {
	char buffer[33];
	std::snprintf(buffer, sizeof(buffer), "%016llx%016llx", static_cast<unsigned long long>(hi),
	              static_cast<unsigned long long>(lo));
	return std::string(buffer);
}

AssetGuid AssetGuid::fromString(std::string_view text) {
	if (text.size() != 32) return {};

	AssetGuid guid;
	uint64_t* halves[2] = {&guid.hi, &guid.lo};
	for (int half = 0; half < 2; ++half) {
		uint64_t value = 0;
		for (size_t i = 0; i < 16; ++i) {
			const char c = text[static_cast<size_t>(half) * 16 + i];
			uint64_t digit = 0;
			if (c >= '0' && c <= '9') {
				digit = static_cast<uint64_t>(c - '0');
			} else if (c >= 'a' && c <= 'f') {
				digit = static_cast<uint64_t>(c - 'a') + 10;
			} else if (c >= 'A' && c <= 'F') {
				digit = static_cast<uint64_t>(c - 'A') + 10;
			} else {
				// A malformed id yields nothing rather than a partial value: half a parsed GUID
				// would point at some other asset, which is worse than pointing at none.
				return {};
			}
			value = (value << 4) | digit;
		}
		*halves[half] = value;
	}
	return guid;
}

AssetGuid AssetGuid::forSubAsset(const AssetGuid& parent, std::string_view localName) {
	if (!parent.valid()) return {};

	// FNV-1a over the parent's bytes followed by the name. Two hashes seeded differently so hi and
	// lo do not move together — a sub-asset id has to be as distinct as a random one, it just also
	// has to be reproducible.
	const auto hash = [](uint64_t seed, const AssetGuid& p, std::string_view name) {
		uint64_t h = seed;
		const auto mix = [&h](uint64_t value) {
			for (int byte = 0; byte < 8; ++byte) {
				h ^= (value >> (byte * 8)) & 0xFF;
				h *= 0x100000001b3ULL;
			}
		};
		mix(p.hi);
		mix(p.lo);
		for (const char c : name) {
			h ^= static_cast<unsigned char>(c);
			h *= 0x100000001b3ULL;
		}
		return h;
	};

	AssetGuid guid;
	guid.hi = hash(0xcbf29ce484222325ULL, parent, localName);
	guid.lo = hash(0x9e3779b97f4a7c15ULL, parent, localName);
	if (!guid.valid()) guid.lo = 1;
	return guid;
}

} // namespace tucano::asset

#pragma once

// AssetGuid — the identity of an asset, independent of where it lives.
//
// Lives in Core rather than in AssetPipeline because *referring* to an asset is not the same as
// knowing the `.tuasset` file format. An ECS component holds one of these; it has no business
// including a chunked binary format header to do it.
//
// Two uint64s and nothing else, so it stays trivially copyable — `EntityManager` moves components
// between archetypes with memcpy and enforces that with a static_assert.
//
// Where the value comes from depends on what is being identified:
//   - a **source** asset (.gltf, .png) gets a random GUID stored in its `.tumeta` sidecar, so the
//     identity survives the file being renamed (see AssetRegistry, CP-29);
//   - a **cooked** `.tuasset` carries its GUID in its own header.
// `fromPath` still exists for the cooker, but it is a *path hash* — it changes when the file moves,
// which is why it is not what the editor stores.

#include <cstdint>
#include <string>

namespace tucano::asset {

struct AssetGuid {
	uint64_t hi = 0;
	uint64_t lo = 0;

	bool valid() const { return hi != 0 || lo != 0; }
	bool operator==(const AssetGuid& o) const { return hi == o.hi && lo == o.lo; }
	bool operator!=(const AssetGuid& o) const { return !(*this == o); }
	bool operator<(const AssetGuid& o) const { return hi < o.hi || (hi == o.hi && lo < o.lo); }

	// 32 lowercase hex digits, hi first. Chosen over two numbers because a GUID ends up in files
	// and in tooltips, and one token is easier to copy, grep and diff than a pair.
	std::string toString() const;

	// Parses what toString wrote. Anything else yields an invalid GUID rather than a partial one:
	// half a parsed id would silently point at the wrong asset.
	static AssetGuid fromString(std::string_view text);

	// Hash of a path. Kept for callers that have nothing better; **not** what the editor stores,
	// because it changes when the file is renamed.
	static AssetGuid fromPath(const std::string& path);

	// Identity of something produced *from* another asset — one mesh primitive out of a .gltf that
	// contains twenty of them.
	//
	// Derived from the parent's GUID plus a local name, never from a path. That gives the two
	// properties a sub-asset needs and a path hash cannot have at the same time: it is **stable**
	// when the source file is renamed (the parent GUID lives in the sidecar and moves with it), and
	// it is **distinct** per primitive. Unity solves this with (guid, localId); this is the same
	// idea folded into one value.
	static AssetGuid forSubAsset(const AssetGuid& parent, std::string_view localName);
};

} // namespace tucano::asset

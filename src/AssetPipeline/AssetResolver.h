#pragma once

// AssetResolver — turns an AssetGuid into a loaded runtime object.
//
// The missing half of the identity work. B-01 to B-05 made the editor store *what* an entity refers
// to; this is what makes the reference produce something the renderer can draw. Without it a
// material assignment is a value in a file and nothing on screen.
//
// One place owns this because every caller needs the same three things and gets them wrong
// separately otherwise:
//
//   1. **GUID → path** goes through the registry, so a renamed file still resolves.
//   2. **Caching**, so ten materials sharing one texture load it once. A texture is the most
//      duplicated asset in any project and the most expensive to upload.
//   3. **Releasing before the device dies.** The cache holds `shared_ptr<Texture>`, which holds GPU
//      memory; freed after the device it came from, that is a use-after-free. The resolver registers
//      `rhi::Device::onBeforeDestroy` on first use rather than asking callers to remember — the
//      lesson from CP-20b, where exactly this bug came back after its fix was deleted.
//
// The cache is a `shared_ptr` and the release callback holds a copy of it, **not** a pointer back to
// the resolver. A resolver is usually a local; registering `[this]` on the device means the device
// calls into freed memory when it is destroyed later, which measured 2 crashes in 20 runs before
// this was fixed. The callback owning the thing it clears removes the question of who dies first.

#include "Core/AssetGuid.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace tucano {

// `struct` and `class` are not interchangeable in a forward declaration on MSVC: the mangled name
// differs, and the mismatch only shows up as an unresolved symbol at link time.
struct Material;
class Texture;
struct MaterialAsset;

namespace rhi {
class Device;
}

namespace asset {
class AssetRegistry;
}

class AssetResolver {
public:
	AssetResolver(rhi::Device& device, const asset::AssetRegistry& registry, std::string root);

	// Null when the GUID is unknown, the file is unreadable, or the GUID is invalid. A missing
	// texture is reported by returning null, not by substituting a placeholder: the caller decides
	// whether a default is better than nothing, and for a normal map it is not.
	std::shared_ptr<Texture> texture(const asset::AssetGuid& guid, bool srgb);

	// Builds the runtime material an asset describes, resolving every texture slot. Factors are
	// copied straight across; only the references need work.
	std::shared_ptr<Material> materialFromAsset(const MaterialAsset& asset);

	// Reads a `.tumat` by identity and builds it. Null when the GUID names no material.
	std::shared_ptr<Material> material(const asset::AssetGuid& guid);

	// Drops every cached object. Called automatically when the device is destroyed; exposed so a
	// test can prove the cache is a cache.
	void clearCache();

	size_t cachedTextureCount() const { return m_textures->size(); }

private:
	// Absolute path for a GUID, or empty when the index does not know it.
	std::string pathFor(const asset::AssetGuid& guid) const;

	rhi::Device& m_device;
	const asset::AssetRegistry& m_registry;
	std::string m_root;

	// Keyed by (guid, srgb): the same file is a different GPU texture depending on whether it is
	// colour or data, and loading albedo as linear is a bug that looks like bad art.
	struct TextureKey {
		asset::AssetGuid guid;
		bool srgb = false;
		bool operator==(const TextureKey& o) const { return guid == o.guid && srgb == o.srgb; }
	};
	struct TextureKeyHash {
		size_t operator()(const TextureKey& k) const {
			return static_cast<size_t>(k.guid.hi ^ (k.guid.lo * 31) ^ (k.srgb ? 0x9e3779b9u : 0u));
		}
	};

	using TextureCache = std::unordered_map<TextureKey, std::shared_ptr<Texture>, TextureKeyHash>;
	std::shared_ptr<TextureCache> m_textures = std::make_shared<TextureCache>();
	bool m_releaseRegistered = false;
};

} // namespace tucano

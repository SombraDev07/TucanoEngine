#include "AssetPipeline/AssetResolver.h"

#include "AssetPipeline/AssetRegistry.h"
#include "Core/TypeSystem/Serialization.h"
#include "Core/TypeSystem/TypeRegistry.h"
#include "RHI/RHI.h"
#include "Renderer/Material.h"
#include "Renderer/MaterialAsset.h"
#include "Renderer/Texture.h"

namespace tucano {

AssetResolver::AssetResolver(rhi::Device& device, const asset::AssetRegistry& registry,
                             std::string root)
    : m_device(device), m_registry(registry), m_root(std::move(root)) {}

std::string AssetResolver::pathFor(const asset::AssetGuid& guid) const {
	if (!guid.valid()) return {};
	const asset::RegistryEntry* entry = m_registry.find(guid);
	if (entry == nullptr) return {};
	return m_root.empty() ? entry->relativePath : m_root + "/" + entry->relativePath;
}

std::shared_ptr<Texture> AssetResolver::texture(const asset::AssetGuid& guid, bool srgb) {
	if (!guid.valid()) return nullptr;

	const TextureKey key{guid, srgb};
	if (const auto found = m_textures->find(key); found != m_textures->end()) return found->second;

	const std::string path = pathFor(guid);
	if (path.empty()) return nullptr;

	// `Texture::loadFromFile` throws on a corrupt or unreadable file. A missing texture must cost
	// that texture, not the editor — the caller already handles null, and an exception escaping here
	// would take down whatever was drawing the frame.
	std::shared_ptr<Texture> loaded;
	try {
		loaded = Texture::loadFromFile(m_device, path, srgb);
	} catch (const std::exception&) {
		loaded = nullptr;
	}
	if (loaded == nullptr) return nullptr;

	if (!m_releaseRegistered) {
		// The cache outlives nothing: it is emptied while the device is still usable, because a
		// shared_ptr<Texture> freed afterwards frees GPU memory through a device that is gone.
		// Registered here, on first success, rather than in the constructor — a resolver that never
		// loaded anything has nothing to release.
		m_releaseRegistered = true;
		// Captures the cache, not `this`: the resolver may well be gone by the time the device is
		// destroyed, and a callback reaching back into it would be reading freed memory.
		m_device.onBeforeDestroy([cache = m_textures] { cache->clear(); });
	}

	m_textures->emplace(key, loaded);
	return loaded;
}

std::shared_ptr<Material> AssetResolver::materialFromAsset(const MaterialAsset& asset) {
	auto material = std::make_shared<Material>();
	material->name = asset.name;
	material->baseColorFactor = asset.baseColorFactor;
	material->metallicFactor = asset.metallicFactor;
	material->roughnessFactor = asset.roughnessFactor;
	material->aoFactor = asset.aoFactor;
	material->reflectance = asset.reflectance;
	material->clearcoat = asset.clearcoat;
	material->clearcoatRoughness = asset.clearcoatRoughness;
	material->fuzz = asset.fuzz;
	material->fuzzColor = asset.fuzzColor;
	material->emissiveFactor = asset.emissiveFactor;
	material->detailScale = asset.detailScale;
	material->alphaMask = asset.alphaMask;
	material->alphaCutoff = asset.alphaCutoff;

	// sRGB for what the eye reads as colour, linear for what the shader reads as data. Getting this
	// backwards produces art that looks subtly wrong everywhere and is hard to trace to one cause.
	material->albedo = texture(asset.albedo, /*srgb=*/true);
	material->emissive = texture(asset.emissive, /*srgb=*/true);
	material->normal = texture(asset.normal, /*srgb=*/false);
	material->metallicRoughness = texture(asset.metallicRoughness, /*srgb=*/false);
	material->ao = texture(asset.ao, /*srgb=*/false);
	return material;
}

std::shared_ptr<Material> AssetResolver::material(const asset::AssetGuid& guid) {
	const std::string path = pathFor(guid);
	if (path.empty()) return nullptr;

	const TypeInfo* type = TypeRegistry::instance().find(TypeID{"MaterialAsset"});
	if (type == nullptr) return nullptr;

	MaterialAsset asset;
	if (!loadFromFile(path, *type, &asset, nullptr)) return nullptr;
	return materialFromAsset(asset);
}

void AssetResolver::clearCache() { m_textures->clear(); }

} // namespace tucano

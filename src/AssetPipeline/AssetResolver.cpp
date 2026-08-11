#include "AssetPipeline/AssetResolver.h"

#include "AssetPipeline/AssetImport.h"
#include "AssetPipeline/AssetRegistry.h"
#include "Core/TypeSystem/Serialization.h"
#include "Core/TypeSystem/TypeRegistry.h"
#include "RHI/RHI.h"
#include "Renderer/Material.h"
#include "Renderer/MaterialAsset.h"
#include "Renderer/Texture.h"

#include <filesystem>

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

	registerRelease();
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

std::shared_ptr<Mesh> AssetResolver::mesh(const asset::AssetGuid& guid, std::string* error,
                                          std::shared_ptr<Material>* outMaterial) {
	const auto fail = [&](std::string reason) -> std::shared_ptr<Mesh> {
		if (error != nullptr) *error = std::move(reason);
		return nullptr;
	};

	if (!guid.valid()) return fail("no mesh assigned");

	if (const auto found = m_meshes->find(guid); found != m_meshes->end()) {
		// The cached mesh carries no material with it: the material is a separate reference on the
		// component, and re-reading the file just to recover the cooked one would defeat the cache.
		return found->second;
	}

	const std::string path = pathFor(guid);
	if (path.empty()) return fail("the project index does not know this mesh");

	// Only cooked assets. `loadTuassetMesh` reads the .tuasset chunk layout and nothing else, and a
	// source model is not one mesh — a glTF is a scene of primitives, each of which becomes its own
	// asset at import. Saying so beats returning null with no reason.
	const std::string extension = std::filesystem::path(path).extension().string();
	if (extension != ".tuasset") {
		return fail("not a cooked mesh (" + extension + ") — import it first");
	}

	// `loadTuassetMesh` ends in `Mesh::create`, which **throws** on a submesh range it cannot honour
	// (Mesh.cpp:28) — it does not return null. Same shape as `Texture::loadFromFile` in CP-34: a
	// corrupt asset must cost that asset, not the frame that happened to be drawing.
	std::string loadError;
	std::shared_ptr<Mesh> loaded;
	try {
		loaded = loadTuassetMesh(m_device, path, &loadError, outMaterial);
	} catch (const std::exception& ex) {
		return fail(std::string("mesh could not be built: ") + ex.what());
	}
	if (loaded == nullptr) return fail(loadError.empty() ? ("could not read " + path) : loadError);

	registerRelease();
	m_meshes->emplace(guid, loaded);
	return loaded;
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

void AssetResolver::registerRelease() {
	if (m_releaseRegistered) return;
	m_releaseRegistered = true;
	// The caches are emptied while the device is still usable: a shared_ptr<Texture> or
	// shared_ptr<Mesh> freed afterwards frees GPU memory through a device that is gone.
	// Captures the caches, not `this` — the resolver is usually a local, and by the time the device
	// is destroyed a callback reaching back into it would be reading freed memory. That exact bug
	// cost 2/20 crashes in CP-34 before the capture was changed.
	m_device.onBeforeDestroy([textures = m_textures, meshes = m_meshes] {
		textures->clear();
		meshes->clear();
	});
}

void AssetResolver::clearCache() {
	m_textures->clear();
	m_meshes->clear();
}

} // namespace tucano

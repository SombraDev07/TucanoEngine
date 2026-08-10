#include "AssetPipeline/AssetRegistry.h"

#include "Core/Json.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace tucano::asset {

void AssetRegistry::scanDirectory(const std::string& rootDir) {
	m_entries.clear();
	m_byGuid.clear();
	m_byPath.clear();

	std::error_code ec;
	for (auto& entry : std::filesystem::recursive_directory_iterator(rootDir, ec)) {
		if (!entry.is_regular_file(ec)) continue;
		auto ext = entry.path().extension().string();
		if (ext != ".tuasset") continue;

		addOrUpdate(rootDir, entry.path().string());
	}
}

void AssetRegistry::addOrUpdate(const std::string& rootDir, const std::string& filePath) {
	TucanoAssetHeader hdr;
	if (!TucanoAssetReader::readHeader(filePath, hdr)) return;

	std::string relPath = filePath;
	if (relPath.find(rootDir) == 0)
		relPath = relPath.substr(rootDir.size() + 1);

	RegistryEntry entry;
	entry.guid = hdr.guid;
	entry.type = hdr.type;
	entry.relativePath = relPath;
	entry.name = std::filesystem::path(filePath).stem().string();

	auto guidIt = m_byGuid.find(hdr.guid);
	if (guidIt != m_byGuid.end()) {
		m_entries[guidIt->second] = entry;
		return;
	}

	size_t idx = m_entries.size();
	m_entries.push_back(entry);
	m_byGuid[hdr.guid] = idx;
	m_byPath[relPath] = idx;
}

void AssetRegistry::save(const std::string& regPath) const {
	std::ofstream file(regPath, std::ios::binary);
	if (!file.is_open()) return;

	uint32_t count = uint32_t(m_entries.size());
	file.write(reinterpret_cast<const char*>(&count), sizeof(count));

	for (const auto& e : m_entries) {
		file.write(reinterpret_cast<const char*>(&e.guid), sizeof(e.guid));
		file.write(reinterpret_cast<const char*>(&e.type), sizeof(e.type));
		uint32_t nameLen = uint32_t(e.name.size());
		uint32_t pathLen = uint32_t(e.relativePath.size());
		file.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
		file.write(reinterpret_cast<const char*>(&pathLen), sizeof(pathLen));
		file.write(e.name.data(), e.name.size());
		file.write(e.relativePath.data(), e.relativePath.size());
	}
}

bool AssetRegistry::load(const std::string& regPath) {
	std::ifstream file(regPath, std::ios::binary);
	if (!file.is_open()) return false;

	m_entries.clear();
	m_byGuid.clear();
	m_byPath.clear();

	uint32_t count = 0;
	file.read(reinterpret_cast<char*>(&count), sizeof(count));
	m_entries.reserve(count);

	for (uint32_t i = 0; i < count; ++i) {
		RegistryEntry e;
		file.read(reinterpret_cast<char*>(&e.guid), sizeof(e.guid));
		file.read(reinterpret_cast<char*>(&e.type), sizeof(e.type));
		uint32_t nameLen = 0, pathLen = 0;
		file.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
		file.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));
		e.name.resize(nameLen);
		e.relativePath.resize(pathLen);
		file.read(e.name.data(), nameLen);
		file.read(e.relativePath.data(), pathLen);

		size_t idx = m_entries.size();
		m_entries.push_back(std::move(e));
		m_byGuid[m_entries.back().guid] = idx;
		m_byPath[m_entries.back().relativePath] = idx;
	}
	return true;
}

const RegistryEntry* AssetRegistry::find(const AssetGuid& guid) const {
	auto it = m_byGuid.find(guid);
	return it != m_byGuid.end() ? &m_entries[it->second] : nullptr;
}

const RegistryEntry* AssetRegistry::findByPath(const std::string& path) const {
	auto it = m_byPath.find(path);
	return it != m_byPath.end() ? &m_entries[it->second] : nullptr;
}


// ── Project scan (B-01) ─────────────────────────────────────────────────────

namespace {

std::string lowercased(std::string text) {
	std::transform(text.begin(), text.end(), text.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

const char* typeName(AssetType type) {
	switch (type) {
		case AssetType::Mesh: return "mesh";
		case AssetType::Texture: return "texture";
		case AssetType::Material: return "material";
		case AssetType::Scene: return "scene";
		case AssetType::Animation: return "animation";
		case AssetType::Skeleton: return "skeleton";
		case AssetType::Audio: return "audio";
		case AssetType::Shader: return "shader";
		case AssetType::Font: return "font";
		default: return "unknown";
	}
}

} // namespace

AssetType AssetRegistry::typeFromExtension(const std::string& extension) {
	const std::string ext = lowercased(extension);
	if (ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".obj") return AssetType::Mesh;
	if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".dds" ||
	    ext == ".bmp" || ext == ".hdr" || ext == ".exr") {
		return AssetType::Texture;
	}
	if (ext == ".tumat") return AssetType::Material;
	if (ext == ".tuscene") return AssetType::Scene;
	if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac") return AssetType::Audio;
	if (ext == ".hlsl" || ext == ".cso") return AssetType::Shader;
	if (ext == ".ttf" || ext == ".otf") return AssetType::Font;
	return AssetType::Unknown;
}

std::string AssetRegistry::metaPathFor(const std::string& assetPath) { return assetPath + ".tumeta"; }

AssetGuid AssetRegistry::generateGuid() {
	// random_device rather than the path: the whole point is a value the path cannot reproduce, so
	// that renaming the file keeps the identity.
	static std::mt19937_64 engine{std::random_device{}()};
	AssetGuid guid;
	guid.hi = engine();
	guid.lo = engine();
	// A GUID of all zeros reads as "invalid" everywhere, so it must never be handed out.
	if (!guid.valid()) guid.lo = 1;
	return guid;
}

bool AssetRegistry::writeMeta(const std::string& metaPath, const AssetGuid& guid, AssetType type) {
	// Written through a temporary and renamed: a sidecar truncated by a crash would orphan every
	// reference to that asset, which is worse than not having written it.
	const std::string temporary = metaPath + ".tmp";
	{
		std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
		if (!file) return false;
		file << "{\n";
		file << "  \"format\": \"tumeta\",\n";
		file << "  \"version\": 1,\n";
		file << "  \"guid\": \"" << guid.toString() << "\",\n";
		file << "  \"type\": \"" << typeName(type) << "\"\n";
		file << "}\n";
		if (!file) return false;
	}
	std::error_code ec;
	std::filesystem::rename(temporary, metaPath, ec);
	if (ec) {
		std::filesystem::remove(temporary, ec);
		return false;
	}
	return true;
}

AssetGuid AssetRegistry::readMetaGuid(const std::string& metaPath) {
	std::ifstream file(metaPath, std::ios::binary);
	if (!file) return {};
	const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

	core::JsonValue root;
	if (!core::JsonValue::parse(text, root, nullptr) || !root.isObject()) return {};
	const core::JsonValue* guidNode = root.find("guid");
	if (guidNode == nullptr || !guidNode->isString()) return {};

	const std::string& hex = guidNode->asString();
	if (hex.size() != 32) return {};
	AssetGuid guid;
	try {
		guid.hi = std::stoull(hex.substr(0, 16), nullptr, 16);
		guid.lo = std::stoull(hex.substr(16, 16), nullptr, 16);
	} catch (...) {
		// A malformed sidecar is treated as absent: the asset gets a fresh identity rather than
		// the scan aborting over one bad file.
		return {};
	}
	return guid;
}

void AssetRegistry::insert(RegistryEntry entry) {
	auto guidIt = m_byGuid.find(entry.guid);
	if (guidIt != m_byGuid.end()) {
		m_entries[guidIt->second] = std::move(entry);
		return;
	}
	m_byPath[entry.relativePath] = m_entries.size();
	m_byGuid[entry.guid] = m_entries.size();
	m_entries.push_back(std::move(entry));
}

bool AssetRegistry::addSource(const std::string& rootDir, const std::string& filePath,
                              bool createMissingMeta) {
	const std::filesystem::path path(filePath);
	const AssetType type = typeFromExtension(path.extension().string());
	if (type == AssetType::Unknown) return false;

	const std::string metaPath = metaPathFor(filePath);
	AssetGuid guid = readMetaGuid(metaPath);
	if (!guid.valid()) {
		if (!createMissingMeta) {
			m_missingMeta.push_back(filePath);
			return false;
		}
		guid = generateGuid();
		if (!writeMeta(metaPath, guid, type)) {
			m_missingMeta.push_back(filePath);
			return false;
		}
	}

	std::error_code ec;
	RegistryEntry entry;
	entry.guid = guid;
	entry.type = type;
	entry.relativePath = std::filesystem::relative(path, rootDir, ec).generic_string();
	entry.name = path.stem().string();
	insert(std::move(entry));
	return true;
}

size_t AssetRegistry::scanProject(const std::string& rootDir, const ScanOptions& options) {
	clear();

	std::error_code ec;
	if (!std::filesystem::is_directory(rootDir, ec)) return 0;

	// skip_permission_denied so one unreadable folder costs that folder, not the whole project.
	const auto walkOptions = std::filesystem::directory_options::skip_permission_denied;
	for (std::filesystem::recursive_directory_iterator it(rootDir, walkOptions, ec), end; it != end;
	     it.increment(ec)) {
		if (ec) {
			ec.clear();
			continue;
		}
		if (!it->is_regular_file(ec)) continue;

		const std::string extension = lowercased(it->path().extension().string());
		// Sidecars describe assets, they are not assets.
		if (extension == ".tumeta" || extension == ".tmp") continue;

		if (extension == ".tuasset") {
			if (options.includeCooked) addOrUpdate(rootDir, it->path().string());
			continue;
		}
		if (options.includeSources) addSource(rootDir, it->path().string(), options.createMissingMeta);
	}
	return m_entries.size();
}

std::vector<const RegistryEntry*> AssetRegistry::byType(AssetType type) const {
	std::vector<const RegistryEntry*> out;
	for (const RegistryEntry& entry : m_entries) {
		if (entry.type == type) out.push_back(&entry);
	}
	return out;
}

void AssetRegistry::clear() {
	m_entries.clear();
	m_byGuid.clear();
	m_byPath.clear();
	m_missingMeta.clear();
}

} // namespace tucano::asset

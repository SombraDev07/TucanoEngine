#include "AssetPipeline/AssetRegistry.h"

#include <filesystem>
#include <fstream>
#include <cstring>

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

} // namespace tucano::asset

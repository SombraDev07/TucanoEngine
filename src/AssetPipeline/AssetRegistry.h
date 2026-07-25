#pragma once

// AssetRegistry — GUID-to-path mapping for the Tucano asset system.
//
// On startup, scans the project's Asset directory for .tuasset files,
// reads their headers (64B each) and builds an in-memory registry
// mapping GUID → file path. This allows referencing assets by GUID
// instead of hardcoded paths — renaming a file doesn't break references.
//
// The registry is persisted as .turegistry (binary) for fast cold load.

#include "AssetPipeline/TucanoAsset.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace tucano::asset {

struct RegistryEntry {
	AssetGuid guid;
	std::string relativePath;   // relative to project root
	AssetType type = AssetType::Unknown;
	std::string name;           // display name
};

class AssetRegistry {
public:
	void scanDirectory(const std::string& rootDir);
	void save(const std::string& regPath) const;
	bool load(const std::string& regPath);

	const RegistryEntry* find(const AssetGuid& guid) const;
	const RegistryEntry* findByPath(const std::string& path) const;

	const std::vector<RegistryEntry>& all() const { return m_entries; }
	size_t size() const { return m_entries.size(); }

	// Re-scan specific file
	void addOrUpdate(const std::string& rootDir, const std::string& filePath);

private:
	std::vector<RegistryEntry> m_entries;
	std::unordered_map<AssetGuid, size_t> m_byGuid;
	std::unordered_map<std::string, size_t> m_byPath;
};

} // namespace tucano::asset

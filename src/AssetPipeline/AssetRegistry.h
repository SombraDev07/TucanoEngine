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

	// ── Project scan (B-01) ─────────────────────────────────────────────────
	//
	// `scanDirectory` above only sees `.tuasset` — cooked output, which carries its GUID in its
	// own header. But what a person picks in the editor is the **source**: a .gltf, a .png, an
	// .hdr. Those have nowhere to keep a GUID, and `AssetGuid::fromPath` is a hash of the path,
	// so it changes the moment the file is renamed — exactly what a GUID exists to survive.
	//
	// So a source asset gets a `.tumeta` sidecar holding a GUID generated once. Rename the asset
	// and the sidecar moves with it, and every reference still resolves. This is the same trade
	// Unity and Godot make, for the same reason.
	//
	// Writing sidecars touches the user's asset folder, so it is a decision, not a side effect:
	// `createMissingMeta` is off unless the caller says otherwise.
	struct ScanOptions {
		bool includeSources = true;   // .gltf, .png, .hdr, ...
		bool includeCooked = true;    // .tuasset
		bool createMissingMeta = false;
	};

	// Returns how many entries the registry holds afterwards.
	size_t scanProject(const std::string& rootDir, const ScanOptions& options = {});

	// Entries of one kind, for a picker that only wants meshes.
	std::vector<const RegistryEntry*> byType(AssetType type) const;

	// Extension to type. Unknown for anything the editor has no use for, which is how a scan
	// ignores .txt files sitting next to the art.
	static AssetType typeFromExtension(const std::string& extension);

	// Path of the sidecar for a source asset: "Tree.gltf" -> "Tree.gltf.tumeta". A suffix rather
	// than a replaced extension, so "Tree.gltf" and "Tree.png" cannot collide on one meta file.
	static std::string metaPathFor(const std::string& assetPath);

	// Reads the sidecar's GUID; returns an invalid GUID when there is none or it is unreadable.
	static AssetGuid readMetaGuid(const std::string& metaPath);

	// Writes a sidecar. Fails only on IO.
	static bool writeMeta(const std::string& metaPath, const AssetGuid& guid, AssetType type);

	// A GUID no path can produce. Used when a source asset gets its sidecar for the first time.
	static AssetGuid generateGuid();

	// Everything found in the last scan that had no sidecar and was not given one, so a caller
	// running read-only can report what it skipped instead of silently indexing nothing.
	const std::vector<std::string>& missingMeta() const { return m_missingMeta; }

	void clear();

private:
	// Adds one source file, creating or reading its sidecar. Returns false when it was skipped.
	bool addSource(const std::string& rootDir, const std::string& filePath, bool createMissingMeta);
	void insert(RegistryEntry entry);

	std::vector<RegistryEntry> m_entries;
	std::vector<std::string> m_missingMeta;
	std::unordered_map<AssetGuid, size_t> m_byGuid;
	std::unordered_map<std::string, size_t> m_byPath;
};

} // namespace tucano::asset

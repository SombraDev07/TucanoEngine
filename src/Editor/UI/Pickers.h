#pragma once

// Pickers — widgets for choosing a thing that lives somewhere else: a file on disk, or a type in
// the registry.
//
// Derived from Esoterica (MIT) — Code/EngineTools/Widgets/Pickers/{DataPathPicker,TypeInfoPicker}.h
//
// P4-04 built these against a directory scan, because there was no index. B-06 gives them the
// `AssetRegistry`: with one bound, the list is the *project's* assets — typed, and each carrying a
// stable GUID, so a pick survives the file being renamed. Without one it falls back to the
// extension scan, which is what a host with no project still gets.
//
// The fallback is scaffolding with a cost worth naming: a path picked that way breaks the moment
// the file moves. That is exactly the failure the registry exists to remove, so the fallback is
// the degraded mode, not an equal option.
//
// Both pickers scan lazily and cache. A recursive directory walk or a registry sweep per frame
// would be paid on every frame the panel is open, for data that changes on the order of never.

#include "AssetPipeline/AssetRegistry.h"
#include "Core/TypeSystem/TypeID.h"
#include "Editor/UI/Widgets.h"

#include <string>
#include <vector>

namespace tucano {
struct TypeInfo;
}

namespace tucano::editor::ui {

// ── AssetPicker ─────────────────────────────────────────────────────────────
//
// Chooses a file under a root directory, filtered by what the field is for. The value is a path
// relative to that root, which is what the engine stores and what survives moving the project.

class AssetPicker {
public:
	// What the field expects. Kind is deliberately about *purpose*, not about extension: a field
	// wanting a texture does not care whether the artist saved .png or .dds.
	enum class Kind { Any, Mesh, Texture, Hdri, Scene, Text, Material };

	// Root the paths are relative to. Changing it invalidates the scan.
	void setRoot(std::string root);
	const std::string& root() const { return m_root; }

	// Bind the project index. With one set, `candidates()` comes from it and the directory scan is
	// not run at all. Null goes back to scanning.
	void setRegistry(const asset::AssetRegistry* registry);
	bool usingRegistry() const { return m_registry != nullptr; }

	// GUID of the current value when it came from the registry; invalid otherwise. This is what a
	// caller should store — the path is a display detail.
	const asset::AssetGuid& guid() const { return m_guid; }
	void setGuid(const asset::AssetGuid& guid);

	// AssetType this Kind maps onto, so the picker and the registry agree on what "mesh" means.
	static asset::AssetType assetTypeFor(Kind kind);

	void setKind(Kind kind);
	Kind kind() const { return m_kind; }

	// The current value, relative to the root. Setting it does not check that the file exists —
	// a path to something not yet on disk is shown as missing rather than silently cleared.
	void setPath(std::string path);
	const std::string& path() const { return m_path; }

	// True when the value changed this frame. Draws the value, a browse button and a clear button.
	bool draw(const char* id, float width = -1.0f);

	// Drops the cached scan; the next draw rebuilds it. Call after importing an asset.
	void refresh() { m_scanned = false; }

	// Files that matched the current root and kind at the last scan, relative to the root.
	const std::vector<std::string>& candidates() const { return m_candidates; }

	// True when `relativePath` is the kind of file this picker accepts.
	static bool matchesKind(const std::string& relativePath, Kind kind);

	void setDisabled(bool disabled) { m_disabled = disabled; }
	bool disabled() const { return m_disabled; }

	// Rebuilds the candidate list now instead of at the next draw. Exposed so a test can look at
	// the scan without an ImGui frame around it.
	void scan();

private:
	std::string m_root;
	std::string m_path;
	Kind m_kind = Kind::Any;
	bool m_disabled = false;
	const asset::AssetRegistry* m_registry = nullptr;
	asset::AssetGuid m_guid;

	bool m_scanned = false;
	std::vector<std::string> m_candidates;
	Filter m_filter;
};

// ── TypePicker ──────────────────────────────────────────────────────────────
//
// Chooses a registered type, optionally constrained to those deriving from a base. Nothing in the
// engine stores a TypeID in authored data yet; this is what the node graph (P6) and the resource
// system (P5) need to ask "which one of these do you want to create".

class TypePicker {
public:
	// Empty base means "any registered type". A type counts as derived from itself, matching how
	// the registry answers it.
	void setBaseType(TypeID base);
	TypeID baseType() const { return m_base; }

	void setSelected(TypeID id);
	TypeID selected() const { return m_selected; }
	const TypeInfo* selectedInfo() const;

	bool draw(const char* id, float width = -1.0f);

	void refresh() { m_scanned = false; }
	const std::vector<const TypeInfo*>& candidates() const { return m_candidates; }

	void setDisabled(bool disabled) { m_disabled = disabled; }

	// As with AssetPicker::scan — lets a test inspect the options with no frame in flight.
	void scan();

private:
	TypeID m_base;
	TypeID m_selected;
	bool m_disabled = false;

	bool m_scanned = false;
	std::vector<const TypeInfo*> m_candidates;
	Filter m_filter;
};

} // namespace tucano::editor::ui

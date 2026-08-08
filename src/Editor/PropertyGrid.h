#pragma once

#include "Editor/UI/Pickers.h"
#include "Editor/UI/Widgets.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

// PropertyGrid — edits any registered type without a line of UI written for it.
//
// Derived from Esoterica (MIT) — Code/EngineTools/PropertyGrid/PropertyGrid.{h,cpp}
//
// This is what the reflection work was for. The old Inspector was hand-written per field, so every
// new setting cost UI code, and panels drifted out of sync with the structs they edited. Here the
// grid walks the TypeInfo: a struct that declares its properties gets a complete editor, with its
// ranges, tooltips, categories and undo, the moment it is registered.
//
//   PropertyGrid grid;
//   grid.setUndoStack(&tool.undoStack());
//   grid.draw(waterParams);            // registered type — nothing else needed
//
// What it deliberately does not do: decide what a value *means*. Ranges and labels come from the
// type's metadata, so the same struct reads the same way in every panel that shows it.

namespace tucano {
struct TypeInfo;
struct PropertyInfo;
} // namespace tucano

namespace tucano::editor {

class UndoStack;

class PropertyGrid {
public:
	// Draws an editor for `instance`, which must be of type `type`. Returns true if any value
	// changed this frame.
	bool draw(const TypeInfo& type, void* instance);

	// Same, for a type registered under T. Returns false if T was never registered.
	template <typename T>
	bool draw(T& instance);

	// Edits are recorded here when set. Drags coalesce into one step, the way they do everywhere
	// else in the editor.
	void setUndoStack(UndoStack* stack) { m_undo = stack; }

	// Greys out every row. For inspecting something that is not the user's to change — a running
	// simulation, a read-only asset.
	void setReadOnly(bool readOnly) { m_readOnly = readOnly; }
	bool readOnly() const { return m_readOnly; }

	// Rows whose label does not match are hidden. Shared with the caller so a panel can put the
	// search box wherever it likes.
	ui::Filter& filter() { return m_filter; }

	// Draws the search box above the grid. Returns true when the query changed.
	bool drawFilterBox(float width = -1.0f);

	// Fired after a property is edited, so a panel can mark itself dirty or rebuild something.
	std::function<void(const PropertyInfo&, void*)> onChanged;

	// Width of the name column, as a fraction of the grid. Names that wrap are worse than values
	// that are a little narrow, so this leans wide.
	void setLabelColumnFraction(float fraction) { m_labelFraction = fraction; }

	// Directory that asset-path properties are browsed and stored relative to. Properties marked
	// with `assetKind` draw a picker; without a root the picker has nothing to offer and says so.
	void setAssetRoot(std::string root);
	const std::string& assetRoot() const { return m_assetRoot; }

private:
	bool drawCategory(const char* category, const TypeInfo& type, void* instance);
	// False when every property in the category is filtered out, transient or hidden by a rule —
	// in which case the header is skipped rather than drawn over nothing.
	bool categoryHasRows(std::string_view category, const TypeInfo& type, const void* instance) const;
	// `owner` is the type the property was declared on, which is what the editing rules are keyed
	// by — a nested struct brings its own rules, evaluated against its own instance.
	bool drawProperty(const TypeInfo& owner, const PropertyInfo& property, void* instance, int depth);
	bool drawValue(const PropertyInfo& property, void* instance);
	bool drawScalar(const PropertyInfo& property, void* instance);
	bool drawVector(const PropertyInfo& property, void* instance, int components);
	bool drawQuat(const PropertyInfo& property, void* instance);
	bool drawAssetPath(const PropertyInfo& property, void* instance);
	bool drawEnum(const PropertyInfo& property, void* instance);
	bool drawStruct(const PropertyInfo& property, void* instance, int depth);
	bool drawArray(const TypeInfo& owner, const PropertyInfo& property, void* instance, int depth);

	// True when this property should be shown under the current filter.
	bool visible(const PropertyInfo& property) const;

	UndoStack* m_undo = nullptr;
	ui::Filter m_filter;
	bool m_readOnly = false;
	float m_labelFraction = 0.42f;

	// A quaternion is edited as Euler degrees, and the two are not one-to-one: recomputing the angles
	// from the quaternion every frame makes them jump mid-drag (180/-180, gimbal-flipped triples). So
	// while one rotation row is being dragged, the angles the user is holding are kept here and the
	// quaternion is written from them. Cleared as soon as that row stops being the active item.
	uint32_t m_eulerOwner = 0; // ImGuiID of the row that owns m_euler; 0 = nobody
	float m_euler[3] = {0.0f, 0.0f, 0.0f};

	// One picker per asset-path property, because a picker caches its directory scan. Keyed by the
	// PropertyInfo address, which is static data and outlives every grid.
	std::string m_assetRoot;
	std::unordered_map<const PropertyInfo*, ui::AssetPicker> m_assetPickers;
};

} // namespace tucano::editor

#include "Core/TypeSystem/TypeRegistry.h"

namespace tucano::editor {

template <typename T>
bool PropertyGrid::draw(T& instance) {
	const TypeInfo* type = TypeRegistry::instance().find<T>();
	return type != nullptr ? draw(*type, &instance) : false;
}

} // namespace tucano::editor

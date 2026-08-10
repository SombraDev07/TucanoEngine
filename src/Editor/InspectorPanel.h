#pragma once

#include "Editor/PropertyGrid.h"

#include <string>
#include <vector>

namespace tucano::asset {
class AssetRegistry;
}

// InspectorPanel — the properties of whatever is selected in the Outliner.
//
// P4-05 replaced ~85 lines of hand-written ImGui with PropertyGrids over reflection data. C-02c
// then moved what it edits: with a `World` bound it shows the selected **entity's components**,
// which is the unit the editor authors (decision C-02, section 8 of the roadmap). Without a world
// it falls back to the old `Scene::objects` path, so hosts that have not been migrated keep
// working — the fallback is scaffolding, not a second design.
//
// One grid per component rather than one shared grid: each keeps its own column split and filter
// state, and a grid rebuilt for a different type every frame would lose both.

namespace tucano::editor {

struct EditorContext;
class UndoStack;

class InspectorPanel {
public:
	void draw(EditorContext& context);

	// Edits become undo steps when a stack is bound. The loose-panel hosts have no undo stack, so
	// this is optional rather than a constructor argument.
	void setUndoStack(UndoStack* stack);

	// Where asset-path properties browse from. Passed to every component grid, so a MeshComponent
	// gets a picker over the project index rather than a text box.
	void setAssetSource(const asset::AssetRegistry* registry, const std::string& root);

	// True when the last draw() changed something; the host decides what "dirty" means for it.
	bool changed() const { return m_changed; }

private:
	void drawEntity(EditorContext& context);
	void drawSceneObject(EditorContext& context);

	// Indexed by the component table in the .cpp, so a grid belongs to a component type for the
	// life of the panel.
	// Draws the Add Component button and its menu, offering only what the entity lacks.
	void drawAddComponent(EditorContext& context, uint32_t entity);

	std::vector<PropertyGrid> m_componentGrids;

	// The pre-C-02 path: object and its material are separate registered types reached through a
	// shared_ptr, so they are two grids.
	PropertyGrid m_objectGrid;
	PropertyGrid m_materialGrid;
	int m_materialSlot = 0;
	bool m_changed = false;
	UndoStack* m_undo = nullptr;
	const asset::AssetRegistry* m_assetRegistry = nullptr;
	std::string m_assetRoot;
};

} // namespace tucano::editor

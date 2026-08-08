#pragma once

#include "Editor/PropertyGrid.h"

// InspectorPanel — the properties of whatever is selected in the Outliner.
//
// P4-05 of the roadmap. This panel used to be ~85 lines of hand-written ImGui: a DragFloat3 per
// transform field, a fixed handful of material sliders, and a quaternion-to-Euler conversion done
// inline. It is now two PropertyGrids over the reflection generated from the
// annotations on Material and RenderObject themselves (P3-03), so
// what it shows is decided by the structs rather than by this file.
//
// The practical difference: `aoFactor`, `reflectance`, `clearcoatRoughness`, `fuzzColor`,
// `detailScale`, `alphaMask` and `alphaCutoff` were live material parameters the hand-written panel
// never exposed. They appear now because they are declared, not because anyone wrote a row for
// them — and the same will be true of the next field added to Material.

namespace tucano::editor {

struct EditorContext;
class UndoStack;

class InspectorPanel {
public:
	void draw(EditorContext& context);

	// Edits become undo steps when a stack is bound. The loose-panel hosts have no undo stack, so
	// this is optional rather than a constructor argument.
	void setUndoStack(UndoStack* stack);

	// True when the last draw() changed something; the host decides what "dirty" means for it.
	bool changed() const { return m_changed; }

private:
	// The object and its material are separate registered types reached through a shared_ptr, so
	// they are two grids. Both are driven by the single filter box the panel draws.
	PropertyGrid m_objectGrid;
	PropertyGrid m_materialGrid;
	int m_materialSlot = 0;
	bool m_changed = false;
};

} // namespace tucano::editor

#include "Editor/InspectorPanel.h"

#include "Editor/EditorContext.h"
#include "Renderer/Scene.h"
#include "Generated/Reflection.g.h"

#include <imgui.h>

#include <cstdio>

namespace tucano::editor {

void InspectorPanel::setUndoStack(UndoStack* stack) {
	m_objectGrid.setUndoStack(stack);
	m_materialGrid.setUndoStack(stack);
}

void InspectorPanel::draw(EditorContext& context) {
	m_changed = false;

	if (context.scene == nullptr) {
		ImGui::TextDisabled("No scene.");
		return;
	}
	auto& objects = context.scene->objects;
	if (context.selectedObject < 0 ||
	    static_cast<size_t>(context.selectedObject) >= objects.size()) {
		ImGui::TextDisabled("Select an object in the Outliner.");
		return;
	}

	RenderObject& object = objects[static_cast<size_t>(context.selectedObject)];

	// One box drives both grids: someone typing "rough" is looking for the property and does not
	// care that roughness happens to live on the material rather than the object.
	m_objectGrid.drawFilterBox();
	m_materialGrid.filter().setText(m_objectGrid.filter().text());

	m_changed |= m_objectGrid.draw(object);

	// Read-only facts about the mesh. Not properties — nothing here is editable, and a grid row
	// would imply otherwise.
	if (object.mesh) {
		ImGui::Spacing();
		ImGui::TextDisabled("Mesh: %zu verts, %zu material slot(s)", object.mesh->vertexCount(),
		                    object.materials.size());
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	if (object.materials.empty()) {
		ImGui::TextDisabled("No material.");
		return;
	}

	// The old panel showed slot 0 and nothing else, so a multi-material mesh was half uneditable.
	// The slot is clamped every frame because selecting another object can shrink the list under us.
	const int slotCount = static_cast<int>(object.materials.size());
	if (m_materialSlot >= slotCount) m_materialSlot = 0;

	const auto slotLabel = [&](int index, char* out, size_t size) {
		const auto& material = object.materials[static_cast<size_t>(index)];
		std::snprintf(out, size, "Slot %d - %s", index,
		              (material && !material->name.empty()) ? material->name.c_str() : "unnamed");
	};

	if (slotCount > 1) {
		char preview[160];
		slotLabel(m_materialSlot, preview, sizeof(preview));
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##materialSlot", preview)) {
			for (int i = 0; i < slotCount; ++i) {
				char label[160];
				slotLabel(i, label, sizeof(label));
				if (ImGui::Selectable(label, i == m_materialSlot)) m_materialSlot = i;
			}
			ImGui::EndCombo();
		}
	}

	const auto& material = object.materials[static_cast<size_t>(m_materialSlot)];
	if (!material) {
		ImGui::TextDisabled("Empty material slot.");
		return;
	}
	m_changed |= m_materialGrid.draw(*material);
}

} // namespace tucano::editor

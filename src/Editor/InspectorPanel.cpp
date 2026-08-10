#include "Editor/InspectorPanel.h"

#include "Editor/SceneCommands.h"
#include "Editor/UI/Icons.h"

#include "Editor/EditorContext.h"
#include "Editor/UI/Style.h"
#include "Editor/UI/Widgets.h"
#include "ECS/AuthoringComponents.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Renderer/Scene.h"
#include "Generated/Reflection.g.h"

#include <imgui.h>

#include <cstdio>

namespace tucano::editor {
namespace {

// The components the Inspector edits, in the order they read best: what it is, where it is, then
// what it does. Runtime-only components (a Jolt body id, an index into the render scene) are absent
// on purpose — the same list the scene file writes, for the same reason.
} // namespace

void InspectorPanel::setUndoStack(UndoStack* stack) {
	m_undo = stack;
	m_objectGrid.setUndoStack(stack);
	m_materialGrid.setUndoStack(stack);
	for (PropertyGrid& grid : m_componentGrids) grid.setUndoStack(stack);
}

void InspectorPanel::setAssetSource(const asset::AssetRegistry* registry, const std::string& root) {
	m_assetRegistry = registry;
	m_assetRoot = root;
	const auto apply = [&](PropertyGrid& grid) {
		grid.setAssetRegistry(registry);
		grid.setAssetRoot(root);
	};
	apply(m_objectGrid);
	apply(m_materialGrid);
	for (PropertyGrid& grid : m_componentGrids) apply(grid);
}

void InspectorPanel::draw(EditorContext& context) {
	m_changed = false;

	// The entity path is what the editor authors; the Scene path below is what hosts that have not
	// been migrated still use.
	if (context.world != nullptr) {
		drawEntity(context);
		return;
	}
	drawSceneObject(context);
}

void InspectorPanel::drawEntity(EditorContext& context) {
	if (!context.hasSelectedEntity()) {
		ImGui::TextDisabled("Select an entity in the Outliner.");
		return;
	}
	const ecs::Entity entity = context.selectedEntity;
	if (!context.world->alive(entity)) {
		// The selection outlived what it pointed at — deleted, or a scene was loaded under it.
		ImGui::TextDisabled("The selected entity no longer exists.");
		context.clearSelection();
		return;
	}

	const size_t componentCount = ecs::authoringComponentCount();
	if (m_componentGrids.size() < componentCount) {
		m_componentGrids.resize(componentCount);
		for (PropertyGrid& grid : m_componentGrids) {
			grid.setUndoStack(m_undo);
			grid.setAssetRegistry(m_assetRegistry);
			grid.setAssetRoot(m_assetRoot);
		}
	}

	// One filter box for the whole entity: someone typing "range" wants the property, and does not
	// care which component holds it.
	m_componentGrids[0].drawFilterBox();
	const std::string query = m_componentGrids[0].filter().text();

	int drawn = 0;
	// Deferred to after the loop: removing a component mid-iteration migrates the entity's archetype
	// and invalidates every pointer the loop is holding.
	const ecs::AuthoringComponentInfo* pendingRemove = nullptr;

	for (size_t i = 0; i < componentCount; ++i) {
		const ecs::AuthoringComponentInfo& entry = ecs::authoringComponents()[i];
		if (*entry.id == ecs::kInvalidEntity) continue;
		if (!context.world->entities().has(entity, *entry.id)) continue;

		const TypeInfo* type = TypeRegistry::instance().find(TypeID{entry.typeName});
		void* data = context.world->entities().get(entity, *entry.id);
		if (type == nullptr || data == nullptr) continue;

		++drawn;
		if (i > 0) m_componentGrids[i].filter().setText(query);

		// Removal lives on a per-component menu rather than a visible X: it is destructive, and a
		// button sitting next to every heading is one stray click from deleting a light.
		if (!entry.essential) {
			ImGui::PushID(static_cast<int>(i));
			if (ImGui::SmallButton(TUCANO_ICON_DOTS_VERTICAL)) ImGui::OpenPopup("##componentMenu");
			if (ImGui::BeginPopup("##componentMenu")) {
				if (ImGui::MenuItem((std::string("Remove ") + entry.label).c_str())) {
					pendingRemove = &entry;
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
			ImGui::SameLine();
		}
		ImGui::TextDisabled("%s", entry.label);

		if (m_componentGrids[i].draw(*type, data)) m_changed = true;
	}

	if (drawn == 0) {
		ImGui::TextDisabled("This entity has no editable components.");
	}

	drawAddComponent(context, entity);

	if (pendingRemove != nullptr) {
		if (removeComponent(*context.world, m_undo, entity, *pendingRemove)) m_changed = true;
	}
}

void InspectorPanel::drawAddComponent(EditorContext& context, uint32_t entity) {
	ImGui::Separator();
	if (ImGui::Button(TUCANO_ICON_PLUS "  Add Component", ImVec2(-1.0f, 0.0f))) {
		ImGui::OpenPopup("##addComponent");
	}

	if (!ImGui::BeginPopup("##addComponent")) return;

	// Only what the entity does not already have. Offering a component it has would either do
	// nothing or reset values someone just tuned, and both read as a bug.
	int offered = 0;
	for (size_t i = 0; i < ecs::authoringComponentCount(); ++i) {
		const ecs::AuthoringComponentInfo& entry = ecs::authoringComponents()[i];
		if (*entry.id == ecs::kInvalidEntity) continue;
		if (context.world->entities().has(entity, *entry.id)) continue;
		++offered;
		if (ImGui::MenuItem(entry.label)) {
			if (addComponent(*context.world, m_undo, entity, entry)) m_changed = true;
		}
	}
	if (offered == 0) ImGui::TextDisabled("Nothing left to add.");
	ImGui::EndPopup();
}

void InspectorPanel::drawSceneObject(EditorContext& context) {
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

#pragma once

#include "Editor/EditorContext.h"
#include "Editor/SceneCommands.h"
#include "Editor/UI/Icons.h"
#include "ECS/AuthoringComponents.h"
#include "ECS/EntityManager.h"
#include "ECS/World.h"
#include "Renderer/Scene.h"

#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <vector>

// The scene hierarchy.
//
// C-02c: with a `World` bound it lists **entities**, which is what the editor authors. Without one
// it lists `Scene::objects`, so a host that has not been migrated keeps working. That fallback is
// scaffolding and is expected to go once every host binds a world.

namespace tucano::editor {

class OutlinerPanel {
public:
	void draw(EditorContext& ctx) {
		if (ctx.world != nullptr) {
			drawEntities(ctx);
			return;
		}
		drawSceneObjects(ctx);
	}

private:
	// Entities in a stable order. Archetype/chunk order shifts whenever a component is added, and a
	// list that reorders itself under the cursor is unusable.
	static std::vector<ecs::Entity> liveEntities(ecs::World& world) {
		std::vector<ecs::Entity> out;
		for (ecs::EntityManager::Archetype& archetype : world.entities().archetypes()) {
			for (ecs::EntityManager::Chunk& chunk : archetype.chunks) {
				for (uint32_t i = 0; i < chunk.count; ++i) out.push_back(chunk.entities[i]);
			}
		}
		std::sort(out.begin(), out.end());
		return out;
	}

	void drawEntities(EditorContext& ctx) {
		// Creating is a top-level action, not something hidden in a right-click on an existing
		// entity — an empty scene has nothing to right-click.
		if (ImGui::Button(TUCANO_ICON_PLUS "  Add entity")) {
			createEntity(ctx, "Entity");
			m_renaming = ctx.selectedEntity;  // named on creation, like every other editor
			m_renameBuffer[0] = 0;
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1);
		m_filter.Draw("##OutlinerFilter", -1.0f);
		ImGui::Separator();
		ImGui::BeginChild("OutlinerTree", ImVec2(0, 0), false);

		const std::vector<ecs::Entity> entities = liveEntities(*ctx.world);
		int shown = 0;
		for (const ecs::Entity entity : entities) {
			// Unnamed entities are runtime-only (a physics proxy, a spawned effect) and would be
			// noise in a hierarchy meant for authoring.
			auto* name = ctx.world->get<ecs::NameComponent>(entity);
			if (name == nullptr) continue;
			if (m_filter.IsActive() && !m_filter.PassFilter(name->name.c_str())) continue;
			++shown;

			const bool selected = ctx.selectedEntity == entity;
			ImGui::PushID(static_cast<int>(entity));

			const char* icon = ctx.world->get<ecs::LightComponent>(entity) != nullptr
			                       ? TUCANO_ICON_LIGHTBULB
			                       : TUCANO_ICON_CUBE_OUTLINE;
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth |
			                           ImGuiTreeNodeFlags_NoTreePushOnOpen;
			if (selected) flags |= ImGuiTreeNodeFlags_Selected;

			auto* meshRef = ctx.world->get<ecs::MeshComponent>(entity);
			const bool hidden = meshRef != nullptr && !meshRef->visible;

			if (m_renaming == entity) {
				// Editing in place rather than in a dialog: renaming is a small correction, and a
				// modal for it costs two extra clicks every time.
				if (m_renameBuffer[0] == 0) {
					std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", name->name.c_str());
				}
				ImGui::SetNextItemWidth(-1);
				if (ImGui::IsWindowAppearing() || m_renameFocus) {
					ImGui::SetKeyboardFocusHere();
					m_renameFocus = false;
				}
				const bool committed = ImGui::InputText("##rename", m_renameBuffer,
				                                        sizeof(m_renameBuffer),
				                                        ImGuiInputTextFlags_EnterReturnsTrue);
				// Enter commits, losing focus commits, Escape throws the edit away.
				if (committed || (!ImGui::IsItemActive() && !ImGui::IsItemFocused())) {
					if (!ImGui::IsKeyPressed(ImGuiKey_Escape)) {
						renameEntity(*ctx.world, ctx.undo, entity, m_renameBuffer);
					}
					m_renaming = ecs::kInvalidEntity;
				}
				ImGui::PopID();
				continue;
			}

			if (hidden) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));
			ImGui::TreeNodeEx("##entity", flags, "%s  %s", icon, name->name.c_str());
			if (hidden) ImGui::PopStyleColor();

			if (ImGui::IsItemClicked()) ctx.selectedEntity = entity;
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
				ctx.selectedEntity = entity;
				m_renaming = entity;
				m_renameBuffer[0] = 0;
				m_renameFocus = true;
			}

			if (ImGui::BeginPopupContextItem()) {
				ctx.selectedEntity = entity;
				if (ImGui::MenuItem("Rename", "F2")) {
					m_renaming = entity;
					m_renameBuffer[0] = 0;
					m_renameFocus = true;
				}
				if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
					duplicateSelected(ctx);
					ImGui::EndPopup();
					ImGui::PopID();
					break; // the list changed under the loop
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Delete", "Del")) {
					deleteSelected(ctx);
					ImGui::EndPopup();
					ImGui::PopID();
					break;
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}

		if (shown == 0) {
			ImGui::TextDisabled(m_filter.IsActive() ? "Nothing matches the filter."
			                                        : "The scene has no entities yet.");
		}
		ImGui::EndChild();
	}

	void drawSceneObjects(EditorContext& ctx) {
		if (!ctx.scene) { ImGui::TextDisabled("No scene."); return; }

		ImGui::SetNextItemWidth(-1);
		m_filter.Draw("##OutlinerFilter", -1.0f);

		ImGui::Separator();
		ImGui::BeginChild("OutlinerTree", ImVec2(0, 0), false);

		auto& objects = ctx.scene->objects;
		for (size_t i = 0; i < objects.size(); ++i) {
			const auto& obj = objects[i];
			if (m_filter.IsActive() && !m_filter.PassFilter(obj.name.c_str())) continue;

			const bool selected = (static_cast<int>(i) == ctx.selectedObject);
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth;
			if (selected) flags |= ImGuiTreeNodeFlags_Selected;
			if (!obj.visible) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));

			ImGui::PushID(static_cast<int>(i));
			const bool opened = ImGui::TreeNodeEx(obj.name.empty() ? "Unnamed" : obj.name.c_str(), flags);
			if (ImGui::IsItemClicked()) { ctx.selectedObject = static_cast<int>(i); }
			if (ImGui::BeginPopupContextItem()) {
				ctx.selectedObject = static_cast<int>(i);
				if (ImGui::MenuItem("Focus")) {}
				if (ImGui::MenuItem("Duplicate")) {}
				if (ImGui::Selectable("Rename")) {}
				ImGui::Separator();
				if (ImGui::MenuItem("Delete", "Del")) {
					objects.erase(objects.begin() + i);
					if (ctx.selectedObject >= static_cast<int>(objects.size())) ctx.selectedObject = -1;
					ImGui::CloseCurrentPopup();
					if (!obj.visible) ImGui::PopStyleColor();
					ImGui::TreePop(); ImGui::PopID(); ImGui::EndChild(); return;
				}
				ImGui::EndPopup();
			}
			if (opened) ImGui::TreePop();
			if (!obj.visible) ImGui::PopStyleColor();
			ImGui::PopID();
		}
		if (objects.empty()) ImGui::TextDisabled("Scene is empty.");
		ImGui::EndChild();
	}

	ImGuiTextFilter m_filter;
	// Rename is in-place, so the panel has to remember which row is being edited across frames.
	ecs::Entity m_renaming = ecs::kInvalidEntity;
	bool m_renameFocus = false;
	char m_renameBuffer[64] = {};
};

} // namespace tucano::editor

#pragma once

#include "Editor/EditorContext.h"
#include "Animation/AnimationGraphComponent.h"
#include "Animation/AnimationClip.h"
#include "Animation/Skeleton.h"
#include "Renderer/Scene.h"

#include <imgui.h>
#include <vector>
#include <memory>

namespace tucano::editor {

class AnimationPanel {
public:
	void draw(EditorContext& ctx) {
		if (!ctx.scene) { ImGui::TextDisabled("No scene."); return; }

		auto& objects = ctx.scene->objects;
		if (ctx.selectedObject < 0 || static_cast<size_t>(ctx.selectedObject) >= objects.size()) {
			ImGui::TextDisabled("Select an object with animations.");
			return;
		}

		auto& obj = objects[ctx.selectedObject];

		ImGui::Text("Object: %s", obj.name.empty() ? "Unnamed" : obj.name.c_str());
		ImGui::Separator();

		// ── Animation Graph State ──
		if (m_graphComp.instance) {
			ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1), "Graph: Active");
			ImGui::Text("Nodes: %zu", m_graphComp.definition ? m_graphComp.definition->nodes.size() : 0);
		} else {
			ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "Graph: None");
		}

		// ── Animation Clips ──
		if (!m_clips.empty()) {
			ImGui::Separator();
			ImGui::Text("Clips: %zu", m_clips.size());

			if (ImGui::BeginListBox("##ClipList", ImVec2(-1, 120))) {
				for (size_t i = 0; i < m_clips.size(); ++i) {
					const bool selected = (static_cast<int>(i) == m_activeClip);
					char label[128];
					snprintf(label, sizeof(label), "%s (%.1fs)", m_clips[i]->name().c_str(), m_clips[i]->duration());
					if (ImGui::Selectable(label, selected)) {
						m_activeClip = static_cast<int>(i);
						m_time = 0.0f;
					}
				}
				ImGui::EndListBox();
			}

			// ── Playback ──
			ImGui::Separator();
			if (ImGui::Button(m_playing ? "Pause" : "Play")) {
				m_playing = !m_playing;
			}
			ImGui::SameLine();
			if (ImGui::Button("Stop")) {
				m_playing = false;
				m_time = 0.0f;
			}
			ImGui::SameLine();
			ImGui::Checkbox("Loop", &m_loop);

			ImGui::SliderFloat("Time", &m_time, 0.0f, m_activeClip >= 0 && m_activeClip < static_cast<int>(m_clips.size()) ? m_clips[m_activeClip]->duration() : 1.0f);
			ImGui::SliderFloat("Speed", &m_speed, 0.1f, 3.0f);

			// ── Build Graph ──
			ImGui::Separator();
			if (ImGui::Button("Build Locomotion Graph")) {
				buildDefaultGraph(ctx);
			}
			ImGui::SameLine();
			if (ImGui::Button("Clear Graph")) {
				m_graphComp = animation::AnimationGraphComponent{};
			}

			// ── Skeleton info ──
			if (m_skeleton && !m_skeleton->bones().empty()) {
				ImGui::Separator();
				ImGui::TextDisabled("Skeleton: %zu bones", m_skeleton->bones().size());
			}

			// ── Evaluate and apply ──
			if (m_playing && m_graphComp.instance && m_skeleton) {
				float dt = 1.0f / 60.0f;
				m_time += dt * m_speed;
				tucano::anim::Pose pose;
				if (m_graphComp.evaluate(dt, m_speed / 3.0f, pose)) {
					m_skeleton->computeSkinningMatrices(pose, obj.skinningMatrices);
				}
			}
		} else {
			ImGui::TextDisabled("No animation clips. Import a glTF with animations.");
		}
	}

	// Called by host to register clips and skeleton for the selected object
	void setClips(const std::vector<const tucano::anim::AnimationClip*>& clips,
	              const tucano::anim::Skeleton* skeleton) {
		m_clips = clips;
		m_skeleton = skeleton;
		if (!clips.empty() && m_activeClip < 0) m_activeClip = 0;
	}

private:
	void buildDefaultGraph(EditorContext& ctx) {
		if (m_clips.size() >= 1) {
			const auto* idle = m_clips.size() > 0 ? m_clips[0] : nullptr;
			const auto* walk = m_clips.size() > 1 ? m_clips[1] : idle;
			const auto* run  = m_clips.size() > 2 ? m_clips[2] : walk;

			m_graphComp = animation::AnimationGraphComponent::createLocomotion(idle, walk, run, m_skeleton);
			ctx.logInfo("Built locomotion graph: " +
			            std::to_string(m_graphComp.definition->nodes.size()) + " nodes");
		}
	}

	std::vector<const tucano::anim::AnimationClip*> m_clips;
	const tucano::anim::Skeleton* m_skeleton = nullptr;
	animation::AnimationGraphComponent m_graphComp;

	int m_activeClip = -1;
	float m_time = 0.0f;
	float m_speed = 1.0f;
	bool m_playing = false;
	bool m_loop = true;
};

} // namespace tucano::editor

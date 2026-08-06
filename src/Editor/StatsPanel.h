#pragma once

#include "Editor/EditorContext.h"
#include "Core/Memory.h"

#include <imgui.h>

namespace tucano::editor {

class StatsPanel {
public:
	void draw(EditorContext& ctx) {
		const float fps = ctx.frameMs > 0.0f ? 1000.0f / ctx.frameMs : 0.0f;

		ImGui::Text("FPS: %.1f", fps);
		ImGui::Text("Frame: %.2f ms", ctx.frameMs);
		ImGui::Separator();

		ImGui::Text("Draw Calls: %u", ctx.drawCalls);
		ImGui::Text("Viewport: %ux%u", ctx.viewportW, ctx.viewportH);
		ImGui::Separator();

		if (ctx.simMs > 0.0f) {
			ImGui::Text("Sim:    %.2f ms", ctx.simMs);
			ImGui::Text("Veget.: %.2f ms", ctx.vegMs);
			ImGui::Text("Render: %.2f ms", ctx.renderMs);
			ImGui::Text("UI:     %.2f ms", ctx.uiMs);
			ImGui::Separator();

			float total = ctx.simMs + ctx.vegMs + ctx.renderMs + ctx.uiMs;
			if (total > 0.0f) {
				ImGui::ProgressBar(ctx.renderMs / total, ImVec2(-1, 0), "");
				ImGui::SameLine();
				ImGui::TextDisabled("render");
				ImGui::ProgressBar(ctx.simMs / total, ImVec2(-1, 0), "");
				ImGui::SameLine();
				ImGui::TextDisabled("sim");
			}
		}

		ImGui::Separator();
		if (ImGui::Button("Reset")) {
			m_frameCount = 0;
			m_accumMs = 0.0f;
		}
		ImGui::SameLine();
		ImGui::Text("avg: %.2f ms (%u frames)", m_frameCount > 0 ? m_accumMs / m_frameCount : 0.0f, m_frameCount);

		ImGui::Separator();
		ImGui::TextDisabled("Memory");
		auto printAlloc = [](const char* label, const tucano::core::MemoryAllocator& a) {
			float mb = a.bytesAllocated() / (1024.0f * 1024.0f);
			ImGui::Text("%s: %.2f MB (%llu allocs)", label, mb, a.allocationCount());
		};
		printAlloc("Global", tucano::core::g_allocGlobal);
		printAlloc("RHI", tucano::core::g_allocRHI);
		printAlloc("ECS", tucano::core::g_allocECS);
		printAlloc("Renderer", tucano::core::g_allocRenderer);
		printAlloc("Streaming", tucano::core::g_allocStreaming);
		printAlloc("Physics", tucano::core::g_allocPhysics);

		m_accumMs += ctx.frameMs;
		++m_frameCount;
	}

private:
	uint32_t m_frameCount = 0;
	double m_accumMs = 0.0;
};

} // namespace tucano::editor

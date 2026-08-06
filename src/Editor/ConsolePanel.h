#pragma once

#include "Editor/EditorContext.h"

#include <imgui.h>

namespace tucano::editor {

class ConsolePanel {
public:
	void draw(EditorContext& ctx) {
		ImGui::Checkbox("Info", &m_showInfo);
		ImGui::SameLine();
		ImGui::Checkbox("Warn", &m_showWarn);
		ImGui::SameLine();
		ImGui::Checkbox("Error", &m_showError);
		ImGui::SameLine();
		if (ImGui::Button("Clear")) {
			ctx.clearLog();
		}
		ImGui::SameLine();
		m_autoScroll = ImGui::Checkbox("Auto-scroll", &m_autoScroll);

		ImGui::Separator();

		ImGui::BeginChild("LogScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
		for (size_t i = 0; i < ctx.log.size(); ++i) {
			const auto& entry = ctx.log[i];
			bool visible = false;
			switch (entry.level) {
			case LogEntry::Level::Info:    visible = m_showInfo; break;
			case LogEntry::Level::Warning: visible = m_showWarn; break;
			case LogEntry::Level::Error:   visible = m_showError; break;
			}
			if (!visible) continue;

			ImVec4 color(1, 1, 1, 1);
			const char* prefix = "";
			switch (entry.level) {
			case LogEntry::Level::Info:    color = ImVec4(0.7f, 0.7f, 0.7f, 1); break;
			case LogEntry::Level::Warning: color = ImVec4(1.0f, 0.8f, 0.2f, 1); prefix = "[WARN] "; break;
			case LogEntry::Level::Error:   color = ImVec4(1.0f, 0.3f, 0.3f, 1); prefix = "[ERROR] "; break;
			}
			ImGui::TextColored(color, "%s%s", prefix, entry.message.c_str());
		}
		if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
			ImGui::SetScrollHereY(1.0f);
		}
		ImGui::EndChild();
	}

private:
	bool m_showInfo = true;
	bool m_showWarn = true;
	bool m_showError = true;
	bool m_autoScroll = true;
};

} // namespace tucano::editor

#pragma once

#include "Editor/EditorContext.h"

#include <imgui.h>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>

namespace tucano::editor {

class ContentBrowser {
public:
	void draw(EditorContext& ctx) {
		if (m_currentPath.empty()) {
			m_currentPath = "Assets";
		}

		// ── Navigation bar ──
		if (ImGui::Button("<")) {
			if (m_currentPath != "Assets") {
				m_currentPath = std::filesystem::path(m_currentPath).parent_path().string();
			}
		}
		ImGui::SameLine();
		ImGui::TextUnformatted(m_currentPath.c_str());

		ImGui::SameLine();
		ImGui::SetNextItemWidth(180);
		const char* filterItems[] = {"All", "Meshes", "Textures", "Materials"};
		ImGui::Combo("##Filter", &m_filterIndex, filterItems, IM_ARRAYSIZE(filterItems));

		ImGui::Separator();

		// ── Content area ──
		ImGui::BeginChild("ContentArea", ImVec2(0, 0), false);

		std::error_code ec;
		std::vector<std::filesystem::path> dirs, files;

		for (const auto& entry : std::filesystem::directory_iterator(m_currentPath, ec)) {
			if (entry.is_directory()) {
				dirs.push_back(entry.path());
			} else {
				files.push_back(entry.path());
			}
		}
		if (ec) {
			ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Cannot read: %s", m_currentPath.c_str());
			ImGui::EndChild();
			return;
		}

		std::sort(dirs.begin(), dirs.end());
		std::sort(files.begin(), files.end());

		const float itemSize = 80.0f;
		const float availWidth = ImGui::GetContentRegionAvail().x;
		int columns = std::max(1, static_cast<int>(availWidth / (itemSize + ImGui::GetStyle().ItemSpacing.x)));

		int col = 0;
		ImGui::Columns(columns, nullptr, false);

		// Directories
		for (const auto& dir : dirs) {
			drawItem(dir, true, itemSize);
			++col;
			if (col >= columns) { ImGui::NextColumn(); col = 0; }
		}

		// Files
		for (const auto& file : files) {
			if (!passesFilter(file)) continue;
			drawItem(file, false, itemSize);
			++col;
			if (col >= columns) { ImGui::NextColumn(); col = 0; }
		}

		ImGui::Columns(1);
		ImGui::EndChild();
	}

private:
	void drawItem(const std::filesystem::path& path, bool isDir, float size) {
		ImGui::BeginGroup();
		ImGui::PushID(path.string().c_str());

		const std::string label = path.filename().string();
		const ImVec2 btnSize(size, size);

		if (ImGui::Button(isDir ? "D" : "F", btnSize)) {
			if (isDir) {
				m_currentPath = path.string();
			}
		}

		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && !isDir) {
			// Import asset on double-click
		}

		ImGui::TextWrapped("%s", label.c_str());
		ImGui::PopID();
		ImGui::EndGroup();
		ImGui::SameLine();
	}

	bool passesFilter(const std::filesystem::path& path) const {
		if (m_filterIndex == 0) return true; // All
		const auto ext = path.extension().string();
		switch (m_filterIndex) {
		case 1: return ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".tucmesh"; // Meshes
		case 2: return ext == ".png" || ext == ".jpg" || ext == ".dds" || ext == ".hdr" || ext == ".tga"; // Textures
		case 3: return ext == ".tmat"; // Materials
		default: return true;
		}
	}

	std::string m_currentPath;
	int m_filterIndex = 0;
};

} // namespace tucano::editor

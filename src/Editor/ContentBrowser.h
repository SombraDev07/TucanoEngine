#pragma once

#include "AssetPipeline/AssetRegistry.h"
#include "Editor/EditorContext.h"
#include "Editor/UI/Icons.h"
#include "Editor/UI/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

// Content Browser — the project's assets.
//
// B-02: it used to walk the disk with `directory_iterator` every frame and classify files by
// extension. It now reads the `AssetRegistry` (B-01), which buys three things it could not do
// before: every item carries a **stable GUID**, so what gets picked survives the file being
// renamed; the folder tree is the *project's* shape rather than whatever happens to sit on disk
// beside it; and there is no filesystem traffic per frame.
//
// Folders are virtual — derived from the registry's relative paths, never queried. A directory
// holding nothing the editor understands therefore does not appear, which is the right answer for
// a browser whose job is to show assets.

namespace tucano::editor {

class ContentBrowser {
public:
	void draw(EditorContext& ctx) {
		if (ctx.assets == nullptr) {
			ImGui::TextDisabled("No project scanned.");
			return;
		}

		drawToolbar(ctx);
		ImGui::Separator();

		std::vector<std::string> folders;
		std::vector<const asset::RegistryEntry*> files;
		collect(*ctx.assets, folders, files);

		ImGui::BeginChild("ContentArea", ImVec2(0, 0), false);

		const float itemSize = 72.0f;
		const float available = ImGui::GetContentRegionAvail().x;
		const int columns =
		    std::max(1, static_cast<int>(available / (itemSize + ImGui::GetStyle().ItemSpacing.x)));

		int column = 0;
		ImGui::Columns(columns, nullptr, false);

		for (const std::string& folder : folders) {
			drawFolder(folder, itemSize);
			if (++column >= columns) {
				ImGui::NextColumn();
				column = 0;
			}
		}
		for (const asset::RegistryEntry* entry : files) {
			drawAsset(*entry, itemSize);
			if (++column >= columns) {
				ImGui::NextColumn();
				column = 0;
			}
		}

		ImGui::Columns(1);
		if (folders.empty() && files.empty()) {
			ImGui::TextDisabled(m_filter.empty() ? "Nothing here." : "Nothing matches the filter.");
		}
		ImGui::EndChild();
	}

	// What was last clicked, so a panel that wants to act on a selection can. Invalid until
	// something is selected — and a GUID, not a path, because that is what survives a rename.
	const asset::AssetGuid& selected() const { return m_selected; }

private:
	void drawToolbar(EditorContext& ctx) {
		ImGui::BeginDisabled(m_folder.empty());
		if (ui::flatIconButton(TUCANO_ICON_ARROW_LEFT, "Up one folder")) {
			const size_t slash = m_folder.find_last_of('/');
			m_folder = slash == std::string::npos ? std::string() : m_folder.substr(0, slash);
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		const std::string here = m_folder.empty() ? "Assets" : "Assets/" + m_folder;
		ImGui::TextUnformatted(here.c_str());

		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		const char* kTypes[] = {"All", "Meshes", "Textures", "Materials", "Scenes", "Audio"};
		ImGui::Combo("##type", &m_typeIndex, kTypes, IM_ARRAYSIZE(kTypes));

		ImGui::SameLine();
		m_filter.setHint("Filter assets...");
		m_filter.draw("##contentFilter", -1.0f);

		ImGui::TextDisabled("%zu assets indexed", ctx.assets->size());
	}

	asset::AssetType selectedType() const {
		switch (m_typeIndex) {
			case 1: return asset::AssetType::Mesh;
			case 2: return asset::AssetType::Texture;
			case 3: return asset::AssetType::Material;
			case 4: return asset::AssetType::Scene;
			case 5: return asset::AssetType::Audio;
			default: return asset::AssetType::Unknown; // "All"
		}
	}

	// Splits the registry into the folders under `m_folder` and the assets directly inside it.
	void collect(const asset::AssetRegistry& registry, std::vector<std::string>& folders,
	             std::vector<const asset::RegistryEntry*>& files) const {
		const asset::AssetType wanted = selectedType();
		const std::string prefix = m_folder.empty() ? std::string() : m_folder + "/";

		for (const asset::RegistryEntry& entry : registry.all()) {
			if (wanted != asset::AssetType::Unknown && entry.type != wanted) continue;
			if (entry.relativePath.size() < prefix.size()) continue;
			if (entry.relativePath.compare(0, prefix.size(), prefix) != 0) continue;

			const std::string_view rest(entry.relativePath.data() + prefix.size(),
			                            entry.relativePath.size() - prefix.size());
			const size_t slash = rest.find('/');
			if (slash == std::string_view::npos) {
				if (m_filter.empty() || m_filter.matches(entry.name)) files.push_back(&entry);
				continue;
			}
			std::string folder(rest.substr(0, slash));
			if (std::find(folders.begin(), folders.end(), folder) == folders.end()) {
				folders.push_back(std::move(folder));
			}
		}

		std::sort(folders.begin(), folders.end());
		std::sort(files.begin(), files.end(),
		          [](const asset::RegistryEntry* a, const asset::RegistryEntry* b) {
			          return a->name < b->name;
		          });
	}

	void drawFolder(const std::string& folder, float size) {
		ImGui::BeginGroup();
		ImGui::PushID(folder.c_str());
		if (ImGui::Button(TUCANO_ICON_FOLDER, ImVec2(size, size))) {
			m_folder = m_folder.empty() ? folder : m_folder + "/" + folder;
		}
		wrappedLabel(folder.c_str(), size);
		ImGui::PopID();
		ImGui::EndGroup();
	}

	void drawAsset(const asset::RegistryEntry& entry, float size) {
		ImGui::BeginGroup();
		ImGui::PushID(static_cast<int>(entry.guid.lo));

		const bool selected = m_selected == entry.guid;
		if (selected) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		}
		if (ImGui::Button(icon(entry.type), ImVec2(size, size))) m_selected = entry.guid;
		if (selected) ImGui::PopStyleColor();

		// The GUID is what survives a rename, so it is what the tooltip leads with; the path is the
		// part that can change under you.
		ui::itemTooltip("%s\n%s\nGUID %s", entry.name.c_str(), entry.relativePath.c_str(),
		                entry.guid.toString().c_str());

		wrappedLabel(entry.name.c_str(), size);
		ImGui::PopID();
		ImGui::EndGroup();
	}

	// Names are long and the cells are narrow, so the label has to wrap *inside* its own column.
	// Without the explicit wrap position it wraps at the window edge and neighbouring cells overlap.
	static void wrappedLabel(const char* text, float width) {
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
		ImGui::TextUnformatted(text);
		ImGui::PopTextWrapPos();
	}

	static const char* icon(asset::AssetType type) {
		switch (type) {
			case asset::AssetType::Mesh: return TUCANO_ICON_CUBE_OUTLINE;
			case asset::AssetType::Texture: return TUCANO_ICON_IMAGE;
			case asset::AssetType::Material: return TUCANO_ICON_PALETTE;
			case asset::AssetType::Scene: return TUCANO_ICON_FILE_TREE;
			case asset::AssetType::Audio: return TUCANO_ICON_VOLUME_HIGH;
			default: return TUCANO_ICON_FILE;
		}
	}

	std::string m_folder; // relative to the project root; empty is the root
	int m_typeIndex = 0;
	ui::Filter m_filter;
	asset::AssetGuid m_selected;
};

} // namespace tucano::editor

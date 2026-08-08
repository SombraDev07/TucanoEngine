#include "Editor/UI/Pickers.h"

#include "Core/TypeSystem/TypeInfo.h"
#include "Core/TypeSystem/TypeRegistry.h"
#include "Editor/UI/Icons.h"
#include "Editor/UI/Style.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace tucano::editor::ui {
namespace {

// Popup height. A picker that grows with the project would push its own list off screen.
constexpr float kPopupHeight = 260.0f;
constexpr float kPopupWidth = 380.0f;

// Guard on the recursive walk. A picker pointed at the wrong directory — a drive root, a build tree
// with its dependency checkouts — must degrade to "too many results" instead of freezing the editor.
constexpr size_t kMaxCandidates = 4000;

std::string lowercase(std::string text) {
	std::transform(text.begin(), text.end(), text.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

// Purpose to extensions. Kept here rather than at the call sites so a new texture format is one
// edit, and so a field says what it is *for* rather than what it will accept.
bool extensionMatches(const std::string& extension, AssetPicker::Kind kind) {
	switch (kind) {
		case AssetPicker::Kind::Any:
			return true;
		case AssetPicker::Kind::Mesh:
			return extension == ".gltf" || extension == ".glb" || extension == ".fbx" ||
			       extension == ".obj" || extension == ".tuasset";
		case AssetPicker::Kind::Texture:
			return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
			       extension == ".tga" || extension == ".dds" || extension == ".bmp";
		case AssetPicker::Kind::Hdri:
			// Separate from Texture on purpose: pointing IBL at an 8-bit PNG produces a plausible
			// image and quietly wrong lighting, which is worse than an empty list.
			return extension == ".hdr" || extension == ".exr";
		case AssetPicker::Kind::Scene:
			return extension == ".gltf" || extension == ".glb" || extension == ".tuscene";
		case AssetPicker::Kind::Text:
			return extension == ".txt" || extension == ".json" || extension == ".lua";
	}
	return false;
}

// Backslashes in, forward slashes out. A path picked on Windows has to survive being read on a
// machine that has never heard of them.
std::string normalise(const std::filesystem::path& path) {
	std::string text = path.generic_string();
	return text;
}

} // namespace

// ── AssetPicker ─────────────────────────────────────────────────────────────

void AssetPicker::setRoot(std::string root) {
	if (root == m_root) return;
	m_root = std::move(root);
	m_scanned = false;
}

void AssetPicker::setKind(Kind kind) {
	if (kind == m_kind) return;
	m_kind = kind;
	m_scanned = false;
}

void AssetPicker::setPath(std::string path) { m_path = std::move(path); }

bool AssetPicker::matchesKind(const std::string& relativePath, Kind kind) {
	const std::filesystem::path path(relativePath);
	return extensionMatches(lowercase(path.extension().string()), kind);
}

void AssetPicker::scan() {
	m_candidates.clear();
	m_scanned = true;
	if (m_root.empty()) return;

	std::error_code ec;
	const std::filesystem::path root(m_root);
	if (!std::filesystem::is_directory(root, ec)) return;

	// skip_permission_denied rather than a try/catch: one unreadable directory somewhere in a
	// project tree should cost that directory, not the whole list.
	auto options = std::filesystem::directory_options::skip_permission_denied;
	for (std::filesystem::recursive_directory_iterator it(root, options, ec), end; it != end;
	     it.increment(ec)) {
		if (ec) {
			ec.clear();
			continue;
		}
		if (!it->is_regular_file(ec)) continue;
		if (!extensionMatches(lowercase(it->path().extension().string()), m_kind)) continue;

		m_candidates.push_back(normalise(std::filesystem::relative(it->path(), root, ec)));
		if (m_candidates.size() >= kMaxCandidates) break;
	}

	std::sort(m_candidates.begin(), m_candidates.end());
}

bool AssetPicker::draw(const char* id, float width) {
	if (!m_scanned) scan();

	ImGui::PushID(id);
	ImGui::BeginGroup();

	const float clearWidth = Style::kIconButtonWidthSmall;
	const float browseWidth = Style::kIconButtonWidthSmall;
	const float spacing = ImGui::GetStyle().ItemSpacing.x;
	const float total = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
	const float fieldWidth = std::max(60.0f, total - clearWidth - browseWidth - spacing * 2.0f);

	// The value is shown, not typed. A path field invites a typo that produces a missing asset with
	// no feedback; browsing cannot produce one.
	const bool missing = !m_path.empty() && !m_root.empty() &&
	                     !std::filesystem::exists(std::filesystem::path(m_root) / m_path);

	// Red for a path that points at nothing: a missing asset is the failure this widget exists to
	// prevent, and it has to be visible without hovering.
	const Color textColor = missing          ? Style::kAxisX
	                        : m_path.empty() ? Style::kTextDisabled
	                                         : Style::kText;
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(textColor));
	std::string shown = m_path.empty() ? std::string("(none)") : m_path;
	ImGui::SetNextItemWidth(fieldWidth);
	ImGui::InputText("##value", shown.data(), shown.size() + 1, ImGuiInputTextFlags_ReadOnly);
	ImGui::PopStyleColor();
	if (missing) itemTooltip("Not found under %s", m_root.c_str());
	else if (!m_path.empty()) itemTooltip("%s", m_path.c_str());

	bool changed = false;

	ImGui::SameLine(0.0f, spacing);
	ImGui::BeginDisabled(m_disabled);
	if (flatIconButton(TUCANO_ICON_FOLDER_OPEN, "Browse")) {
		m_filter.clear();
		scan(); // a browse is the one moment the user expects to see files added since they started
		ImGui::OpenPopup("##assetPickerPopup");
	}

	ImGui::SameLine(0.0f, spacing);
	ImGui::BeginDisabled(m_path.empty());
	if (flatIconButton(TUCANO_ICON_CLOSE, "Clear")) {
		m_path.clear();
		changed = true;
	}
	ImGui::EndDisabled();

	ImGui::SetNextWindowSize(ImVec2(kPopupWidth, kPopupHeight), ImGuiCond_Appearing);
	if (ImGui::BeginPopup("##assetPickerPopup")) {
		m_filter.setHint("Filter assets...");
		m_filter.draw("##assetFilter");

		if (m_candidates.empty()) {
			textColored(Style::kTextDisabled, "Nothing of this kind under %s",
			            m_root.empty() ? "(no root set)" : m_root.c_str());
		} else if (m_candidates.size() >= kMaxCandidates) {
			// Never silently truncate: a list that stops at 4000 looks complete.
			textColored(Style::kAccent0, "Showing the first %zu matches — narrow the filter",
			            kMaxCandidates);
		}

		ImGui::BeginChild("##assetList", ImVec2(0.0f, 0.0f), false);
		for (const std::string& candidate : m_candidates) {
			if (!m_filter.matches(candidate)) continue;
			const bool selected = candidate == m_path;
			if (ImGui::Selectable(candidate.c_str(), selected)) {
				m_path = candidate;
				changed = true;
				ImGui::CloseCurrentPopup();
			}
			if (selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndChild();
		ImGui::EndPopup();
	}
	ImGui::EndDisabled();

	ImGui::EndGroup();
	ImGui::PopID();
	return changed;
}

// ── TypePicker ──────────────────────────────────────────────────────────────

void TypePicker::setBaseType(TypeID base) {
	if (base == m_base) return;
	m_base = base;
	m_scanned = false;
}

void TypePicker::setSelected(TypeID id) { m_selected = id; }

const TypeInfo* TypePicker::selectedInfo() const {
	return m_selected.isValid() ? TypeRegistry::instance().find(m_selected) : nullptr;
}

void TypePicker::scan() {
	m_scanned = true;
	m_candidates = TypeRegistry::instance().allTypes();

	if (m_base.isValid()) {
		const auto& registry = TypeRegistry::instance();
		m_candidates.erase(std::remove_if(m_candidates.begin(), m_candidates.end(),
		                                  [&](const TypeInfo* info) {
			                                  return info == nullptr ||
			                                         !registry.isDerivedFrom(info->id, m_base);
		                                  }),
		                   m_candidates.end());
	}

	// allTypes() comes out of a hash map, so its order is whatever the table decided. A picker has
	// to be alphabetical or it is unusable. Compared with strcmp, not `<`: TypeInfo::name is a
	// const char*, and comparing those compares addresses.
	std::sort(m_candidates.begin(), m_candidates.end(), [](const TypeInfo* a, const TypeInfo* b) {
		return std::strcmp(a->name, b->name) < 0;
	});
}

bool TypePicker::draw(const char* id, float width) {
	if (!m_scanned) scan();

	const TypeInfo* current = selectedInfo();
	const char* preview = current != nullptr ? current->name
	                      : m_selected.isValid() ? "(unregistered type)"
	                                             : "(none)";

	bool changed = false;
	ImGui::PushID(id);
	ImGui::BeginDisabled(m_disabled);
	ImGui::SetNextItemWidth(width > 0.0f ? width : -1.0f);

	if (ImGui::BeginCombo("##type", preview)) {
		m_filter.setHint("Filter types...");
		m_filter.draw("##typeFilter");

		if (m_candidates.empty()) {
			textColored(Style::kTextDisabled, m_base.isValid() ? "No type derives from this base"
			                                                   : "No types registered");
		}

		for (const TypeInfo* info : m_candidates) {
			if (!m_filter.matches(info->name)) continue;
			const bool selected = info->id == m_selected;
			if (ImGui::Selectable(info->name, selected)) {
				m_selected = info->id;
				changed = true;
			}
			if (selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	ImGui::EndDisabled();
	ImGui::PopID();
	return changed;
}

} // namespace tucano::editor::ui

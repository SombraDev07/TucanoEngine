#include "Editor/DialogManager.h"
#include "Editor/UI/Icons.h"
#include "Editor/UI/Style.h"
#include "Editor/UI/Widgets.h"

#include <imgui.h>

namespace tucano::editor {
namespace {

// One ImGui popup id for every dialog: the manager shows one at a time, so they can share it, and
// the title is drawn as content rather than as the window's own label.
constexpr const char* kPopupId = "###editorModal";
constexpr float kButtonWidth = 110.0f;

} // namespace

struct DialogManager::Request {
	std::string title;
	// Returns true when the dialog is finished and should close.
	std::function<bool()> body;
	float width = 0.0f;
	float height = 0.0f;
};

DialogManager::DialogManager() = default;
DialogManager::~DialogManager() = default;

bool DialogManager::hasActiveDialog() const { return !m_queue.empty(); }
size_t DialogManager::queued() const { return m_queue.size(); }

void DialogManager::clear() {
	m_queue.clear();
	m_openPending = false;
}

void DialogManager::custom(std::string title, std::function<bool()> body, float width, float height) {
	if (!body) return;
	auto request = std::make_unique<Request>();
	request->title = std::move(title);
	request->body = std::move(body);
	request->width = width;
	request->height = height;
	m_queue.push_back(std::move(request));
	// Only the front of the queue is ever shown; a request added behind one waits its turn.
	if (m_queue.size() == 1) m_openPending = true;
}

void DialogManager::message(std::string title, std::string text) {
	custom(std::move(title), [text = std::move(text)]() {
		ImGui::TextUnformatted(text.c_str());
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		// Enter and Escape both dismiss: there is only one thing to do here.
		return ImGui::Button("OK", ImVec2(kButtonWidth, 0.0f)) ||
		       ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Enter);
	});
}

void DialogManager::confirm(std::string title, std::string text, std::function<void(bool)> onResult,
                            std::string confirmLabel, std::string cancelLabel) {
	custom(std::move(title), [text = std::move(text), onResult = std::move(onResult),
	                          confirmLabel = std::move(confirmLabel),
	                          cancelLabel = std::move(cancelLabel)]() {
		ImGui::TextUnformatted(text.c_str());
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (ui::colorButton(Style::kAccent2, confirmLabel.c_str(), kButtonWidth)) {
			if (onResult) onResult(true);
			return true;
		}
		ImGui::SameLine();
		if (ImGui::Button(cancelLabel.c_str(), ImVec2(kButtonWidth, 0.0f)) ||
		    ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			if (onResult) onResult(false);
			return true;
		}
		return false;
	});
}

void DialogManager::choice(std::string title, std::string text, std::string primaryLabel,
                           std::string secondaryLabel, std::function<void(Choice)> onResult) {
	custom(std::move(title), [text = std::move(text), primaryLabel = std::move(primaryLabel),
	                          secondaryLabel = std::move(secondaryLabel),
	                          onResult = std::move(onResult)]() {
		ImGui::TextUnformatted(text.c_str());
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (ui::colorButton(Style::kAccent2, primaryLabel.c_str(), kButtonWidth)) {
			if (onResult) onResult(Choice::Primary);
			return true;
		}
		ImGui::SameLine();
		// Red: the secondary action in a three-way prompt is the destructive one (Discard,
		// Don't Save), and it must not look like the safe default.
		if (ui::colorButton(0xFF2020C0, secondaryLabel.c_str(), kButtonWidth)) {
			if (onResult) onResult(Choice::Secondary);
			return true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(kButtonWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			if (onResult) onResult(Choice::Cancel);
			return true;
		}
		return false;
	});
}

void DialogManager::draw() {
	if (m_queue.empty() || ImGui::GetCurrentContext() == nullptr) return;

	Request& request = *m_queue.front();

	if (m_openPending) {
		ImGui::OpenPopup(kPopupId);
		m_openPending = false;
	}

	// Centred: a modal that appears under the cursor gets dismissed by the click already on its way.
	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (request.width > 0.0f || request.height > 0.0f) {
		ImGui::SetNextWindowSize(ImVec2(request.width, request.height), ImGuiCond_Appearing);
	}

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
	if (request.width <= 0.0f && request.height <= 0.0f) {
		flags |= ImGuiWindowFlags_AlwaysAutoResize;
	}

	bool finished = false;
	if (ImGui::BeginPopupModal(kPopupId, nullptr, flags)) {
		{
			// Title as content, so every dialog gets the same treatment regardless of what ImGui
			// would do with a window title.
			ui::textColored(Style::kAccent0, TUCANO_ICON_ALERT_CIRCLE_OUTLINE "  %s",
			                request.title.c_str());
		}
		ImGui::Spacing();

		finished = request.body();

		if (finished) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	if (finished) {
		m_queue.erase(m_queue.begin());
		// Next in line opens on the following frame, never stacked on top of the one just closed.
		if (!m_queue.empty()) m_openPending = true;
	}
}

} // namespace tucano::editor

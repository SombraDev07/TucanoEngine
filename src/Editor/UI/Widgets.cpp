#include "Editor/UI/Widgets.h"
#include "Editor/UI/Icons.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>

namespace tucano::editor::ui {
namespace {

// ImGui only keeps a hover timer for items that can be hovered as items; text does not qualify,
// which is why the tooltip helpers check HoveredIdTimer rather than IsItemHovered's delay flag.
bool hoveredLongEnough() {
	return ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
	       GImGui->HoveredIdTimer > Style::kTooltipDelay;
}

std::string formatV(const char* fmt, va_list args) {
	va_list copy;
	va_copy(copy, args);
	const int n = std::vsnprintf(nullptr, 0, fmt, copy);
	va_end(copy);
	if (n <= 0) return {};
	std::string out(static_cast<size_t>(n), '\0');
	std::vsnprintf(out.data(), static_cast<size_t>(n) + 1, fmt, args);
	return out;
}

std::string toLower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(),
	               [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
	return s;
}

} // namespace

// ── Tooltips ────────────────────────────────────────────────────────────────

void itemTooltip(const char* fmt, ...) {
	if (!hoveredLongEnough()) return;
	va_list args;
	va_start(args, fmt);
	const std::string text = formatV(fmt, args);
	va_end(args);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
	ImGui::BeginTooltip();
	ImGui::TextUnformatted(text.c_str());
	ImGui::EndTooltip();
	ImGui::PopStyleVar();
}

void itemTooltipCustom(const std::function<void()>& body) {
	if (!hoveredLongEnough() || !body) return;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
	ImGui::BeginTooltip();
	body();
	ImGui::EndTooltip();
	ImGui::PopStyleVar();
}

// ── Text ────────────────────────────────────────────────────────────────────

void textColored(Color c, const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	const std::string text = formatV(fmt, args);
	va_end(args);
	ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(c));
	ImGui::TextUnformatted(text.c_str());
	ImGui::PopStyleColor();
}

void textFont(Font f, const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	const std::string text = formatV(fmt, args);
	va_end(args);
	ScopedFont scoped(f);
	ImGui::TextUnformatted(text.c_str());
}

void textEllipsis(const char* text, float maxWidth) {
	if (text == nullptr) return;
	if (ImGui::CalcTextSize(text).x <= maxWidth) {
		ImGui::TextUnformatted(text);
		return;
	}

	// Trim from the end until the string plus the ellipsis fits. Linear is fine: these are labels,
	// not documents, and it runs once per visible row.
	const char* kEllipsis = "...";
	const float ellipsisWidth = ImGui::CalcTextSize(kEllipsis).x;
	std::string trimmed(text);
	while (!trimmed.empty() && ImGui::CalcTextSize(trimmed.c_str()).x + ellipsisWidth > maxWidth) {
		trimmed.pop_back();
	}
	trimmed += kEllipsis;
	ImGui::TextUnformatted(trimmed.c_str());
	// The full text has to stay reachable, or trimming silently destroys information.
	itemTooltip("%s", text);
}

void helpMarker(const char* helpText) {
	ImGui::TextDisabled(TUCANO_ICON_HELP_CIRCLE_OUTLINE);
	itemTooltip("%s", helpText);
}

float helpMarkerWidth() { return ImGui::CalcTextSize(TUCANO_ICON_HELP_CIRCLE_OUTLINE).x; }

// ── Layout ──────────────────────────────────────────────────────────────────

void sectionHeader(const char* label) {
	ImGui::Spacing();
	{
		ScopedFont f(Font::SmallBold);
		ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(Style::kAccent0));
		ImGui::TextUnformatted(label);
		ImGui::PopStyleColor();
	}
	ImGui::Separator();
	ImGui::Spacing();
}

void sameLineSeparator(float width) {
	const float w = width > 0.0f ? width : ImGui::GetStyle().ItemSpacing.x * 2.0f;
	ImGui::SameLine(0, w * 0.5f);

	ImDrawList* draw = ImGui::GetWindowDrawList();
	const float x = ImGui::GetCursorScreenPos().x;
	const float y = ImGui::GetCursorScreenPos().y;
	const float h = ImGui::GetFrameHeight();
	draw->AddLine(ImVec2(x, y + 2.0f), ImVec2(x, y + h - 2.0f), Style::kGray3);

	ImGui::SameLine(0, w * 0.5f);
}

bool beginGroupBox(const char* label, bool defaultOpen) {
	ImGui::PushStyleColor(ImGuiCol_Header, toImVec4(Style::kGray7));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, toImVec4(Style::kGray5));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, toImVec4(Style::kGray4));
	const bool open =
	    ImGui::CollapsingHeader(label, defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
	ImGui::PopStyleColor(3);
	if (open) {
		ImGui::Indent(ImGui::GetStyle().IndentSpacing * 0.5f);
	}
	return open;
}

void endGroupBox() {
	ImGui::Unindent(ImGui::GetStyle().IndentSpacing * 0.5f);
	ImGui::Spacing();
}

// ── Buttons ─────────────────────────────────────────────────────────────────

namespace {

// Derives hover/active shades from one base colour, so a caller supplies a single colour and still
// gets the full interaction feedback.
void pushButtonColors(Color background) {
	const ImVec4 base = toImVec4(background);
	const auto lighten = [](const ImVec4& v, float amount) {
		return ImVec4(std::min(v.x + amount, 1.0f), std::min(v.y + amount, 1.0f),
		              std::min(v.z + amount, 1.0f), v.w);
	};
	ImGui::PushStyleColor(ImGuiCol_Button, base);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, lighten(base, 0.08f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, lighten(base, 0.16f));
}

} // namespace

bool colorButton(Color background, const char* label, float width, float height) {
	pushButtonColors(background);
	const bool pressed = ImGui::Button(label, ImVec2(width, height));
	ImGui::PopStyleColor(3);
	return pressed;
}

bool iconButton(const char* icon, const char* tooltip, Color tint, float width) {
	ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(tint));
	const bool pressed = ImGui::Button(icon, ImVec2(width, ImGui::GetFrameHeight()));
	ImGui::PopStyleColor();
	if (tooltip != nullptr) itemTooltip("%s", tooltip);
	return pressed;
}

bool iconLabelButton(const char* icon, const char* label, Color iconTint, float width, float height) {
	// One invisible button covers both parts so the whole thing is a single hit target; the icon and
	// the label are then drawn over it in their own colours.
	const ImGuiStyle& style = ImGui::GetStyle();
	const ImVec2 iconSize = ImGui::CalcTextSize(icon);
	const ImVec2 labelSize = ImGui::CalcTextSize(label);
	const float spacing = style.ItemInnerSpacing.x;
	const ImVec2 total(width > 0.0f ? width : iconSize.x + spacing + labelSize.x + style.FramePadding.x * 2.0f,
	                   height > 0.0f ? height : ImGui::GetFrameHeight());

	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const bool pressed = ImGui::Button(("##" + std::string(label)).c_str(), total);

	ImDrawList* draw = ImGui::GetWindowDrawList();
	const float textY = pos.y + (total.y - iconSize.y) * 0.5f;
	float x = pos.x + style.FramePadding.x;
	draw->AddText(ImVec2(x, textY), iconTint, icon);
	x += iconSize.x + spacing;
	draw->AddText(ImVec2(x, pos.y + (total.y - labelSize.y) * 0.5f), Style::kText, label);
	return pressed;
}

bool flatButton(const char* label, float width, float height) {
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	const bool pressed = ImGui::Button(label, ImVec2(width, height));
	ImGui::PopStyleColor();
	return pressed;
}

bool flatIconButton(const char* icon, const char* tooltip, Color tint) {
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	const bool pressed = iconButton(icon, tooltip, tint);
	ImGui::PopStyleColor();
	return pressed;
}

// ── Toggles ─────────────────────────────────────────────────────────────────

bool toggleButton(const char* label, bool& state, float width, float height) {
	// Lit uses the accent so "on" reads at a glance across a toolbar.
	pushButtonColors(state ? Style::kAccent2 : Style::kGray4);
	const bool pressed = ImGui::Button(label, ImVec2(width, height));
	ImGui::PopStyleColor(3);
	if (pressed) state = !state;
	return pressed;
}

bool toggleIconButton(const char* onIcon, const char* offIcon, bool& state, const char* tooltip) {
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	const bool pressed = iconButton(state ? onIcon : offIcon, tooltip,
	                                state ? Style::kAccent0 : Style::kTextDisabled);
	ImGui::PopStyleColor();
	if (pressed) state = !state;
	return pressed;
}

bool triStateCheckbox(const char* label, int& state) {
	bool changed = false;
	if (state < 0) {
		// ImGui's own mixed-value flag: renders a dash instead of a tick.
		ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
		bool dummy = false;
		if (ImGui::Checkbox(label, &dummy)) {
			state = 1; // a click on "mixed" resolves to "all on"
			changed = true;
		}
		ImGui::PopItemFlag();
	} else {
		bool value = state > 0;
		if (ImGui::Checkbox(label, &value)) {
			state = value ? 1 : 0;
			changed = true;
		}
	}
	return changed;
}

// ── Text input ──────────────────────────────────────────────────────────────

bool inputTextWithClear(const char* id, std::string& text, const char* hint, float width) {
	const ImGuiStyle& style = ImGui::GetStyle();
	const float clearWidth = ImGui::GetFrameHeight();
	const float total = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;

	ImGui::PushID(id);
	ImGui::SetNextItemWidth(total - clearWidth - style.ItemInnerSpacing.x);

	char buffer[512];
	std::snprintf(buffer, sizeof(buffer), "%s", text.c_str());

	bool changed = false;
	const bool edited = hint != nullptr
	                        ? ImGui::InputTextWithHint("##text", hint, buffer, sizeof(buffer))
	                        : ImGui::InputText("##text", buffer, sizeof(buffer));
	if (edited) {
		text = buffer;
		changed = true;
	}

	ImGui::SameLine(0, style.ItemInnerSpacing.x);
	// Disabled rather than hidden: a control that appears and disappears makes the row jump.
	ImGui::BeginDisabled(text.empty());
	if (flatIconButton(TUCANO_ICON_CLOSE, nullptr, Style::kTextDisabled)) {
		text.clear();
		changed = true;
	}
	ImGui::EndDisabled();
	ImGui::PopID();
	return changed;
}

// ── Filter ──────────────────────────────────────────────────────────────────

void Filter::tokenize() {
	m_tokens.clear();
	const std::string lower = toLower(m_query);
	size_t start = 0;
	while (start < lower.size()) {
		const size_t end = lower.find(' ', start);
		const std::string token = lower.substr(start, end == std::string::npos ? std::string::npos : end - start);
		if (!token.empty()) m_tokens.push_back(token);
		if (end == std::string::npos) break;
		start = end + 1;
	}
}

bool Filter::draw(const char* id, float width) {
	if (inputTextWithClear(id, m_query, m_hint.c_str(), width)) {
		tokenize();
		return true;
	}
	return false;
}

bool Filter::matches(const std::string& text) const {
	if (m_tokens.empty()) return true;
	const std::string lower = toLower(text);
	// Every token must appear — matching how people narrow a list by adding words.
	for (const std::string& token : m_tokens) {
		if (lower.find(token) == std::string::npos) return false;
	}
	return true;
}

bool Filter::matches(const char* text) const {
	return text != nullptr ? matches(std::string(text)) : m_tokens.empty();
}

void Filter::clear() {
	m_query.clear();
	m_tokens.clear();
}

void Filter::setText(std::string query) {
	if (query == m_query) return; // re-tokenizing every frame for an unchanged mirror is pure waste
	m_query = std::move(query);
	tokenize();
}

// ── Progress / activity ─────────────────────────────────────────────────────

void spinner(const char* id, float radius, float thickness, Color c) {
	ImGui::PushID(id);
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const ImVec2 size(radius * 2.0f, radius * 2.0f);
	// Behaves as an item so it participates in layout like any other control.
	ImGui::Dummy(size);

	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 centre(pos.x + radius, pos.y + radius);
	const float t = static_cast<float>(ImGui::GetTime());
	constexpr int kSegments = 24;
	const float arc = 1.6f; // radians of visible sweep
	const float start = t * 3.0f;
	draw->PathClear();
	for (int i = 0; i <= kSegments; ++i) {
		const float a = start + (static_cast<float>(i) / kSegments) * arc;
		draw->PathLineTo(ImVec2(centre.x + std::cos(a) * radius, centre.y + std::sin(a) * radius));
	}
	draw->PathStroke(c, 0, thickness);
	ImGui::PopID();
}

} // namespace tucano::editor::ui

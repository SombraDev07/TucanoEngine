#pragma once

#include "Editor/UI/Fonts.h"
#include "Editor/UI/Style.h"

#include <functional>
#include <string>
#include <vector>

// Editor widget library — the controls ImGui does not ship and every tool ends up re-inventing.
//
// Derived from Esoterica (MIT) — Code/Base/Imgui/ImguiX.h + ImguiX.cpp
//
// The point is not novelty, it is consistency: an icon button is the same width everywhere, a
// tooltip waits the same beat everywhere, a section header looks the same in every panel. Panels
// that hand-roll these drift apart within a week.
//
// Only depends on ImGui through the .cpp, so panels can include this without dragging ImGui into
// their own headers.

namespace tucano::editor::ui {

// ── Tooltips ────────────────────────────────────────────────────────────────
// ImGui's SetItemTooltip fires instantly, which makes a dense panel flicker as the cursor crosses
// it. These wait Style::kTooltipDelay, like a desktop app.

// Tooltip for the item just submitted.
void itemTooltip(const char* fmt, ...);
// Same, but the caller draws the body — for tooltips with widgets or images in them.
void itemTooltipCustom(const std::function<void()>& body);

// ── Text ────────────────────────────────────────────────────────────────────

void textColored(Color c, const char* fmt, ...);
// Text in a specific face, without the caller managing push/pop.
void textFont(Font f, const char* fmt, ...);
// Trims with an ellipsis to fit `maxWidth`, and shows the full string as a tooltip when trimmed.
void textEllipsis(const char* text, float maxWidth);

// "?" marker that reveals `helpText` on hover — the standard way to explain a field without
// spending a line of panel on it.
void helpMarker(const char* helpText);
float helpMarkerWidth();

// ── Layout ──────────────────────────────────────────────────────────────────

// Horizontal rule with a label, for grouping fields inside a panel.
void sectionHeader(const char* label);
// SameLine plus a vertical rule — separates toolbar clusters.
void sameLineSeparator(float width = -1.0f);

// Collapsible box with a header, returning whether the body should be drawn.
// Always pair with endGroupBox() when it returns true.
bool beginGroupBox(const char* label, bool defaultOpen = true);
void endGroupBox();

// ── Buttons ─────────────────────────────────────────────────────────────────

// Button with an explicit background colour. Size 0 means "fit the label".
bool colorButton(Color background, const char* label, float width = 0.0f, float height = 0.0f);

// Button whose label is an icon, sized so a row of them lines up (Style::kIconButtonWidth*).
bool iconButton(const char* icon, const char* tooltip = nullptr, Color tint = Style::kText,
                float width = Style::kIconButtonWidth);

// Icon + text, with the icon tinted independently of the label — used when the icon carries state
// (green = valid, red = error) but the label should stay readable.
bool iconLabelButton(const char* icon, const char* label, Color iconTint = Style::kText,
                     float width = 0.0f, float height = 0.0f);

// Button with no background until hovered — for toolbars, where a full button per action is loud.
bool flatButton(const char* label, float width = 0.0f, float height = 0.0f);
bool flatIconButton(const char* icon, const char* tooltip = nullptr, Color tint = Style::kText);

// ── Toggles ─────────────────────────────────────────────────────────────────

// Button that stays lit while `state` is true. Returns true on the frame it was clicked, with
// `state` already flipped — so callers act on the new value.
bool toggleButton(const char* label, bool& state, float width = 0.0f, float height = 0.0f);
// Toggle whose face is an icon; `onIcon`/`offIcon` may differ (eye / eye-off).
bool toggleIconButton(const char* onIcon, const char* offIcon, bool& state,
                      const char* tooltip = nullptr);

// Checkbox that also understands "mixed": < 0 mixed, 0 false, > 0 true. Needed whenever a control
// summarises a multi-selection.
bool triStateCheckbox(const char* label, int& state);

// ── Text input ──────────────────────────────────────────────────────────────

// Input with placeholder text while empty and an "x" to clear. Returns true when the value changed.
bool inputTextWithClear(const char* id, std::string& text, const char* hint = nullptr,
                        float width = -1.0f);

// ── Filter ──────────────────────────────────────────────────────────────────

// Search box state. Splits the query into lowercase tokens so `matches()` is a cheap
// all-tokens-present test, which is what users expect from a filter box.
class Filter {
public:
	// Draws the box. Returns true when the query changed this frame.
	bool draw(const char* id = "##filter", float width = -1.0f);

	// True when `text` satisfies the query (case-insensitive, all tokens must appear).
	bool matches(const std::string& text) const;
	bool matches(const char* text) const;

	bool empty() const { return m_tokens.empty(); }
	void clear();

	// The raw query, so one visible box can drive several filters — the Inspector has two grids and
	// one search field, and a user typing "rough" means it for both.
	const std::string& text() const { return m_query; }
	void setText(std::string query);
	void setHint(std::string hint) { m_hint = std::move(hint); }

private:
	void tokenize();

	std::string m_query;
	std::string m_hint = "Search...";
	std::vector<std::string> m_tokens;
};

// ── Progress / activity ─────────────────────────────────────────────────────

// Indeterminate spinner — for work with no known duration. Behaves as an item for layout.
void spinner(const char* id, float radius = 6.0f, float thickness = 2.0f, Color c = Style::kAccent0);

} // namespace tucano::editor::ui

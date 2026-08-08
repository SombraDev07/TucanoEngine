#pragma once

#include <cstdint>

// Editor visual style — the Tucano palette and the ImGui metrics that go with it.
//
// Derived from Esoterica (MIT) — Code/Base/Imgui/ImguiStyle.h + ImguiStyle.cpp
// The *structure* is theirs (a nine-step grey ramp plus a small accent ramp, applied over every
// ImGuiCol). The colours are not: Esoterica is grey-on-green, Tucano is black-and-gold.
//
// Why a ramp instead of picking colours per widget: with a ramp, a panel, its header, its frames
// and its scrollbar all come from the same ladder, so depth reads consistently and a new widget has
// an obvious slot to sit in. Hand-picked per-widget colours drift the moment two people touch them.

struct ImVec4;

namespace tucano::editor {

// A colour as 0xAABBGGRR — the packing ImGui's IM_COL32 uses, so it drops straight into draw lists.
using Color = uint32_t;

struct Style {
	// Grey ramp: Gray0 is the brightest, Gray9 the darkest. Backgrounds come from the dark end,
	// interactive surfaces from the middle, hover/active from the light end.
	static constexpr Color kGray0 = 0xFF6A6A6A;
	static constexpr Color kGray1 = 0xFF585858;
	static constexpr Color kGray2 = 0xFF4A4A4A;
	static constexpr Color kGray3 = 0xFF3C3C3C;
	static constexpr Color kGray4 = 0xFF323232;
	static constexpr Color kGray5 = 0xFF2A2A2A;
	static constexpr Color kGray6 = 0xFF212121;
	static constexpr Color kGray7 = 0xFF1A1A1A;
	static constexpr Color kGray8 = 0xFF141414;
	static constexpr Color kGray9 = 0xFF0D0D0D;

	static constexpr Color kText = 0xFFF2F2F2;
	static constexpr Color kTextDisabled = 0xFF8A8A8A;

	// Toucan gold. Accent0 is the brightest (selection overlines, focus), Accent2 the deepest
	// (plots, filled tracks). Stored 0xAABBGGRR, so the byte order reads reversed from hex RGB.
	static constexpr Color kAccent0 = 0xFF4FC8F5; // #F5C84F
	static constexpr Color kAccent1 = 0xFF2FA8D8; // #D8A82F
	static constexpr Color kAccent2 = 0xFF1C7FA8; // #A87F1C

	// Axis colours, used by gizmos and by vector fields so X/Y/Z read the same everywhere.
	static constexpr Color kAxisX = 0xFF3A3AE0;
	static constexpr Color kAxisY = 0xFF46C846;
	static constexpr Color kAxisZ = 0xFFE08C3A;
	static constexpr Color kAxisW = 0xFF3AC8E0;

	// Width of a button holding only an icon, per font size — icon buttons have to line up in a
	// toolbar, and deriving this from the label would make each one a different width.
	static constexpr float kIconButtonWidthTiny = 26.0f;
	static constexpr float kIconButtonWidthSmall = 30.0f;
	static constexpr float kIconButtonWidth = 34.0f;
	static constexpr float kIconButtonWidthLarge = 40.0f;

	// How long the cursor must rest on an item before its tooltip appears.
	static constexpr float kTooltipDelay = 0.4f;

	// Applies the palette and metrics to the current ImGui context. Resets the style first, so it is
	// safe to call again after a theme change.
	static void apply(float dpiScale = 1.0f);

	// Largest DPI scale across attached monitors — the editor rasterises for the sharpest one so it
	// stays crisp when a window is dragged onto it.
	static float maxDpiScale();
};

// Unpacks a Style colour for the ImGui APIs that want floats.
ImVec4 toImVec4(Color c);

// Same colour at a different opacity — for disabled and ghosted states.
Color withAlpha(Color c, float alpha);

} // namespace tucano::editor

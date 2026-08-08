#include "Editor/UI/Style.h"

#include <imgui.h>

#include <algorithm>

namespace tucano::editor {

ImVec4 toImVec4(Color c) {
	// 0xAABBGGRR
	const float r = static_cast<float>((c >> 0) & 0xFF) / 255.0f;
	const float g = static_cast<float>((c >> 8) & 0xFF) / 255.0f;
	const float b = static_cast<float>((c >> 16) & 0xFF) / 255.0f;
	const float a = static_cast<float>((c >> 24) & 0xFF) / 255.0f;
	return ImVec4(r, g, b, a);
}

Color withAlpha(Color c, float alpha) {
	const auto a = static_cast<uint32_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
	return (c & 0x00FFFFFFu) | (a << 24);
}

float Style::maxDpiScale() {
	float scale = 1.0f;
	const ImGuiPlatformIO& io = ImGui::GetPlatformIO();
	for (const ImGuiPlatformMonitor& monitor : io.Monitors) {
		scale = std::max(monitor.DpiScale, scale);
	}
	return std::max(scale, 1.0f);
}

void Style::apply(float dpiScale) {
	if (ImGui::GetCurrentContext() == nullptr) {
		return;
	}
	ImGuiStyle& style = ImGui::GetStyle();
	style = ImGuiStyle(); // start from defaults so a re-apply cannot accumulate

	ImVec4* c = style.Colors;

	c[ImGuiCol_Text] = toImVec4(kText);
	c[ImGuiCol_TextDisabled] = toImVec4(kTextDisabled);
	c[ImGuiCol_TextSelectedBg] = toImVec4(withAlpha(kAccent2, 0.55f));

	// Surfaces: title bars sit darkest, panels one step up, so a docked panel reads as raised.
	c[ImGuiCol_TitleBg] = toImVec4(kGray9);
	c[ImGuiCol_TitleBgActive] = toImVec4(kGray8);
	c[ImGuiCol_TitleBgCollapsed] = toImVec4(kGray8);
	c[ImGuiCol_WindowBg] = toImVec4(kGray6);
	c[ImGuiCol_ChildBg] = toImVec4(kGray6);
	c[ImGuiCol_PopupBg] = toImVec4(kGray7);
	c[ImGuiCol_MenuBarBg] = toImVec4(kGray8);
	c[ImGuiCol_Border] = toImVec4(kGray3);
	c[ImGuiCol_BorderShadow] = toImVec4(kGray9);

	// Input frames go *darker* than the panel: a field reads as a well, not a button.
	c[ImGuiCol_FrameBg] = toImVec4(kGray8);
	c[ImGuiCol_FrameBgHovered] = toImVec4(kGray7);
	c[ImGuiCol_FrameBgActive] = toImVec4(kGray5);

	c[ImGuiCol_Tab] = toImVec4(kGray7);
	c[ImGuiCol_TabSelected] = toImVec4(kGray4);
	c[ImGuiCol_TabSelectedOverline] = toImVec4(kAccent0);
	c[ImGuiCol_TabHovered] = toImVec4(kGray3);
	c[ImGuiCol_TabDimmed] = toImVec4(kGray7);
	c[ImGuiCol_TabDimmedSelected] = toImVec4(kGray5);
	c[ImGuiCol_TabDimmedSelectedOverline] = toImVec4(kGray1);

	c[ImGuiCol_Header] = toImVec4(kGray4);
	c[ImGuiCol_HeaderHovered] = toImVec4(kGray3);
	c[ImGuiCol_HeaderActive] = toImVec4(kGray2);

	c[ImGuiCol_Separator] = toImVec4(kGray3);
	c[ImGuiCol_SeparatorHovered] = toImVec4(kGray1);
	c[ImGuiCol_SeparatorActive] = toImVec4(kAccent1);

	c[ImGuiCol_NavCursor] = toImVec4(kAccent1);
	c[ImGuiCol_DockingPreview] = toImVec4(withAlpha(kAccent2, 0.45f));
	c[ImGuiCol_DockingEmptyBg] = toImVec4(kGray9);

	c[ImGuiCol_ScrollbarBg] = toImVec4(kGray7);
	c[ImGuiCol_ScrollbarGrab] = toImVec4(kGray3);
	c[ImGuiCol_ScrollbarGrabHovered] = toImVec4(kGray2);
	c[ImGuiCol_ScrollbarGrabActive] = toImVec4(kGray1);

	// The handle a user drags is gold: the one control whose position carries the value.
	c[ImGuiCol_SliderGrab] = toImVec4(kAccent1);
	c[ImGuiCol_SliderGrabActive] = toImVec4(kAccent0);

	c[ImGuiCol_ResizeGrip] = toImVec4(kGray4);
	c[ImGuiCol_ResizeGripHovered] = toImVec4(kGray2);
	c[ImGuiCol_ResizeGripActive] = toImVec4(kAccent2);

	c[ImGuiCol_Button] = toImVec4(kGray4);
	c[ImGuiCol_ButtonHovered] = toImVec4(kGray2);
	c[ImGuiCol_ButtonActive] = toImVec4(kGray1);

	c[ImGuiCol_CheckMark] = toImVec4(kAccent0);

	c[ImGuiCol_PlotLines] = toImVec4(kAccent1);
	c[ImGuiCol_PlotLinesHovered] = toImVec4(kAccent0);
	c[ImGuiCol_PlotHistogram] = toImVec4(kAccent2);
	c[ImGuiCol_PlotHistogramHovered] = toImVec4(kAccent1);

	c[ImGuiCol_TableHeaderBg] = toImVec4(kGray7);
	c[ImGuiCol_TableBorderStrong] = toImVec4(kGray4);
	c[ImGuiCol_TableBorderLight] = toImVec4(kGray5);
	c[ImGuiCol_TableRowBg] = toImVec4(kGray6);
	c[ImGuiCol_TableRowBgAlt] = toImVec4(kGray5);

	c[ImGuiCol_DragDropTarget] = toImVec4(kAccent0);
	c[ImGuiCol_TextLink] = toImVec4(kAccent0);

	// Metrics. Square windows and lightly rounded frames: panels read as panels, controls as
	// controls. Roomy padding is what separates a tool from a debug overlay.
	style.WindowPadding = ImVec2(6, 6);
	style.FramePadding = ImVec2(6, 5);
	style.CellPadding = ImVec2(4, 5);
	style.ItemSpacing = ImVec2(6, 6);
	style.ItemInnerSpacing = ImVec2(6, 4);
	style.IndentSpacing = 14.0f;

	style.WindowRounding = 0.0f;
	style.WindowBorderSize = 1.0f;
	style.ChildBorderSize = 0.0f;
	style.PopupRounding = 3.0f;
	style.FrameRounding = 3.0f;
	style.GrabRounding = 3.0f;
	style.GrabMinSize = 9.0f;
	style.TabRounding = 5.0f;
	style.TabBorderSize = 1.0f;
	style.ScrollbarSize = 14.0f;
	style.ScrollbarRounding = 0.0f;

	style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
	style.WindowMenuButtonPosition = ImGuiDir_None; // the collapse arrow is noise in a docked editor

	style.ScaleAllSizes(dpiScale > 0.0f ? dpiScale : 1.0f);
}

} // namespace tucano::editor

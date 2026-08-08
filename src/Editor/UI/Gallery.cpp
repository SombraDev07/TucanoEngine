#include "Editor/UI/Gallery.h"
#include "Editor/PropertyGrid.h"
#include "Editor/UndoStack.h"
#include "Editor/UI/CurveEditor.h"
#include "Generated/Reflection.g.h"
#include "Editor/UI/Fonts.h"
#include "Editor/UI/Icons.h"
#include "Editor/UI/Notifications.h"
#include "Editor/UI/Pickers.h"
#include "Editor/UI/Style.h"
#include "Editor/UI/TreeListView.h"
#include "Editor/UI/Widgets.h"

#include <imgui.h>

#include <string>

namespace tucano::editor::ui {
namespace {

// Persisted across frames so the interactive controls actually hold state — a gallery of controls
// that reset every frame cannot show hover, toggle or edit behaviour.
struct State {
	bool toggleA = true;
	bool toggleB = false;
	bool visible = true;
	int triState = -1;
	std::string textValue = "Sponza.gltf";
	Filter filter;
	float slider = 0.35f;
	int combo = 1;
	TreeListView tree;
	Filter treeFilter;
	bool treeBuilt = false;
	Curve curve;
	bool curveBuilt = false;
	// A real engine struct, not a mock: the point of the grid is that it needs nothing written for
	// the type it is shown.
	WaterParams water;
	PropertyGrid grid;
	UndoStack undo;
	bool gridBound = false;
	AssetPicker meshPicker;
	AssetPicker hdriPicker;
	TypePicker typePicker;
	bool pickersBound = false;
};

// A small scene-shaped tree, so the widget is exercised the way the Outliner will use it:
// headers, nesting, per-item icons and colours.
std::vector<TreeListView::Item> buildSampleTree() {
	using Item = TreeListView::Item;
	auto mesh = [](uint64_t id, const char* name) {
		Item i;
		i.id = id;
		i.label = name;
		i.icon = TUCANO_ICON_CUBE_OUTLINE;
		i.iconColor = Style::kAccent1;
		return i;
	};
	auto light = [](uint64_t id, const char* name) {
		Item i;
		i.id = id;
		i.label = name;
		i.icon = TUCANO_ICON_LIGHTBULB;
		i.iconColor = 0xFF40D8F0;
		return i;
	};

	Item scene;
	scene.id = 1;
	scene.label = "Sponza";
	scene.icon = TUCANO_ICON_FILE_TREE;

	Item geometry;
	geometry.id = 2;
	geometry.label = "Geometry";
	geometry.isHeader = true;
	geometry.children = {mesh(10, "Floor"), mesh(11, "Column_A"), mesh(12, "Column_B"),
	                     mesh(13, "Curtain_Red"), mesh(14, "Lion_Head")};

	Item lights;
	lights.id = 3;
	lights.label = "Lights";
	lights.isHeader = true;
	lights.children = {light(20, "Sun"), light(21, "Point_Hall"), light(22, "Spot_Entrance")};

	Item terrain;
	terrain.id = 4;
	terrain.label = "Terrain";
	terrain.icon = TUCANO_ICON_TERRAIN;
	terrain.iconColor = 0xFF50C878;

	scene.children = {geometry, lights, terrain};
	return {scene};
}

State& state() {
	static State s;
	return s;
}

void drawTypography() {
	if (!beginGroupBox("Typography")) return;

	// The four sizes in the four weights: this is the grid a panel author picks from.
	const Font faces[] = {Font::Tiny,   Font::TinyBold,   Font::Small,  Font::SmallBold,
	                      Font::Medium, Font::MediumBold, Font::Large,  Font::LargeBold};
	const char* names[] = {"Tiny 12",   "Tiny Bold",   "Small 14",  "Small Bold",
	                       "Medium 16", "Medium Bold", "Large 20",  "Large Bold"};
	for (int i = 0; i < 8; ++i) {
		textFont(faces[i], TUCANO_ICON_FORMAT_FONT "  %s — Tucano Engine", names[i]);
	}

	ImGui::Spacing();
	textColored(Style::kAccent0, TUCANO_ICON_STAR "  Accent");
	textColored(Style::kAxisX, "X axis");
	ImGui::SameLine();
	textColored(Style::kAxisY, "Y axis");
	ImGui::SameLine();
	textColored(Style::kAxisZ, "Z axis");

	ImGui::TextDisabled("Disabled text");
	textEllipsis("A label far too long for the space it has been given, so it must be trimmed", 220.0f);

	endGroupBox();
}

void drawButtons() {
	if (!beginGroupBox("Buttons")) return;

	ImGui::Button("Standard");
	ImGui::SameLine();
	colorButton(Style::kAccent2, "Accent");
	ImGui::SameLine();
	colorButton(0xFF2020C0, "Danger");
	ImGui::SameLine();
	flatButton("Flat");

	// Icon buttons at a fixed width, so a toolbar row lines up regardless of glyph.
	iconButton(TUCANO_ICON_CONTENT_SAVE, "Save");
	ImGui::SameLine();
	iconButton(TUCANO_ICON_FOLDER_OPEN, "Open");
	ImGui::SameLine();
	iconButton(TUCANO_ICON_DELETE, "Delete", 0xFF4040E0);
	sameLineSeparator();
	flatIconButton(TUCANO_ICON_UNDO, "Undo");
	ImGui::SameLine();
	flatIconButton(TUCANO_ICON_REDO, "Redo");

	iconLabelButton(TUCANO_ICON_CHECK_CIRCLE, "Valid", 0xFF40C040);
	ImGui::SameLine();
	iconLabelButton(TUCANO_ICON_ALERT_CIRCLE, "Error", 0xFF4040E0);

	endGroupBox();
}

void drawToggles() {
	if (!beginGroupBox("Toggles")) return;
	State& s = state();

	toggleButton("Wireframe", s.toggleA);
	ImGui::SameLine();
	toggleButton("Gizmos", s.toggleB);
	ImGui::SameLine();
	toggleIconButton(TUCANO_ICON_EYE, TUCANO_ICON_EYE_OFF, s.visible, "Visibility");

	triStateCheckbox("Tri-state (multi-selection)", s.triState);
	ImGui::SameLine();
	helpMarker("Below zero means mixed: the selected objects disagree on this value.");

	endGroupBox();
}

void drawInputs() {
	if (!beginGroupBox("Inputs")) return;
	State& s = state();

	ImGui::TextUnformatted("Text with clear");
	inputTextWithClear("asset", s.textValue, "Asset name...", 320.0f);

	ImGui::TextUnformatted("Filter");
	s.filter.draw("##galleryFilter", 320.0f);
	ImGui::SameLine();
	ImGui::TextDisabled(s.filter.empty() ? "(no query)" : "filtering");

	ImGui::SetNextItemWidth(320.0f);
	ImGui::SliderFloat("Roughness", &s.slider, 0.0f, 1.0f);

	ImGui::SetNextItemWidth(320.0f);
	const char* items[] = {"Off", "Low", "Medium", "High", "Ultra"};
	ImGui::Combo("Quality", &s.combo, items, IM_ARRAYSIZE(items));

	endGroupBox();
}

void drawFeedback() {
	if (!beginGroupBox("Feedback")) return;

	// Toasts: one button per severity, so the colours and the icons can be compared side by side.
	if (colorButton(Style::kGray4, TUCANO_ICON_INFORMATION "  Info")) {
		notifyInfo("Streaming %d cells around the camera.", 42);
	}
	ImGui::SameLine();
	if (colorButton(Style::kGray4, TUCANO_ICON_CHECK_CIRCLE "  Success")) {
		notifySuccess("Imported Sponza.gltf (25 materials, 69 textures).");
	}
	ImGui::SameLine();
	if (colorButton(Style::kGray4, TUCANO_ICON_ALERT "  Warning")) {
		notifyWarning("Mesh has no tangents; generating them at load time.");
	}
	ImGui::SameLine();
	if (colorButton(Style::kGray4, TUCANO_ICON_ALERT_CIRCLE "  Error")) {
		notifyError("Shader compilation failed: DeferredLighting.hlsl(214).");
	}
	ImGui::TextDisabled("%zu toast(s) ativos", notificationCount());

	ImGui::Spacing();
	spinner("gallerySpinner");
	ImGui::SameLine();
	ImGui::TextUnformatted("Working...");

	ImGui::ProgressBar(0.62f, ImVec2(320.0f, 0.0f));

	ImGui::Button("Hover me for a tooltip");
	itemTooltip("Tooltips wait %.1fs before appearing, so crossing a dense panel does not flicker.",
	            Style::kTooltipDelay);

	endGroupBox();
}

void drawTree() {
	if (!beginGroupBox("Tree (Outliner / Content Browser base)")) return;
	State& s = state();
	if (!s.treeBuilt) {
		s.tree.setRoot(buildSampleTree());
		s.tree.select(11);
		s.treeBuilt = true;
	}

	s.treeFilter.setHint("Filter objects...");
	s.treeFilter.draw("##treeFilter", 320.0f);

	ImGui::BeginChild("##treeChild", ImVec2(0.0f, 220.0f), ImGuiChildFlags_Borders);
	s.tree.draw(&s.treeFilter);
	ImGui::EndChild();

	ImGui::TextDisabled("%zu selecionado(s) — F2 renomeia, Ctrl/Shift multi-seleciona",
	                    s.tree.selection().size());

	endGroupBox();
}

void drawCurve() {
	if (!beginGroupBox("Curve")) return;
	State& s = state();
	if (!s.curveBuilt) {
		// An ease-in/out, the shape most settings start from.
		s.curve = Curve({{0.0f, 0.0f}, {0.35f, 0.08f}, {0.7f, 0.85f}, {1.0f, 1.0f}});
		s.curveBuilt = true;
	}

	curveEditor("gallery", s.curve, -1.0f, 150.0f);
	ImGui::TextDisabled("Arraste um ponto · duplo clique adiciona · botao direito remove");
	ImGui::TextDisabled("f(0.25) = %.3f   f(0.50) = %.3f   f(0.75) = %.3f", s.curve.evaluate(0.25f),
	                    s.curve.evaluate(0.5f), s.curve.evaluate(0.75f));

	endGroupBox();
}

void drawPropertyGrid() {
	if (!beginGroupBox("Property grid (generated)")) return;
	State& s = state();
	if (!s.gridBound) {
		s.grid.setUndoStack(&s.undo);
		s.gridBound = true;
	}

	ImGui::TextDisabled("WaterParams — 29 campos, zero linhas de UI escritas para ele");
	s.grid.drawFilterBox(320.0f);
	ImGui::BeginChild("##gridChild", ImVec2(0.0f, 260.0f), ImGuiChildFlags_Borders);
	s.grid.draw(s.water);
	ImGui::EndChild();
	ImGui::TextDisabled("undo: %zu   %s", s.undo.undoDepth(),
	                    s.undo.canUndo() ? s.undo.undoName().c_str() : "");

	endGroupBox();
}

void drawPickers() {
	if (!beginGroupBox("Pickers")) return;
	State& s = state();
	if (!s.pickersBound) {
		s.meshPicker.setRoot("Assets");
		s.meshPicker.setKind(AssetPicker::Kind::Mesh);
		s.hdriPicker.setRoot(TUCANO_ENGINE_ASSETS_DIR);
		s.hdriPicker.setKind(AssetPicker::Kind::Hdri);
		s.pickersBound = true;
	}

	// Real directories, not a mock list: a picker that cannot find the project's own assets is
	// broken in the only way that matters, and a fake list would hide it.
	ImGui::TextDisabled("Mesh — %zu sob Assets/", s.meshPicker.candidates().size());
	s.meshPicker.draw("##galleryMesh");

	ImGui::TextDisabled("HDRI — %zu sob EngineAssets/", s.hdriPicker.candidates().size());
	s.hdriPicker.draw("##galleryHdri");

	ImGui::TextDisabled("Tipo registrado — %zu no TypeRegistry",
	                    s.typePicker.candidates().size());
	s.typePicker.draw("##galleryType");

	endGroupBox();
}

void drawPalette() {
	if (!beginGroupBox("Palette", false)) return;

	const Color greys[] = {Style::kGray0, Style::kGray1, Style::kGray2, Style::kGray3, Style::kGray4,
	                       Style::kGray5, Style::kGray6, Style::kGray7, Style::kGray8, Style::kGray9};
	for (int i = 0; i < 10; ++i) {
		if (i > 0) ImGui::SameLine();
		ImGui::PushID(i);
		colorButton(greys[i], "##grey", 40.0f, 26.0f);
		itemTooltip("Gray%d", i);
		ImGui::PopID();
	}

	const Color accents[] = {Style::kAccent0, Style::kAccent1, Style::kAccent2};
	for (int i = 0; i < 3; ++i) {
		if (i > 0) ImGui::SameLine();
		ImGui::PushID(100 + i);
		colorButton(accents[i], "##accent", 40.0f, 26.0f);
		itemTooltip("Accent%d", i);
		ImGui::PopID();
	}

	endGroupBox();
}

} // namespace

void drawGallery(bool* open) {
	ImGui::SetNextWindowSize(ImVec2(1000.0f, 900.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("UI Gallery", open)) {
		ImGui::End();
		return;
	}

	sectionHeader(TUCANO_ICON_PALETTE "  Tucano Editor UI");

	// Two columns: the whole surface has to be comparable at a glance, and one column makes the
	// lower half invisible without scrolling — which is exactly how a widget rots unnoticed.
	if (ImGui::BeginTable("##galleryColumns", 2, ImGuiTableFlags_SizingStretchSame)) {
		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		drawTypography();
		drawButtons();
		drawToggles();
		drawPalette();
		drawPropertyGrid();

		ImGui::TableSetColumnIndex(1);
		drawInputs();
		drawPickers();
		drawFeedback();
		drawCurve();
		drawTree();

		ImGui::EndTable();
	}

	ImGui::End();
}

} // namespace tucano::editor::ui

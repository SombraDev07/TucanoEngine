#pragma once

#include "Editor/UI/Style.h"

#include <cstddef>
#include <vector>

// Editable curve — the control for anything that varies over a parameter rather than sitting at one
// value: an animation ease, a sky gradient over the day, vegetation density against slope.
//
// Derived from Esoterica (MIT) — Code/Base/Imgui/ImguiCurveEditor.{h,cpp}
//
// A slider answers "how much"; a curve answers "how much, when". Tuning a day-night cycle or a
// falloff with four sliders is guesswork — the shape is the thing being edited, so it has to be
// visible while it is edited.
//
//   ui::Curve density;                     // owned by whatever is being tuned
//   ui::curveEditor("Density x Slope", density, ImVec2(0, 160));
//   const float d = density.evaluate(slope01);
//
// Evaluation is independent of the editor, so runtime code can sample a curve authored here without
// any UI in the build.

namespace tucano::editor::ui {

class Curve {
public:
	struct Point {
		float x = 0.0f; // parameter, normalised 0..1
		float y = 0.0f; // value, normalised 0..1
	};

	Curve();
	// Two points minimum, sorted by x — the invariant everything else relies on.
	explicit Curve(std::vector<Point> points);

	// Cubic-smoothstep between neighbours: continuous where a linear ramp would show a crease, and
	// cheap enough to call per-instance.
	float evaluate(float x) const;

	// Returns the index of the inserted point (points stay sorted by x).
	size_t addPoint(float x, float y);
	void removePoint(size_t index);
	// Moves a point, clamping it between its neighbours so the curve cannot fold over itself.
	void movePoint(size_t index, float x, float y);

	const std::vector<Point>& points() const { return m_points; }
	size_t size() const { return m_points.size(); }
	void reset();

private:
	void sort();
	std::vector<Point> m_points;
};

// Draws an editable curve. Returns true when the curve changed this frame.
// `height` <= 0 picks a default proportional to the current font.
bool curveEditor(const char* id, Curve& curve, float width = -1.0f, float height = 0.0f,
                 Color lineColor = Style::kAccent0);

} // namespace tucano::editor::ui

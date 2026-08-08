#include "Editor/UI/CurveEditor.h"
#include "Editor/UI/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace tucano::editor::ui {
namespace {

constexpr float kHandleRadius = 5.0f;
constexpr float kGrabRadius = 9.0f; // forgiving hit area: 5px handles are hard to grab
constexpr int kCurveSegments = 96;

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

} // namespace

Curve::Curve() : m_points{{0.0f, 0.0f}, {1.0f, 1.0f}} {}

Curve::Curve(std::vector<Point> points) : m_points(std::move(points)) {
	if (m_points.size() < 2) {
		m_points = {{0.0f, 0.0f}, {1.0f, 1.0f}};
	}
	sort();
}

void Curve::sort() {
	std::sort(m_points.begin(), m_points.end(),
	          [](const Point& a, const Point& b) { return a.x < b.x; });
}

void Curve::reset() { m_points = {{0.0f, 0.0f}, {1.0f, 1.0f}}; }

float Curve::evaluate(float x) const {
	if (m_points.empty()) return 0.0f;
	x = clamp01(x);
	if (x <= m_points.front().x) return m_points.front().y;
	if (x >= m_points.back().x) return m_points.back().y;

	for (size_t i = 1; i < m_points.size(); ++i) {
		const Point& b = m_points[i];
		if (x > b.x) continue;
		const Point& a = m_points[i - 1];
		const float span = b.x - a.x;
		if (span <= 0.0f) return b.y;
		const float t = (x - a.x) / span;
		// Smoothstep rather than a straight lerp: a linear ramp between control points shows a
		// visible crease at every point, which reads as an artefact when the curve drives lighting.
		const float smooth = t * t * (3.0f - 2.0f * t);
		return a.y + (b.y - a.y) * smooth;
	}
	return m_points.back().y;
}

size_t Curve::addPoint(float x, float y) {
	m_points.push_back({clamp01(x), clamp01(y)});
	sort();
	for (size_t i = 0; i < m_points.size(); ++i) {
		if (m_points[i].x == clamp01(x)) return i;
	}
	return m_points.size() - 1;
}

void Curve::removePoint(size_t index) {
	// The endpoints define the domain; removing them would leave the curve undefined at 0 or 1.
	if (index >= m_points.size() || m_points.size() <= 2) return;
	if (index == 0 || index == m_points.size() - 1) return;
	m_points.erase(m_points.begin() + static_cast<ptrdiff_t>(index));
}

void Curve::movePoint(size_t index, float x, float y) {
	if (index >= m_points.size()) return;
	const bool isFirst = index == 0;
	const bool isLast = index == m_points.size() - 1;

	// Endpoints keep their x so the domain stays 0..1; interior points are clamped between their
	// neighbours so dragging one past another cannot fold the curve.
	float newX = m_points[index].x;
	if (!isFirst && !isLast) {
		const float lo = m_points[index - 1].x + 0.001f;
		const float hi = m_points[index + 1].x - 0.001f;
		newX = std::clamp(clamp01(x), lo, hi);
	}
	m_points[index] = {newX, clamp01(y)};
}

bool curveEditor(const char* id, Curve& curve, float width, float height, Color lineColor) {
	ImGui::PushID(id);

	const ImGuiStyle& style = ImGui::GetStyle();
	// Clamped: a docked window can report zero available space on the frame it is being laid out,
	// and InvisibleButton asserts on a zero-sized item. Drawing something tiny for one frame beats
	// taking down the editor.
	const float w = std::max(width > 0.0f ? width : ImGui::GetContentRegionAvail().x, 8.0f);
	const float h = std::max(height > 0.0f ? height : ImGui::GetFontSize() * 8.0f, 8.0f);

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImVec2 size(w, h);
	ImGui::InvisibleButton("##canvas", size);
	const bool hovered = ImGui::IsItemHovered();

	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 bottomRight(origin.x + size.x, origin.y + size.y);

	draw->AddRectFilled(origin, bottomRight, Style::kGray9, style.FrameRounding);
	draw->AddRect(origin, bottomRight, Style::kGray3, style.FrameRounding);

	// Grid: quarters. Enough to judge a shape without turning into graph paper.
	for (int i = 1; i < 4; ++i) {
		const float t = static_cast<float>(i) / 4.0f;
		draw->AddLine(ImVec2(origin.x + size.x * t, origin.y),
		              ImVec2(origin.x + size.x * t, bottomRight.y), Style::kGray7);
		draw->AddLine(ImVec2(origin.x, origin.y + size.y * t),
		              ImVec2(bottomRight.x, origin.y + size.y * t), Style::kGray7);
	}

	// Curve space is y-up; screen space is y-down.
	const auto toScreen = [&](float cx, float cy) {
		return ImVec2(origin.x + cx * size.x, origin.y + (1.0f - cy) * size.y);
	};
	const auto toCurve = [&](const ImVec2& p) {
		return Curve::Point{(p.x - origin.x) / size.x, 1.0f - (p.y - origin.y) / size.y};
	};

	draw->PathClear();
	for (int i = 0; i <= kCurveSegments; ++i) {
		const float x = static_cast<float>(i) / kCurveSegments;
		draw->PathLineTo(toScreen(x, curve.evaluate(x)));
	}
	draw->PathStroke(lineColor, 0, 2.0f);

	bool changed = false;

	// Which handle is being dragged has to survive between frames, or the point is dropped the
	// moment the cursor leaves its radius.
	static const char* s_activeId = nullptr;
	static size_t s_activeIndex = 0;
	const bool isActiveEditor = s_activeId == id;

	size_t hoveredIndex = static_cast<size_t>(-1);
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	for (size_t i = 0; i < curve.size(); ++i) {
		const Curve::Point& p = curve.points()[i];
		const ImVec2 sp = toScreen(p.x, p.y);
		const float dx = mouse.x - sp.x;
		const float dy = mouse.y - sp.y;
		if (dx * dx + dy * dy <= kGrabRadius * kGrabRadius) {
			hoveredIndex = i;
			break;
		}
	}

	if (hovered && hoveredIndex != static_cast<size_t>(-1)) {
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			s_activeId = id;
			s_activeIndex = hoveredIndex;
		}
		// Right-click removes; endpoints refuse inside removePoint().
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
			curve.removePoint(hoveredIndex);
			changed = true;
		}
	} else if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		// Double-click on empty canvas adds a point where the cursor is.
		const Curve::Point p = toCurve(mouse);
		s_activeIndex = curve.addPoint(p.x, p.y);
		s_activeId = id;
		changed = true;
	}

	if (isActiveEditor && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		const Curve::Point p = toCurve(mouse);
		curve.movePoint(s_activeIndex, p.x, p.y);
		changed = true;
	} else if (isActiveEditor && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		s_activeId = nullptr;
	}

	for (size_t i = 0; i < curve.size(); ++i) {
		const Curve::Point& p = curve.points()[i];
		const ImVec2 sp = toScreen(p.x, p.y);
		const bool active = isActiveEditor && s_activeIndex == i;
		const bool hot = i == hoveredIndex || active;
		draw->AddCircleFilled(sp, hot ? kHandleRadius + 1.5f : kHandleRadius,
		                      hot ? Style::kText : lineColor);
		draw->AddCircle(sp, hot ? kHandleRadius + 1.5f : kHandleRadius, Style::kGray9, 0, 1.5f);
	}

	if (hovered && hoveredIndex != static_cast<size_t>(-1)) {
		const Curve::Point& p = curve.points()[hoveredIndex];
		ImGui::SetTooltip("%.3f, %.3f", p.x, p.y);
	}

	ImGui::PopID();
	return changed;
}

} // namespace tucano::editor::ui

#include "Editor/UI/Notifications.h"
#include "Editor/UI/Fonts.h"
#include "Editor/UI/Icons.h"
#include "Editor/UI/Style.h"

#include <imgui.h>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace tucano::editor::ui {
namespace {

constexpr float kFadeSeconds = 0.18f;
constexpr float kPadding = 12.0f;      // gap from the viewport edge
constexpr float kStackSpacing = 8.0f;  // gap between stacked toasts
constexpr float kWidth = 340.0f;

float g_lifetime = 3.5f;

struct Toast {
	NotificationType type;
	std::string message;
	double postedAt;
	float lifetime;
};

std::vector<Toast> g_toasts;

// Errors linger: missing one costs more than missing a "saved".
float lifetimeFor(NotificationType type) {
	return type == NotificationType::Error ? g_lifetime * 2.0f : g_lifetime;
}

Color colorFor(NotificationType type) {
	switch (type) {
		case NotificationType::Success: return 0xFF50C878;
		case NotificationType::Warning: return 0xFF3CC8F0;
		case NotificationType::Error:   return 0xFF4040E8;
		case NotificationType::Info:
		default:                        return Style::kAccent0;
	}
}

const char* iconFor(NotificationType type) {
	switch (type) {
		case NotificationType::Success: return TUCANO_ICON_CHECK_CIRCLE;
		case NotificationType::Warning: return TUCANO_ICON_ALERT;
		case NotificationType::Error:   return TUCANO_ICON_ALERT_CIRCLE;
		case NotificationType::Info:
		default:                        return TUCANO_ICON_INFORMATION;
	}
}

// 0 while fading in, 1 while resting, back to 0 while fading out.
float opacityOf(const Toast& t, double now) {
	const float age = static_cast<float>(now - t.postedAt);
	if (age < kFadeSeconds) {
		return age / kFadeSeconds;
	}
	const float restEnd = kFadeSeconds + t.lifetime;
	if (age < restEnd) {
		return 1.0f;
	}
	return 1.0f - (age - restEnd) / kFadeSeconds;
}

bool expired(const Toast& t, double now) {
	return static_cast<float>(now - t.postedAt) > kFadeSeconds + t.lifetime + kFadeSeconds;
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

void post(NotificationType type, const char* fmt, va_list args) {
	Toast t;
	t.type = type;
	t.message = formatV(fmt, args);
	// GetTime() needs a context; without one the toast would sit at age 0 forever.
	t.postedAt = ImGui::GetCurrentContext() != nullptr ? ImGui::GetTime() : 0.0;
	t.lifetime = lifetimeFor(type);
	g_toasts.push_back(std::move(t));
}

} // namespace

void notify(NotificationType type, const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	post(type, fmt, args);
	va_end(args);
}

void notifyInfo(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	post(NotificationType::Info, fmt, args);
	va_end(args);
}

void notifySuccess(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	post(NotificationType::Success, fmt, args);
	va_end(args);
}

void notifyWarning(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	post(NotificationType::Warning, fmt, args);
	va_end(args);
}

void notifyError(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	post(NotificationType::Error, fmt, args);
	va_end(args);
}

size_t notificationCount() { return g_toasts.size(); }

void clearNotifications() { g_toasts.clear(); }

void setNotificationLifetime(float seconds) {
	if (seconds > 0.0f) g_lifetime = seconds;
}

void drawNotifications() {
	if (ImGui::GetCurrentContext() == nullptr || g_toasts.empty()) {
		return;
	}
	const double now = ImGui::GetTime();

	// Retire first, so a toast that expired this frame does not claim a slot in the stack and make
	// the ones below it jump.
	for (size_t i = g_toasts.size(); i-- > 0;) {
		if (expired(g_toasts[i], now)) {
			g_toasts.erase(g_toasts.begin() + static_cast<ptrdiff_t>(i));
		}
	}
	if (g_toasts.empty()) return;

	const ImGuiViewport* vp = ImGui::GetMainViewport();
	// Bottom-right, growing upwards: the newest toast is nearest the corner the eye returns to, and
	// older ones drift away rather than shoving the new one around.
	float y = vp->WorkPos.y + vp->WorkSize.y - kPadding;

	for (size_t i = g_toasts.size(); i-- > 0;) {
		const Toast& t = g_toasts[i];
		const float opacity = opacityOf(t, now);
		if (opacity <= 0.0f) continue;

		ImGui::SetNextWindowBgAlpha(opacity * 0.94f);
		ImGui::SetNextWindowSizeConstraints(ImVec2(kWidth, 0.0f), ImVec2(kWidth, FLT_MAX));

		const std::string id = "##toast" + std::to_string(i);
		// Measured after the fact, so the first frame of a toast is positioned from an estimate and
		// settles immediately — cheaper than a two-pass layout for something this short-lived.
		ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - kPadding, y), ImGuiCond_Always,
		                        ImVec2(1.0f, 1.0f));

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
		ImGui::PushStyleColor(ImGuiCol_Border, toImVec4(withAlpha(colorFor(t.type), opacity * 0.8f)));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, toImVec4(Style::kGray8));

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
		                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
		                               ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
		                               ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoInputs;
		if (ImGui::Begin(id.c_str(), nullptr, flags)) {
			{
				ScopedFont f(Font::MediumBold);
				ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(withAlpha(colorFor(t.type), opacity)));
				ImGui::TextUnformatted(iconFor(t.type));
				ImGui::PopStyleColor();
			}
			ImGui::SameLine(0, 8.0f);
			ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(withAlpha(Style::kText, opacity)));
			ImGui::PushTextWrapPos(kWidth - 44.0f);
			ImGui::TextUnformatted(t.message.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();

			y -= ImGui::GetWindowSize().y + kStackSpacing;
		}
		ImGui::End();

		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(2);
	}
}

} // namespace tucano::editor::ui

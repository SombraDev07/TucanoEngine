#pragma once

#include <cstddef>

// Toast notifications — transient messages that do not steal focus.
//
// Derived from Esoterica (MIT) — Code/Base/Imgui/ImguiXNotifications.{h,cpp}
//
// The alternative to toasts is a modal (interrupts work for something that needs no answer) or the
// log (nobody is looking at it). A toast is for "the thing you asked for happened": an asset
// imported, a save failed, a shader recompiled. It fades in, sits, fades out.
//
// Posting is decoupled from drawing on purpose — engine code deep in a load path can call notify()
// without knowing anything about the frame it lands in.
//
//   ui::notifySuccess("Imported %s", path.c_str());
//
// The host calls drawNotifications() once per frame, after its panels, so toasts stack above them.

namespace tucano::editor::ui {

enum class NotificationType : unsigned char { Info, Success, Warning, Error };

// Posts a toast. Safe from anywhere; the message is copied.
void notify(NotificationType type, const char* fmt, ...);
void notifyInfo(const char* fmt, ...);
void notifySuccess(const char* fmt, ...);
void notifyWarning(const char* fmt, ...);
void notifyError(const char* fmt, ...);

// Draws every live toast and retires the expired ones. Call once per frame.
void drawNotifications();

// Live toasts, including those still fading out — for gates and for tools that want to know whether
// anything is on screen.
size_t notificationCount();

void clearNotifications();

// Seconds a toast stays fully opaque, before its fade-out. Errors default to longer, since missing
// one costs more than missing a success.
void setNotificationLifetime(float seconds);

} // namespace tucano::editor::ui

#pragma once

// Borderless window chrome — the editor draws its own title bar instead of wearing the OS one.
//
// Derived from Esoterica (MIT) — Code/Base/Application/Platform/Application_Win32.cpp
//
// This is the detail that most separates "a tool" from "a debug overlay": the menu, the document
// name and the window buttons live in one strip the application controls, instead of a grey Windows
// caption sitting above the app's own UI.
//
// The naive way — an undecorated window — throws away snap, the drop shadow, edge resize and the
// maximise animation, and then those get reimplemented badly. Instead the window keeps its normal
// Win32 style and WM_NCCALCSIZE removes only the *visual* frame, so everything Windows does for a
// real window keeps working. WM_NCHITTEST then tells Windows which part of our own client area
// behaves like a caption.
//
// The ImGui side has to report two things every frame: how tall the bar it drew is, and whether the
// cursor is over something clickable in it — otherwise dragging starts when the user meant to open
// a menu.

namespace tucano::editor {

class WindowChrome {
public:
	~WindowChrome();

	// Subclasses the window. Returns false if it could not be installed, in which case the caller
	// keeps the ordinary OS title bar and nothing else changes.
	bool install(void* hwnd);
	void shutdown();
	bool installed() const { return m_originalProc != nullptr; }

	// Height of the title bar the UI drew, in pixels. Everything above it drags the window.
	void setTitleBarHeight(float height) { m_titleBarHeight = height; }

	// Set when the cursor is over a menu or a window button, so that area does not start a drag.
	void setInteractiveHovered(bool hovered) { m_interactiveHovered = hovered; }

	bool isMaximized() const;
	void minimize();
	void toggleMaximize();
	void requestClose();

private:
#ifdef _WIN32
	static long long __stdcall wndProc(void* hwnd, unsigned int msg, unsigned long long wParam,
	                                   long long lParam);
	long long handle(void* hwnd, unsigned int msg, unsigned long long wParam, long long lParam,
	                 bool& handled);
#endif

	void* m_hwnd = nullptr;
	void* m_originalProc = nullptr;
	float m_titleBarHeight = 0.0f;
	bool m_interactiveHovered = false;
};

} // namespace tucano::editor

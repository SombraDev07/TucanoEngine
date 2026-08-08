#include "Editor/WindowChrome.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h> // GET_X_LPARAM

#include <algorithm>

namespace tucano::editor {
namespace {

// One editor, one window. A map would be the general answer, but a global here keeps the window
// procedure — which runs on every message — free of a lookup.
WindowChrome* g_chrome = nullptr;

// Width of the invisible resize band around the window, matching what Windows 10+ uses.
int resizeBorderX() { return GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER); }
int resizeBorderY() { return GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER); }

bool windowIsMaximized(HWND hwnd) {
	WINDOWPLACEMENT placement{};
	placement.length = sizeof(placement);
	if (!GetWindowPlacement(hwnd, &placement)) return false;
	return placement.showCmd == SW_SHOWMAXIMIZED;
}

} // namespace

WindowChrome::~WindowChrome() { shutdown(); }

bool WindowChrome::install(void* hwnd) {
	if (hwnd == nullptr || m_originalProc != nullptr) return false;

	m_hwnd = hwnd;
	g_chrome = this;
	m_originalProc = reinterpret_cast<void*>(
	    SetWindowLongPtrW(static_cast<HWND>(hwnd), GWLP_WNDPROC,
	                      reinterpret_cast<LONG_PTR>(&WindowChrome::wndProc)));
	if (m_originalProc == nullptr) {
		g_chrome = nullptr;
		m_hwnd = nullptr;
		return false;
	}

	// The frame is only recalculated when Windows is told to; without this the old caption stays on
	// screen until the first resize.
	SetWindowPos(static_cast<HWND>(hwnd), nullptr, 0, 0, 0, 0,
	             SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	return true;
}

void WindowChrome::shutdown() {
	if (m_originalProc != nullptr && m_hwnd != nullptr) {
		SetWindowLongPtrW(static_cast<HWND>(m_hwnd), GWLP_WNDPROC,
		                  reinterpret_cast<LONG_PTR>(m_originalProc));
		SetWindowPos(static_cast<HWND>(m_hwnd), nullptr, 0, 0, 0, 0,
		             SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
	m_originalProc = nullptr;
	m_hwnd = nullptr;
	if (g_chrome == this) g_chrome = nullptr;
}

bool WindowChrome::isMaximized() const {
	return m_hwnd != nullptr && windowIsMaximized(static_cast<HWND>(m_hwnd));
}

void WindowChrome::minimize() {
	if (m_hwnd != nullptr) ShowWindow(static_cast<HWND>(m_hwnd), SW_MINIMIZE);
}

void WindowChrome::toggleMaximize() {
	if (m_hwnd == nullptr) return;
	ShowWindow(static_cast<HWND>(m_hwnd), isMaximized() ? SW_RESTORE : SW_MAXIMIZE);
}

void WindowChrome::requestClose() {
	if (m_hwnd != nullptr) PostMessageW(static_cast<HWND>(m_hwnd), WM_CLOSE, 0, 0);
}

long long __stdcall WindowChrome::wndProc(void* hwnd, unsigned int msg, unsigned long long wParam,
                                          long long lParam) {
	if (g_chrome != nullptr) {
		bool handled = false;
		const long long result = g_chrome->handle(hwnd, msg, wParam, lParam, handled);
		if (handled) return result;
		return CallWindowProcW(reinterpret_cast<WNDPROC>(g_chrome->m_originalProc),
		                       static_cast<HWND>(hwnd), msg, static_cast<WPARAM>(wParam),
		                       static_cast<LPARAM>(lParam));
	}
	return DefWindowProcW(static_cast<HWND>(hwnd), msg, static_cast<WPARAM>(wParam),
	                      static_cast<LPARAM>(lParam));
}

long long WindowChrome::handle(void* hwndRaw, unsigned int msg, unsigned long long wParam,
                               long long lParam, bool& handled) {
	HWND hwnd = static_cast<HWND>(hwndRaw);

	switch (msg) {
		case WM_NCCALCSIZE: {
			if (wParam != TRUE) break;
			handled = true;
			auto& params = *reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);

			if (windowIsMaximized(hwnd)) {
				// A maximised window's rect extends past the monitor by the frame thickness. Left as
				// is, the top of our own title bar would sit off-screen and the taskbar would be
				// covered.
				HMONITOR monitor = MonitorFromRect(&params.rgrc[0], MONITOR_DEFAULTTONEAREST);
				MONITORINFO info{};
				info.cbSize = sizeof(info);
				if (monitor != nullptr && GetMonitorInfoW(monitor, &info)) {
					params.rgrc[0] = info.rcWork;
				}
			} else {
				// Give back the caption but keep a sliver at the edges: that band is what
				// WM_NCHITTEST turns into resize handles, and it is why edge resizing still works.
				params.rgrc[0].left += resizeBorderX();
				params.rgrc[0].right -= resizeBorderX();
				params.rgrc[0].bottom -= resizeBorderY();
				// The top is deliberately not inset: inseting it leaves a visible line of old frame.
			}
			return 0;
		}

		case WM_NCHITTEST: {
			handled = true;
			const POINT cursor{GET_X_LPARAM(static_cast<LPARAM>(lParam)),
			                   GET_Y_LPARAM(static_cast<LPARAM>(lParam))};
			RECT window{};
			GetWindowRect(hwnd, &window);

			const int borderX = resizeBorderX();
			const int borderY = resizeBorderY();

			// Edges first: resize wins over drag, or the window can never be resized from the top.
			if (!windowIsMaximized(hwnd)) {
				const bool left = cursor.x < window.left + borderX;
				const bool right = cursor.x >= window.right - borderX;
				const bool top = cursor.y < window.top + borderY;
				const bool bottom = cursor.y >= window.bottom - borderY;

				if (top && left) return HTTOPLEFT;
				if (top && right) return HTTOPRIGHT;
				if (bottom && left) return HTBOTTOMLEFT;
				if (bottom && right) return HTBOTTOMRIGHT;
				if (left) return HTLEFT;
				if (right) return HTRIGHT;
				if (top) return HTTOP;
				if (bottom) return HTBOTTOM;
			}

			// Inside the bar the UI drew, and not over anything clickable: behave like a caption, so
			// Windows gives us drag, double-click-to-maximise, snap and the system menu for free.
			if (!m_interactiveHovered && m_titleBarHeight > 0.0f) {
				const int barBottom = window.top + static_cast<int>(m_titleBarHeight);
				if (cursor.y >= window.top && cursor.y < barBottom) {
					return HTCAPTION;
				}
			}
			return HTCLIENT;
		}

		default:
			break;
	}
	return 0;
}

} // namespace tucano::editor

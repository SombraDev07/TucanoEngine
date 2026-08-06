#include "Platform/Window.h"
#include "Core/Memory.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <stdexcept>

namespace tucano {

Window::Window(const WindowDesc& desc) : m_width(desc.width), m_height(desc.height) {
  core::memoryInit();

  if (!glfwInit()) {
    throw std::runtime_error("glfwInit failed");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);
  glfwWindowHint(GLFW_DECORATED, desc.decorated ? GLFW_TRUE : GLFW_FALSE);
  glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

  m_window = glfwCreateWindow(static_cast<int>(desc.width), static_cast<int>(desc.height),
                              desc.title.c_str(), nullptr, nullptr);
  if (!m_window) {
    glfwTerminate();
    throw std::runtime_error("glfwCreateWindow failed");
  }

  // The requested size is a request: GLFW shrinks the window to fit the work area (a 1080p request
  // on a 1080p desktop lands at ~1061 once the title bar is accounted for). Everything downstream —
  // swapchain extent, renderer resolution, ImGui's DisplaySize — has to agree on the real client
  // size, and the framebuffer-size callback does not fire for the initial size, so read it here.
  // Before this, ImGui rendered in framebuffer space while the swapchain used the requested size,
  // which pushed the top rows of the UI outside the presented image.
  int fbWidth = 0;
  int fbHeight = 0;
  glfwGetFramebufferSize(m_window, &fbWidth, &fbHeight);
  if (fbWidth > 0 && fbHeight > 0) {
    m_width = static_cast<uint32_t>(fbWidth);
    m_height = static_cast<uint32_t>(fbHeight);
  }

  glfwSetWindowUserPointer(m_window, this);
  glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* w, int width, int height) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
    if (!self || width <= 0 || height <= 0) {
      return;
    }
    self->m_width = static_cast<uint32_t>(width);
    self->m_height = static_cast<uint32_t>(height);
    if (self->m_resizeCb) {
      self->m_resizeCb(self->m_width, self->m_height);
    }
  });
}

Window::~Window() {
  if (m_window) {
    glfwDestroyWindow(m_window);
    m_window = nullptr;
  }
  glfwTerminate();
}

bool Window::shouldClose() const { return glfwWindowShouldClose(m_window) != 0; }

void Window::pollEvents() { glfwPollEvents(); }

void Window::pollEventsEmbedded() {
#ifdef _WIN32
  HWND hwnd = glfwGetWin32Window(m_window);
  if (!hwnd) {
    glfwPollEvents();
    return;
  }
  // PeekMessage with a window handle only returns messages for that window and its children, so
  // the host's messages stay queued for the host's own loop. DispatchMessage still routes each
  // one through GLFW's WndProc, which is what keeps GLFW's input state current.
  MSG msg;
  while (PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
#else
  glfwPollEvents();
#endif
}

void Window::setTitle(const std::string& title) { glfwSetWindowTitle(m_window, title.c_str()); }

void* Window::nativeHandle() const { return glfwGetWin32Window(m_window); }

float Window::aspect() const {
  return m_height > 0 ? static_cast<float>(m_width) / static_cast<float>(m_height) : 1.0f;
}

} // namespace tucano

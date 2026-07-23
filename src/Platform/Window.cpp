#include "Platform/Window.h"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <stdexcept>

namespace tucano {

Window::Window(const WindowDesc& desc) : m_width(desc.width), m_height(desc.height) {
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

void Window::setTitle(const std::string& title) { glfwSetWindowTitle(m_window, title.c_str()); }

void* Window::nativeHandle() const { return glfwGetWin32Window(m_window); }

float Window::aspect() const {
  return m_height > 0 ? static_cast<float>(m_width) / static_cast<float>(m_height) : 1.0f;
}

} // namespace tucano

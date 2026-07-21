#include "Platform/Input.h"

#include <GLFW/glfw3.h>
#include <unordered_map>

namespace tucano {
namespace {
std::unordered_map<GLFWwindow*, Input*> g_inputs;

void scrollCallback(GLFWwindow* w, double, double yoff) {
  if (auto it = g_inputs.find(w); it != g_inputs.end()) {
    it->second->addScroll(static_cast<float>(yoff));
  }
}
} // namespace

Input::Input(GLFWwindow* window) : m_window(window) {
  g_inputs[window] = this;
  glfwSetScrollCallback(window, scrollCallback);
}

void Input::beginFrame() {
  double x = 0.0, y = 0.0;
  glfwGetCursorPos(m_window, &x, &y);
  if (m_firstMouse) {
    m_lastX = x;
    m_lastY = y;
    m_firstMouse = false;
  }
  m_dx = static_cast<float>(x - m_lastX);
  m_dy = static_cast<float>(y - m_lastY);
  m_lastX = x;
  m_lastY = y;
}

void Input::endFrame() {
  for (int i = 0; i < 512; ++i) {
    m_keysPrev[i] = keyDown(i);
  }
  for (int i = 0; i < 8; ++i) {
    m_mousePrev[i] = mouseDown(i);
  }
  m_scrollY = 0.0f;
}

bool Input::keyDown(int key) const { return glfwGetKey(m_window, key) == GLFW_PRESS; }

bool Input::keyPressed(int key) const { return keyDown(key) && !m_keysPrev[key]; }

bool Input::mouseDown(int button) const { return glfwGetMouseButton(m_window, button) == GLFW_PRESS; }

bool Input::mousePressed(int button) const { return mouseDown(button) && !m_mousePrev[button]; }

void Input::mouseDelta(float& dx, float& dy) const {
  dx = m_dx;
  dy = m_dy;
}

void Input::mousePosition(float& x, float& y) const {
  double px = 0.0, py = 0.0;
  glfwGetCursorPos(m_window, &px, &py);
  x = static_cast<float>(px);
  y = static_cast<float>(py);
}

void Input::setCursorCaptured(bool captured) {
  m_captured = captured;
  glfwSetInputMode(m_window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
  m_firstMouse = true;
}

} // namespace tucano

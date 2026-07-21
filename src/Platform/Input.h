#pragma once

#include <cstdint>

struct GLFWwindow;

namespace tucano {

class Input {
public:
  explicit Input(GLFWwindow* window);

  void beginFrame();
  void endFrame();

  bool keyDown(int key) const;
  bool keyPressed(int key) const;
  bool mouseDown(int button) const;
  bool mousePressed(int button) const;

  void mouseDelta(float& dx, float& dy) const;
  void mousePosition(float& x, float& y) const;
  float scrollY() const { return m_scrollY; }

  void setCursorCaptured(bool captured);
  bool cursorCaptured() const { return m_captured; }
  void addScroll(float y) { m_scrollY += y; }

private:
  GLFWwindow* m_window = nullptr;
  double m_lastX = 0.0;
  double m_lastY = 0.0;
  float m_dx = 0.0f;
  float m_dy = 0.0f;
  float m_scrollY = 0.0f;
  bool m_firstMouse = true;
  bool m_captured = false;
  bool m_keysPrev[512]{};
  bool m_mousePrev[8]{};
};

} // namespace tucano

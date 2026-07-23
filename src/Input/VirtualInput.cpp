#include "Input/VirtualInput.h"

#include "Platform/Input.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace tucano::input {
namespace {

// Engine ButtonCode → GLFW key/mouse code. Returns -1 for anything GLFW has no key for.
int toGlfwKey(ButtonCode c) {
  using B = ButtonCode;
  if (c >= B::KeyA && c <= B::KeyZ) {
    return GLFW_KEY_A + (static_cast<int>(c) - static_cast<int>(B::KeyA));
  }
  if (c >= B::Key0 && c <= B::Key9) {
    return GLFW_KEY_0 + (static_cast<int>(c) - static_cast<int>(B::Key0));
  }
  if (c >= B::KeyF1 && c <= B::KeyF12) {
    return GLFW_KEY_F1 + (static_cast<int>(c) - static_cast<int>(B::KeyF1));
  }
  if (c >= B::KeyNumpad0 && c <= B::KeyNumpad9) {
    return GLFW_KEY_KP_0 + (static_cast<int>(c) - static_cast<int>(B::KeyNumpad0));
  }

  switch (c) {
    case B::KeyEscape: return GLFW_KEY_ESCAPE;
    case B::KeyTab: return GLFW_KEY_TAB;
    case B::KeyCapsLock: return GLFW_KEY_CAPS_LOCK;
    case B::KeyBackspace: return GLFW_KEY_BACKSPACE;
    case B::KeyEnter: return GLFW_KEY_ENTER;
    case B::KeySpace: return GLFW_KEY_SPACE;
    case B::KeyInsert: return GLFW_KEY_INSERT;
    case B::KeyDelete: return GLFW_KEY_DELETE;
    case B::KeyHome: return GLFW_KEY_HOME;
    case B::KeyEnd: return GLFW_KEY_END;
    case B::KeyPageUp: return GLFW_KEY_PAGE_UP;
    case B::KeyPageDown: return GLFW_KEY_PAGE_DOWN;
    case B::KeyLeft: return GLFW_KEY_LEFT;
    case B::KeyRight: return GLFW_KEY_RIGHT;
    case B::KeyUp: return GLFW_KEY_UP;
    case B::KeyDown: return GLFW_KEY_DOWN;
    case B::KeyLeftShift: return GLFW_KEY_LEFT_SHIFT;
    case B::KeyRightShift: return GLFW_KEY_RIGHT_SHIFT;
    case B::KeyLeftControl: return GLFW_KEY_LEFT_CONTROL;
    case B::KeyRightControl: return GLFW_KEY_RIGHT_CONTROL;
    case B::KeyLeftAlt: return GLFW_KEY_LEFT_ALT;
    case B::KeyRightAlt: return GLFW_KEY_RIGHT_ALT;
    case B::KeyLeftSuper: return GLFW_KEY_LEFT_SUPER;
    case B::KeyRightSuper: return GLFW_KEY_RIGHT_SUPER;
    case B::KeyMinus: return GLFW_KEY_MINUS;
    case B::KeyEquals: return GLFW_KEY_EQUAL;
    case B::KeyLeftBracket: return GLFW_KEY_LEFT_BRACKET;
    case B::KeyRightBracket: return GLFW_KEY_RIGHT_BRACKET;
    case B::KeyBackslash: return GLFW_KEY_BACKSLASH;
    case B::KeySemicolon: return GLFW_KEY_SEMICOLON;
    case B::KeyApostrophe: return GLFW_KEY_APOSTROPHE;
    case B::KeyGrave: return GLFW_KEY_GRAVE_ACCENT;
    case B::KeyComma: return GLFW_KEY_COMMA;
    case B::KeyPeriod: return GLFW_KEY_PERIOD;
    case B::KeySlash: return GLFW_KEY_SLASH;
    case B::KeyNumpadDecimal: return GLFW_KEY_KP_DECIMAL;
    case B::KeyNumpadDivide: return GLFW_KEY_KP_DIVIDE;
    case B::KeyNumpadMultiply: return GLFW_KEY_KP_MULTIPLY;
    case B::KeyNumpadSubtract: return GLFW_KEY_KP_SUBTRACT;
    case B::KeyNumpadAdd: return GLFW_KEY_KP_ADD;
    case B::KeyNumpadEnter: return GLFW_KEY_KP_ENTER;
    default: return -1;
  }
}

int toGlfwMouse(ButtonCode c) {
  switch (c) {
    case ButtonCode::MouseLeft: return GLFW_MOUSE_BUTTON_LEFT;
    case ButtonCode::MouseRight: return GLFW_MOUSE_BUTTON_RIGHT;
    case ButtonCode::MouseMiddle: return GLFW_MOUSE_BUTTON_MIDDLE;
    case ButtonCode::MouseButton4: return GLFW_MOUSE_BUTTON_4;
    case ButtonCode::MouseButton5: return GLFW_MOUSE_BUTTON_5;
    default: return -1;
  }
}

bool isMouseButton(ButtonCode c) {
  return c >= ButtonCode::MouseLeft && c <= ButtonCode::MouseButton5;
}

} // namespace

void fillSnapshotFromPlatform(const tucano::Input& in, InputSnapshot& out) {
  for (uint32_t i = 1; i < static_cast<uint32_t>(ButtonCode::Count); ++i) {
    const auto code = static_cast<ButtonCode>(i);
    if (isMouseButton(code)) {
      const int m = toGlfwMouse(code);
      out.down[i] = m >= 0 && in.mouseDown(m);
    } else {
      const int k = toGlfwKey(code);
      out.down[i] = k >= 0 && in.keyDown(k);
    }
  }

  float dx = 0.0f, dy = 0.0f;
  in.mouseDelta(dx, dy);
  out.axis[static_cast<size_t>(InputAxis::MouseX)] = dx;
  out.axis[static_cast<size_t>(InputAxis::MouseY)] = dy;
  out.axis[static_cast<size_t>(InputAxis::MouseWheel)] = in.scrollY();
}

VirtualInput::VirtualInput(InputConfiguration config) : m_config(std::move(config)) {}

void VirtualInput::setConfiguration(InputConfiguration config) {
  m_config = std::move(config);
  // Stale states would report phantom presses for bindings that no longer exist.
  m_states.clear();
  m_axisValues.clear();
}

void VirtualInput::update(const InputSnapshot& snapshot) {
  const bool shift = snapshot.down[static_cast<size_t>(ButtonCode::KeyLeftShift)] ||
                     snapshot.down[static_cast<size_t>(ButtonCode::KeyRightShift)];
  const bool ctrl = snapshot.down[static_cast<size_t>(ButtonCode::KeyLeftControl)] ||
                    snapshot.down[static_cast<size_t>(ButtonCode::KeyRightControl)];
  const bool alt = snapshot.down[static_cast<size_t>(ButtonCode::KeyLeftAlt)] ||
                   snapshot.down[static_cast<size_t>(ButtonCode::KeyRightAlt)];

  for (const auto& [name, bindings] : m_config.buttons()) {
    bool held = false;
    bool repeats = false;
    for (const auto& b : bindings) {
      const auto idx = static_cast<size_t>(b.button);
      if (idx >= static_cast<size_t>(ButtonCode::Count) || !snapshot.down[idx]) continue;

      // Every required modifier must be held; extra ones are tolerated so Shift+W still moves.
      if (hasModifier(b.modifiers, ButtonModifier::Shift) && !shift) continue;
      if (hasModifier(b.modifiers, ButtonModifier::Ctrl) && !ctrl) continue;
      if (hasModifier(b.modifiers, ButtonModifier::Alt) && !alt) continue;

      held = true;
      repeats = repeats || b.repeatable;
    }

    const ButtonState prev = m_states.count(name) ? m_states[name] : ButtonState::Off;
    const bool wasHeld = prev == ButtonState::On || prev == ButtonState::ToggledOn;

    ButtonState next;
    if (held && !wasHeld) next = ButtonState::ToggledOn;
    else if (held && repeats) next = ButtonState::ToggledOn; // repeat re-fires Down every frame
    else if (held) next = ButtonState::On;
    else if (!held && wasHeld) next = ButtonState::ToggledOff;
    else next = ButtonState::Off;

    m_states[name] = next;

    if (next == ButtonState::ToggledOn && onButtonDown) onButtonDown(name);
    if (next == ButtonState::ToggledOff && onButtonUp) onButtonUp(name);
  }

  for (const auto& [name, binding] : m_config.axes()) {
    const auto idx = static_cast<size_t>(binding.axis);
    float v = idx < static_cast<size_t>(InputAxis::Count) ? snapshot.axis[idx] : 0.0f;

    if (std::abs(v) < binding.deadZone) {
      v = 0.0f;
    } else {
      v *= binding.sensitivity;
      if (binding.invert) v = -v;
      if (binding.normalize) v = std::clamp(v, -1.0f, 1.0f);
    }
    m_axisValues[name] = v;
  }
}

bool VirtualInput::isButtonDown(const std::string& name) const {
  const auto it = m_states.find(name);
  return it != m_states.end() && it->second == ButtonState::ToggledOn;
}

bool VirtualInput::isButtonUp(const std::string& name) const {
  const auto it = m_states.find(name);
  return it != m_states.end() && it->second == ButtonState::ToggledOff;
}

bool VirtualInput::isButtonHeld(const std::string& name) const {
  const auto it = m_states.find(name);
  return it != m_states.end() &&
         (it->second == ButtonState::On || it->second == ButtonState::ToggledOn);
}

float VirtualInput::axisValue(const std::string& name) const {
  const auto it = m_axisValues.find(name);
  return it == m_axisValues.end() ? 0.0f : it->second;
}

} // namespace tucano::input

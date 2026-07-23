#include "Input/InputConfiguration.h"

namespace tucano::input {

void InputConfiguration::registerButton(const std::string& name, ButtonCode button,
                                        ButtonModifier modifiers, bool repeatable) {
  auto& list = m_buttons[name];
  // Re-registering the same physical button updates it rather than stacking duplicates.
  for (auto& b : list) {
    if (b.button == button) {
      b.modifiers = modifiers;
      b.repeatable = repeatable;
      return;
    }
  }
  list.push_back(VirtualButtonBinding{button, modifiers, repeatable});
}

void InputConfiguration::unregisterButton(const std::string& name) { m_buttons.erase(name); }

void InputConfiguration::registerAxis(const std::string& name, const VirtualAxisBinding& binding) {
  m_axes[name] = binding;
}

void InputConfiguration::unregisterAxis(const std::string& name) { m_axes.erase(name); }

const std::vector<VirtualButtonBinding>* InputConfiguration::findButton(
    const std::string& name) const {
  const auto it = m_buttons.find(name);
  return it == m_buttons.end() ? nullptr : &it->second;
}

const VirtualAxisBinding* InputConfiguration::findAxis(const std::string& name) const {
  const auto it = m_axes.find(name);
  return it == m_axes.end() ? nullptr : &it->second;
}

void InputConfiguration::clear() {
  m_buttons.clear();
  m_axes.clear();
}

InputConfiguration InputConfiguration::makeDefault() {
  InputConfiguration c;

  // Movement: WASD with arrow keys as a second binding on the same virtual names.
  c.registerButton("MoveForward", ButtonCode::KeyW);
  c.registerButton("MoveForward", ButtonCode::KeyUp);
  c.registerButton("MoveBack", ButtonCode::KeyS);
  c.registerButton("MoveBack", ButtonCode::KeyDown);
  c.registerButton("MoveLeft", ButtonCode::KeyA);
  c.registerButton("MoveLeft", ButtonCode::KeyLeft);
  c.registerButton("MoveRight", ButtonCode::KeyD);
  c.registerButton("MoveRight", ButtonCode::KeyRight);
  c.registerButton("MoveUp", ButtonCode::KeyE);
  c.registerButton("MoveDown", ButtonCode::KeyQ);

  c.registerButton("Sprint", ButtonCode::KeyLeftShift);
  c.registerButton("Jump", ButtonCode::KeySpace);
  c.registerButton("Crouch", ButtonCode::KeyLeftControl);

  c.registerButton("Fire", ButtonCode::MouseLeft);
  c.registerButton("AltFire", ButtonCode::MouseRight);
  c.registerButton("Interact", ButtonCode::KeyF);
  c.registerButton("Cancel", ButtonCode::KeyEscape);

  // Look: mouse deltas are relative, so they stay un-normalised by default.
  VirtualAxisBinding lookX;
  lookX.axis = InputAxis::MouseX;
  lookX.sensitivity = 1.0f;
  lookX.deadZone = 0.0f;
  c.registerAxis("LookX", lookX);

  VirtualAxisBinding lookY;
  lookY.axis = InputAxis::MouseY;
  lookY.sensitivity = 1.0f;
  lookY.deadZone = 0.0f;
  lookY.invert = true; // screen Y grows downward; looking up should be positive
  c.registerAxis("LookY", lookY);

  VirtualAxisBinding zoom;
  zoom.axis = InputAxis::MouseWheel;
  zoom.sensitivity = 1.0f;
  c.registerAxis("Zoom", zoom);

  return c;
}

} // namespace tucano::input

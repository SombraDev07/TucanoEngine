#pragma once

#include "Input/InputFwd.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace tucano::input {

// How a physical button triggers a virtual one.
struct VirtualButtonBinding {
  ButtonCode button = ButtonCode::Unassigned;
  ButtonModifier modifiers = ButtonModifier::None;
  // When true the virtual button keeps reporting "down" every frame while held, for things like
  // menu repeat. Off means down fires once per press.
  bool repeatable = false;
};

// How a physical axis feeds a virtual one. Ported from B3D's VirtualAxisInformation.
struct VirtualAxisBinding {
  InputAxis axis = InputAxis::MouseX;
  // Values whose magnitude falls below this read as exactly zero — kills controller/mouse jitter.
  float deadZone = 0.0001f;
  // Multiplier applied after the dead zone; higher reaches the extremes sooner.
  float sensitivity = 1.0f;
  bool invert = false;
  // Mouse axes report relative movement, not a normalised position. Enabling this clamps the
  // result into [-1, 1] so gameplay code can treat every axis the same way.
  bool normalize = false;
};

/// Maps physical input to named virtual buttons and axes, so gameplay code never mentions a key.
/// Rebinding is a matter of editing this, with nothing else aware of the change.
class InputConfiguration {
public:
  // A name may carry several bindings; any of them firing triggers the virtual button.
  void registerButton(const std::string& name, ButtonCode button,
                      ButtonModifier modifiers = ButtonModifier::None, bool repeatable = false);
  void unregisterButton(const std::string& name);

  void registerAxis(const std::string& name, const VirtualAxisBinding& binding);
  void unregisterAxis(const std::string& name);

  const std::vector<VirtualButtonBinding>* findButton(const std::string& name) const;
  const VirtualAxisBinding* findAxis(const std::string& name) const;

  const std::unordered_map<std::string, std::vector<VirtualButtonBinding>>& buttons() const {
    return m_buttons;
  }
  const std::unordered_map<std::string, VirtualAxisBinding>& axes() const { return m_axes; }

  void clear();

  /// Movement/look/action set every project needs, so a fresh runtime is usable immediately.
  static InputConfiguration makeDefault();

private:
  std::unordered_map<std::string, std::vector<VirtualButtonBinding>> m_buttons;
  std::unordered_map<std::string, VirtualAxisBinding> m_axes;
};

} // namespace tucano::input

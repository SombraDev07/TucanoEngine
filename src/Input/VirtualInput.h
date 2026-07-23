#pragma once

#include "Input/InputConfiguration.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tucano {
class Input;
}

namespace tucano::input {

/// Per-frame state of the physical devices, filled by a backend. Keeping this separate from the
/// windowing layer is what lets VirtualInput be unit-tested without a window.
struct InputSnapshot {
  bool down[static_cast<size_t>(ButtonCode::Count)]{};
  float axis[static_cast<size_t>(InputAxis::Count)]{};
};

/// Resolves physical input into named virtual buttons and axes.
/// Ported from B3DFramework's VirtualInput; the identifier interning and the Down/Up/Held state
/// machine follow the original, the storage is simplified to a single device.
class VirtualInput {
public:
  explicit VirtualInput(InputConfiguration config = InputConfiguration::makeDefault());

  void setConfiguration(InputConfiguration config);
  InputConfiguration& configuration() { return m_config; }
  const InputConfiguration& configuration() const { return m_config; }

  /// Advances the state machine. Call once per frame, after the backend fills the snapshot.
  void update(const InputSnapshot& snapshot);

  /// True only on the frame the button became held (or every frame when the binding repeats).
  bool isButtonDown(const std::string& name) const;
  /// True only on the frame the button was released.
  bool isButtonUp(const std::string& name) const;
  /// True for as long as it stays held.
  bool isButtonHeld(const std::string& name) const;

  /// Axis value after dead zone, sensitivity, inversion and optional normalisation.
  float axisValue(const std::string& name) const;

  /// Fired once per press/release, for code that would rather not poll.
  std::function<void(const std::string&)> onButtonDown;
  std::function<void(const std::string&)> onButtonUp;

private:
  enum class ButtonState : uint8_t { Off, On, ToggledOn, ToggledOff };

  InputConfiguration m_config;
  std::unordered_map<std::string, ButtonState> m_states;
  std::unordered_map<std::string, float> m_axisValues;
};

/// Fills an InputSnapshot from the GLFW-backed platform input.
/// Lives here rather than in Platform so the mapping from engine ButtonCode to GLFW keys stays in
/// one place; swapping windowing libraries means rewriting only this.
void fillSnapshotFromPlatform(const tucano::Input& platformInput, InputSnapshot& out);

} // namespace tucano::input

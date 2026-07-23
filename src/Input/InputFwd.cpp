#include "Input/InputFwd.h"

#include <cstring>

namespace tucano::input {
namespace {

// Index must line up with the ButtonCode enum; the static_assert below catches drift.
constexpr const char* kNames[] = {
    "Unassigned",
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    "Escape", "Tab", "CapsLock", "Backspace", "Enter", "Space",
    "Insert", "Delete", "Home", "End", "PageUp", "PageDown",
    "Left", "Right", "Up", "Down",
    "LeftShift", "RightShift", "LeftControl", "RightControl",
    "LeftAlt", "RightAlt", "LeftSuper", "RightSuper",
    "Minus", "Equals", "LeftBracket", "RightBracket", "Backslash",
    "Semicolon", "Apostrophe", "Grave", "Comma", "Period", "Slash",
    "Numpad0", "Numpad1", "Numpad2", "Numpad3", "Numpad4",
    "Numpad5", "Numpad6", "Numpad7", "Numpad8", "Numpad9",
    "NumpadDecimal", "NumpadDivide", "NumpadMultiply",
    "NumpadSubtract", "NumpadAdd", "NumpadEnter",
    "MouseLeft", "MouseRight", "MouseMiddle", "MouseButton4", "MouseButton5",
};

static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<size_t>(ButtonCode::Count),
              "ButtonCode names table is out of sync with the enum");

} // namespace

const char* buttonCodeName(ButtonCode code) {
  const auto i = static_cast<size_t>(code);
  return i < static_cast<size_t>(ButtonCode::Count) ? kNames[i] : "Unassigned";
}

ButtonCode buttonCodeFromName(const char* name) {
  if (!name) return ButtonCode::Unassigned;
  for (size_t i = 0; i < static_cast<size_t>(ButtonCode::Count); ++i) {
    if (std::strcmp(kNames[i], name) == 0) return static_cast<ButtonCode>(i);
  }
  return ButtonCode::Unassigned;
}

} // namespace tucano::input

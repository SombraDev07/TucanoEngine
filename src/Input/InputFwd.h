#pragma once

#include <cstdint>

namespace tucano::input {

// Engine-stable physical button identifiers, ported from B3DFramework's ButtonCode.
// Deliberately *not* GLFW's values: the C ABI and saved input configurations must keep meaning
// even if the windowing backend changes. InputBackendGLFW does the translation.
enum class ButtonCode : uint32_t {
  Unassigned = 0,

  // Letters
  KeyA, KeyB, KeyC, KeyD, KeyE, KeyF, KeyG, KeyH, KeyI, KeyJ, KeyK, KeyL, KeyM,
  KeyN, KeyO, KeyP, KeyQ, KeyR, KeyS, KeyT, KeyU, KeyV, KeyW, KeyX, KeyY, KeyZ,

  // Digits (top row)
  Key0, Key1, Key2, Key3, Key4, Key5, Key6, Key7, Key8, Key9,

  // Function keys
  KeyF1, KeyF2, KeyF3, KeyF4, KeyF5, KeyF6, KeyF7, KeyF8, KeyF9, KeyF10, KeyF11, KeyF12,

  // Navigation / editing
  KeyEscape, KeyTab, KeyCapsLock, KeyBackspace, KeyEnter, KeySpace,
  KeyInsert, KeyDelete, KeyHome, KeyEnd, KeyPageUp, KeyPageDown,
  KeyLeft, KeyRight, KeyUp, KeyDown,

  // Modifiers
  KeyLeftShift, KeyRightShift, KeyLeftControl, KeyRightControl,
  KeyLeftAlt, KeyRightAlt, KeyLeftSuper, KeyRightSuper,

  // Punctuation
  KeyMinus, KeyEquals, KeyLeftBracket, KeyRightBracket, KeyBackslash,
  KeySemicolon, KeyApostrophe, KeyGrave, KeyComma, KeyPeriod, KeySlash,

  // Numpad
  KeyNumpad0, KeyNumpad1, KeyNumpad2, KeyNumpad3, KeyNumpad4,
  KeyNumpad5, KeyNumpad6, KeyNumpad7, KeyNumpad8, KeyNumpad9,
  KeyNumpadDecimal, KeyNumpadDivide, KeyNumpadMultiply,
  KeyNumpadSubtract, KeyNumpadAdd, KeyNumpadEnter,

  // Mouse
  MouseLeft, MouseRight, MouseMiddle, MouseButton4, MouseButton5,

  Count
};

// Modifiers that must be held for a mapping to fire. Combinable.
enum class ButtonModifier : uint32_t {
  None = 0,
  Shift = 1u << 0,
  Ctrl = 1u << 1,
  Alt = 1u << 2,
};

inline ButtonModifier operator|(ButtonModifier a, ButtonModifier b) {
  return static_cast<ButtonModifier>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline bool hasModifier(ButtonModifier set, ButtonModifier flag) {
  return (static_cast<uint32_t>(set) & static_cast<uint32_t>(flag)) != 0;
}

// Physical axes a virtual axis can be driven by.
enum class InputAxis : uint32_t {
  MouseX = 0,
  MouseY,
  MouseWheel,
  Count
};

// Human-readable name for a button, for config files and UI. Never null.
const char* buttonCodeName(ButtonCode code);

// Inverse of buttonCodeName; returns Unassigned when unknown.
ButtonCode buttonCodeFromName(const char* name);

} // namespace tucano::input

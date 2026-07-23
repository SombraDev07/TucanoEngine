// Gate for the virtual input system (Phase I-0, ported from B3DFramework).
// Drives VirtualInput with synthetic snapshots so the Down/Held/Up state machine, modifiers, dead
// zone, sensitivity, inversion and normalisation are all verified without a window or a human.

#include "Input/VirtualInput.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace tucano::input;

namespace {

int g_failures = 0;

void check(const std::string& label, bool ok) {
  std::printf(ok ? "  OK   %s\n" : "  FAIL %s\n", label.c_str());
  if (!ok) ++g_failures;
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

void press(InputSnapshot& s, ButtonCode c, bool down = true) {
  s.down[static_cast<size_t>(c)] = down;
}

} // namespace

int main() {
  std::printf("-- default configuration --\n");
  {
    VirtualInput vi;
    InputSnapshot s{};

    // Nothing held: every query is false.
    vi.update(s);
    check("idle: MoveForward not held", !vi.isButtonHeld("MoveForward"));
    check("idle: MoveForward not down", !vi.isButtonDown("MoveForward"));

    // Press W: Down fires exactly once, Held stays true.
    press(s, ButtonCode::KeyW);
    vi.update(s);
    check("press W: MoveForward down", vi.isButtonDown("MoveForward"));
    check("press W: MoveForward held", vi.isButtonHeld("MoveForward"));

    vi.update(s);
    check("hold W: down is edge-only", !vi.isButtonDown("MoveForward"));
    check("hold W: still held", vi.isButtonHeld("MoveForward"));

    // Release: Up fires once.
    press(s, ButtonCode::KeyW, false);
    vi.update(s);
    check("release W: up fires", vi.isButtonUp("MoveForward"));
    check("release W: no longer held", !vi.isButtonHeld("MoveForward"));

    vi.update(s);
    check("after release: up is edge-only", !vi.isButtonUp("MoveForward"));

    // The alternate binding on the same name must work identically.
    press(s, ButtonCode::KeyUp);
    vi.update(s);
    check("arrow Up also drives MoveForward", vi.isButtonHeld("MoveForward"));
    press(s, ButtonCode::KeyUp, false);
    vi.update(s);

    // Mouse button binding.
    press(s, ButtonCode::MouseLeft);
    vi.update(s);
    check("mouse left drives Fire", vi.isButtonHeld("Fire"));
    press(s, ButtonCode::MouseLeft, false);
    vi.update(s);
  }

  std::printf("\n-- events --\n");
  {
    VirtualInput vi;
    InputSnapshot s{};
    int downCount = 0, upCount = 0;
    vi.onButtonDown = [&](const std::string& n) { if (n == "Jump") ++downCount; };
    vi.onButtonUp = [&](const std::string& n) { if (n == "Jump") ++upCount; };

    press(s, ButtonCode::KeySpace);
    vi.update(s);
    vi.update(s); // held, must not re-fire
    press(s, ButtonCode::KeySpace, false);
    vi.update(s);

    check("onButtonDown fired once", downCount == 1);
    check("onButtonUp fired once", upCount == 1);
  }

  std::printf("\n-- modifiers --\n");
  {
    InputConfiguration cfg;
    cfg.registerButton("Save", ButtonCode::KeyS, ButtonModifier::Ctrl);
    cfg.registerButton("Plain", ButtonCode::KeyS);
    VirtualInput vi(cfg);
    InputSnapshot s{};

    press(s, ButtonCode::KeyS);
    vi.update(s);
    check("S alone does not trigger Ctrl+S", !vi.isButtonHeld("Save"));
    check("S alone triggers the unmodified binding", vi.isButtonHeld("Plain"));

    press(s, ButtonCode::KeyLeftControl);
    vi.update(s);
    check("Ctrl+S triggers Save", vi.isButtonHeld("Save"));

    // Extra modifiers are tolerated, so Shift+W still moves.
    press(s, ButtonCode::KeyLeftShift);
    vi.update(s);
    check("extra modifiers do not block a binding", vi.isButtonHeld("Save"));
  }

  std::printf("\n-- repeatable --\n");
  {
    InputConfiguration cfg;
    cfg.registerButton("Repeat", ButtonCode::KeyR, ButtonModifier::None, /*repeatable=*/true);
    cfg.registerButton("Once", ButtonCode::KeyO);
    VirtualInput vi(cfg);
    InputSnapshot s{};

    press(s, ButtonCode::KeyR);
    press(s, ButtonCode::KeyO);
    vi.update(s);
    vi.update(s);
    check("repeatable binding re-fires down while held", vi.isButtonDown("Repeat"));
    check("normal binding does not re-fire", !vi.isButtonDown("Once"));
  }

  std::printf("\n-- axes --\n");
  {
    VirtualInput vi;
    InputSnapshot s{};

    s.axis[static_cast<size_t>(InputAxis::MouseX)] = 12.5f;
    s.axis[static_cast<size_t>(InputAxis::MouseY)] = -4.0f;
    vi.update(s);
    check("LookX passes through", near(vi.axisValue("LookX"), 12.5f));
    // LookY is inverted by default so "up" is positive.
    check("LookY inverted by default", near(vi.axisValue("LookY"), 4.0f));
    check("unknown axis reads zero", near(vi.axisValue("NoSuchAxis"), 0.0f));
  }

  std::printf("\n-- dead zone, sensitivity, normalise --\n");
  {
    InputConfiguration cfg;
    VirtualAxisBinding dz;
    dz.axis = InputAxis::MouseX;
    dz.deadZone = 0.5f;
    cfg.registerAxis("Dead", dz);

    VirtualAxisBinding sens;
    sens.axis = InputAxis::MouseX;
    sens.deadZone = 0.0f;
    sens.sensitivity = 3.0f;
    cfg.registerAxis("Fast", sens);

    VirtualAxisBinding norm;
    norm.axis = InputAxis::MouseX;
    norm.deadZone = 0.0f;
    norm.normalize = true;
    cfg.registerAxis("Norm", norm);

    VirtualInput vi(cfg);
    InputSnapshot s{};

    s.axis[static_cast<size_t>(InputAxis::MouseX)] = 0.2f;
    vi.update(s);
    check("below dead zone reads exactly zero", near(vi.axisValue("Dead"), 0.0f));

    s.axis[static_cast<size_t>(InputAxis::MouseX)] = 2.0f;
    vi.update(s);
    check("above dead zone passes", near(vi.axisValue("Dead"), 2.0f));
    check("sensitivity multiplies", near(vi.axisValue("Fast"), 6.0f));
    check("normalise clamps to 1", near(vi.axisValue("Norm"), 1.0f));

    s.axis[static_cast<size_t>(InputAxis::MouseX)] = -9.0f;
    vi.update(s);
    check("normalise clamps to -1", near(vi.axisValue("Norm"), -1.0f));
  }

  std::printf("\n-- rebinding --\n");
  {
    VirtualInput vi;
    InputSnapshot s{};

    // Rebinding is the whole point of the virtual layer: same name, different key.
    vi.configuration().unregisterButton("Fire");
    vi.configuration().registerButton("Fire", ButtonCode::KeySpace);

    press(s, ButtonCode::MouseLeft);
    vi.update(s);
    check("old binding no longer fires", !vi.isButtonHeld("Fire"));

    press(s, ButtonCode::MouseLeft, false);
    press(s, ButtonCode::KeySpace);
    vi.update(s);
    check("new binding fires", vi.isButtonHeld("Fire"));

    // Replacing the whole configuration must clear stale state, not leak a stuck button.
    vi.setConfiguration(InputConfiguration::makeDefault());
    check("state cleared on reconfigure", !vi.isButtonHeld("Fire"));
  }

  std::printf("\n-- button code names --\n");
  {
    check("name of KeyW", std::string(buttonCodeName(ButtonCode::KeyW)) == "W");
    check("name of MouseLeft", std::string(buttonCodeName(ButtonCode::MouseLeft)) == "MouseLeft");
    check("round-trip W", buttonCodeFromName("W") == ButtonCode::KeyW);
    check("round-trip F5", buttonCodeFromName("F5") == ButtonCode::KeyF5);
    check("unknown name is Unassigned", buttonCodeFromName("Nope") == ButtonCode::Unassigned);

    // Every code must have a distinct, non-empty name — a duplicate would make rebinding UI lie.
    bool allNamed = true;
    for (uint32_t i = 1; i < static_cast<uint32_t>(ButtonCode::Count); ++i) {
      const char* n = buttonCodeName(static_cast<ButtonCode>(i));
      if (!n || !*n || buttonCodeFromName(n) != static_cast<ButtonCode>(i)) allNamed = false;
    }
    check("all button codes round-trip through their name", allNamed);
  }

  std::printf("\n=== failures: %d ===\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}

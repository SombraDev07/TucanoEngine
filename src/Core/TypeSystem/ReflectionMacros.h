#pragma once

// Markers the Reflector reads. In a normal build they expand to nothing at all.
//
// P3-03 of the roadmap. Until now every reflected type was declared twice: once as a struct, and
// once again by hand in a WeatherReflection.h / SceneReflection.h, with the two free to drift.
// Nothing catches the drift — a field added to Material and not to the reflection file simply never
// appears in the editor, which is exactly what had happened to seven live material parameters
// (CP-19). These macros put the declaration back on the field itself and let a tool write the
// second copy.
//
//   struct TUCANO_TYPE() Material {
//       TUCANO_FIELD(Color, .label = "Base color", .category = "Surface")
//       glm::vec4 baseColorFactor{1, 1, 1, 1};
//   };
//
// The text inside TUCANO_FIELD is passed through to PropertyMetadata verbatim, so it is the same
// syntax the hand-written files already use and the same compiler errors apply. An optional first
// token names the CoreType; without it the Reflector infers it from the C++ type. Give it when the
// type cannot tell you the intent — `Color` and `Vec4` are the same four floats.
//
// Cost when not running the Reflector: zero. The macros expand to nothing, so a header can be
// annotated without dragging the type system into translation units that only want the struct.

#ifdef TUCANO_REFLECTOR
// Only the Reflector's own clang pass defines this. `annotate` is a clang attribute that survives
// into the AST as a plain string, which is what lets the metadata ride along unparsed.
#define TUCANO_TYPE(...) __attribute__((annotate("tucano.type:" #__VA_ARGS__)))
#define TUCANO_FIELD(...) __attribute__((annotate("tucano.field:" #__VA_ARGS__)))
#define TUCANO_ENUM(...) __attribute__((annotate("tucano.enum:" #__VA_ARGS__)))
#define TUCANO_ENUMERATOR(...) __attribute__((annotate("tucano.enumerator:" #__VA_ARGS__)))
#else
#define TUCANO_TYPE(...)
#define TUCANO_FIELD(...)
#define TUCANO_ENUM(...)
#define TUCANO_ENUMERATOR(...)
#endif

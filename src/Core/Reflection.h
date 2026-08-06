#pragma once

// Declarative field reflection for parameter structs.
//
// The editor's inspector was 75 hand-written property assignments: every new field cost UI code in
// C#, which is why adding one was expensive and why panels drifted out of sync with the structs
// they edit. A type declares its fields once here and any consumer — the editor's generated panel,
// a serialiser, a console command, a diff — walks the same description.
//
// Deliberately narrow: plain-old-data parameter structs with scalar, vector and colour fields.
// It is not a general object model, there is no inheritance and no polymorphism, because the
// things that need editing are settings blocks. Growing it beyond that is a decision to take when
// something actually needs it.
//
// Usage:
//
//   TUCANO_REFLECT_BEGIN(WaterParams)
//     TUCANO_FIELD_BOOL (enabled,   "Enabled", "Draw the water pass at all")
//     TUCANO_FIELD_FLOAT(roughness, "Roughness", "Microfacet roughness", 0.0f, 0.5f, 0.005f)
//     TUCANO_FIELD_VEC3 (absorption,"Absorption", "Beer-Lambert extinction per metre", 0.0f, 2.0f)
//   TUCANO_REFLECT_END()
//
// Field order is display order. Names are stable identifiers (used by serialisation); labels are
// what a person reads.

#include <cstddef>
#include <cstdint>

namespace tucano::reflect {

enum class FieldType : uint32_t {
  Bool = 0,
  Float = 1,
  Int = 2,
  Vec2 = 3,
  Vec3 = 4,
  /// Vec3 edited as a colour rather than three numbers. Same storage.
  Color = 5,
};

/// Number of scalar components a field occupies, for a generic editor laying out rows.
inline uint32_t componentCount(FieldType t) {
  switch (t) {
    case FieldType::Vec2: return 2;
    case FieldType::Vec3:
    case FieldType::Color: return 3;
    default: return 1;
  }
}

struct FieldDesc {
  const char* name = "";     ///< stable identifier
  const char* label = "";    ///< human-facing
  const char* tooltip = "";
  FieldType type = FieldType::Float;
  uint32_t offset = 0;       ///< byte offset within the owning struct
  float minValue = 0.0f;
  float maxValue = 1.0f;
  float step = 0.01f;        ///< suggested increment; 0 means the editor picks
};

struct TypeDesc {
  const char* name = "";
  const FieldDesc* fields = nullptr;
  uint32_t fieldCount = 0;
  uint32_t byteSize = 0;
};

/// Specialised by TUCANO_REFLECT_BEGIN. The unspecialised form deliberately has no `descriptor`,
/// so reflecting a type that never declared its fields is a compile error rather than empty data.
template <typename T>
struct Reflection;

template <typename T>
const TypeDesc& describe() {
  return Reflection<T>::descriptor();
}

// ── Generic access ───────────────────────────────────
//
// Reading and writing go through the field's byte offset. Every field is either a bool or a run of
// floats, so a single scalar accessor covers the whole surface and the editor needs no per-type
// code. Out-of-range component indices are ignored rather than trapping: the caller is often a UI
// that can be a frame out of date.

inline float getScalar(const TypeDesc& type, uint32_t fieldIndex, const void* instance,
                       uint32_t component = 0) {
  if (fieldIndex >= type.fieldCount || !instance) return 0.0f;
  const FieldDesc& f = type.fields[fieldIndex];
  if (component >= componentCount(f.type)) return 0.0f;
  const auto* base = static_cast<const uint8_t*>(instance) + f.offset;

  switch (f.type) {
    case FieldType::Bool: return *reinterpret_cast<const bool*>(base) ? 1.0f : 0.0f;
    case FieldType::Int: return float(*reinterpret_cast<const int32_t*>(base));
    default: return reinterpret_cast<const float*>(base)[component];
  }
}

inline void setScalar(const TypeDesc& type, uint32_t fieldIndex, void* instance, float value,
                      uint32_t component = 0) {
  if (fieldIndex >= type.fieldCount || !instance) return;
  const FieldDesc& f = type.fields[fieldIndex];
  if (component >= componentCount(f.type)) return;
  auto* base = static_cast<uint8_t*>(instance) + f.offset;

  switch (f.type) {
    case FieldType::Bool: *reinterpret_cast<bool*>(base) = value != 0.0f; break;
    case FieldType::Int: *reinterpret_cast<int32_t*>(base) = int32_t(value); break;
    default: reinterpret_cast<float*>(base)[component] = value; break;
  }
}

} // namespace tucano::reflect

// ── Declaration macros ───────────────────────────────

#define TUCANO_REFLECT_BEGIN(Type)                                                    \
  namespace tucano::reflect {                                                         \
  template <>                                                                         \
  struct Reflection<::tucano::Type> {                                                 \
    using Owner = ::tucano::Type;                                                     \
    static constexpr const char* kName = #Type;                                       \
    static const TypeDesc& descriptor() {                                             \
      static const FieldDesc kFields[] = {

#define TUCANO_REFLECT_END()                                                          \
      };                                                                              \
      static const TypeDesc kType{                                                    \
          kName, kFields, uint32_t(sizeof(kFields) / sizeof(kFields[0])),             \
          uint32_t(sizeof(Owner))};                                                   \
      return kType;                                                                   \
    }                                                                                 \
  };                                                                                  \
  }

// The macros below build FieldDesc entries. `offsetof` on these structs is well-defined: they are
// standard-layout aggregates of scalars and glm vectors.

#define TUCANO_FIELD_BOOL(member, label, tooltip)                                     \
  FieldDesc{#member, label, tooltip, FieldType::Bool, uint32_t(offsetof(Owner, member)), 0.0f, 1.0f, 1.0f},

#define TUCANO_FIELD_FLOAT(member, label, tooltip, lo, hi, stp)                       \
  FieldDesc{#member, label, tooltip, FieldType::Float, uint32_t(offsetof(Owner, member)), lo, hi, stp},

#define TUCANO_FIELD_INT(member, label, tooltip, lo, hi)                              \
  FieldDesc{#member, label, tooltip, FieldType::Int, uint32_t(offsetof(Owner, member)), float(lo), float(hi), 1.0f},

#define TUCANO_FIELD_VEC2(member, label, tooltip, lo, hi)                             \
  FieldDesc{#member, label, tooltip, FieldType::Vec2, uint32_t(offsetof(Owner, member)), lo, hi, 0.01f},

#define TUCANO_FIELD_VEC3(member, label, tooltip, lo, hi)                             \
  FieldDesc{#member, label, tooltip, FieldType::Vec3, uint32_t(offsetof(Owner, member)), lo, hi, 0.01f},

#define TUCANO_FIELD_COLOR(member, label, tooltip)                                    \
  FieldDesc{#member, label, tooltip, FieldType::Color, uint32_t(offsetof(Owner, member)), 0.0f, 4.0f, 0.01f},

#pragma once

// The on-disk description of one cell's contents (the `.tcell` file).
//
// Deliberately a plain data description with NO renderer types in it. The World module must not
// depend on Mesh, Material or the RHI — that separation is what keeps the streaming core testable
// headlessly. Turning these records into live engine objects is the job of a CellDataProvider
// (see SceneCellProvider), which is the only place the two worlds meet.
//
// The format is JSON, matching the editor's existing scene format closely enough that a scene and
// a cell describe objects the same way. Text costs more bytes than a binary blob would, but it is
// diffable, hand-editable and survives a format change — the right trade while the format is still
// moving. Swapping in a binary encoder later only touches this file.

#include "World/CellId.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace tucano::world {

/// What kind of geometry an object uses. Primitives are generated at load; Gltf is imported.
enum class CellObjectKind : uint8_t {
  Cube = 0,
  Sphere = 1,
  Plane = 2,
  Gltf = 3,
};

const char* cellObjectKindName(CellObjectKind kind);

/// Physical shape a streamed object contributes, if any.
///
/// Separate from the visual kind on purpose: a detailed rock can be a box collider, and a visual
/// prop can have none at all. Tying collision to the mesh would force every blade of grass to be
/// solid.
enum class CellColliderKind : uint8_t {
  None = 0,
  Box = 1,
  Sphere = 2,
};

const char* cellColliderKindName(CellColliderKind kind);

/// One placed object inside a cell.
struct CellObject {
  CellObjectKind kind = CellObjectKind::Cube;
  std::string name;
  std::string path; ///< Gltf only

  glm::vec3 position{0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 scale{1.0f};

  glm::vec3 baseColor{0.8f};
  float metallic = 0.0f;
  float roughness = 0.7f;

  /// Size of the generated primitive, in metres. Ignored for Gltf.
  float size = 1.0f;

  /// Collision shape. None by default: most streamed content is scenery, and making everything
  /// solid would flood the physics world for no gameplay benefit.
  CellColliderKind collider = CellColliderKind::None;
};

/// Everything one cell holds, for one layer.
struct CellFile {
  CellId id;
  uint32_t layer = 0; ///< raw WorldLayer index, kept as a plain int so this header stays standalone
  std::vector<CellObject> objects;

  /// Serializes to JSON text.
  std::string toJson() const;

  /// Parses JSON text. Returns false on malformed input, leaving `out` untouched.
  static bool fromJson(const std::string& text, CellFile& out);

  bool save(const std::string& path) const;
  bool load(const std::string& path);
};

/// Path of a cell's file inside a world directory: `<root>/cells/L<level>_<x>_<y>_<z>_<layer>.tcell`.
/// Encoding the coordinate in the NAME rather than a lookup table means a cell can be found without
/// reading an index first — which matters when the read happens on a background thread.
std::string cellFilePath(const std::string& worldRoot, const CellId& id, uint32_t layer);

} // namespace tucano::world
